
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 8: headers filter location configuration.
 *
 * Exposes location.headers as a NginxHeaders object:
 *
 *   expires          string   "off"|"epoch"|"max"|"access"|"modified"|
 *                             "daily"|"unset"
 *   expiresTime      number   seconds (meaningful for access/modified/daily)
 *   addHeaders       object[] [ { key, value, always }, ... ]
 *   addTrailers      object[] [ { key, value, always }, ... ]
 *   headersInherit   string   "off"|"on"|"merge"
 *   trailersInherit  string   "off"|"on"|"merge"
 *
 * For add_header/add_trailer entries, value is the literal string when the
 * directive uses no nginx variables; null when the value is dynamic.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_headers_filter_module.h"


typedef struct {
    ngx_http_headers_conf_t  *hcf;
} ngx_js_headers_opaque_t;


static void
ngx_js_headers_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_headers_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_headers_class = {
    "NginxHeaders",
    .finalizer = ngx_js_headers_finalizer
};


static const char *
ngx_js_headers_inherit_str(ngx_uint_t v)
{
    switch (v) {
    case NGX_HTTP_HEADERS_INHERIT_ON:    return "on";
    case NGX_HTTP_HEADERS_INHERIT_MERGE: return "merge";
    default:                             return "off";
    }
}


/*
 * Magic values for ngx_js_headers_get_core:
 *   0 — expires          (string)
 *   1 — expiresTime      (number, seconds)
 *   2 — headersInherit   (string)
 *   3 — trailersInherit  (string)
 */
static JSValue
ngx_js_headers_get_core(JSContext *ctx, JSValueConst this_val, int magic)
{
    static const char *expire_modes[] = {
        "off", "epoch", "max", "access", "modified", "daily", "unset"
    };

    ngx_js_headers_opaque_t  *op;
    ngx_http_expires_t        e;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* expires */
        e = op->hcf->expires;
        if ((ngx_uint_t) e >= (sizeof(expire_modes) / sizeof(expire_modes[0])))
        {
            e = NGX_HTTP_EXPIRES_UNSET;
        }
        return JS_NewString(ctx, expire_modes[e]);

    case 1: /* expiresTime */
        return JS_NewInt64(ctx, (int64_t) op->hcf->expires_time);

    case 2: /* headersInherit */
        return JS_NewString(ctx,
                            ngx_js_headers_inherit_str(op->hcf->headers_inherit));

    case 3: /* trailersInherit */
        return JS_NewString(ctx,
                            ngx_js_headers_inherit_str(op->hcf->trailers_inherit));
    }

    return JS_UNDEFINED;
}


/*
 * Build a JS array from an ngx_array_t of ngx_http_header_val_t.
 * Each element is { key: string, value: string|null, always: bool }.
 * value is null when the directive value contains nginx variables.
 */
static JSValue
ngx_js_headers_build_array(JSContext *ctx, ngx_array_t *arr)
{
    ngx_http_header_val_t  *h;
    JSValue                 js_arr, entry;
    ngx_uint_t              i;

    js_arr = JS_NewArray(ctx);

    if (arr == NULL || arr->nelts == 0) {
        return js_arr;
    }

    h = arr->elts;

    for (i = 0; i < arr->nelts; i++) {
        entry = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, entry, "key",
                          JS_NewStringLen(ctx,
                                         (const char *) h[i].key.data,
                                         h[i].key.len));

        /* value.lengths == NULL means literal (no nginx variables) */
        if (h[i].value.lengths == NULL) {
            JS_SetPropertyStr(ctx, entry, "value",
                              JS_NewStringLen(ctx,
                                             (const char *) h[i].value.value.data,
                                             h[i].value.value.len));
        } else {
            JS_SetPropertyStr(ctx, entry, "value", JS_NULL);
        }

        JS_SetPropertyStr(ctx, entry, "always",
                          JS_NewBool(ctx, (int) h[i].always));

        JS_SetPropertyUint32(ctx, js_arr, (uint32_t) i, entry);
    }

    return js_arr;
}


static JSValue
ngx_js_headers_get_add_headers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_headers_build_array(ctx, op->hcf->headers);
}


static JSValue
ngx_js_headers_get_add_trailers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_headers_build_array(ctx, op->hcf->trailers);
}


/*
 * headersInherit / trailersInherit setter.
 * Accepts "off", "on", or "merge".
 *   Magic 2 — headersInherit
 *   Magic 3 — trailersInherit
 */
static JSValue
ngx_js_headers_set_core(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_headers_opaque_t  *op;
    const char               *s;
    size_t                    slen;
    ngx_uint_t                v;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    s = JS_ToCStringLen(ctx, &slen, val);
    if (!s) { return JS_EXCEPTION; }

    if (slen == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
        v = NGX_HTTP_HEADERS_INHERIT_ON;
    } else if (slen == 5
               && ngx_strncasecmp((u_char *) s, (u_char *) "merge", 5) == 0)
    {
        v = NGX_HTTP_HEADERS_INHERIT_MERGE;
    } else if (slen == 3
               && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0)
    {
        v = 0;
    } else {
        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
                    "inherit value must be \"off\", \"on\", or \"merge\"");
    }

    JS_FreeCString(ctx, s);

    if (magic == 2) {
        op->hcf->headers_inherit  = v;
    } else {
        op->hcf->trailers_inherit = v;
    }

    return JS_UNDEFINED;
}


/*
 * Snapshot-aware getter wrapper.
 */
static JSValue
ngx_js_headers_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_headers_opaque_t  *op;
    ngx_js_worker_t          *w;
    ngx_http_request_t       *r;
    ngx_js_req_ctx_t         *rctx;
    ngx_http_headers_conf_t  *orig_hcf;
    JSValue                   ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    w    = JS_GetContextOpaque(ctx);
    r    = (w != NULL) ? w->current_request : NULL;
    rctx = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;

    if (rctx != NULL && (rctx->read_mode & NGX_JS_WRITE_LOCAL)
        && ngx_js_is_own_conf(r, rctx,
               ngx_http_headers_filter_module.ctx_index, op->hcf))
    {
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_headers_get_core(ctx, this_val, magic);
        op->hcf  = orig_hcf;
        return ret;
    }

    return ngx_js_headers_get_core(ctx, this_val, magic);
}


/*
 * Snapshot-aware setter wrapper.
 */
static JSValue
ngx_js_headers_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_headers_opaque_t  *op;
    ngx_js_worker_t          *w;
    ngx_http_request_t       *r;
    ngx_js_req_ctx_t         *rctx;
    ngx_http_headers_conf_t  *orig_hcf;
    uint32_t                  wmode;
    int                       need_global, need_local;
    JSValue                   ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    w     = JS_GetContextOpaque(ctx);
    r     = (w != NULL) ? w->current_request : NULL;
    rctx  = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;
    wmode = (rctx != NULL) ? rctx->write_mode : NGX_JS_WRITE_GLOBAL;

    need_global = (wmode & NGX_JS_WRITE_GLOBAL) != 0;
    need_local  = (wmode & NGX_JS_WRITE_LOCAL)  != 0;

    if (need_local) {
        if (!ngx_js_is_own_conf(r, rctx,
                ngx_http_headers_filter_module.ctx_index, op->hcf))
        {
            need_local = 0;  /* cross-location: global only */
        }
    }

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_headers_filter_module,
                sizeof(ngx_http_headers_conf_t)) != NGX_OK) {
            return JS_ThrowInternalError(ctx, "headers snapshot alloc failed");
        }
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_headers_set_core(ctx, this_val, val, magic);
        op->hcf  = orig_hcf;
        if (!need_global || JS_IsException(ret)) { return ret; }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        return ngx_js_headers_set_core(ctx, this_val, val, magic);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_headers_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("expires",         ngx_js_headers_get, NULL,                0),
    JS_CGETSET_MAGIC_DEF("expiresTime",     ngx_js_headers_get, NULL,                1),
    JS_CGETSET_MAGIC_DEF("headersInherit",  ngx_js_headers_get, ngx_js_headers_set,  2),
    JS_CGETSET_MAGIC_DEF("trailersInherit", ngx_js_headers_get, ngx_js_headers_set,  3),
    JS_CGETSET_DEF      ("addHeaders",      ngx_js_headers_get_add_headers,  NULL),
    JS_CGETSET_DEF      ("addTrailers",     ngx_js_headers_get_add_trailers, NULL),
};


ngx_int_t
ngx_js_headers_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_headers_proto_funcs,
                               countof(ngx_js_headers_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_headers_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_headers(JSContext *ctx, ngx_http_headers_conf_t *hcf)
{
    JSValue                  obj;
    ngx_js_headers_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_headers_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->hcf = hcf;

    obj = JS_NewObjectClass(ctx, ngx_js_headers_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_headers_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_headers_class_id, &ngx_js_headers_class) < 0
           ? NGX_ERROR : NGX_OK;
}
