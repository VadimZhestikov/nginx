
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
#include "../../stream/ngx_stream_proxy_module.h"
#include "../../stream/ngx_stream_access_module.h"


/* ------------------------------------------------------------------ */
/* Class IDs                                                           */
/* ------------------------------------------------------------------ */

JSClassID  ngx_js_stream_server_class_id;
JSClassID  ngx_js_stream_listener_class_id;
JSClassID  ngx_js_stream_proxy_class_id;

ngx_js_stream_listener_state_t
    *ngx_js_stream_listener_reg[NGX_JS_STREAM_LISTENER_REG_MAX];


/* Forward declaration — NginxStreamProxy is defined below NginxStreamServer
 * but is referenced from ngx_js_stream_server_get (case 6: server.proxy). */
static JSValue ngx_js_wrap_stream_proxy(JSContext *ctx,
    ngx_stream_proxy_srv_conf_t *pscf, ngx_cycle_t *cycle);


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


/*
 * Getter magic: 0=serverName 1=tcpNodelay 2=prereadBufferSize
 *               3=prereadTimeout 4=resolverTimeout 5=proxyProtocolTimeout
 *               6=proxy (NginxStreamProxy)  7=access (NginxStreamAccess)
 * Setter magic: 1-5 (serverName, proxy, and access are read-only)
 */
static JSValue
ngx_js_stream_server_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_server_opaque_t  *op;
    ngx_stream_core_srv_conf_t     *cscf;
    ngx_stream_proxy_srv_conf_t    *pscf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf = op->cscf;

    switch (magic) {
    case 0:   /* serverName */
        return JS_NewStringLen(ctx, (char *) cscf->server_name.data,
                               cscf->server_name.len);
    case 1:   /* tcpNodelay */
        return JS_NewBool(ctx, cscf->tcp_nodelay);
    case 2:   /* prereadBufferSize */
        return JS_NewInt64(ctx, (int64_t) cscf->preread_buffer_size);
    case 3:   /* prereadTimeout */
        return JS_NewInt64(ctx, (int64_t) cscf->preread_timeout);
    case 4:   /* resolverTimeout */
        return JS_NewInt64(ctx, (int64_t) cscf->resolver_timeout);
    case 5:   /* proxyProtocolTimeout */
        return JS_NewInt64(ctx, (int64_t) cscf->proxy_protocol_timeout);
    case 6:   /* proxy — NginxStreamProxy */
        pscf = cscf->ctx->srv_conf[ngx_stream_proxy_module.ctx_index];
        if (pscf == NULL) {
            return JS_NULL;
        }
        return ngx_js_wrap_stream_proxy(ctx, pscf, op->cycle);
    case 7:   /* access — NginxStreamAccess */
    {
        ngx_stream_access_srv_conf_t  *ascf;

        ascf = cscf->ctx->srv_conf[ngx_stream_access_module.ctx_index];
        if (ascf == NULL) {
            return JS_NULL;
        }
        return ngx_js_wrap_stream_access(ctx, ascf);
    }
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_stream_server_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_stream_server_opaque_t  *op;
    ngx_stream_core_srv_conf_t     *cscf;
    int64_t                         n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf = op->cscf;

    switch (magic) {
    case 1: /* tcpNodelay */
        cscf->tcp_nodelay = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 2: /* prereadBufferSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        cscf->preread_buffer_size = (size_t) n;
        return JS_UNDEFINED;
    case 3: /* prereadTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        cscf->preread_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 4: /* resolverTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        cscf->resolver_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 5: /* proxyProtocolTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        cscf->proxy_protocol_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_server_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("serverName",           ngx_js_stream_server_get, NULL,                     0),
    JS_CGETSET_MAGIC_DEF("tcpNodelay",           ngx_js_stream_server_get, ngx_js_stream_server_set, 1),
    JS_CGETSET_MAGIC_DEF("prereadBufferSize",    ngx_js_stream_server_get, ngx_js_stream_server_set, 2),
    JS_CGETSET_MAGIC_DEF("prereadTimeout",       ngx_js_stream_server_get, ngx_js_stream_server_set, 3),
    JS_CGETSET_MAGIC_DEF("resolverTimeout",      ngx_js_stream_server_get, ngx_js_stream_server_set, 4),
    JS_CGETSET_MAGIC_DEF("proxyProtocolTimeout", ngx_js_stream_server_get, ngx_js_stream_server_set, 5),
    JS_CGETSET_MAGIC_DEF("proxy",                ngx_js_stream_server_get, NULL,                     6),
    JS_CGETSET_MAGIC_DEF("access",               ngx_js_stream_server_get, NULL,                     7),
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
/* NginxStreamProxy                                                    */
/* ================================================================== */

typedef struct {
    ngx_stream_proxy_srv_conf_t  *pscf;
    ngx_cycle_t                  *cycle;
} ngx_js_stream_proxy_opaque_t;


static void
ngx_js_stream_proxy_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_proxy_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_proxy_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_proxy_class = {
    "NginxStreamProxy",
    .finalizer = ngx_js_stream_proxy_finalizer,
};


/*
 * Magic values for ngx_js_stream_proxy_get / ngx_js_stream_proxy_set:
 *   0 — connectTimeout      (ms)       writable
 *   1 — timeout             (ms)       writable
 *   2 — nextUpstreamTimeout (ms)       writable
 *   3 — bufferSize          (bytes)    writable
 *   4 — requests            (count)    read-only (runtime stat)
 *   5 — responses           (count)    read-only (runtime stat)
 *   6 — nextUpstreamTries   (count)    writable
 *   7 — nextUpstream        (boolean)  writable
 *   8 — proxyProtocol       (boolean)  writable
 *   9 — halfClose           (boolean)  writable
 *  10 — socketKeepalive     (boolean)  writable
 *  11 — pass                (string)   read-only (upstream name / "dynamic")
 */
static JSValue
ngx_js_stream_proxy_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_proxy_opaque_t  *op;
    ngx_stream_proxy_srv_conf_t   *pscf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    pscf = op->pscf;

    switch (magic) {
    case 0:  return JS_NewInt64(ctx, (int64_t) pscf->connect_timeout);
    case 1:  return JS_NewInt64(ctx, (int64_t) pscf->timeout);
    case 2:  return JS_NewInt64(ctx, (int64_t) pscf->next_upstream_timeout);
    case 3:  return JS_NewInt64(ctx, (int64_t) pscf->buffer_size);
    case 4:  return JS_NewInt64(ctx, (int64_t) pscf->requests);
    case 5:  return JS_NewInt64(ctx, (int64_t) pscf->responses);
    case 6:  return JS_NewInt64(ctx, (int64_t) pscf->next_upstream_tries);
    case 7:  return JS_NewBool(ctx,  (int) pscf->next_upstream);
    case 8:  return JS_NewBool(ctx,  (int) pscf->proxy_protocol);
    case 9:  return JS_NewBool(ctx,  (int) pscf->half_close);
    case 10: return JS_NewBool(ctx,  (int) pscf->socket_keepalive);
    case 11: /* pass — upstream name or "dynamic" */
        if (pscf->upstream != NULL) {
            return JS_NewStringLen(ctx,
                                   (const char *) pscf->upstream->host.data,
                                   pscf->upstream->host.len);
        }
        if (pscf->upstream_value != NULL) {
            return JS_NewString(ctx, "dynamic");
        }
        return JS_NULL;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_stream_proxy_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_stream_proxy_opaque_t  *op;
    ngx_stream_proxy_srv_conf_t   *pscf;
    int64_t                        n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    pscf = op->pscf;

    switch (magic) {
    case 0: /* connectTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        pscf->connect_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 1: /* timeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        pscf->timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 2: /* nextUpstreamTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        pscf->next_upstream_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 3: /* bufferSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        pscf->buffer_size = (size_t) n;
        return JS_UNDEFINED;
    /* case 4: requests  — r/o runtime stat */
    /* case 5: responses — r/o runtime stat */
    case 6: /* nextUpstreamTries */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        pscf->next_upstream_tries = (ngx_uint_t) n;
        return JS_UNDEFINED;
    case 7: /* nextUpstream */
        pscf->next_upstream = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 8: /* proxyProtocol */
        pscf->proxy_protocol = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 9: /* halfClose */
        pscf->half_close = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 10: /* socketKeepalive */
        pscf->socket_keepalive = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_proxy_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("connectTimeout",      ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  0),
    JS_CGETSET_MAGIC_DEF("timeout",             ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  1),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTimeout", ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  2),
    JS_CGETSET_MAGIC_DEF("bufferSize",          ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  3),
    JS_CGETSET_MAGIC_DEF("requests",            ngx_js_stream_proxy_get, NULL,                     4),
    JS_CGETSET_MAGIC_DEF("responses",           ngx_js_stream_proxy_get, NULL,                     5),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTries",   ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  6),
    JS_CGETSET_MAGIC_DEF("nextUpstream",        ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  7),
    JS_CGETSET_MAGIC_DEF("proxyProtocol",       ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  8),
    JS_CGETSET_MAGIC_DEF("halfClose",           ngx_js_stream_proxy_get, ngx_js_stream_proxy_set,  9),
    JS_CGETSET_MAGIC_DEF("socketKeepalive",     ngx_js_stream_proxy_get, ngx_js_stream_proxy_set, 10),
    JS_CGETSET_MAGIC_DEF("pass",                ngx_js_stream_proxy_get, NULL,                    11),
};


static JSValue
ngx_js_wrap_stream_proxy(JSContext *ctx, ngx_stream_proxy_srv_conf_t *pscf,
    ngx_cycle_t *cycle)
{
    JSValue                        obj;
    ngx_js_stream_proxy_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_proxy_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->pscf  = pscf;
    op->cycle = cycle;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_proxy_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
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
    st->cycle                        = cycle;

    if (ngx_js_stream_listener_activate(st, cycle) != NGX_OK) {
        return JS_ThrowInternalError(ctx,
            "addServer: failed to activate stream listener");
    }

    return JS_DupValue(ctx, argv[0]);
}


/* ------------------------------------------------------------------ */
/* ngx_js_stream_listener_build_vnames — Phase H helper                */
/*                                                                     */
/* Rebuilds ngx_stream_virtual_names_t from st->vservers[] and         */
/* installs it in st->addr.conf.virtual_names.                         */
/* ------------------------------------------------------------------ */

static int
ngx_js_stream_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first  = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


static ngx_int_t
ngx_js_stream_listener_build_vnames(ngx_js_stream_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_uint_t                    s, n;
    ngx_stream_core_srv_conf_t   *cscf;
    ngx_stream_server_name_t     *sn;
    ngx_stream_virtual_names_t   *vn;
    ngx_stream_core_main_conf_t  *cmcf;
    ngx_hash_init_t               hash;
    ngx_hash_keys_arrays_t        ha;
    ngx_pool_t                   *temp_pool;
    ngx_int_t                     rc;

    if (st->nvservers == 0) {
        st->addr.conf.virtual_names = NULL;
        return NGX_OK;
    }

    cmcf = ngx_stream_cycle_get_module_main_conf(cycle, ngx_stream_core_module);
    if (cmcf == NULL) {
        return NGX_ERROR;
    }

    temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cycle->log);
    if (temp_pool == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.temp_pool = temp_pool;
    ha.pool      = cycle->pool;

    if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
        ngx_destroy_pool(temp_pool);
        return NGX_ERROR;
    }

    for (s = 0; s < st->nvservers; s++) {
        cscf = st->vservers[s];
        sn   = cscf->server_names.elts;

        for (n = 0; n < cscf->server_names.nelts; n++) {
#if (NGX_PCRE)
            if (sn[n].regex) {
                continue;
            }
#endif
            rc = ngx_hash_add_key(&ha, &sn[n].name, sn[n].server,
                                  NGX_HASH_WILDCARD_KEY);
            if (rc == NGX_ERROR) {
                ngx_destroy_pool(temp_pool);
                return NGX_ERROR;
            }

            if (rc == NGX_BUSY) {
                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                              "JS stream listener: duplicate server name"
                              " \"%V\", ignored", &sn[n].name);
            }
        }
    }

    vn = ngx_pcalloc(cycle->pool, sizeof(ngx_stream_virtual_names_t));
    if (vn == NULL) {
        ngx_destroy_pool(temp_pool);
        return NGX_ERROR;
    }

    ngx_memzero(&hash, sizeof(ngx_hash_init_t));
    hash.key         = ngx_hash_key_lc;
    hash.max_size    = cmcf->server_names_hash_max_size;
    hash.bucket_size = cmcf->server_names_hash_bucket_size;
    hash.name        = "js_stream_listener_server_names_hash";
    hash.pool        = cycle->pool;

    if (ha.keys.nelts) {
        hash.hash      = &vn->names.hash;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }
    }

    if (ha.dns_wc_head.nelts) {
        ngx_qsort(ha.dns_wc_head.elts, ha.dns_wc_head.nelts,
                  sizeof(ngx_hash_key_t), ngx_js_stream_cmp_dns_wildcards);

        hash.hash      = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts,
                                   ha.dns_wc_head.nelts) != NGX_OK)
        {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }

        vn->names.wc_head = (ngx_hash_wildcard_t *) hash.hash;
    }

    if (ha.dns_wc_tail.nelts) {
        ngx_qsort(ha.dns_wc_tail.elts, ha.dns_wc_tail.nelts,
                  sizeof(ngx_hash_key_t), ngx_js_stream_cmp_dns_wildcards);

        hash.hash      = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                   ha.dns_wc_tail.nelts) != NGX_OK)
        {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }

        vn->names.wc_tail = (ngx_hash_wildcard_t *) hash.hash;
    }

#if (NGX_PCRE)
    {
        ngx_uint_t  nregex = 0;

        for (s = 0; s < st->nvservers; s++) {
            cscf = st->vservers[s];
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                if (sn[n].regex) {
                    nregex++;
                }
            }
        }

        if (nregex) {
            vn->nregex = nregex;
            vn->regex  = ngx_palloc(cycle->pool,
                                    nregex * sizeof(ngx_stream_server_name_t));
            if (vn->regex == NULL) {
                ngx_destroy_pool(temp_pool);
                return NGX_ERROR;
            }

            nregex = 0;
            for (s = 0; s < st->nvservers; s++) {
                cscf = st->vservers[s];
                sn   = cscf->server_names.elts;
                for (n = 0; n < cscf->server_names.nelts; n++) {
                    if (sn[n].regex) {
                        vn->regex[nregex++] = sn[n];
                    }
                }
            }
        }
    }
#endif

    ngx_destroy_pool(temp_pool);

    st->addr.conf.virtual_names = vn;
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* listener.addVirtualServer(srv) — Phase H implementation             */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_stream_listener_add_virtual_server(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv)
{
    ngx_js_stream_listener_opaque_t  *op;
    ngx_js_stream_listener_state_t   *st;
    ngx_stream_core_srv_conf_t       *cscf;
    ngx_cycle_t                      *cycle;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "stream.listener.addVirtualServer: post-fork not supported");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_STREAM_LISTENER_REG_MAX
        || ngx_js_stream_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx,
            "stream.listener.addVirtualServer: invalid handle");
    }

    st = ngx_js_stream_listener_reg[op->handle];

    if (!st->activated) {
        return JS_ThrowInternalError(ctx,
            "stream.listener.addVirtualServer: call addServer() first");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "stream.listener.addVirtualServer: NginxStreamServer argument"
            " required");
    }

    cscf = ngx_js_stream_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "stream.listener.addVirtualServer: argument must be a"
            " NginxStreamServer");
    }

    if (st->nvservers >= NGX_JS_STREAM_LISTENER_VSERVERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "stream.listener.addVirtualServer: virtual server limit reached"
            " (max %d)", NGX_JS_STREAM_LISTENER_VSERVERS_MAX);
    }

    st->vservers[st->nvservers++] = cscf;

    if (ngx_js_stream_listener_build_vnames(st, st->cycle) != NGX_OK) {
        st->nvservers--;
        st->vservers[st->nvservers] = NULL;
        return JS_ThrowInternalError(ctx,
            "stream.listener.addVirtualServer: failed to build"
            " virtual names hash");
    }

    return JS_DupValue(ctx, argv[0]);
}


static const JSCFunctionListEntry  ngx_js_stream_listener_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",            ngx_js_stream_listener_get,              NULL, 0),
    JS_CFUNC_DEF(        "addServer",          1, ngx_js_stream_listener_add_server),
    JS_CFUNC_DEF(        "addVirtualServer",   1, ngx_js_stream_listener_add_virtual_server),
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

    cmcf = NULL;

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

    /* nginx.stream main-conf properties (r/o) */
    if (cmcf != NULL) {
        JS_SetPropertyStr(ctx, stream_obj, "serverNamesHashMaxSize",
            JS_NewInt64(ctx, (int64_t) cmcf->server_names_hash_max_size));
        JS_SetPropertyStr(ctx, stream_obj, "serverNamesHashBucketSize",
            JS_NewInt64(ctx, (int64_t) cmcf->server_names_hash_bucket_size));
        JS_SetPropertyStr(ctx, stream_obj, "variablesHashMaxSize",
            JS_NewInt64(ctx, (int64_t) cmcf->variables_hash_max_size));
        JS_SetPropertyStr(ctx, stream_obj, "variablesHashBucketSize",
            JS_NewInt64(ctx, (int64_t) cmcf->variables_hash_bucket_size));
    }

    /* nginx.stream.upstreams[] */
    if (ngx_js_stream_upstream_com_install(ctx, stream_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, stream_obj);
        return NGX_ERROR;
    }

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

    if (JS_NewClass(rt, ngx_js_stream_proxy_class_id,
                    &ngx_js_stream_proxy_class) < 0)
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

    /* NginxStreamProxy */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_proxy_proto_funcs,
                               countof(ngx_js_stream_proxy_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_proxy_class_id, proto);

    return NGX_OK;
}
