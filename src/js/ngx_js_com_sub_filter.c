
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13e — location.subFilter (NginxSubFilter)
 *
 * Exposes ngx_http_sub_loc_conf_t as a JS object on location.subFilter:
 *
 *   pairs[]        Array of {match, replacement} objects.
 *                  Static strings are exposed as strings; dynamic values
 *                  (containing nginx variables) are exposed as null.
 *   once           boolean  — substitute only once per response body
 *   lastModified   boolean  — preserve Last-Modified header
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>

#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_sub_filter_module.h"


/* Defined in ngx_js_com.c; shared via ngx_js_com.h */


typedef struct {
    ngx_http_sub_loc_conf_t  *slcf;
} ngx_js_sub_filter_opaque_t;


static void
ngx_js_sub_filter_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_sub_filter_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_sub_filter_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_sub_filter_class = {
    "NginxSubFilter",
    .finalizer = ngx_js_sub_filter_finalizer,
};


static JSValue
ngx_js_sub_filter_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_sub_filter_opaque_t  *op;
    ngx_http_sub_loc_conf_t     *slcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sub_filter_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    slcf = op->slcf;

    switch (magic) {

    case 0: /* pairs */
    {
        JSValue               arr, obj;
        ngx_uint_t            i;
        ngx_http_sub_pair_t  *pairs;
        JSValue               match_val, value_val;

        arr = JS_NewArray(ctx);

        if (slcf->pairs == NULL || slcf->pairs->nelts == 0) {
            return arr;
        }

        pairs = slcf->pairs->elts;

        for (i = 0; i < slcf->pairs->nelts; i++) {
            obj = JS_NewObject(ctx);

            /* match: static string if lengths == NULL, else null */
            if (pairs[i].match.lengths == NULL) {
                match_val = JS_NewStringLen(ctx,
                    (const char *) pairs[i].match.value.data,
                    pairs[i].match.value.len);
            } else {
                match_val = JS_NULL;
            }

            /* replacement: static string if lengths == NULL, else null */
            if (pairs[i].value.lengths == NULL) {
                value_val = JS_NewStringLen(ctx,
                    (const char *) pairs[i].value.value.data,
                    pairs[i].value.value.len);
            } else {
                value_val = JS_NULL;
            }

            JS_SetPropertyStr(ctx, obj, "match",       match_val);
            JS_SetPropertyStr(ctx, obj, "replacement", value_val);
            JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
        }

        return arr;
    }

    case 1: /* once */
        return JS_NewBool(ctx, (int) slcf->once);

    case 2: /* lastModified */
        return JS_NewBool(ctx, (int) slcf->last_modified);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_sub_filter_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("pairs",        ngx_js_sub_filter_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("once",         ngx_js_sub_filter_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("lastModified", ngx_js_sub_filter_get, NULL, 2),
};


ngx_int_t
ngx_js_sub_filter_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_sub_filter_class_id,
                       &ngx_js_sub_filter_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_sub_filter_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_sub_filter_proto_funcs,
                               countof(ngx_js_sub_filter_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_sub_filter_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_sub_filter(JSContext *ctx, ngx_http_sub_loc_conf_t *slcf)
{
    JSValue                      obj;
    ngx_js_sub_filter_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_sub_filter_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->slcf = slcf;

    obj = JS_NewObjectClass(ctx, ngx_js_sub_filter_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
