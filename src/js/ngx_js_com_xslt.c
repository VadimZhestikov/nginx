
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13o — location.xslt (NginxXslt)
 *
 * Exposes ngx_http_xslt_filter_loc_conf_t as a JS object on location.xslt:
 *
 *   lastModified   boolean  — xslt_last_modified on/off
 *   sheetsCount    number   — number of xslt_stylesheet entries
 *   params         string[] — xslt_param / xslt_string_param names
 *
 * Stylesheet paths are not exposed: after config parsing the loc_conf only
 * retains compiled xsltStylesheetPtr handles (opaque without libxslt headers).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_xslt_filter_module.h"


typedef struct {
    ngx_http_xslt_filter_loc_conf_t  *xcf;
} ngx_js_xslt_opaque_t;


static void
ngx_js_xslt_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_xslt_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_xslt_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_xslt_class = {
    "NginxXslt",
    .finalizer = ngx_js_xslt_finalizer,
};


static JSValue
ngx_js_xslt_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_xslt_opaque_t             *op;
    ngx_http_xslt_filter_loc_conf_t  *xcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_xslt_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    xcf = op->xcf;

    switch (magic) {

    case 0: /* lastModified */
        return JS_NewBool(ctx, (int) xcf->last_modified);

    case 1: /* sheetsCount */
        return JS_NewUint32(ctx, (uint32_t) xcf->sheets.nelts);

    case 2: /* params — array of name strings from loc_conf params */
    {
        JSValue                arr;
        ngx_uint_t             i, n;
        ngx_http_xslt_param_t *p;

        arr = JS_NewArray(ctx);
        n   = 0;

        if (xcf->params != NULL) {
            p = xcf->params->elts;
            for (i = 0; i < xcf->params->nelts; i++) {
                JS_SetPropertyUint32(ctx, arr, (uint32_t) n++,
                    JS_NewString(ctx, (char *) p[i].name));
            }
        }

        return arr;
    }

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_xslt_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("lastModified", ngx_js_xslt_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("sheetsCount",  ngx_js_xslt_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("params",       ngx_js_xslt_get, NULL, 2),
};


ngx_int_t
ngx_js_xslt_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_xslt_class_id, &ngx_js_xslt_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_xslt_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_xslt_proto_funcs,
                               countof(ngx_js_xslt_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_xslt_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_xslt(JSContext *ctx, ngx_http_xslt_filter_loc_conf_t *xcf)
{
    JSValue               obj;
    ngx_js_xslt_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_xslt_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->xcf = xcf;

    obj = JS_NewObjectClass(ctx, ngx_js_xslt_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
