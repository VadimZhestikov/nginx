
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 Phase G — nginx.stream.attach(sock) + stream listener methods
 *
 * Implements:
 *   nginx.stream          — object with .servers[] and .attach()
 *   NginxStreamServer     — wraps ngx_stream_core_srv_conf_t
 *   NginxStreamListener   — returned by nginx.stream.attach(sock)
 *   listener.addServer()  — activates the listener with a stream server
 *
 * JS API:
 *
 *   var srv      = nginx.stream.servers[0];   // NginxStreamServer
 *   var listener = nginx.stream.attach(sock); // NginxStreamListener
 *   listener.addServer(srv);                  // activate
 *   listener.address                          // "host:port" string
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_stream.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "ngx_js_socket.h"
#include "ngx_js_stream_listener.h"


/* ------------------------------------------------------------------ */
/* Class IDs                                                           */
/* ------------------------------------------------------------------ */

JSClassID  ngx_js_stream_server_class_id;
JSClassID  ngx_js_stream_listener_class_id;

ngx_js_stream_listener_state_t
    *ngx_js_stream_listener_reg[NGX_JS_STREAM_LISTENER_REG_MAX];


/* ================================================================== */
/* NginxStreamServer                                                   */
/* ================================================================== */

typedef struct {
    ngx_stream_core_srv_conf_t  *cscf;
    ngx_cycle_t                 *cycle;
} ngx_js_stream_server_opaque_t;


static void
ngx_js_stream_server_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_server_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_server_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_server_class = {
    "NginxStreamServer",
    .finalizer = ngx_js_stream_server_finalizer,
};


/* magic: 0=serverName */
static JSValue
ngx_js_stream_server_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_server_opaque_t  *op;
    ngx_stream_core_srv_conf_t     *cscf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf = op->cscf;

    switch (magic) {
    case 0:   /* serverName */
        return JS_NewStringLen(ctx, (char *) cscf->server_name.data,
                               cscf->server_name.len);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_server_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("serverName", ngx_js_stream_server_get, NULL, 0),
};


static JSValue
ngx_js_wrap_stream_server(JSContext *ctx, ngx_stream_core_srv_conf_t *cscf,
    ngx_cycle_t *cycle)
{
    JSValue                         obj;
    ngx_js_stream_server_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_server_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->cscf  = cscf;
    op->cycle = cycle;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_server_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


ngx_stream_core_srv_conf_t *
ngx_js_stream_server_get_cscf(JSValueConst srv, ngx_cycle_t **cycle_out)
{
    ngx_js_stream_server_opaque_t  *op;

    op = JS_GetOpaque(srv, ngx_js_stream_server_class_id);
    if (op == NULL) {
        return NULL;
    }

    if (cycle_out != NULL) {
        *cycle_out = op->cycle;
    }

    return op->cscf;
}


/* ================================================================== */
/* NginxStreamListener                                                 */
/* ================================================================== */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_stream_listener_reg[] */
} ngx_js_stream_listener_opaque_t;


static void
ngx_js_stream_listener_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_listener_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_listener_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_listener_class = {
    "NginxStreamListener",
    .finalizer = ngx_js_stream_listener_finalizer,
};


/* magic: 0=address */
static JSValue
ngx_js_stream_listener_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_listener_opaque_t  *op;
    ngx_js_stream_listener_state_t   *st;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_STREAM_LISTENER_REG_MAX
        || ngx_js_stream_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx,
            "NginxStreamListener: invalid handle");
    }

    st = ngx_js_stream_listener_reg[op->handle];

    switch (magic) {
    case 0:   /* address */
        return JS_NewStringLen(ctx, (char *) st->addr_text_buf,
                               st->addr_text_len);
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Stream listener activation                                          */
/* ------------------------------------------------------------------ */

static ngx_int_t
ngx_js_stream_listener_activate(ngx_js_stream_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_listening_t             *ls;
    ngx_stream_core_srv_conf_t  *cscf;
    ngx_js_socket_state_t       *sock;

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

    ls->handler = ngx_stream_init_connection;
    ls->servers = &st->port;

    ls->sockaddr = (struct sockaddr *) &st->sin;
    ls->socklen  = sizeof(struct sockaddr_in);

    ls->addr_text_max_len = NGX_INET_ADDRSTRLEN;
    ls->addr_text.data    = st->addr_text_buf;
    ls->addr_text.len     = st->addr_text_len;

    ls->addr_ntop = 1;
    ls->open      = 1;
    ls->bound     = 1;

    cscf = st->default_server;
    ls->pool_size = 256;   /* stream default; cscf has no connection_pool_size */
    ls->logp      = cscf->error_log;
    ls->log.data    = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;

#if !(NGX_WIN32)
    ngx_rbtree_init(&ls->rbtree, &ls->sentinel, ngx_udp_rbtree_insert_value);
#endif

    sock->in_listening = 1;
    st->activated      = 1;
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* listener.addServer(srv)                                             */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_stream_listener_add_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_stream_listener_opaque_t  *op;
    ngx_js_stream_listener_state_t   *st;
    ngx_stream_core_srv_conf_t       *cscf;
    ngx_cycle_t                      *cycle;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "addServer: not supported in worker process");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_STREAM_LISTENER_REG_MAX
        || ngx_js_stream_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx,
            "addServer: invalid listener handle");
    }

    st = ngx_js_stream_listener_reg[op->handle];

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "addServer: expected NginxStreamServer argument");
    }

    cscf = ngx_js_stream_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "addServer: argument must be a NginxStreamServer");
    }

    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx,
            "addServer: server has no associated cycle");
    }

    st->default_server               = cscf;
    st->addr.conf.default_server     = cscf;

    if (ngx_js_stream_listener_activate(st, cycle) != NGX_OK) {
        return JS_ThrowInternalError(ctx,
            "addServer: failed to activate stream listener");
    }

    return JS_DupValue(ctx, argv[0]);
}


static const JSCFunctionListEntry  ngx_js_stream_listener_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",   ngx_js_stream_listener_get, NULL, 0),
    JS_CFUNC_DEF(        "addServer", 1, ngx_js_stream_listener_add_server),
};


static JSValue
ngx_js_wrap_stream_listener(JSContext *ctx, uint32_t handle)
{
    JSValue                           obj;
    ngx_js_stream_listener_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_listener_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_listener_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ================================================================== */
/* nginx.stream.attach(sock)                                           */
/* ================================================================== */

static JSValue
ngx_js_stream_attach(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    uint32_t                          socket_handle;
    uint32_t                          listener_handle;
    ngx_js_socket_state_t            *sock;
    ngx_js_stream_listener_state_t   *st;
    ngx_uint_t                        i;
    u_char                           *p;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "stream.attach: not supported in worker process");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "stream.attach: expected NginxSocket argument");
    }

    socket_handle = ngx_js_socket_get_handle(argv[0]);
    if (socket_handle >= NGX_JS_SOCKET_REG_MAX) {
        return JS_ThrowTypeError(ctx,
            "stream.attach: argument must be a NginxSocket");
    }

    sock = ngx_js_socket_reg[socket_handle];
    if (sock == NULL) {
        return JS_ThrowInternalError(ctx,
            "stream.attach: socket has been closed");
    }

    /* Find a free listener slot */
    listener_handle = NGX_JS_STREAM_LISTENER_REG_MAX;
    for (i = 0; i < NGX_JS_STREAM_LISTENER_REG_MAX; i++) {
        if (ngx_js_stream_listener_reg[i] == NULL) {
            listener_handle = (uint32_t) i;
            break;
        }
    }

    if (listener_handle == NGX_JS_STREAM_LISTENER_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "stream.attach: listener registry full (max %d)",
            NGX_JS_STREAM_LISTENER_REG_MAX);
    }

    st = ngx_alloc(sizeof(ngx_js_stream_listener_state_t), ngx_cycle->log);
    if (st == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memzero(st, sizeof(ngx_js_stream_listener_state_t));

    st->socket_handle = socket_handle;

    /* Build sockaddr_in from socket state */
    ngx_memzero(&st->sin, sizeof(st->sin));
    st->sin.sin_family = AF_INET;
    st->sin.sin_port   = htons(sock->port);

    /*
     * addr_text_buf: copy "host:port" from sock->addr.
     * sock->addr is a NUL-terminated C string like "127.0.0.1:9000".
     */
    st->addr_text_len = ngx_strlen(sock->addr);
    if (st->addr_text_len >= sizeof(st->addr_text_buf)) {
        st->addr_text_len = sizeof(st->addr_text_buf) - 1;
    }

    ngx_memcpy(st->addr_text_buf, sock->addr, st->addr_text_len);

    /* Extract the IPv4 address from "host:port" in sock->addr */
    p = (u_char *) ngx_strchr(sock->addr, ':');
    if (p) {
        char  host[48];
        size_t hlen = (size_t)(p - (u_char *) sock->addr);
        if (hlen < sizeof(host)) {
            ngx_memcpy(host, sock->addr, hlen);
            host[hlen] = '\0';
            (void) inet_pton(AF_INET, host, &st->sin.sin_addr);
        }
    }

    /* Wire up the stream port/addr routing structures */
    st->addr.addr          = st->sin.sin_addr.s_addr;
    st->addr.conf.default_server = NULL;   /* set by addServer() */
    st->addr.conf.virtual_names  = NULL;
#if (NGX_STREAM_SSL)
    st->addr.conf.ssl        = 0;
#endif
    st->addr.conf.proxy_protocol = 0;

    st->port.addrs  = &st->addr;
    st->port.naddrs = 1;

    ngx_js_stream_listener_reg[listener_handle] = st;

    return ngx_js_wrap_stream_listener(ctx, listener_handle);
}


/* ================================================================== */
/* nginx.stream object — servers[] + attach()                         */
/* ================================================================== */

ngx_int_t
ngx_js_stream_install(JSContext *ctx, JSValue nginx_obj, ngx_cycle_t *cycle)
{
    JSValue                       stream_obj, servers_arr, srv_obj;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_stream_core_srv_conf_t  **cscfp;
    ngx_uint_t                    i;

    stream_obj = JS_NewObject(ctx);
    if (JS_IsException(stream_obj)) {
        return NGX_ERROR;
    }

    /* Build nginx.stream.servers[] once at install time (mirrors nginx.http) */
    servers_arr = JS_NewArray(ctx);
    if (JS_IsException(servers_arr)) {
        JS_FreeValue(ctx, stream_obj);
        return NGX_ERROR;
    }

    if (cycle->conf_ctx != NULL) {
        cmcf = ngx_stream_cycle_get_module_main_conf(cycle,
                                                     ngx_stream_core_module);
        if (cmcf != NULL) {
            cscfp = cmcf->servers.elts;

            for (i = 0; i < cmcf->servers.nelts; i++) {
                srv_obj = ngx_js_wrap_stream_server(ctx, cscfp[i], cycle);
                if (JS_IsException(srv_obj)) {
                    JS_FreeValue(ctx, servers_arr);
                    JS_FreeValue(ctx, stream_obj);
                    return NGX_ERROR;
                }

                JS_SetPropertyUint32(ctx, servers_arr, (uint32_t) i, srv_obj);
            }
        }
    }

    JS_SetPropertyStr(ctx, stream_obj, "servers", servers_arr);

    /* attach(sock) */
    JS_SetPropertyStr(ctx, stream_obj, "attach",
                      JS_NewCFunction(ctx, ngx_js_stream_attach,
                                      "attach", 1));

    JS_SetPropertyStr(ctx, nginx_obj, "stream", stream_obj);
    return NGX_OK;
}


/* ================================================================== */
/* Class registration / proto installation                             */
/* ================================================================== */

ngx_int_t
ngx_js_stream_listener_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_stream_server_class_id,
                    &ngx_js_stream_server_class) < 0)
    {
        return NGX_ERROR;
    }

    if (JS_NewClass(rt, ngx_js_stream_listener_class_id,
                    &ngx_js_stream_listener_class) < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_js_stream_listener_install_protos(JSContext *ctx)
{
    JSValue  proto;

    /* NginxStreamServer */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_server_proto_funcs,
                               countof(ngx_js_stream_server_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_server_class_id, proto);

    /* NginxStreamListener */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_listener_proto_funcs,
                               countof(ngx_js_stream_listener_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_listener_class_id, proto);

    return NGX_OK;
}
