
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


/*
 * Resolve a JS string to a charset index in mcf->charsets.
 *
 * Rules:
 *   "off"         → NGX_HTTP_CHARSET_OFF
 *   other string  → find existing entry (case-insensitive) or add new one
 *
 * Returns NGX_ERROR on failure (sets JS exception); sets *out on success.
 */
static ngx_int_t
ngx_js_charset_resolve(JSContext *ctx,
    ngx_http_charset_main_conf_t *mcf, JSValueConst val, ngx_int_t *out)
{
    const char          *s;
    size_t               len;
    ngx_uint_t           i;
    ngx_http_charset_t  *c;
    u_char              *p;

    s = JS_ToCStringLen(ctx, &len, val);
    if (!s) { return NGX_ERROR; }

    /* "off" disables charset conversion */
    if (len == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
        JS_FreeCString(ctx, s);
        *out = NGX_HTTP_CHARSET_OFF;
        return NGX_OK;
    }

    if (mcf == NULL) {
        JS_FreeCString(ctx, s);
        JS_ThrowInternalError(ctx, "setCharset: no charset main conf");
        return NGX_ERROR;
    }

    /* Search existing charsets (case-insensitive) */
    c = mcf->charsets.elts;
    for (i = 0; i < mcf->charsets.nelts; i++) {
        if (c[i].name.len == len
            && ngx_strncasecmp(c[i].name.data, (u_char *) s, len) == 0)
        {
            JS_FreeCString(ctx, s);
            *out = (ngx_int_t) i;
            return NGX_OK;
        }
    }

    /* Not found — add a new entry to mcf->charsets */
    p = ngx_pnalloc(ngx_cycle->pool, len + 1);
    if (p == NULL) {
        JS_FreeCString(ctx, s);
        JS_ThrowOutOfMemory(ctx);
        return NGX_ERROR;
    }
    ngx_memcpy(p, s, len);
    p[len] = '\0';
    JS_FreeCString(ctx, s);

    c = ngx_array_push(&mcf->charsets);
    if (c == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NGX_ERROR;
    }

    c->tables = NULL;
    c->name.data = p;
    c->name.len  = len;
    c->length    = 0;
    c->utf8      = (ngx_strncasecmp(p, (u_char *) "utf-8", 5) == 0
                    && len == 5) ? 1 : 0;

    *out = (ngx_int_t) (mcf->charsets.nelts - 1);
    return NGX_OK;
}


/*
 * setCharset(charset, sourceCharset) — update charset and source_charset
 * on the location config at runtime.
 *
 * Each argument may be:
 *   null / undefined  → NGX_CONF_UNSET (not configured)
 *   "off"             → NGX_HTTP_CHARSET_OFF (explicitly disabled)
 *   string            → charset name; added to mcf->charsets if new
 *
 * The change is visible immediately via the charset/sourceCharset getters
 * and takes effect for subsequent requests on this location.
 */
static JSValue
ngx_js_charset_set(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_charset_opaque_t  *op;
    ngx_int_t                 cs, src;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_charset_class_id);
    if (!op) { return JS_EXCEPTION; }

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
            "setCharset: charset and sourceCharset arguments required");
    }

    if (ngx_js_charset_resolve(ctx, op->mcf, argv[0], &cs) != NGX_OK) {
        return JS_EXCEPTION;
    }

    if (ngx_js_charset_resolve(ctx, op->mcf, argv[1], &src) != NGX_OK) {
        return JS_EXCEPTION;
    }

    op->lcf->charset        = cs;
    op->lcf->source_charset = src;

    return JS_UNDEFINED;
}


static JSValue
ngx_js_charset_set_override(JSContext *ctx, JSValueConst this_val,
    JSValueConst val, int magic)
{
    ngx_js_charset_opaque_t  *op;
    int                       b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_charset_class_id);
    if (!op) { return JS_EXCEPTION; }

    b = JS_ToBool(ctx, val);
    if (b < 0) { return JS_EXCEPTION; }

    op->lcf->override_charset = (ngx_flag_t) b;
    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_charset_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("charset",         ngx_js_charset_get, NULL,                       0),
    JS_CGETSET_MAGIC_DEF("sourceCharset",   ngx_js_charset_get, NULL,                       1),
    JS_CGETSET_MAGIC_DEF("overrideCharset", ngx_js_charset_get, ngx_js_charset_set_override, 2),
    JS_CFUNC_DEF        ("setCharset",      2, ngx_js_charset_set),
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
