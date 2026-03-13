
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 12a: limit_req location configuration.
 *
 * Exposes location.limitReq as a NginxLimitReq object:
 *
 *   limits         object[]  array of {zone, burst, nodelay, delay}
 *     zone         string    shared memory zone name
 *     burst        number    burst queue size (0 = no burst)
 *     nodelay      boolean   true when "nodelay" was specified
 *     delay        number    delay threshold (requests before delay kicks in);
 *                            0 when nodelay is true (NGX_MAX_UINT32_VALUE)
 *   logLevel       string    limit_req_log_level ("info"|"notice"|"warn"|"error")
 *   delayLogLevel  string    derived delay log level
 *   statusCode     number    limit_req_status (default 503)
 *   dryRun         boolean   limit_req_dry_run on/off
 *
 * NGX_MAX_UINT32_VALUE for delay means "nodelay" was specified.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_limit_req_module.h"


typedef struct {
    ngx_http_limit_req_conf_t  *lrcf;
} ngx_js_limit_req_opaque_t;


static void
ngx_js_limit_req_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_limit_req_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_limit_req_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_limit_req_class = {
    "NginxLimitReq",
    .finalizer = ngx_js_limit_req_finalizer
};


/*
 * Helper: map NGX_LOG_* numeric level to a string label.
 */
static JSValue
ngx_js_log_level_str(JSContext *ctx, ngx_uint_t level)
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
 * Each element: {zone, burst, nodelay, delay}
 */
static JSValue
ngx_js_limit_req_get_limits(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_limit_req_opaque_t    *op;
    ngx_http_limit_req_conf_t    *lrcf;
    ngx_http_limit_req_limit_t   *lim;
    JSValue                       arr, obj;
    ngx_uint_t                    i;
    ngx_str_t                    *name;
    int                           nodelay;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    lrcf = op->lrcf;
    arr = JS_NewArray(ctx);

    if (lrcf->limits.nelts == 0) {
        return arr;
    }

    lim = lrcf->limits.elts;

    for (i = 0; i < lrcf->limits.nelts; i++) {
        obj = JS_NewObject(ctx);

        name = &lim[i].shm_zone->shm.name;
        JS_SetPropertyStr(ctx, obj, "zone",
            JS_NewStringLen(ctx, (const char *) name->data, name->len));

        /* burst and delay are stored internally as value * 1000 */
        JS_SetPropertyStr(ctx, obj, "burst",
            JS_NewUint32(ctx, (uint32_t) (lim[i].burst / 1000)));

        nodelay = (lim[i].delay == NGX_MAX_UINT32_VALUE);
        JS_SetPropertyStr(ctx, obj, "nodelay", JS_NewBool(ctx, nodelay));

        JS_SetPropertyStr(ctx, obj, "delay",
            JS_NewUint32(ctx, nodelay ? 0 : (uint32_t) (lim[i].delay / 1000)));

        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


/*
 * Magic values for ngx_js_limit_req_get:
 *   0 — logLevel
 *   1 — delayLogLevel
 *   2 — statusCode
 *   3 — dryRun
 */
static JSValue
ngx_js_limit_req_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_limit_req_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return ngx_js_log_level_str(ctx, op->lrcf->limit_log_level);
    case 1: return ngx_js_log_level_str(ctx, op->lrcf->delay_log_level);
    case 2: return JS_NewUint32(ctx, (uint32_t) op->lrcf->status_code);
    case 3: return JS_NewBool(ctx, op->lrcf->dry_run);
    }

    return JS_UNDEFINED;
}


/*
 * Helper: parse a log-level label string to NGX_LOG_* numeric value.
 * Accepts "error"(4), "warn"(5), "notice"(6), "info"(7).
 * Returns NGX_LOG_ERR (4) for anything unrecognised.
 */
static ngx_uint_t
ngx_js_parse_log_level(const char *s)
{
    if (ngx_strcmp(s, "warn")   == 0) { return 5; }
    if (ngx_strcmp(s, "notice") == 0) { return 6; }
    if (ngx_strcmp(s, "info")   == 0) { return 7; }
    return 4;  /* error */
}


static JSValue
ngx_js_limit_req_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_limit_req_opaque_t  *op;
    const char                 *s;
    int64_t                     n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_class_id);
    if (!op) { return JS_EXCEPTION; }

    switch (magic) {
    case 0: /* logLevel */
        s = JS_ToCString(ctx, val);
        if (!s) { return JS_EXCEPTION; }
        op->lrcf->limit_log_level = ngx_js_parse_log_level(s);
        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    case 1: /* delayLogLevel */
        s = JS_ToCString(ctx, val);
        if (!s) { return JS_EXCEPTION; }
        op->lrcf->delay_log_level = ngx_js_parse_log_level(s);
        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    case 2: /* statusCode */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        op->lrcf->status_code = (ngx_uint_t) n;
        return JS_UNDEFINED;
    case 3: /* dryRun */
        op->lrcf->dry_run = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_limit_req_proto_funcs[] = {
    JS_CGETSET_DEF       ("limits",        ngx_js_limit_req_get_limits, NULL),
    JS_CGETSET_MAGIC_DEF ("logLevel",      ngx_js_limit_req_get, ngx_js_limit_req_set, 0),
    JS_CGETSET_MAGIC_DEF ("delayLogLevel", ngx_js_limit_req_get, ngx_js_limit_req_set, 1),
    JS_CGETSET_MAGIC_DEF ("statusCode",    ngx_js_limit_req_get, ngx_js_limit_req_set, 2),
    JS_CGETSET_MAGIC_DEF ("dryRun",        ngx_js_limit_req_get, ngx_js_limit_req_set, 3),
};


ngx_int_t
ngx_js_limit_req_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_limit_req_proto_funcs,
                               countof(ngx_js_limit_req_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_limit_req_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_limit_req(JSContext *ctx, ngx_http_limit_req_conf_t *lrcf)
{
    JSValue                    obj;
    ngx_js_limit_req_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_limit_req_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->lrcf = lrcf;

    obj = JS_NewObjectClass(ctx, ngx_js_limit_req_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_limit_req_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_limit_req_class_id, &ngx_js_limit_req_class) < 0
           ? NGX_ERROR : NGX_OK;
}
