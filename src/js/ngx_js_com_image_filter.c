
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
ngx_js_image_filter_set(JSContext *ctx, JSValueConst this_val,
    JSValueConst val, int magic)
{
    ngx_js_image_filter_opaque_t  *op;
    ngx_http_image_filter_conf_t  *icf;
    int64_t                        n;
    int                            b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_image_filter_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    icf = op->icf;

    switch (magic) {

    case 0: /* action */
    {
        static const struct { const char *name; ngx_uint_t val; } map[] = {
            { "off",    NGX_HTTP_IMAGE_OFF    },
            { "test",   NGX_HTTP_IMAGE_TEST   },
            { "size",   NGX_HTTP_IMAGE_SIZE   },
            { "resize", NGX_HTTP_IMAGE_RESIZE },
            { "crop",   NGX_HTTP_IMAGE_CROP   },
            { "rotate", NGX_HTTP_IMAGE_ROTATE },
        };
        const char  *s;
        size_t       len;
        ngx_uint_t   i;

        s = JS_ToCStringLen(ctx, &len, val);
        if (!s) {
            return JS_EXCEPTION;
        }

        for (i = 0; i < countof(map); i++) {
            if (strlen(map[i].name) == len
                && ngx_strncasecmp((u_char *) map[i].name,
                                   (u_char *) s, len) == 0)
            {
                JS_FreeCString(ctx, s);
                icf->filter = map[i].val;
                return JS_UNDEFINED;
            }
        }

        JS_ThrowTypeError(ctx, "imageFilter.action: unknown value \"%s\"", s);
        JS_FreeCString(ctx, s);
        return JS_EXCEPTION;
    }

    case 1: /* width */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.width must be >= 0");
        }
        icf->width = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 2: /* height */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.height must be >= 0");
        }
        icf->height = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 3: /* angle */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.angle must be >= 0");
        }
        icf->angle = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 4: /* jpegQuality */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.jpegQuality must be >= 0");
        }
        icf->jpeg_quality = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 5: /* webpQuality */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.webpQuality must be >= 0");
        }
        icf->webp_quality = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 6: /* sharpen */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.sharpen must be >= 0");
        }
        icf->sharpen = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 7: /* transparency */
        b = JS_ToBool(ctx, val);
        if (b < 0) { return JS_EXCEPTION; }
        icf->transparency = (ngx_flag_t) b;
        return JS_UNDEFINED;

    case 8: /* interlace */
        b = JS_ToBool(ctx, val);
        if (b < 0) { return JS_EXCEPTION; }
        icf->interlace = (ngx_flag_t) b;
        return JS_UNDEFINED;

    case 9: /* bufferSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        if (n < 0) {
            return JS_ThrowRangeError(ctx, "imageFilter.bufferSize must be >= 0");
        }
        icf->buffer_size = (size_t) n;
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


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
    JS_CGETSET_MAGIC_DEF("action",       ngx_js_image_filter_get, ngx_js_image_filter_set, 0),
    JS_CGETSET_MAGIC_DEF("width",        ngx_js_image_filter_get, ngx_js_image_filter_set, 1),
    JS_CGETSET_MAGIC_DEF("height",       ngx_js_image_filter_get, ngx_js_image_filter_set, 2),
    JS_CGETSET_MAGIC_DEF("angle",        ngx_js_image_filter_get, ngx_js_image_filter_set, 3),
    JS_CGETSET_MAGIC_DEF("jpegQuality",  ngx_js_image_filter_get, ngx_js_image_filter_set, 4),
    JS_CGETSET_MAGIC_DEF("webpQuality",  ngx_js_image_filter_get, ngx_js_image_filter_set, 5),
    JS_CGETSET_MAGIC_DEF("sharpen",      ngx_js_image_filter_get, ngx_js_image_filter_set, 6),
    JS_CGETSET_MAGIC_DEF("transparency", ngx_js_image_filter_get, ngx_js_image_filter_set, 7),
    JS_CGETSET_MAGIC_DEF("interlace",    ngx_js_image_filter_get, ngx_js_image_filter_set, 8),
    JS_CGETSET_MAGIC_DEF("bufferSize",   ngx_js_image_filter_get, ngx_js_image_filter_set, 9),
};


ngx_int_t
ngx_js_image_filter_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_image_filter_class_id,
                       &ngx_js_image_filter_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_image_filter_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_image_filter_proto_funcs,
                               countof(ngx_js_image_filter_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_image_filter_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_image_filter(JSContext *ctx, ngx_http_image_filter_conf_t *icf)
{
    JSValue                        obj;
    ngx_js_image_filter_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_image_filter_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->icf = icf;

    obj = JS_NewObjectClass(ctx, ngx_js_image_filter_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
