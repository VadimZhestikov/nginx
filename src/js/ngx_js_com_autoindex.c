
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13f — location.autoindex (NginxAutoindex)
 *
 * Exposes ngx_http_autoindex_loc_conf_t as a JS object:
 *
 *   enable      boolean  — autoindex on/off
 *   format      string   — "html" | "json" | "jsonp" | "xml"
 *   localtime   boolean  — autoindex_localtime on/off
 *   exactSize   boolean  — autoindex_exact_size on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_autoindex_module.h"


typedef struct {
    ngx_http_autoindex_loc_conf_t  *alcf;
} ngx_js_autoindex_opaque_t;


static void
ngx_js_autoindex_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_autoindex_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_autoindex_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_autoindex_class = {
    "NginxAutoindex",
    .finalizer = ngx_js_autoindex_finalizer,
};


static JSValue
ngx_js_autoindex_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_autoindex_opaque_t      *op;
    ngx_http_autoindex_loc_conf_t  *alcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_autoindex_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    alcf = op->alcf;

    switch (magic) {

    case 0: /* enable */
        return JS_NewBool(ctx, (int) alcf->enable);

    case 1: /* format */
        switch (alcf->format) {
        case NGX_HTTP_AUTOINDEX_JSON:  return JS_NewString(ctx, "json");
        case NGX_HTTP_AUTOINDEX_JSONP: return JS_NewString(ctx, "jsonp");
        case NGX_HTTP_AUTOINDEX_XML:   return JS_NewString(ctx, "xml");
        default:                       return JS_NewString(ctx, "html");
        }

    case 2: /* localtime */
        return JS_NewBool(ctx, (int) alcf->localtime);

    case 3: /* exactSize */
        return JS_NewBool(ctx, (int) alcf->exact_size);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_autoindex_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable",    ngx_js_autoindex_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("format",    ngx_js_autoindex_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("localtime", ngx_js_autoindex_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("exactSize", ngx_js_autoindex_get, NULL, 3),
};


ngx_int_t
ngx_js_autoindex_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_autoindex_class_id,
                       &ngx_js_autoindex_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_autoindex(JSContext *ctx, ngx_http_autoindex_loc_conf_t *alcf)
{
    JSValue                     obj, proto;
    ngx_js_autoindex_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_autoindex_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->alcf = alcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_autoindex_proto_funcs,
                               countof(ngx_js_autoindex_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_autoindex_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
