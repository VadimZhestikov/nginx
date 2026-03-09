
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13q — location.mp4 (NginxMp4)
 *
 * Exposes ngx_http_mp4_conf_t as a JS object on location.mp4:
 *
 *   bufferSize     number   — mp4_buffer_size (bytes)
 *   maxBufferSize  number   — mp4_max_buffer_size (bytes)
 *   startKeyFrame  boolean  — mp4_start_key_frame on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_mp4_module.h"


typedef struct {
    ngx_http_mp4_conf_t  *mcf;
} ngx_js_mp4_opaque_t;


static void
ngx_js_mp4_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_mp4_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_mp4_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_mp4_class = {
    "NginxMp4",
    .finalizer = ngx_js_mp4_finalizer,
};


static JSValue
ngx_js_mp4_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_mp4_opaque_t  *op;
    ngx_http_mp4_conf_t  *mcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_mp4_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    mcf = op->mcf;

    switch (magic) {

    case 0: /* bufferSize */
        return JS_NewUint32(ctx, (uint32_t) mcf->buffer_size);

    case 1: /* maxBufferSize */
        return JS_NewUint32(ctx, (uint32_t) mcf->max_buffer_size);

    case 2: /* startKeyFrame */
        return JS_NewBool(ctx, (int) mcf->start_key_frame);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_mp4_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("bufferSize",    ngx_js_mp4_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("maxBufferSize", ngx_js_mp4_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("startKeyFrame", ngx_js_mp4_get, NULL, 2),
};


ngx_int_t
ngx_js_mp4_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_mp4_class_id, &ngx_js_mp4_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_mp4(JSContext *ctx, ngx_http_mp4_conf_t *mcf)
{
    JSValue               obj, proto;
    ngx_js_mp4_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_mp4_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->mcf = mcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_mp4_proto_funcs,
                               countof(ngx_js_mp4_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_mp4_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
