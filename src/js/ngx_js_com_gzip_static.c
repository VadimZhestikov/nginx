
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13t — location.gzipStatic (NginxGzipStatic)
 *
 * Exposes ngx_http_gzip_static_conf_t as a JS object on
 * location.gzipStatic:
 *
 *   enable  string  — "off" | "on" | "always"
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_gzip_static_module.h"


typedef struct {
    ngx_http_gzip_static_conf_t  *gcf;
} ngx_js_gzip_static_opaque_t;


static void
ngx_js_gzip_static_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_gzip_static_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_gzip_static_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_gzip_static_class = {
    "NginxGzipStatic",
    .finalizer = ngx_js_gzip_static_finalizer,
};


static JSValue
ngx_js_gzip_static_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_gzip_static_opaque_t  *op;
    ngx_http_gzip_static_conf_t  *gcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_static_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    gcf = op->gcf;

    switch (magic) {

    case 0: /* enable */
        switch (gcf->enable) {
        case NGX_HTTP_GZIP_STATIC_ON:     return JS_NewString(ctx, "on");
        case NGX_HTTP_GZIP_STATIC_ALWAYS: return JS_NewString(ctx, "always");
        default:                          return JS_NewString(ctx, "off");
        }

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_gzip_static_set(JSContext *ctx, JSValueConst this_val,
    JSValueConst val, int magic)
{
    ngx_js_gzip_static_opaque_t  *op;
    ngx_http_gzip_static_conf_t  *gcf;
    const char                   *s;
    size_t                        len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_static_class_id);
    if (op == NULL) { return JS_EXCEPTION; }

    gcf = op->gcf;

    switch (magic) {

    case 0: /* enable — "off" | "on" | "always" */
        s = JS_ToCStringLen(ctx, &len, val);
        if (!s) { return JS_EXCEPTION; }

        if (len == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            gcf->enable = NGX_HTTP_GZIP_STATIC_OFF;
        } else if (len == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
            gcf->enable = NGX_HTTP_GZIP_STATIC_ON;
        } else if (len == 6 && ngx_strncasecmp((u_char *) s, (u_char *) "always", 6) == 0) {
            gcf->enable = NGX_HTTP_GZIP_STATIC_ALWAYS;
        } else {
            JS_ThrowTypeError(ctx,
                "gzipStatic.enable: expected \"off\", \"on\", or \"always\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_gzip_static_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable", ngx_js_gzip_static_get, ngx_js_gzip_static_set, 0),
};


ngx_int_t
ngx_js_gzip_static_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_gzip_static_class_id,
                       &ngx_js_gzip_static_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_gzip_static_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_gzip_static_proto_funcs,
                               countof(ngx_js_gzip_static_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_gzip_static_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_gzip_static(JSContext *ctx, ngx_http_gzip_static_conf_t *gcf)
{
    JSValue                       obj;
    ngx_js_gzip_static_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_gzip_static_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->gcf = gcf;

    obj = JS_NewObjectClass(ctx, ngx_js_gzip_static_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
