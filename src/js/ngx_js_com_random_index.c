
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13r — location.randomIndex (NginxRandomIndex)
 *
 * Exposes ngx_http_random_index_loc_conf_t as a JS object on
 * location.randomIndex:
 *
 *   enable  boolean  — random_index on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_random_index_module.h"


typedef struct {
    ngx_http_random_index_loc_conf_t  *rcf;
} ngx_js_random_index_opaque_t;


static void
ngx_js_random_index_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_random_index_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_random_index_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_random_index_class = {
    "NginxRandomIndex",
    .finalizer = ngx_js_random_index_finalizer,
};


static JSValue
ngx_js_random_index_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_random_index_opaque_t  *op;
    ngx_http_random_index_loc_conf_t  *rcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_random_index_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    rcf = op->rcf;

    switch (magic) {

    case 0: /* enable */
        return JS_NewBool(ctx, (int) rcf->enable);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_random_index_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable", ngx_js_random_index_get, NULL, 0),
};


ngx_int_t
ngx_js_random_index_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_random_index_class_id,
                       &ngx_js_random_index_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_random_index_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_random_index_proto_funcs,
                               countof(ngx_js_random_index_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_random_index_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_random_index(JSContext *ctx,
    ngx_http_random_index_loc_conf_t *rcf)
{
    JSValue                        obj;
    ngx_js_random_index_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_random_index_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rcf = rcf;

    obj = JS_NewObjectClass(ctx, ngx_js_random_index_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
