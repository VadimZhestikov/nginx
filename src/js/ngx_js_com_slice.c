
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13m — location.slice (NginxSlice)
 *
 * Exposes ngx_http_slice_loc_conf_t as a JS object on location.slice:
 *
 *   size   number — bytes per subrequest (0 = disabled)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_slice_filter_module.h"


typedef struct {
    ngx_http_slice_loc_conf_t  *scf;
} ngx_js_slice_opaque_t;


static void
ngx_js_slice_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_slice_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_slice_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_slice_class = {
    "NginxSlice",
    .finalizer = ngx_js_slice_finalizer,
};


static JSValue
ngx_js_slice_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_slice_opaque_t      *op;
    ngx_http_slice_loc_conf_t  *scf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_slice_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    scf = op->scf;

    switch (magic) {

    case 0: /* size */
        return JS_NewUint32(ctx, (uint32_t) scf->size);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_slice_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("size", ngx_js_slice_get, NULL, 0),
};


ngx_int_t
ngx_js_slice_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_slice_class_id, &ngx_js_slice_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_slice(JSContext *ctx, ngx_http_slice_loc_conf_t *scf)
{
    JSValue                 obj, proto;
    ngx_js_slice_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_slice_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->scf = scf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_slice_proto_funcs,
                               countof(ngx_js_slice_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_slice_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
