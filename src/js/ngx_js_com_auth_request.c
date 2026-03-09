
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


static const JSCFunctionListEntry ngx_js_auth_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("uri", ngx_js_auth_request_get, NULL, 0),
};


ngx_int_t
ngx_js_auth_request_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_auth_request_class_id,
                       &ngx_js_auth_request_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_auth_request(JSContext *ctx, ngx_http_auth_request_conf_t *arcf)
{
    JSValue                        obj, proto;
    ngx_js_auth_request_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_auth_request_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->arcf = arcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_auth_request_proto_funcs,
                               countof(ngx_js_auth_request_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_auth_request_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
