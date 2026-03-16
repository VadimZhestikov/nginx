
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 Phase B — nginx.http.attach(sock)
 *
 * Wires a NginxSocket into nginx's HTTP connection pipeline by building
 * the routing structures (ngx_http_port_t / ngx_http_in_addr_t /
 * ngx_http_addr_conf_t) and preparing the ngx_listening_t template.
 *
 * The socket is NOT added to cycle->listening here; that is deferred
 * to ngx_js_listener_activate() which is called by addServer() (Phase C)
 * once a valid default_server is available.
 *
 * JS API (Phase B):
 *
 *   const listener = nginx.http.attach(sock);
 *   listener.address    // "host:port" string (same as sock.address)
 *   listener.socket     // the NginxSocket that was attached
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "ngx_js_socket.h"
#include "ngx_js_listener.h"


JSClassID                     ngx_js_http_listener_class_id;
ngx_js_http_listener_state_t *ngx_js_listener_reg[NGX_JS_LISTENER_REG_MAX];


/* ------------------------------------------------------------------ */
/* NginxHttpListener opaque + finalizer                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_listener_reg[] */
} ngx_js_listener_opaque_t;


static void
ngx_js_listener_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_listener_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_http_listener_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_listener_class = {
    "NginxHttpListener",
    .finalizer = ngx_js_listener_finalizer,
};


/* ------------------------------------------------------------------ */
/* NginxHttpListener property getters                                  */
/* magic: 0=address                                                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_js_socket_state_t         *sock;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    switch (magic) {
    case 0: /* address — same as sock.address */
        if (st->socket_handle >= NGX_JS_SOCKET_REG_MAX
            || ngx_js_socket_reg[st->socket_handle] == NULL)
        {
            return JS_NewString(ctx, "");
        }
        sock = ngx_js_socket_reg[st->socket_handle];
        return JS_NewString(ctx, sock->addr);
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* listener.addServer(srv) — Phase C implementation                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_cycle_t                   *cycle;

    /* Phase C: pre-fork only */
    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: post-fork not yet supported");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "listener.addServer: NginxServer argument required");
    }

    cscf = ngx_js_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "listener.addServer: argument must be a NginxServer object");
    }

    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: server has no associated cycle");
    }

    if (st->activated) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: listener already activated");
    }

    /* Wire the cscf into both the state and the routing structures */
    st->default_server           = cscf;
    st->addr.conf.default_server = cscf;

    if (ngx_js_listener_activate(st, cycle) != NGX_OK) {
        st->default_server           = NULL;
        st->addr.conf.default_server = NULL;
        return JS_ThrowInternalError(ctx,
            "listener.addServer: activation failed");
    }

    return JS_DupValue(ctx, argv[0]);
}


static const JSCFunctionListEntry  ngx_js_listener_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",   ngx_js_listener_get,    NULL, 0),
    JS_CFUNC_DEF(        "addServer", 1, ngx_js_listener_add_server),
};


ngx_int_t
ngx_js_listener_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_http_listener_class_id,
                       &ngx_js_listener_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_listener_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_listener_proto_funcs,
                               countof(ngx_js_listener_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_http_listener_class_id, proto);
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Wrap a listener registry slot in a JS NginxHttpListener object      */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_wrap_listener(JSContext *ctx, uint32_t handle)
{
    JSValue                    obj;
    ngx_js_listener_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_listener_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;

    obj = JS_NewObjectClass(ctx, ngx_js_http_listener_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_listener_activate — push ls into cycle->listening            */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_listener_activate(ngx_js_http_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_listening_t           *ls;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_js_socket_state_t     *sock;

    if (st->activated) {
        return NGX_OK;
    }

    if (st->default_server == NULL) {
        return NGX_ERROR;
    }

    sock = ngx_js_socket_reg[st->socket_handle];
    if (sock == NULL) {
        return NGX_ERROR;
    }

    ls = ngx_array_push(&cycle->listening);
    if (ls == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(ls, sizeof(ngx_listening_t));

    ls->fd      = (ngx_socket_t) sock->fd;
    ls->type    = SOCK_STREAM;
    ls->backlog = NGX_LISTEN_BACKLOG;
    ls->rcvbuf  = -1;
    ls->sndbuf  = -1;

    ls->handler = ngx_http_init_connection;
    ls->servers = &st->port;

    ls->sockaddr = (struct sockaddr *) &st->sin;
    ls->socklen  = sizeof(struct sockaddr_in);

    ls->addr_text_max_len = NGX_INET_ADDRSTRLEN;
    ls->addr_text.data    = st->addr_text_buf;
    ls->addr_text.len     = st->addr_text_len;

    ls->addr_ntop = 1;
    ls->open      = 1;
    ls->bound     = 1;

    /* cscf-dependent fields */
    cscf = st->default_server;
    ls->pool_size = cscf->connection_pool_size;

    clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
    ls->logp = clcf->error_log;
    ls->log.data    = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;

#if !(NGX_WIN32)
    ngx_rbtree_init(&ls->rbtree, &ls->sentinel, ngx_udp_rbtree_insert_value);
#endif

    st->activated = 1;
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* nginx.http.attach(sock) — Phase B implementation                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_http_attach(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_http_listener_state_t  *st;
    ngx_js_socket_state_t         *sock;
    uint32_t                       socket_handle, handle;
    int                            i;

    /* Phase B: only valid before fork */
    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "http.attach: post-fork worker attach not yet supported");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "http.attach: expected NginxSocket argument");
    }

    socket_handle = ngx_js_socket_get_handle(argv[0]);
    if (socket_handle >= NGX_JS_SOCKET_REG_MAX) {
        return JS_ThrowTypeError(ctx,
            "http.attach: argument must be a NginxSocket");
    }

    if (ngx_js_socket_reg[socket_handle] == NULL) {
        return JS_ThrowInternalError(ctx,
            "http.attach: invalid socket handle");
    }

    sock = ngx_js_socket_reg[socket_handle];

    /* Find a free listener registry slot */
    handle = (uint32_t) NGX_JS_LISTENER_REG_MAX;
    for (i = 0; i < NGX_JS_LISTENER_REG_MAX; i++) {
        if (ngx_js_listener_reg[i] == NULL) {
            handle = (uint32_t) i;
            break;
        }
    }

    if (handle == (uint32_t) NGX_JS_LISTENER_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "http.attach: listener registry full (max %d)",
            NGX_JS_LISTENER_REG_MAX);
    }

    /* Allocate listener state on the heap (COW-shared after fork) */
    st = ngx_alloc(sizeof(ngx_js_http_listener_state_t), ngx_cycle->log);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "http.attach: ngx_alloc failed");
    }

    ngx_memzero(st, sizeof(ngx_js_http_listener_state_t));
    st->socket_handle = socket_handle;

    /* Fill sockaddr from the socket state */
    st->sin.sin_family = AF_INET;
    st->sin.sin_port   = htons(sock->port);
    /* Reconstruct in_addr from sock->addr string (up to ':') */
    {
        char  host[48];
        char *colon = strrchr(sock->addr, ':');
        if (colon) {
            size_t n = (size_t)(colon - sock->addr);
            if (n >= sizeof(host)) { n = sizeof(host) - 1; }
            ngx_memcpy(host, sock->addr, n);
            host[n] = '\0';
            (void) inet_pton(AF_INET, host, &st->sin.sin_addr);
        }
    }

    /* Build addr_text ("host:port") */
    {
        u_char *p = ngx_snprintf(st->addr_text_buf,
                                 sizeof(st->addr_text_buf) - 1,
                                 "%s%Z", sock->addr);
        st->addr_text_len = (size_t)(p - st->addr_text_buf) - 1; /* skip NUL */
    }

    /* Set up routing structures */
    st->addr.addr         = st->sin.sin_addr.s_addr;
    st->addr.conf.default_server = NULL;     /* set by addServer() */
    st->addr.conf.virtual_names  = NULL;
    st->addr.conf.ssl            = 0;
    st->addr.conf.http2          = 0;
    st->addr.conf.quic           = 0;
    st->addr.conf.proxy_protocol = 0;

    st->port.addrs  = &st->addr;
    st->port.naddrs = 1;

    ngx_js_listener_reg[handle] = st;

    return ngx_js_wrap_listener(ctx, handle);
}


ngx_int_t
ngx_js_listener_install(JSContext *ctx, JSValue http_obj)
{
    JS_SetPropertyStr(ctx, http_obj, "attach",
                      JS_NewCFunction(ctx, ngx_js_http_attach, "attach", 1));
    return NGX_OK;
}
