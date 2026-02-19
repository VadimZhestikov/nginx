
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — read-only Phase 1.
 *
 * Exposes:
 *   nginx.http.servers[]           — NginxServer objects
 *   nginx.http.servers[i].name     — first server_name string
 *   nginx.http.servers[i].names[]  — all server_name strings
 *   nginx.http.servers[i].root     — default document root
 *   nginx.http.servers[i].locations[]
 *   nginx.http.servers[i].locations[j].path
 *   nginx.http.servers[i].locations[j].root
 *
 * nginx.http.upstreams[] is delegated to ngx_js_com_upstream.c.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static JSValue ngx_js_wrap_location(JSContext *ctx,
    ngx_http_core_loc_conf_t *clcf);

static void ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx);

static JSValue ngx_js_build_locations(JSContext *ctx,
    ngx_http_core_loc_conf_t *root_clcf);

static JSValue ngx_js_wrap_server(JSContext *ctx,
    ngx_http_core_srv_conf_t *cscf, ngx_cycle_t *cycle);


/* ------------------------------------------------------------------ */
/* NginxLocation wrapper                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_core_loc_conf_t  *clcf;
} ngx_js_location_opaque_t;


static void
ngx_js_location_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_location_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_location_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_location_class = {
    "NginxLocation",
    .finalizer = ngx_js_location_finalizer
};


/*
 * Magic values for ngx_js_location_get / ngx_js_location_set:
 *   0 — path    (r/o: location name/pattern)
 *   1 — root    (r/w)
 *   2 — handler (w/o: JS function name, installs ngx_js_content_handler)
 */
static JSValue
ngx_js_location_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf = op->clcf;

    switch (magic) {
    case 0: /* path */
        return JS_NewStringLen(ctx, (const char *) clcf->name.data,
                               clcf->name.len);
    case 1: /* root */
        return JS_NewStringLen(ctx, (const char *) clcf->root.data,
                               clcf->root.len);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_location_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_cycle_t               *cycle;
    const char                *cstr;
    size_t                     len;
    u_char                    *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf  = op->clcf;
    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);

    switch (magic) {
    case 1: /* root */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        clcf->root.data    = data;
        clcf->root.len     = len;
        clcf->root_lengths = NULL;  /* mark as literal (no variables) */
        return JS_UNDEFINED;

    case 2: /* handler — install a JS content handler for this location */
    {
        ngx_js_loc_conf_t  *jlcf;

        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];
        jlcf->handler.data = data;
        jlcf->handler.len  = len;

        /* Wire up the content handler pointer */
        clcf->handler = ngx_js_content_handler;
        return JS_UNDEFINED;
    }
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_location_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("path",    ngx_js_location_get, NULL,                0),
    JS_CGETSET_MAGIC_DEF("root",    ngx_js_location_get, ngx_js_location_set, 1),
    JS_CGETSET_MAGIC_DEF("handler", NULL,                ngx_js_location_set, 2),
};


static JSValue
ngx_js_wrap_location(JSContext *ctx, ngx_http_core_loc_conf_t *clcf)
{
    JSValue                    obj, proto;
    ngx_js_location_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_location_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->clcf = clcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_location_proto_funcs,
                               countof(ngx_js_location_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_location_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/*
 * Recursively walk the static location tree and append a NginxLocation
 * object for every exact or inclusive entry found.
 */
static void
ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx)
{
    if (node == NULL) {
        return;
    }

    /* left subtree */
    ngx_js_collect_locations(ctx, arr, node->left, idx);

    /* this node's exact match (= prefix) */
    if (node->exact) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location(ctx, node->exact));
    }

    /* this node's inclusive (prefix) match */
    if (node->inclusive) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location(ctx, node->inclusive));
    }

    /* child subtree (shared prefix children) */
    ngx_js_collect_locations(ctx, arr, node->tree, idx);

    /* right subtree */
    ngx_js_collect_locations(ctx, arr, node->right, idx);
}


/*
 * Build a JS Array of NginxLocation objects for a server's default
 * root location config (which holds the static_locations tree).
 */
static JSValue
ngx_js_build_locations(JSContext *ctx, ngx_http_core_loc_conf_t *root_clcf)
{
    JSValue   arr;
    uint32_t  idx;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    idx = 0;

#if (NGX_PCRE)
    if (root_clcf->regex_locations) {
        ngx_http_core_loc_conf_t  **rloc;

        for (rloc = root_clcf->regex_locations; *rloc; rloc++) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 ngx_js_wrap_location(ctx, *rloc));
        }
    }
#endif

    ngx_js_collect_locations(ctx, arr, root_clcf->static_locations, &idx);

    return arr;
}


/* ------------------------------------------------------------------ */
/* NginxServer wrapper                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_core_srv_conf_t  *cscf;
    ngx_cycle_t               *cycle;
} ngx_js_server_opaque_t;


static void
ngx_js_server_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_server_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_server_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_server_class = {
    "NginxServer",
    .finalizer = ngx_js_server_finalizer
};


/*
 * Magic values for ngx_js_server_get / ngx_js_server_set:
 *   0 — name   (r/o: first server_name entry, or "" if none)
 *   1 — root   (r/w: document root from the server's implicit / location)
 */
static JSValue
ngx_js_server_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_server_name_t    *sn;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf = op->cscf;

    switch (magic) {

    case 0: /* name — first server_name or "" */
        if (cscf->server_names.nelts == 0) {
            return JS_NewString(ctx, "");
        }

        sn = cscf->server_names.elts;
        return JS_NewStringLen(ctx, (const char *) sn[0].name.data,
                               sn[0].name.len);

    case 1: /* root — from the server's default (/) location config */
        clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
        return JS_NewStringLen(ctx, (const char *) clcf->root.data,
                               clcf->root.len);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_server_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_cycle_t               *cycle;
    const char                *cstr;
    size_t                     len;
    u_char                    *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf  = op->cscf;
    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);

    switch (magic) {
    case 1: /* root — server's default location */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
        clcf->root.data   = data;
        clcf->root.len    = len;
        clcf->root_lengths = NULL;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * nginx.http.servers[i].names — all server_name values as a JS Array.
 */
static JSValue
ngx_js_server_get_names(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t  *op;
    ngx_http_server_name_t  *sn;
    JSValue                  arr;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    sn = op->cscf->server_names.elts;

    for (i = 0; i < op->cscf->server_names.nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             JS_NewStringLen(ctx,
                                             (const char *) sn[i].name.data,
                                             sn[i].name.len));
    }

    return arr;
}


/*
 * nginx.http.servers[i].locations — JS Array of NginxLocation objects.
 */
static JSValue
ngx_js_server_get_locations(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_loc_conf_t  *clcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf = op->cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    return ngx_js_build_locations(ctx, clcf);
}


static const JSCFunctionListEntry ngx_js_server_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name",      ngx_js_server_get,           NULL,               0),
    JS_CGETSET_MAGIC_DEF("root",      ngx_js_server_get,           ngx_js_server_set,  1),
    JS_CGETSET_MAGIC_DEF("names",     ngx_js_server_get_names,     NULL,               0),
    JS_CGETSET_MAGIC_DEF("locations", ngx_js_server_get_locations, NULL,               0),
};


static JSValue
ngx_js_wrap_server(JSContext *ctx, ngx_http_core_srv_conf_t *cscf,
    ngx_cycle_t *cycle)
{
    JSValue                  obj, proto;
    ngx_js_server_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_server_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->cscf  = cscf;
    op->cycle = cycle;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_server_proto_funcs,
                               countof(ngx_js_server_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_server_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_http_com_install                                              */
/* ------------------------------------------------------------------ */

/*
 * Register the NginxServer and NginxLocation classes with this runtime.
 * Called by ngx_js_com_register_classes() from ngx_js_com.c.
 */
static ngx_int_t
ngx_js_http_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_server_class_id,   &ngx_js_server_class)   < 0
     || JS_NewClass(rt, ngx_js_location_class_id, &ngx_js_location_class) < 0)
    {
        return NGX_ERROR;
    }

    if (ngx_js_request_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Build nginx.http and attach it to nginx_obj.
 *
 *   nginx.http.servers[]    — Array of NginxServer
 *   nginx.http.upstreams[]  — delegated to ngx_js_upstream_com_install()
 */
ngx_int_t
ngx_js_http_com_install(JSContext *ctx, JSValue nginx_obj,
    ngx_cycle_t *cycle)
{
    JSRuntime                   *rt;
    JSValue                      http_obj, servers_arr;
    ngx_http_conf_ctx_t         *http_ctx;
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_uint_t                   i;

    rt = JS_GetRuntime(ctx);

    if (ngx_js_http_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    http_ctx = (ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index];
    if (http_ctx == NULL) {
        /* http{} block was not present in nginx.conf */
        http_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);
        return NGX_OK;
    }

    cmcf = http_ctx->main_conf[ngx_http_core_module.ctx_index];

    /* ---- Build nginx.http.servers[] ---- */

    servers_arr = JS_NewArray(ctx);
    if (JS_IsException(servers_arr)) {
        return NGX_ERROR;
    }

    cscfp = cmcf->servers.elts;

    for (i = 0; i < cmcf->servers.nelts; i++) {
        JS_SetPropertyUint32(ctx, servers_arr, (uint32_t) i,
                             ngx_js_wrap_server(ctx, cscfp[i], cycle));
    }

    /* ---- Assemble nginx.http ---- */

    http_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, http_obj, "servers", servers_arr);

    /* nginx.http.upstreams[] — delegated to upstream COM */
    if (ngx_js_upstream_com_install(ctx, http_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, http_obj);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);

    return NGX_OK;
}
