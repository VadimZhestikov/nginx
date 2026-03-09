
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 13d: charset location configuration.
 *
 * Exposes location.charset as a NginxCharset object:
 *
 *   charset          string | null   target charset name ("UTF-8", etc.),
 *                                    "off" if charset off,
 *                                    null if not configured
 *   sourceCharset    string | null   source_charset name, same rules
 *   overrideCharset  boolean         override_charset on/off
 *
 * charset/source_charset are stored as integer indices into the main conf's
 * charsets array; NGX_CONF_UNSET means not configured, NGX_HTTP_CHARSET_OFF
 * means explicitly disabled.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_charset_filter_module.h"


typedef struct {
    ngx_http_charset_loc_conf_t   *lcf;
    ngx_http_charset_main_conf_t  *mcf;
} ngx_js_charset_opaque_t;


static void
ngx_js_charset_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_charset_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_charset_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_charset_class = {
    "NginxCharset",
    .finalizer = ngx_js_charset_finalizer
};


/*
 * Resolve a charset index to a JS string.
 *   NGX_CONF_UNSET  → JS_NULL  (not configured)
 *   NGX_HTTP_CHARSET_OFF → "off"
 *   >= 0            → name from main conf charsets array
 */
static JSValue
ngx_js_charset_index_to_str(JSContext *ctx,
    ngx_http_charset_main_conf_t *mcf, ngx_int_t idx)
{
    ngx_http_charset_t  *charsets;

    if (idx == NGX_CONF_UNSET) {
        return JS_NULL;
    }

    if (idx == NGX_HTTP_CHARSET_OFF) {
        return JS_NewString(ctx, "off");
    }

    if (idx < 0 || mcf == NULL || (ngx_uint_t) idx >= mcf->charsets.nelts) {
        return JS_NULL;
    }

    charsets = mcf->charsets.elts;

    return JS_NewStringLen(ctx,
               (const char *) charsets[idx].name.data,
               charsets[idx].name.len);
}


/*
 * Magic values for ngx_js_charset_get:
 *   0 — charset
 *   1 — sourceCharset
 *   2 — overrideCharset
 */
static JSValue
ngx_js_charset_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_charset_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_charset_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0:
        return ngx_js_charset_index_to_str(ctx, op->mcf,
                                            op->lcf->charset);
    case 1:
        return ngx_js_charset_index_to_str(ctx, op->mcf,
                                            op->lcf->source_charset);
    case 2:
        return JS_NewBool(ctx, op->lcf->override_charset);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_charset_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("charset",         ngx_js_charset_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("sourceCharset",   ngx_js_charset_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("overrideCharset", ngx_js_charset_get, NULL, 2),
};


ngx_int_t
ngx_js_charset_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_charset_proto_funcs,
                               countof(ngx_js_charset_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_charset_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_charset(JSContext *ctx, ngx_http_charset_loc_conf_t *lcf,
    ngx_http_charset_main_conf_t *mcf)
{
    JSValue                   obj;
    ngx_js_charset_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_charset_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->lcf = lcf;
    op->mcf = mcf;

    obj = JS_NewObjectClass(ctx, ngx_js_charset_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_charset_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_charset_class_id, &ngx_js_charset_class) < 0
           ? NGX_ERROR : NGX_OK;
}
