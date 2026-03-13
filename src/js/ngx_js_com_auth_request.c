
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13s — location.authRequest (NginxAuthRequest)
 *
 * Exposes ngx_http_auth_request_conf_t as a JS object on
 * location.authRequest:
 *
 *   uri   string  — auth_request subrequest URI (empty string if not set)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_auth_request_module.h"


typedef struct {
    ngx_http_auth_request_conf_t  *arcf;
} ngx_js_auth_request_opaque_t;


static void
ngx_js_auth_request_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_auth_request_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_auth_request_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_auth_request_class = {
    "NginxAuthRequest",
    .finalizer = ngx_js_auth_request_finalizer,
};


static JSValue
ngx_js_auth_request_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_auth_request_opaque_t  *op;
    ngx_http_auth_request_conf_t  *arcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_auth_request_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    arcf = op->arcf;

    switch (magic) {

    case 0: /* uri */
        return JS_NewStringLen(ctx, (char *) arcf->uri.data, arcf->uri.len);

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_auth_request_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_auth_request_opaque_t  *op;
    const char                    *s;
    size_t                         len;
    u_char                        *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_auth_request_class_id);
    if (!op) { return JS_EXCEPTION; }

    if (magic != 0) { return JS_UNDEFINED; }  /* only uri (0) is writable */

    s = JS_ToCStringLen(ctx, &len, val);
    if (!s) { return JS_EXCEPTION; }

    p = ngx_pnalloc(ngx_cycle->pool, len);
    if (p == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, s, len);
    JS_FreeCString(ctx, s);

    op->arcf->uri.data = p;
    op->arcf->uri.len  = len;

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_auth_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("uri", ngx_js_auth_request_get, ngx_js_auth_request_set, 0),
};


ngx_int_t
ngx_js_auth_request_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_auth_request_class_id,
                       &ngx_js_auth_request_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_auth_request_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_auth_request_proto_funcs,
                               countof(ngx_js_auth_request_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_auth_request_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_auth_request(JSContext *ctx, ngx_http_auth_request_conf_t *arcf)
{
    JSValue                        obj;
    ngx_js_auth_request_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_auth_request_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->arcf = arcf;

    obj = JS_NewObjectClass(ctx, ngx_js_auth_request_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
