
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13p — location.secureLink (NginxSecureLink)
 *
 * Exposes ngx_http_secure_link_conf_t as a JS object on location.secureLink:
 *
 *   secret    string  — secure_link_secret value (empty if not set)
 *   variable  string  — static portion of secure_link expression (or "")
 *   md5       string  — static portion of secure_link_md5 expression (or "")
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_secure_link_module.h"


typedef struct {
    ngx_http_secure_link_conf_t  *scf;
} ngx_js_secure_link_opaque_t;


static void
ngx_js_secure_link_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_secure_link_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_secure_link_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_secure_link_class = {
    "NginxSecureLink",
    .finalizer = ngx_js_secure_link_finalizer,
};


static JSValue
ngx_js_secure_link_set(JSContext *ctx, JSValueConst this_val,
    JSValueConst val, int magic)
{
    ngx_js_secure_link_opaque_t  *op;
    ngx_http_secure_link_conf_t  *scf;
    const char                   *s;
    size_t                        len;
    u_char                       *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_secure_link_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    scf = op->scf;

    switch (magic) {

    case 0: /* secret */
        s = JS_ToCStringLen(ctx, &len, val);
        if (!s) {
            return JS_EXCEPTION;
        }

        p = ngx_pnalloc(ngx_js_conf_cycle()->pool, len + 1);
        if (p == NULL) {
            JS_FreeCString(ctx, s);
            JS_ThrowOutOfMemory(ctx);
            return JS_EXCEPTION;
        }

        ngx_memcpy(p, s, len);
        p[len] = '\0';
        JS_FreeCString(ctx, s);

        scf->secret.data = p;
        scf->secret.len  = len;
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_secure_link_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_secure_link_opaque_t  *op;
    ngx_http_secure_link_conf_t  *scf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_secure_link_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    scf = op->scf;

    switch (magic) {

    case 0: /* secret */
        return JS_NewStringLen(ctx, (char *) scf->secret.data,
                               scf->secret.len);

    case 1: /* variable — static portion of secure_link expression */
        if (scf->variable == NULL) {
            return JS_NewString(ctx, "");
        }
        return JS_NewStringLen(ctx,
                               (char *) scf->variable->value.data,
                               scf->variable->value.len);

    case 2: /* md5 — static portion of secure_link_md5 expression */
        if (scf->md5 == NULL) {
            return JS_NewString(ctx, "");
        }
        return JS_NewStringLen(ctx,
                               (char *) scf->md5->value.data,
                               scf->md5->value.len);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_secure_link_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("secret",   ngx_js_secure_link_get, ngx_js_secure_link_set, 0),
    JS_CGETSET_MAGIC_DEF("variable", ngx_js_secure_link_get, NULL,                   1),
    JS_CGETSET_MAGIC_DEF("md5",      ngx_js_secure_link_get, NULL,                   2),
};


ngx_int_t
ngx_js_secure_link_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_secure_link_class_id,
                       &ngx_js_secure_link_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_secure_link_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_secure_link_proto_funcs,
                               countof(ngx_js_secure_link_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_secure_link_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_secure_link(JSContext *ctx, ngx_http_secure_link_conf_t *scf)
{
    JSValue                       obj;
    ngx_js_secure_link_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_secure_link_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->scf = scf;

    obj = JS_NewObjectClass(ctx, ngx_js_secure_link_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
