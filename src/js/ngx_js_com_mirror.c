
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13w — location.mirror (NginxMirror)
 *
 * Exposes ngx_http_mirror_loc_conf_t as a JS object on location.mirror:
 *
 *   uris         string[]  — mirror target URIs (empty array if none)
 *   requestBody  boolean   — mirror_request_body on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_mirror_module.h"


typedef struct {
    ngx_http_mirror_loc_conf_t  *mlcf;
} ngx_js_mirror_opaque_t;


static void
ngx_js_mirror_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_mirror_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_mirror_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_mirror_class = {
    "NginxMirror",
    .finalizer = ngx_js_mirror_finalizer,
};


static JSValue
ngx_js_mirror_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_mirror_opaque_t      *op;
    ngx_http_mirror_loc_conf_t  *mlcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_mirror_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    mlcf = op->mlcf;

    switch (magic) {

    case 0: /* uris — array of mirror target URI strings */
    {
        JSValue      arr;
        ngx_uint_t   i;
        ngx_str_t   *uris;

        arr = JS_NewArray(ctx);

        if (mlcf->mirror != NULL) {
            uris = mlcf->mirror->elts;
            for (i = 0; i < mlcf->mirror->nelts; i++) {
                JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                    JS_NewStringLen(ctx, (char *) uris[i].data,
                                   uris[i].len));
            }
        }

        return arr;
    }

    case 1: /* requestBody */
        return JS_NewBool(ctx, (int) mlcf->request_body);

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_mirror_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_mirror_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_mirror_class_id);
    if (!op) { return JS_EXCEPTION; }

    if (magic == 1) { /* requestBody */
        op->mlcf->request_body = JS_ToBool(ctx, val);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_mirror_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("uris",        ngx_js_mirror_get, NULL,              0),
    JS_CGETSET_MAGIC_DEF("requestBody", ngx_js_mirror_get, ngx_js_mirror_set, 1),
};


ngx_int_t
ngx_js_mirror_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_mirror_class_id, &ngx_js_mirror_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_mirror_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_mirror_proto_funcs,
                               countof(ngx_js_mirror_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_mirror_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_mirror(JSContext *ctx, ngx_http_mirror_loc_conf_t *mlcf)
{
    JSValue                  obj;
    ngx_js_mirror_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_mirror_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->mlcf = mlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_mirror_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
