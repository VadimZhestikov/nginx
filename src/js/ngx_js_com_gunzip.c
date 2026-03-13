
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13l — location.gunzip (NginxGunzip)
 *
 * Exposes ngx_http_gunzip_conf_t as a JS object on location.gunzip:
 *
 *   enable          boolean — gunzip on/off
 *   buffers         object  — { num: N, size: S } from gunzip_buffers
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_gunzip_filter_module.h"


typedef struct {
    ngx_http_gunzip_conf_t  *gcf;
} ngx_js_gunzip_opaque_t;


static void
ngx_js_gunzip_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_gunzip_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_gunzip_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_gunzip_class = {
    "NginxGunzip",
    .finalizer = ngx_js_gunzip_finalizer,
};


static JSValue
ngx_js_gunzip_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_gunzip_opaque_t  *op;
    ngx_http_gunzip_conf_t  *gcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gunzip_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    gcf = op->gcf;

    switch (magic) {

    case 0: /* enable */
        return JS_NewBool(ctx, (int) gcf->enable);

    case 1: /* buffers — { num, size } */
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "num",
                          JS_NewInt32(ctx, (int32_t) gcf->bufs.num));
        JS_SetPropertyStr(ctx, obj, "size",
                          JS_NewUint32(ctx, (uint32_t) gcf->bufs.size));
        return obj;
    }

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_gunzip_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_gunzip_opaque_t  *op;
    JSValue                  num_val, size_val;
    int64_t                  num, size;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gunzip_class_id);
    if (!op) { return JS_EXCEPTION; }

    switch (magic) {
    case 0: /* enable */
        op->gcf->enable = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 1: /* buffers — { num, size } */
        num_val  = JS_GetPropertyStr(ctx, val, "num");
        size_val = JS_GetPropertyStr(ctx, val, "size");
        if (JS_ToInt64(ctx, &num,  num_val)  < 0
            || JS_ToInt64(ctx, &size, size_val) < 0)
        {
            JS_FreeValue(ctx, num_val);
            JS_FreeValue(ctx, size_val);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, num_val);
        JS_FreeValue(ctx, size_val);
        op->gcf->bufs.num  = (ngx_uint_t) num;
        op->gcf->bufs.size = (size_t) size;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_gunzip_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable",  ngx_js_gunzip_get, ngx_js_gunzip_set, 0),
    JS_CGETSET_MAGIC_DEF("buffers", ngx_js_gunzip_get, ngx_js_gunzip_set, 1),
};


ngx_int_t
ngx_js_gunzip_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_gunzip_class_id, &ngx_js_gunzip_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_gunzip_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_gunzip_proto_funcs,
                               countof(ngx_js_gunzip_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_gunzip_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_gunzip(JSContext *ctx, ngx_http_gunzip_conf_t *gcf)
{
    JSValue                  obj;
    ngx_js_gunzip_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_gunzip_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->gcf = gcf;

    obj = JS_NewObjectClass(ctx, ngx_js_gunzip_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
