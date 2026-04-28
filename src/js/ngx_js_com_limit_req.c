
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 12a: limit_req location configuration.
 *
 * Exposes location.limitReq as a NginxLimitReq object:
 *
 *   limits         NginxLimitReqLimit[]  live proxy objects per limit entry
 *     zone         string    shared memory zone name (read-only)
 *     burst        number    burst queue size (r/w); 0 = no burst
 *     nodelay      boolean   true when delay == NGX_MAX_UINT32_VALUE (r/w)
 *     delay        number    delay threshold — requests before delay kicks in;
 *                            ignored when nodelay is true (r/w)
 *     rate         number    zone rate in r/s (r/w, acquires shm mutex)
 *   logLevel       string    limit_req_log_level (r/w)
 *   delayLogLevel  string    derived delay log level (r/w)
 *   statusCode     number    limit_req_status (r/w, default 503)
 *   dryRun         boolean   limit_req_dry_run on/off (r/w)
 *
 * All setters modify cf->pool fields (per-worker copies after fork).
 * Use nginx.broadcast() to propagate changes to every worker.
 *
 * Rate is stored internally as value * 1000 (1 unit = 0.001 r/s).
 * burst and delay are stored internally as value * 1000.
 * NGX_MAX_UINT32_VALUE for delay means "nodelay" was specified.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_limit_req_module.h"


/* ── NginxLimitReq ──────────────────────────────────────────────────────── */

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


/* ── NginxLimitReqLimit ─────────────────────────────────────────────────── */

typedef struct {
    ngx_http_limit_req_limit_t  *lim;   /* points into lrcf->limits.elts[i] */
    ngx_http_limit_req_ctx_t    *ctx;   /* lim->shm_zone->data              */
} ngx_js_limit_req_limit_opaque_t;


static void
ngx_js_limit_req_limit_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_limit_req_limit_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_limit_req_limit_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_limit_req_limit_class = {
    "NginxLimitReqLimit",
    .finalizer = ngx_js_limit_req_limit_finalizer
};


/*
 * Magic values for ngx_js_limit_req_limit_get / _set:
 *   0 — zone   (read-only string)
 *   1 — burst  (r/w, stored ×1000)
 *   2 — nodelay (r/w, alias for delay == NGX_MAX_UINT32_VALUE)
 *   3 — delay  (r/w, stored ×1000; writing also clears nodelay)
 *   4 — rate   (r/w, ctx->rate stored ×1000, reads/writes under shm mutex)
 */
static JSValue
ngx_js_limit_req_limit_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_limit_req_limit_opaque_t  *op;
    ngx_str_t                        *name;
    int                               nodelay;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_limit_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* zone */
        name = &op->lim->shm_zone->shm.name;
        return JS_NewStringLen(ctx, (const char *) name->data, name->len);

    case 1: /* burst */
        return JS_NewUint32(ctx, (uint32_t) (op->lim->burst / 1000));

    case 2: /* nodelay */
        nodelay = (op->lim->delay == NGX_MAX_UINT32_VALUE);
        return JS_NewBool(ctx, nodelay);

    case 3: /* delay */
        if (op->lim->delay == NGX_MAX_UINT32_VALUE) {
            return JS_NewUint32(ctx, 0);
        }
        return JS_NewUint32(ctx, (uint32_t) (op->lim->delay / 1000));

    case 4: /* rate */
        return JS_NewUint32(ctx, (uint32_t) (op->ctx->rate / 1000));
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_limit_req_limit_set(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_limit_req_limit_opaque_t  *op;
    int64_t                           n;
    int                               b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_limit_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* zone — read-only */
        return JS_ThrowTypeError(ctx, "zone is read-only");

    case 1: /* burst */
        if (JS_ToInt64(ctx, &n, val) < 0 || n < 0) {
            return JS_ThrowRangeError(ctx, "burst must be a non-negative integer");
        }
        op->lim->burst = (ngx_uint_t) n * 1000;
        return JS_UNDEFINED;

    case 2: /* nodelay */
        b = JS_ToBool(ctx, val);
        if (b < 0) {
            return JS_EXCEPTION;
        }
        if (b) {
            op->lim->delay = NGX_MAX_UINT32_VALUE;
        } else if (op->lim->delay == NGX_MAX_UINT32_VALUE) {
            op->lim->delay = 0;  /* clear nodelay; caller sets delay separately */
        }
        return JS_UNDEFINED;

    case 3: /* delay */
        if (JS_ToInt64(ctx, &n, val) < 0 || n < 0) {
            return JS_ThrowRangeError(ctx, "delay must be a non-negative integer");
        }
        op->lim->delay = (ngx_uint_t) n * 1000;
        return JS_UNDEFINED;

    case 4: /* rate */
        if (JS_ToInt64(ctx, &n, val) < 0 || n < 0) {
            return JS_ThrowRangeError(ctx, "rate must be a non-negative integer");
        }
        if (op->ctx->shpool != NULL) {
            /* request-handler phase: shm is initialized, use the mutex */
            ngx_shmtx_lock(&op->ctx->shpool->mutex);
            op->ctx->rate = (ngx_uint_t) n * 1000;
            ngx_shmtx_unlock(&op->ctx->shpool->mutex);
        } else {
            /* init_conf phase: shm not yet allocated, write directly */
            op->ctx->rate = (ngx_uint_t) n * 1000;
        }
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_limit_req_limit_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("zone",    ngx_js_limit_req_limit_get,
                                    ngx_js_limit_req_limit_set, 0),
    JS_CGETSET_MAGIC_DEF("burst",   ngx_js_limit_req_limit_get,
                                    ngx_js_limit_req_limit_set, 1),
    JS_CGETSET_MAGIC_DEF("nodelay", ngx_js_limit_req_limit_get,
                                    ngx_js_limit_req_limit_set, 2),
    JS_CGETSET_MAGIC_DEF("delay",   ngx_js_limit_req_limit_get,
                                    ngx_js_limit_req_limit_set, 3),
    JS_CGETSET_MAGIC_DEF("rate",    ngx_js_limit_req_limit_get,
                                    ngx_js_limit_req_limit_set, 4),
};


static JSValue
ngx_js_wrap_limit_req_limit(JSContext *ctx, ngx_http_limit_req_limit_t *lim)
{
    JSValue                          obj;
    ngx_js_limit_req_limit_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_limit_req_limit_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->lim = lim;
    op->ctx = lim->shm_zone->data;

    obj = JS_NewObjectClass(ctx, ngx_js_limit_req_limit_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ── NginxLimitReq helpers ──────────────────────────────────────────────── */

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


static ngx_uint_t
ngx_js_parse_log_level(const char *s)
{
    if (ngx_strcmp(s, "warn")   == 0) { return 5; }
    if (ngx_strcmp(s, "notice") == 0) { return 6; }
    if (ngx_strcmp(s, "info")   == 0) { return 7; }
    return 4;  /* error */
}


/*
 * limits getter: returns array of NginxLimitReqLimit live-proxy objects.
 */
static JSValue
ngx_js_limit_req_get_limits(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_limit_req_opaque_t   *op;
    ngx_http_limit_req_conf_t   *lrcf;
    ngx_http_limit_req_limit_t  *lim;
    JSValue                      arr, entry;
    ngx_uint_t                   i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_limit_req_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    lrcf = op->lrcf;
    arr  = JS_NewArray(ctx);

    if (lrcf->limits.nelts == 0) {
        return arr;
    }

    lim = lrcf->limits.elts;

    for (i = 0; i < lrcf->limits.nelts; i++) {
        entry = ngx_js_wrap_limit_req_limit(ctx, &lim[i]);
        if (JS_IsException(entry)) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, entry);
    }

    return arr;
}


/*
 * Magic values for ngx_js_limit_req_get / ngx_js_limit_req_set:
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

    JS_SetClassProto(ctx, ngx_js_limit_req_class_id, proto);
    return NGX_OK;
}


ngx_int_t
ngx_js_limit_req_limit_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_limit_req_limit_proto_funcs,
                               countof(ngx_js_limit_req_limit_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_limit_req_limit_class_id, proto);
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


ngx_int_t
ngx_js_limit_req_limit_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_limit_req_limit_class_id,
                       &ngx_js_limit_req_limit_class) < 0
           ? NGX_ERROR : NGX_OK;
}
