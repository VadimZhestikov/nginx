
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13k — location.addition (NginxAddition)
 *
 * Exposes ngx_http_addition_conf_t as a JS object on location.addition:
 *
 *   addBeforeBody  string — add_before_body subrequest URI (or "")
 *   addAfterBody   string — add_after_body  subrequest URI (or "")
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_addition_filter_module.h"


typedef struct {
    ngx_http_addition_conf_t  *acf;
} ngx_js_addition_opaque_t;


static void
ngx_js_addition_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_addition_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_addition_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_addition_class = {
    "NginxAddition",
    .finalizer = ngx_js_addition_finalizer,
};


static JSValue
ngx_js_addition_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_addition_opaque_t  *op;
    ngx_http_addition_conf_t  *acf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_addition_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    acf = op->acf;

    switch (magic) {

    case 0: /* addBeforeBody */
        return JS_NewStringLen(ctx, (char *) acf->before_body.data,
                               acf->before_body.len);

    case 1: /* addAfterBody */
        return JS_NewStringLen(ctx, (char *) acf->after_body.data,
                               acf->after_body.len);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_addition_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("addBeforeBody", ngx_js_addition_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("addAfterBody",  ngx_js_addition_get, NULL, 1),
};


ngx_int_t
ngx_js_addition_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_addition_class_id, &ngx_js_addition_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_addition_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_addition_proto_funcs,
                               countof(ngx_js_addition_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_addition_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_addition(JSContext *ctx, ngx_http_addition_conf_t *acf)
{
    JSValue                    obj;
    ngx_js_addition_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_addition_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->acf = acf;

    obj = JS_NewObjectClass(ctx, ngx_js_addition_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
