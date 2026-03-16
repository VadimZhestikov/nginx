
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 — nginx.createSocket(addrPort)
 *
 * Creates a bound + listening TCP socket and wraps it in a NginxSocket
 * JS object.
 *
 * Phase A (pre-fork only): syscalls execute directly in the master process
 * during init_conf.  Phase F will add a manager-thread round-trip for
 * sockets created post-fork from worker request handlers.
 *
 * JS API:
 *
 *   const sock = nginx.createSocket('0.0.0.0:9000');
 *   sock.address  // "0.0.0.0:9000"
 *   sock.port     // 9000  (number)
 *   sock.fd       // OS file-descriptor number
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <quickjs.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "ngx_js_socket.h"


JSClassID              ngx_js_socket_class_id;
ngx_js_socket_state_t *ngx_js_socket_reg[NGX_JS_SOCKET_REG_MAX];


/* ------------------------------------------------------------------ */
/* NginxSocket opaque + finalizer                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_socket_reg[] */
} ngx_js_socket_opaque_t;


static void
ngx_js_socket_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_socket_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_socket_class = {
    "NginxSocket",
    .finalizer = ngx_js_socket_finalizer,
};


/* ------------------------------------------------------------------ */
/* NginxSocket property getters                                         */
/* magic: 0=address, 1=port, 2=fd                                      */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_socket_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_SOCKET_REG_MAX
        || ngx_js_socket_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxSocket: invalid handle");
    }

    st = ngx_js_socket_reg[op->handle];

    switch (magic) {
    case 0:  return JS_NewString(ctx, st->addr);
    case 1:  return JS_NewInt32(ctx, (int32_t) st->port);
    case 2:  return JS_NewInt32(ctx, (int32_t) st->fd);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_socket_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address", ngx_js_socket_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("port",    ngx_js_socket_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("fd",      ngx_js_socket_get, NULL, 2),
};


ngx_int_t
ngx_js_socket_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_socket_class_id, &ngx_js_socket_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_socket_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_socket_proto_funcs,
                               countof(ngx_js_socket_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_socket_class_id, proto);
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Wrap a registry slot in a JS NginxSocket object                     */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_wrap_socket(JSContext *ctx, uint32_t handle)
{
    JSValue                  obj;
    ngx_js_socket_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_socket_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;

    obj = JS_NewObjectClass(ctx, ngx_js_socket_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* Address parser: "host:port" → host string + port number             */
/* ------------------------------------------------------------------ */

static int
ngx_js_parse_addr_port(const char *s, char *host_buf, size_t host_bufsz,
    uint16_t *port_out)
{
    const char  *colon;
    size_t       host_len;
    long         port;
    char        *endp;

    /* Use the last ':' so IPv6 literals like "[::1]:80" work if we
     * strip the brackets first — for Phase A we only support IPv4.  */
    colon = strrchr(s, ':');
    if (colon == NULL) {
        return -1;
    }

    host_len = (size_t) (colon - s);
    if (host_len == 0 || host_len >= host_bufsz) {
        return -1;
    }

    ngx_memcpy(host_buf, s, host_len);
    host_buf[host_len] = '\0';

    port = strtol(colon + 1, &endp, 10);
    if (*endp != '\0' || port < 1 || port > 65535) {
        return -1;
    }

    *port_out = (uint16_t) port;
    return 0;
}


/* ------------------------------------------------------------------ */
/* nginx.createSocket('host:port')                                      */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_create_socket(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char              *s;
    char                     host[48];
    char                     addr_str[64];
    uint16_t                 port;
    struct sockaddr_in       sin;
    int                      fd, opt, i, saved;
    uint32_t                 handle;
    ngx_js_socket_state_t   *st;

    /* Phase A: only valid before fork (master / init_conf context). */
    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "createSocket: post-fork worker creation not yet supported");
    }

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "createSocket: expected string argument 'host:port'");
    }

    s = JS_ToCString(ctx, argv[0]);
    if (!s) {
        return JS_EXCEPTION;
    }

    if (ngx_js_parse_addr_port(s, host, sizeof(host), &port) != 0) {
        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
            "createSocket: invalid address, expected 'host:port'");
    }

    JS_FreeCString(ctx, s);

    ngx_snprintf((u_char *) addr_str, sizeof(addr_str) - 1,
                 "%s:%d%Z", host, (int) port);

    /* Find a free registry slot. */
    handle = (uint32_t) NGX_JS_SOCKET_REG_MAX;
    for (i = 0; i < NGX_JS_SOCKET_REG_MAX; i++) {
        if (ngx_js_socket_reg[i] == NULL) {
            handle = (uint32_t) i;
            break;
        }
    }

    if (handle == (uint32_t) NGX_JS_SOCKET_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "createSocket: registry full (max %d sockets)",
            NGX_JS_SOCKET_REG_MAX);
    }

    /* Create the TCP socket. */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return JS_ThrowInternalError(ctx,
            "createSocket: socket() failed: %s", strerror(errno));
    }

    opt = 1;
    (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ngx_memzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);

    if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
        close(fd);
        return JS_ThrowTypeError(ctx,
            "createSocket: invalid IPv4 address '%s'", host);
    }

    if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        saved = errno;
        close(fd);
        return JS_ThrowInternalError(ctx,
            "createSocket: bind('%s') failed: %s", addr_str, strerror(saved));
    }

    if (listen(fd, 511) < 0) {
        saved = errno;
        close(fd);
        return JS_ThrowInternalError(ctx,
            "createSocket: listen() failed: %s", strerror(saved));
    }

    /* Allocate state on the heap (survives fork; COW-shared). */
    st = ngx_alloc(sizeof(ngx_js_socket_state_t), ngx_cycle->log);
    if (st == NULL) {
        close(fd);
        return JS_ThrowInternalError(ctx, "createSocket: ngx_alloc failed");
    }

    st->fd   = fd;
    st->port = port;
    ngx_cpystrn((u_char *) st->addr, (u_char *) addr_str, sizeof(st->addr));

    ngx_js_socket_reg[handle] = st;

    return ngx_js_wrap_socket(ctx, handle);
}


uint32_t
ngx_js_socket_get_handle(JSValueConst sock)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(sock, ngx_js_socket_class_id);
    if (op == NULL) {
        return (uint32_t) NGX_JS_SOCKET_REG_MAX;
    }

    return op->handle;
}


ngx_int_t
ngx_js_socket_install(JSContext *ctx, JSValue nginx_obj)
{
    JS_SetPropertyStr(ctx, nginx_obj, "createSocket",
                      JS_NewCFunction(ctx, ngx_js_create_socket,
                                      "createSocket", 1));
    return NGX_OK;
}
