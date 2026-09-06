
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 11b: auth_basic location configuration.
 *
 * Exposes location.auth as a NginxAuth object:
 *
 *   realm      string | null   auth_basic realm (literal) or null if
 *                               dynamic (contains nginx variables).
 *                               The string "off" means auth is disabled.
 *   userFile   string | null   auth_basic_user_file path (literal) or null.
 *
 * NGX_CONF_UNSET_PTR fields (no directive set) return null.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_auth_basic_module.h"


typedef struct {
    ngx_http_auth_basic_loc_conf_t  *alcf;
} ngx_js_auth_opaque_t;


static void
ngx_js_auth_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_auth_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_auth_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_auth_class = {
    "NginxAuth",
    .finalizer = ngx_js_auth_finalizer
};


/*
 * Helper: return the literal string from a complex value pointer,
 * or JS_NULL when the pointer is unset (NGX_CONF_UNSET_PTR), NULL,
 * or the value is dynamic (has nginx variables).
 */
static JSValue
ngx_js_auth_cv_str(JSContext *ctx, ngx_http_complex_value_t *cv)
{
    /* NGX_CONF_UNSET_PTR is (void *) -1 */
    if (cv == NULL || cv == NGX_CONF_UNSET_PTR) {
        return JS_NULL;
    }

    /* dynamic value (contains nginx variables) */
    if (cv->lengths != NULL) {
        return JS_NULL;
    }

    return JS_NewStringLen(ctx, (const char *) cv->value.data, cv->value.len);
}


/*
 * Magic values for ngx_js_auth_get:
 *   0 — realm
 *   1 — userFile
 */
static JSValue
ngx_js_auth_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_auth_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_auth_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return ngx_js_auth_cv_str(ctx, op->alcf->realm);
    case 1: return ngx_js_auth_cv_str(ctx, op->alcf->user_file);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_auth_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_auth_opaque_t          *op;
    ngx_http_complex_value_t     **field;
    ngx_http_complex_value_t      *cv;
    const char                    *s;
    size_t                         len;
    u_char                        *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_auth_class_id);
    if (!op) { return JS_EXCEPTION; }

    switch (magic) {
    case 0:  field = &op->alcf->realm;     break;
    case 1:  field = &op->alcf->user_file; break;
    default: return JS_UNDEFINED;
    }

    s = JS_ToCStringLen(ctx, &len, val);
    if (!s) { return JS_EXCEPTION; }

    cv = ngx_pcalloc(ngx_js_conf_cycle()->pool, sizeof(ngx_http_complex_value_t));
    if (cv == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowOutOfMemory(ctx);
    }

    p = ngx_pnalloc(ngx_js_conf_cycle()->pool, len);
    if (p == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, s, len);
    JS_FreeCString(ctx, s);

    cv->value.data = p;
    cv->value.len  = len;
    /* lengths == NULL marks this as a literal (no nginx variables) */

    *field = cv;

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_auth_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("realm",    ngx_js_auth_get, ngx_js_auth_set, 0),
    JS_CGETSET_MAGIC_DEF("userFile", ngx_js_auth_get, ngx_js_auth_set, 1),
};


ngx_int_t
ngx_js_auth_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_auth_proto_funcs,
                               countof(ngx_js_auth_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_auth_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_auth(JSContext *ctx, ngx_http_auth_basic_loc_conf_t *alcf)
{
    JSValue               obj;
    ngx_js_auth_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_auth_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->alcf = alcf;

    obj = JS_NewObjectClass(ctx, ngx_js_auth_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_auth_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_auth_class_id, &ngx_js_auth_class) < 0
           ? NGX_ERROR : NGX_OK;
}
