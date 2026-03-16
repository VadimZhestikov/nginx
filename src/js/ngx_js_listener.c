
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 Phases B-D — nginx.http.attach(sock) + listener methods
 *
 * Phase B — nginx.http.attach(sock):
 *   Wires a NginxSocket into nginx's HTTP connection pipeline by building
 *   the routing structures (ngx_http_port_t / ngx_http_in_addr_t /
 *   ngx_http_addr_conf_t) and preparing the ngx_listening_t template.
 *
 * Phase C — listener.addServer(srv):
 *   Sets the default_server on the listener and activates it by pushing
 *   the ngx_listening_t into cycle->listening.
 *
 * Phase D — listener.addVirtualServer(srv):
 *   Adds an additional server for Host-header routing.  The virtual_names
 *   hash (ngx_http_virtual_names_t) is rebuilt from all vservers[] entries
 *   and stored in addr_conf->virtual_names so workers see the updated table
 *   after fork.
 *
 * JS API:
 *
 *   const listener = nginx.http.attach(sock);
 *   listener.address                    // "host:port" string
 *   listener.addServer(srv)             // activate with default server
 *   listener.addVirtualServer(srv)      // add named virtual host
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
/* ngx_js_listener_build_vnames — Phase D helper                       */
/*                                                                     */
/* Builds ngx_http_virtual_names_t from st->vservers[] and installs it */
/* in st->addr.conf.virtual_names.  Hash tables are allocated in       */
/* cycle->pool; a temporary pool is used for ngx_hash_keys_arrays_t.  */
/* ------------------------------------------------------------------ */

static int
ngx_js_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first  = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


static ngx_int_t
ngx_js_listener_build_vnames(ngx_js_http_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_uint_t                  s, n;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_server_name_t     *sn;
    ngx_http_virtual_names_t   *vn;
    ngx_http_conf_ctx_t        *http_ctx;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_pool_t                 *temp_pool;
    ngx_int_t                   rc;

    if (st->nvservers == 0) {
        st->addr.conf.virtual_names = NULL;
        return NGX_OK;
    }

    http_ctx = (ngx_http_conf_ctx_t *)
                   cycle->conf_ctx[ngx_http_module.index];
    cmcf     = http_ctx->main_conf[ngx_http_core_module.ctx_index];

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
                              "JS listener: duplicate server name \"%V\","
                              " ignored", &sn[n].name);
            }
        }
    }

    vn = ngx_pcalloc(cycle->pool, sizeof(ngx_http_virtual_names_t));
    if (vn == NULL) {
        ngx_destroy_pool(temp_pool);
        return NGX_ERROR;
    }

    ngx_memzero(&hash, sizeof(ngx_hash_init_t));
    hash.key         = ngx_hash_key_lc;
    hash.max_size    = cmcf->server_names_hash_max_size;
    hash.bucket_size = cmcf->server_names_hash_bucket_size;
    hash.name        = "js_listener_server_names_hash";
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
                  sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

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
                  sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

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
                                    nregex * sizeof(ngx_http_server_name_t));
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
/* listener.addVirtualServer(srv) — Phase D implementation             */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_virtual_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_cycle_t                   *cycle;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: post-fork not yet supported");
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

    if (!st->activated) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: call addServer() first to activate"
            " the listener");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "listener.addVirtualServer: NginxServer argument required");
    }

    cscf = ngx_js_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "listener.addVirtualServer: argument must be a NginxServer"
            " object");
    }

    if (st->nvservers >= NGX_JS_LISTENER_VSERVERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: virtual server limit reached"
            " (max %d)", NGX_JS_LISTENER_VSERVERS_MAX);
    }

    st->vservers[st->nvservers++] = cscf;

    if (ngx_js_listener_build_vnames(st, cycle) != NGX_OK) {
        st->nvservers--;
        st->vservers[st->nvservers] = NULL;
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: failed to build virtual names hash");
    }

    return JS_DupValue(ctx, argv[0]);
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
    JS_CGETSET_MAGIC_DEF("address",          ngx_js_listener_get,              NULL, 0),
    JS_CFUNC_DEF(        "addServer",        1, ngx_js_listener_add_server),
    JS_CFUNC_DEF(        "addVirtualServer", 1, ngx_js_listener_add_virtual_server),
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
