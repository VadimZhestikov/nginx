
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13n — location.imageFilter (NginxImageFilter)
 *
 * Exposes ngx_http_image_filter_conf_t as a JS object on location.imageFilter:
 *
 *   action        string  — "off"/"test"/"size"/"resize"/"crop"/"rotate"
 *   width         number  — static width arg (0 if dynamic/unset)
 *   height        number  — static height arg (0 if dynamic/unset)
 *   angle         number  — static rotation angle (0 if dynamic/unset)
 *   jpegQuality   number  — static jpeg quality (0 if dynamic/unset)
 *   webpQuality   number  — static webp quality (0 if dynamic/unset)
 *   sharpen       number  — static sharpen factor (0 if dynamic/unset)
 *   transparency  boolean — image_filter_transparency on/off
 *   interlace     boolean — image_filter_interlace on/off
 *   bufferSize    number  — image_filter_buffer bytes
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_image_filter_module.h"


typedef struct {
    ngx_http_image_filter_conf_t  *icf;
} ngx_js_image_filter_opaque_t;


static void
ngx_js_image_filter_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_image_filter_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_image_filter_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_image_filter_class = {
    "NginxImageFilter",
    .finalizer = ngx_js_image_filter_finalizer,
};


static JSValue
ngx_js_image_filter_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_image_filter_opaque_t  *op;
    ngx_http_image_filter_conf_t  *icf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_image_filter_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    icf = op->icf;

    switch (magic) {

    case 0: /* action */
    {
        static const char *names[] = {
            "off", "test", "size", "resize", "crop", "rotate"
        };
        ngx_uint_t  f = icf->filter;

        if (f < countof(names)) {
            return JS_NewString(ctx, names[f]);
        }

        return JS_NewString(ctx, "off");
    }

    case 1: /* width */
        return JS_NewUint32(ctx, (uint32_t) icf->width);

    case 2: /* height */
        return JS_NewUint32(ctx, (uint32_t) icf->height);

    case 3: /* angle */
        return JS_NewUint32(ctx, (uint32_t) icf->angle);

    case 4: /* jpegQuality */
        return JS_NewUint32(ctx, (uint32_t) icf->jpeg_quality);

    case 5: /* webpQuality */
        return JS_NewUint32(ctx, (uint32_t) icf->webp_quality);

    case 6: /* sharpen */
        return JS_NewUint32(ctx, (uint32_t) icf->sharpen);

    case 7: /* transparency */
        return JS_NewBool(ctx, (int) icf->transparency);

    case 8: /* interlace */
        return JS_NewBool(ctx, (int) icf->interlace);

    case 9: /* bufferSize */
        return JS_NewUint32(ctx, (uint32_t) icf->buffer_size);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_image_filter_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("action",       ngx_js_image_filter_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("width",        ngx_js_image_filter_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("height",       ngx_js_image_filter_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("angle",        ngx_js_image_filter_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("jpegQuality",  ngx_js_image_filter_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("webpQuality",  ngx_js_image_filter_get, NULL, 5),
    JS_CGETSET_MAGIC_DEF("sharpen",      ngx_js_image_filter_get, NULL, 6),
    JS_CGETSET_MAGIC_DEF("transparency", ngx_js_image_filter_get, NULL, 7),
    JS_CGETSET_MAGIC_DEF("interlace",    ngx_js_image_filter_get, NULL, 8),
    JS_CGETSET_MAGIC_DEF("bufferSize",   ngx_js_image_filter_get, NULL, 9),
};


ngx_int_t
ngx_js_image_filter_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_image_filter_class_id,
                       &ngx_js_image_filter_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_image_filter(JSContext *ctx, ngx_http_image_filter_conf_t *icf)
{
    JSValue                        obj, proto;
    ngx_js_image_filter_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_image_filter_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->icf = icf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_image_filter_proto_funcs,
                               countof(ngx_js_image_filter_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_image_filter_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
