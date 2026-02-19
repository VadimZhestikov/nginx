
/*
 * Copyright (C) nginx JS contributors
 *
 * Upstream COM layer — Phase 1 read-only.
 *
 * Exposes:
 *   nginx.http.upstreams[]           — NginxUpstream objects
 *   nginx.http.upstreams[i].name     — upstream block name (r/o)
 *   nginx.http.upstreams[i].peers[]  — NginxPeer objects (r/o)
 *   peer.address, peer.weight, peer.maxFails, peer.down, peer.backup
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* NginxPeer wrapper (config-phase, ngx_http_upstream_server_t)        */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_upstream_server_t  *srv;
} ngx_js_peer_opaque_t;


static void
ngx_js_peer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_peer_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_peer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_peer_class = {
    "NginxPeer",
    .finalizer = ngx_js_peer_finalizer
};


/*
 * Magic values:
 *   0 — address   (r/o)
 *   1 — weight    (r/o)
 *   2 — maxFails  (r/o)
 *   3 — down      (r/o)
 *   4 — backup    (r/o)
 */
static JSValue
ngx_js_peer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_peer_opaque_t        *op;
    ngx_http_upstream_server_t  *srv;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    srv = op->srv;

    switch (magic) {
    case 0: return JS_NewStringLen(ctx, (const char *) srv->name.data,
                                   srv->name.len);
    case 1: return JS_NewInt32(ctx, (int32_t) srv->weight);
    case 2: return JS_NewInt32(ctx, (int32_t) srv->max_fails);
    case 3: return JS_NewBool(ctx, (int) srv->down);
    case 4: return JS_NewBool(ctx, (int) srv->backup);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_peer_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_peer_opaque_t        *op;
    ngx_http_upstream_server_t  *srv;
    int32_t                      i32;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    srv = op->srv;

    switch (magic) {
    case 1:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->weight = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 2:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->max_fails = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 3:
        srv->down = (ngx_uint_t) JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_peer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",  ngx_js_peer_get, NULL,            0),
    JS_CGETSET_MAGIC_DEF("weight",   ngx_js_peer_get, ngx_js_peer_set, 1),
    JS_CGETSET_MAGIC_DEF("maxFails", ngx_js_peer_get, ngx_js_peer_set, 2),
    JS_CGETSET_MAGIC_DEF("down",     ngx_js_peer_get, ngx_js_peer_set, 3),
    JS_CGETSET_MAGIC_DEF("backup",   ngx_js_peer_get, NULL,            4),
};


/* ------------------------------------------------------------------ */
/* NginxUpstream wrapper                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_upstream_srv_conf_t  *uscf;
} ngx_js_upstream_opaque_t;


static void
ngx_js_upstream_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_upstream_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_upstream_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_upstream_class = {
    "NginxUpstream",
    .finalizer = ngx_js_upstream_finalizer
};


static JSValue
ngx_js_upstream_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_upstream_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* name */
        return JS_NewStringLen(ctx,
                               (const char *) op->uscf->host.data,
                               op->uscf->host.len);
    }

    return JS_UNDEFINED;
}


/*
 * nginx.http.upstreams[i].peers[] — config-phase NginxPeer objects.
 */
static JSValue
ngx_js_upstream_get_peers(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_upstream_opaque_t    *op;
    ngx_http_upstream_server_t  *srv;
    JSValue                      arr;
    ngx_uint_t                   i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    if (op->uscf->servers == NULL) {
        return arr;
    }

    srv = op->uscf->servers->elts;

    for (i = 0; i < op->uscf->servers->nelts; i++) {
        ngx_js_peer_opaque_t  *pop;
        JSValue                obj, proto;

        pop = js_mallocz(ctx, sizeof(ngx_js_peer_opaque_t));
        if (!pop) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }

        pop->srv = &srv[i];

        proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, proto,
                                   ngx_js_peer_proto_funcs,
                                   countof(ngx_js_peer_proto_funcs));

        obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_peer_class_id);
        JS_FreeValue(ctx, proto);

        if (JS_IsException(obj)) {
            js_free(ctx, pop);
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }

        JS_SetOpaque(obj, pop);
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


static const JSCFunctionListEntry ngx_js_upstream_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name",  ngx_js_upstream_get,       NULL, 0),
    JS_CGETSET_MAGIC_DEF("peers", ngx_js_upstream_get_peers, NULL, 0),
};


static JSValue
ngx_js_wrap_upstream(JSContext *ctx, ngx_http_upstream_srv_conf_t *uscf)
{
    JSValue                    obj, proto;
    ngx_js_upstream_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_upstream_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->uscf = uscf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_upstream_proto_funcs,
                               countof(ngx_js_upstream_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_upstream_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_upstream_com_install                                          */
/* ------------------------------------------------------------------ */

static ngx_int_t
ngx_js_upstream_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_upstream_class_id, &ngx_js_upstream_class) < 0
     || JS_NewClass(rt, ngx_js_peer_class_id,     &ngx_js_peer_class)     < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Build nginx.http.upstreams[] and attach it to http_obj.
 * Called from ngx_js_http_com_install() in ngx_js_com_http.c.
 */
ngx_int_t
ngx_js_upstream_com_install(JSContext *ctx, JSValue http_obj,
    ngx_cycle_t *cycle)
{
    JSRuntime                      *rt;
    JSValue                         upstreams_arr;
    ngx_http_conf_ctx_t            *http_ctx;
    ngx_http_upstream_main_conf_t  *umcf;
    ngx_http_upstream_srv_conf_t  **uscfp;
    ngx_uint_t                      i;

    rt = JS_GetRuntime(ctx);

    if (ngx_js_upstream_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    upstreams_arr = JS_NewArray(ctx);
    if (JS_IsException(upstreams_arr)) {
        return NGX_ERROR;
    }

    http_ctx = (ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index];
    if (http_ctx == NULL) {
        JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    umcf = http_ctx->main_conf[ngx_http_upstream_module.ctx_index];
    if (umcf == NULL) {
        JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {
        if (!(uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE)) {
            continue;
        }

        JS_SetPropertyUint32(ctx, upstreams_arr, (uint32_t) i,
                             ngx_js_wrap_upstream(ctx, uscfp[i]));
    }

    JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);

    return NGX_OK;
}
