
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

    switch (magic) {

    case 0: /* uris = ["string", ...] */
    {
        ngx_array_t  *arr;
        ngx_str_t    *uri;
        JSValue       len_v, item;
        const char   *s;
        size_t        slen;
        int64_t       len, i;
        u_char       *p;

        if (!JS_IsArray(ctx, val)) {
            return JS_ThrowTypeError(ctx, "mirror.uris must be an array");
        }

        len_v = JS_GetPropertyStr(ctx, val, "length");
        if (JS_ToInt64(ctx, &len, len_v) < 0) {
            JS_FreeValue(ctx, len_v);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, len_v);

        if (len == 0) {
            op->mlcf->mirror = NULL;
            return JS_UNDEFINED;
        }

        arr = ngx_array_create(ngx_cycle->pool, (ngx_uint_t) len,
                               sizeof(ngx_str_t));
        if (!arr) {
            return JS_EXCEPTION;
        }

        for (i = 0; i < len; i++) {
            item = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
            s    = JS_ToCStringLen(ctx, &slen, item);
            JS_FreeValue(ctx, item);

            if (!s) {
                return JS_EXCEPTION;
            }

            p = ngx_pnalloc(ngx_cycle->pool, slen);
            if (!p) {
                JS_FreeCString(ctx, s);
                return JS_EXCEPTION;
            }

            ngx_memcpy(p, s, slen);
            JS_FreeCString(ctx, s);

            uri = ngx_array_push(arr);
            if (!uri) {
                return JS_EXCEPTION;
            }

            uri->data = p;
            uri->len  = slen;
        }

        op->mlcf->mirror = arr;
        return JS_UNDEFINED;
    }

    case 1: /* requestBody */
        op->mlcf->request_body = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_mirror_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("uris",        ngx_js_mirror_get, ngx_js_mirror_set, 0),
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
