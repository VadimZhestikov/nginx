
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
#include "ngx_js_listener.h"
#include "ngx_js_stream_listener.h"
#include "ngx_js_sw.h"


JSClassID              ngx_js_socket_class_id;
ngx_js_socket_state_t *ngx_js_socket_reg[NGX_JS_SOCKET_REG_MAX];


ngx_js_compartment_t
ngx_js_socket_owner(uint32_t handle)
{
    if (handle >= NGX_JS_SOCKET_REG_MAX || ngx_js_socket_reg[handle] == NULL) {
        return NGX_JS_COMPARTMENT_HOST_ROOT;
    }

    return ngx_js_socket_reg[handle]->owner;
}


/* ------------------------------------------------------------------ */
/* NginxSocket opaque + finalizer                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_socket_reg[] */
    uint32_t  mask;     /* COMCON mediate: allowed fields, bit==magic (see get) */
} ngx_js_socket_opaque_t;


int32_t
ngx_js_socket_handle(JSValueConst val)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_socket_class_id);
    if (op == NULL) {
        return -1;
    }

    return (int32_t) op->handle;
}


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
/* magic: 0=address, 1=port, 2=fd, 3=listener                         */
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

    /*
     * COMCON mediate: a redacted field (mask bit clear for this magic) reads as
     * undefined — the membrane hides it. Attenuation-only: a mask can only
     * remove authority a wrapper already had (A(cap′) ⊆ A(cap)).
     */
    if (magic >= 0 && magic < 32 && !(op->mask & (1u << magic))) {
        return JS_UNDEFINED;
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

    case 3:  /* listener — NginxHttpListener | NginxStreamListener | null */
    {
        ngx_uint_t  i;

        /*
         * COMCON A1.1 (defense-in-depth): the listener edge is the entry to
         * the sock→listener→serverByName→server→addLocation reach cycle. A
         * compartment that was handed a socket it does not own cannot walk it.
         * No-op today (current compartment is HOST_ROOT); isolates once
         * confined fragments run.
         */
        if (!ngx_js_compartment_may_reach(st->owner)
            && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_LISTENER,
                                         st->addr))
        {
            return JS_NULL;
        }

        for (i = 0; i < NGX_JS_LISTENER_REG_MAX; i++) {
            if (ngx_js_listener_reg[i] != NULL
                && ngx_js_listener_reg[i]->socket_handle == op->handle)
            {
                return ngx_js_wrap_listener(ctx, (uint32_t) i);
            }
        }

        for (i = 0; i < NGX_JS_STREAM_LISTENER_REG_MAX; i++) {
            if (ngx_js_stream_listener_reg[i] != NULL
                && ngx_js_stream_listener_reg[i]->socket_handle == op->handle)
            {
                return ngx_js_wrap_stream_listener(ctx, (uint32_t) i);
            }
        }

        return JS_NULL;
    }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* sock.close() — Phase E (pre-fork) + F3 (worker)                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_socket_close(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;
    ngx_js_worker_t         *w;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_SOCKET_REG_MAX
        || ngx_js_socket_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx,
            "sock.close: socket already closed or invalid");
    }

    st = ngx_js_socket_reg[op->handle];

    /* COMCON SR-1 MEDIUM-4: close() destroys host state — a mutating op, not a
     * scalar read. A tenant handed this socket via grantToTenant may not close
     * a socket it does not own. */
    if (!ngx_js_compartment_may_reach(st->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_MUTATE, st->addr))
    {
        return JS_ThrowTypeError(ctx, "sock.close: denied (not owner)");
    }

    if (st->in_listening) {
        return JS_ThrowInternalError(ctx,
            "sock.close: cannot close a socket already added to"
            " cycle->listening — call addServer() activates the socket"
            " for nginx workers");
    }

    /* Close the OS file descriptor */
    if (st->fd >= 0) {
        (void) close(st->fd);
        st->fd = -1;
    }

    /* Remove from global registry and free the state */
    ngx_js_socket_reg[op->handle] = NULL;
    ngx_free(st);

    /* F3: also remove from the worker-local registry if in a worker */
    if (ngx_process == NGX_PROCESS_WORKER) {
        w = JS_GetContextOpaque(ctx);
        if (w != NULL) {
            for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
                if (w->local_socket_reg[i] == st) {
                    w->local_socket_reg[i] = NULL;
                    break;
                }
            }
        }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* sock.broadcast() — Phase F4: deliver socket fd to all other workers */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_socket_broadcast(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;
    int                      rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_SOCKET_REG_MAX
        || ngx_js_socket_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: socket already closed or invalid");
    }

    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: only valid in worker processes");
    }

    st = ngx_js_socket_reg[op->handle];

    /* COMCON SR-1 MEDIUM-4: broadcast distributes the fd fleet-wide — mutating;
     * gate on ownership like close(). */
    if (!ngx_js_compartment_may_reach(st->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_MUTATE, st->addr))
    {
        return JS_ThrowTypeError(ctx, "sock.broadcast: denied (not owner)");
    }

    rc = ngx_js_socket_mgr_broadcast(op->handle, st->addr,
                                     ngx_strlen(st->addr));
    if (rc < 0) {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: manager failed to distribute socket to workers");
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_socket_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",   ngx_js_socket_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("port",      ngx_js_socket_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("fd",        ngx_js_socket_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("listener",  ngx_js_socket_get, NULL, 3),
    JS_CFUNC_DEF(        "close",     0, ngx_js_socket_close),
    JS_CFUNC_DEF(        "broadcast", 0, ngx_js_socket_broadcast),
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

JSValue
ngx_js_socket_wrap(JSContext *ctx, uint32_t handle)
{
    return ngx_js_socket_wrap_masked(ctx, handle, NGX_JS_SOCKET_MASK_ALL);
}


/*
 * COMCON mediate: wrap a socket with a field mask (bit index == getter magic:
 * 0 address, 1 port, 2 fd, 3 listener). A clear bit hides that field
 * (reads undefined). Attenuation-only — a membrane never adds authority.
 */
JSValue
ngx_js_socket_wrap_masked(JSContext *ctx, uint32_t handle, uint32_t mask)
{
    JSValue                  obj;
    ngx_js_socket_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_socket_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;
    op->mask = mask;

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
    ngx_js_worker_t         *w         = NULL;
    ngx_uint_t               local_slot = NGX_JS_LOCAL_SOCKET_REG_MAX;

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

    if (ngx_process == NGX_PROCESS_WORKER) {
        /*
         * Phase F: post-fork worker — ask the master's manager thread to
         * create the socket and hand the fd back via SCM_RIGHTS.
         */
        fd = ngx_js_socket_mgr_create(addr_str, ngx_strlen(addr_str));
        if (fd < 0) {
            return JS_ThrowInternalError(ctx,
                "createSocket: manager failed to create socket for '%s'",
                addr_str);
        }

        /*
         * F3: verify there is a free slot in the worker-local registry
         * before allocating state, so we can always track this socket.
         */
        w = JS_GetContextOpaque(ctx);
        local_slot = NGX_JS_LOCAL_SOCKET_REG_MAX;
        if (w != NULL) {
            for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
                if (w->local_socket_reg[i] == NULL) {
                    local_slot = (ngx_uint_t) i;
                    break;
                }
            }
            if (local_slot == NGX_JS_LOCAL_SOCKET_REG_MAX) {
                close(fd);
                return JS_ThrowInternalError(ctx,
                    "createSocket: worker-local socket registry full"
                    " (max %d)", NGX_JS_LOCAL_SOCKET_REG_MAX);
            }
        }

    } else {
        /* Pre-fork (master / init_conf): create socket directly. */
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
                "createSocket: bind('%s') failed: %s",
                addr_str, strerror(saved));
        }

        if (listen(fd, 511) < 0) {
            saved = errno;
            close(fd);
            return JS_ThrowInternalError(ctx,
                "createSocket: listen() failed: %s", strerror(saved));
        }

        /*
         * The listening socket MUST be non-blocking.  nginx's event loop calls
         * accept() on a wakeup expecting EAGAIN when the backlog was already
         * drained by a peer worker (shared listener); on a blocking socket that
         * accept() sleeps in the kernel (wchan inet_csk_accept) and freezes the
         * whole worker.  nginx sets this in ngx_open_listening_sockets(), which
         * our injected fd bypasses, so we must do it ourselves.
         */
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
            saved = errno;
            close(fd);
            return JS_ThrowInternalError(ctx,
                "createSocket: set O_NONBLOCK failed: %s", strerror(saved));
        }
    }

    /* Allocate state.  Pre-fork: ngx_alloc() → COW-shared heap.
     * Post-fork: same allocator but memory is worker-private (no fork after). */
    st = ngx_alloc(sizeof(ngx_js_socket_state_t), ngx_cycle->log);
    if (st == NULL) {
        close(fd);
        return JS_ThrowInternalError(ctx, "createSocket: ngx_alloc failed");
    }

    st->fd          = fd;
    st->port        = port;
    st->in_listening = 0;
    st->owner        = ngx_js_current_compartment();   /* COMCON A1.1 */
    ngx_cpystrn((u_char *) st->addr, (u_char *) addr_str, sizeof(st->addr));

    ngx_js_socket_reg[handle] = st;

    /* F3: register in worker-local registry for cleanup tracking */
    if (ngx_process == NGX_PROCESS_WORKER && w != NULL
        && local_slot < NGX_JS_LOCAL_SOCKET_REG_MAX)
    {
        w->local_socket_reg[local_slot] = st;
    }

    return ngx_js_socket_wrap(ctx, handle);
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
