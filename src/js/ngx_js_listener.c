
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


/* F2: ngx_js_wrap_server is defined in ngx_js_com_http.c */
extern JSValue  ngx_js_wrap_server(JSContext *ctx,
    ngx_http_core_srv_conf_t *cscf, ngx_cycle_t *cycle);


JSClassID                     ngx_js_http_listener_class_id;
JSClassID                     ngx_js_connection_class_id;
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
/* NginxConnection — JS wrapper for a freshly-accepted ngx_connection_t */
/* P4: created inside ngx_js_http_accept_handler, short-lived.         */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_connection_t  *c;
    unsigned           rejected:1;
} ngx_js_conn_opaque_t;


static void
ngx_js_connection_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_conn_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_connection_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_connection_class = {
    "NginxConnection",
    .finalizer = ngx_js_connection_finalizer,
};


/* magic: 0=remoteAddr  1=remotePort */
static JSValue
ngx_js_connection_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_conn_opaque_t  *op;
    ngx_connection_t      *c;
    u_char                 buf[NGX_SOCKADDR_STRLEN];
    size_t                 len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_connection_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    c = op->c;

    switch (magic) {
    case 0: /* remoteAddr — IP only, no port */
        len = ngx_sock_ntop(c->sockaddr, c->socklen, buf, sizeof(buf), 0);
        if (len == 0) {
            return JS_NewString(ctx, "");
        }
        return JS_NewStringLen(ctx, (char *) buf, len);

    case 1: /* remotePort */
        switch (c->sockaddr->sa_family) {
        case AF_INET:
            return JS_NewInt32(ctx, ntohs(
                ((struct sockaddr_in *) c->sockaddr)->sin_port));
#if (NGX_HAVE_INET6)
        case AF_INET6:
            return JS_NewInt32(ctx, ntohs(
                ((struct sockaddr_in6 *) c->sockaddr)->sin6_port));
#endif
        default:
            return JS_NewInt32(ctx, 0);
        }
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_connection_reject(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_conn_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_connection_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rejected = 1;
    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_connection_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("remoteAddr", ngx_js_connection_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("remotePort", ngx_js_connection_get, NULL, 1),
    JS_CFUNC_DEF(        "reject",     0, ngx_js_connection_reject),
};


static JSValue
ngx_js_wrap_connection(JSContext *ctx, ngx_connection_t *c)
{
    ngx_js_conn_opaque_t  *op;
    JSValue                obj;

    op = js_mallocz(ctx, sizeof(ngx_js_conn_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->c = c;

    obj = JS_NewObjectClass(ctx, ngx_js_connection_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return obj;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* NginxHttpListener property getters                                  */
/* magic: 0=address  1=socket  2=serverNames                          */
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

    case 1: /* socket — NginxSocket back-reference */
        return ngx_js_socket_wrap(ctx, st->socket_handle);

    case 2: /* serverNames[] — array of server name strings */
    {
        JSValue                    arr;
        ngx_uint_t                 idx, s, n;
        ngx_http_core_srv_conf_t  *cscf;
        ngx_http_server_name_t    *sn;

        arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) {
            return arr;
        }

        idx = 0;

        /* default server */
        if (st->default_server != NULL) {
            cscf = st->default_server;
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                    JS_NewStringLen(ctx, (char *) sn[n].name.data,
                                   sn[n].name.len));
            }
        }

        /* virtual servers */
        for (s = 0; s < st->nvservers; s++) {
            cscf = st->vservers[s];
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                    JS_NewStringLen(ctx, (char *) sn[n].name.data,
                                   sn[n].name.len));
            }
        }

        return arr;
    }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Accept hook registry — P4                                           */
/* Functions stored in global __ngx_accept_hooks__ array (GC root).   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_accept_hooks_get_registry(JSContext *ctx)
{
    JSValue  global, reg;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_accept_hooks__");

    if (JS_IsUndefined(reg)) {
        JS_FreeValue(ctx, reg);
        reg = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_accept_hooks__",
                          JS_DupValue(ctx, reg));
    }

    JS_FreeValue(ctx, global);
    return reg;
}


static uint32_t
ngx_js_accept_hook_register_fn(JSContext *ctx, JSValueConst fn)
{
    JSValue   reg, lenval;
    uint32_t  idx;

    reg    = ngx_js_accept_hooks_get_registry(ctx);
    lenval = JS_GetPropertyStr(ctx, reg, "length");
    JS_ToUint32(ctx, &idx, lenval);
    JS_FreeValue(ctx, lenval);
    JS_SetPropertyUint32(ctx, reg, idx, JS_DupValue(ctx, fn));
    JS_FreeValue(ctx, reg);

    return idx;
}


static JSValue
ngx_js_accept_hook_get_fn(JSContext *ctx, uint32_t idx)
{
    JSValue  reg, fn;

    reg = ngx_js_accept_hooks_get_registry(ctx);
    fn  = JS_GetPropertyUint32(ctx, reg, idx);
    JS_FreeValue(ctx, reg);

    return fn;
}


/* ------------------------------------------------------------------ */
/* C accept handler — replaces ngx_http_init_connection for JS        */
/* listeners that have at least one accept hook registered.            */
/* ------------------------------------------------------------------ */

static void
ngx_js_http_accept_handler(ngx_connection_t *c)
{
    ngx_js_http_listener_state_t  *st;
    ngx_js_conf_t                 *jcf;
    ngx_js_conn_opaque_t          *op;
    JSContext                     *ctx;
    JSRuntime                     *rt;
    JSValue                        conn_obj, fn, ret;
    uint32_t                      *indices;
    ngx_uint_t                     i;

    /*
     * Recover the listener state from ls->servers, which points to &st->port.
     * Use offsetof to convert the port pointer back to the containing struct.
     */
    st = (ngx_js_http_listener_state_t *)
             ((u_char *) c->listening->servers
              - offsetof(ngx_js_http_listener_state_t, port));

    /*
     * Get the JS context.  jcf->ctx is COW-shared; in worker processes
     * its opaque points to the current ngx_js_worker_t.
     */
    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        ngx_http_init_connection(c);
        return;
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);

    /* Wrap the raw connection */
    conn_obj = ngx_js_wrap_connection(ctx, c);
    if (JS_IsException(conn_obj)) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "js accept hook: failed to allocate NginxConnection");
        ngx_http_init_connection(c);
        return;
    }

    op      = JS_GetOpaque(conn_obj, ngx_js_connection_class_id);
    indices = st->accept_handlers;

    for (i = 0; i < st->n_accept_handlers; i++) {
        fn  = ngx_js_accept_hook_get_fn(ctx, indices[i]);
        ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &conn_obj);
        JS_FreeValue(ctx, fn);

        if (JS_IsException(ret)) {
            ngx_js_log_exception(ctx, c->log);
            JS_FreeValue(ctx, ret);
            op->rejected = 1;
            break;
        }

        JS_FreeValue(ctx, ret);

        /* Drain microtasks */
        while (JS_ExecutePendingJob(rt, NULL) > 0) { /* empty */ }

        if (op->rejected) {
            break;
        }
    }

    if (op->rejected) {
        JS_FreeValue(ctx, conn_obj);
        ngx_close_connection(c);
        return;
    }

    JS_FreeValue(ctx, conn_obj);
    ngx_http_init_connection(c);
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


/* ------------------------------------------------------------------ */
/* listener.serverByName(name) — F2 cross-reference                   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_server_by_name(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_http_server_name_t        *sn;
    ngx_cycle_t                   *cycle;
    const char                    *query;
    char                          *lc;
    size_t                         qlen, i;
    ngx_uint_t                     s, n;
    int                            found;

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

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "listener.serverByName: string argument required");
    }

    query = JS_ToCString(ctx, argv[0]);
    if (!query) {
        return JS_EXCEPTION;
    }

    qlen = strlen(query);
    lc   = js_malloc(ctx, qlen + 1);
    if (!lc) {
        JS_FreeCString(ctx, query);
        return JS_EXCEPTION;
    }

    for (i = 0; i < qlen; i++) {
        lc[i] = (char) ngx_tolower((u_char) query[i]);
    }
    lc[qlen] = '\0';
    JS_FreeCString(ctx, query);

    /* We need cycle for ngx_js_wrap_server — use ngx_cycle global */
    cycle = (ngx_cycle_t *) ngx_cycle;

    cscf  = NULL;
    found = 0;

    /* Search default server */
    if (st->default_server != NULL) {
        sn = st->default_server->server_names.elts;
        for (n = 0; n < st->default_server->server_names.nelts; n++) {
            if (sn[n].name.len == qlen
                && ngx_strncasecmp(sn[n].name.data,
                                   (u_char *) lc, qlen) == 0)
            {
                cscf  = st->default_server;
                found = 1;
                break;
            }
        }
    }

    /* Search virtual servers */
    if (!found) {
        for (s = 0; s < st->nvservers && !found; s++) {
            sn = st->vservers[s]->server_names.elts;
            for (n = 0; n < st->vservers[s]->server_names.nelts; n++) {
                if (sn[n].name.len == qlen
                    && ngx_strncasecmp(sn[n].name.data,
                                       (u_char *) lc, qlen) == 0)
                {
                    cscf  = st->vservers[s];
                    found = 1;
                    break;
                }
            }
        }
    }

    js_free(ctx, lc);

    if (!found) {
        return JS_NULL;
    }

    return ngx_js_wrap_server(ctx, cscf, cycle);
}


/* ------------------------------------------------------------------ */
/* listener.on(event, fn) — P4: register accept hook                  */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_on(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    const char                    *event;
    uint32_t                       idx;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.on: cannot register hooks after fork");
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

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "listener.on: expected (event, fn)");
    }

    event = JS_ToCString(ctx, argv[0]);
    if (!event) {
        return JS_EXCEPTION;
    }

    if (ngx_strcmp(event, "accept") != 0) {
        JS_FreeCString(ctx, event);
        return JS_ThrowTypeError(ctx,
            "listener.on: unknown event (expected 'accept')");
    }

    JS_FreeCString(ctx, event);

    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
            "listener.on: second argument must be a function");
    }

    if (st->n_accept_handlers >= NGX_JS_ACCEPT_HANDLERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.on: accept handler limit reached (max %d)",
            NGX_JS_ACCEPT_HANDLERS_MAX);
    }

    idx = ngx_js_accept_hook_register_fn(ctx, argv[1]);
    st->accept_handlers[st->n_accept_handlers++] = idx;

    /*
     * If this is the first handler and the listener is already activated,
     * switch ls->handler from ngx_http_init_connection to our wrapper.
     * If not yet activated, ngx_js_listener_activate will pick this up.
     */
    if (st->n_accept_handlers == 1 && st->ls != NULL) {
        st->ls->handler = ngx_js_http_accept_handler;
    }

    return JS_DupValue(ctx, this_val);   /* allow chaining */
}


static const JSCFunctionListEntry  ngx_js_listener_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",          ngx_js_listener_get,              NULL, 0),
    JS_CGETSET_MAGIC_DEF("socket",           ngx_js_listener_get,              NULL, 1),
    JS_CGETSET_MAGIC_DEF("serverNames",      ngx_js_listener_get,              NULL, 2),
    JS_CFUNC_DEF(        "serverByName",     1, ngx_js_listener_server_by_name),
    JS_CFUNC_DEF(        "addServer",        1, ngx_js_listener_add_server),
    JS_CFUNC_DEF(        "addVirtualServer", 1, ngx_js_listener_add_virtual_server),
    JS_CFUNC_DEF(        "on",               2, ngx_js_listener_on),
};


ngx_int_t
ngx_js_listener_register_class(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_http_listener_class_id,
                    &ngx_js_listener_class) < 0)
    {
        return NGX_ERROR;
    }

    if (JS_NewClass(rt, ngx_js_connection_class_id,
                    &ngx_js_connection_class) < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
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

    /* NginxConnection proto */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_connection_proto_funcs,
                               countof(ngx_js_connection_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_connection_class_id, proto);

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Wrap a listener registry slot in a JS NginxHttpListener object      */
/* ------------------------------------------------------------------ */

JSValue
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

    ls->handler = (st->n_accept_handlers > 0)
                  ? ngx_js_http_accept_handler
                  : ngx_http_init_connection;
    st->ls = ls;
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

    sock->in_listening = 1;
    st->activated      = 1;
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
