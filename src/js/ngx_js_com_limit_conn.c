
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 12b: limit_conn location configuration.
 *
 * Exposes location.limitConn as a NginxLimitConn object:
 *
 *   limits       object[]  array of {zone, conn}
 *     zone       string    shared memory zone name
 *     conn       number    maximum concurrent connections per key
 *   logLevel     string    limit_conn_log_level ("info"|"notice"|"warn"|"error")
 *   statusCode   number    limit_conn_status (default 503)
 *   dryRun       boolean   limit_conn_dry_run on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_limit_conn_module.h"


typedef struct {
    ngx_http_limit_conn_conf_t  *lccf;
} ngx_js_limit_conn_opaque_t;


static void
ngx_js_limit_conn_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_limit_conn_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_limit_conn_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_limit_conn_class = {
    "NginxLimitConn",
    .finalizer = ngx_js_limit_conn_finalizer
};


/*
 * Helper: map NGX_LOG_* numeric level to a string label.
 */
static JSValue
ngx_js_lc_log_level_str(JSContext *ctx, ngx_uint_t level)
{
    const char *s;

    switch (level) {
    case 4:  s = "error";  break;  /* NGX_LOG_ERR    */
    case 5:  s = "warn";   break;  /* NGX_LOG_WARN   */
    case 6:  s = "notice"; break;  /* NGX_LOG_NOTICE */
    case 7:  s = "info";   break;  /* NGX_LOG_INFO   */
    default: s = "error";  break;
    }

    return JS_NewString(ctx, s);
}


/*
 * limits getter: builds an array of limit objects.
 * Each element: {zone, conn}
 */
static JSValue
ngx_js_limit_conn_get_limits(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_limit_conn_opaque_t    *op;
    ngx_http_limit_conn_conf_t    *lccf;
    ngx_http_limit_conn_limit_t   *lim;
    JSValue                        arr, obj;
    ngx_uint_t                     i;
    ngx_str_t                     *name;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_conn_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    lccf = op->lccf;
    arr = JS_NewArray(ctx);

    if (lccf->limits.nelts == 0) {
        return arr;
    }

    lim = lccf->limits.elts;

    for (i = 0; i < lccf->limits.nelts; i++) {
        obj = JS_NewObject(ctx);

        name = &lim[i].shm_zone->shm.name;
        JS_SetPropertyStr(ctx, obj, "zone",
            JS_NewStringLen(ctx, (const char *) name->data, name->len));

        JS_SetPropertyStr(ctx, obj, "conn",
            JS_NewUint32(ctx, (uint32_t) lim[i].conn));

        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


/*
 * Magic values for ngx_js_limit_conn_get:
 *   0 — logLevel
 *   1 — statusCode
 *   2 — dryRun
 */
static JSValue
ngx_js_limit_conn_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_limit_conn_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_conn_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return ngx_js_lc_log_level_str(ctx, op->lccf->log_level);
    case 1: return JS_NewUint32(ctx, (uint32_t) op->lccf->status_code);
    case 2: return JS_NewBool(ctx, op->lccf->dry_run);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_limit_conn_proto_funcs[] = {
    JS_CGETSET_DEF       ("limits",     ngx_js_limit_conn_get_limits, NULL),
    JS_CGETSET_MAGIC_DEF ("logLevel",   ngx_js_limit_conn_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF ("statusCode", ngx_js_limit_conn_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF ("dryRun",     ngx_js_limit_conn_get, NULL, 2),
};


JSValue
ngx_js_wrap_limit_conn(JSContext *ctx, ngx_http_limit_conn_conf_t *lccf)
{
    JSValue                     obj, proto;
    ngx_js_limit_conn_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_limit_conn_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->lccf = lccf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_limit_conn_proto_funcs,
                               countof(ngx_js_limit_conn_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_limit_conn_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_limit_conn_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_limit_conn_class_id, &ngx_js_limit_conn_class) < 0
           ? NGX_ERROR : NGX_OK;
}
