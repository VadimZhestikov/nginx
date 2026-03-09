
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13g — location.referer (NginxReferer)
 *
 * Exposes ngx_http_referer_conf_t flags as a JS object:
 *
 *   noReferer        boolean  — "none" in valid_referers (no Referer header)
 *   blockedReferer   boolean  — "blocked" in valid_referers
 *   serverNames      boolean  — "server_names" in valid_referers
 *   hashMaxSize      number   — referer_hash_max_size
 *   hashBucketSize   number   — referer_hash_bucket_size
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_referer_module.h"


typedef struct {
    ngx_http_referer_conf_t  *rlcf;
} ngx_js_referer_opaque_t;


static void
ngx_js_referer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_referer_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_referer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_referer_class = {
    "NginxReferer",
    .finalizer = ngx_js_referer_finalizer,
};


static JSValue
ngx_js_referer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_referer_opaque_t  *op;
    ngx_http_referer_conf_t  *rlcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_referer_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    rlcf = op->rlcf;

    switch (magic) {
    case 0: return JS_NewBool(ctx, (int) rlcf->no_referer);
    case 1: return JS_NewBool(ctx, (int) rlcf->blocked_referer);
    case 2: return JS_NewBool(ctx, (int) rlcf->server_names);
    case 3: return JS_NewUint32(ctx, (uint32_t) rlcf->referer_hash_max_size);
    case 4: return JS_NewUint32(ctx, (uint32_t) rlcf->referer_hash_bucket_size);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_referer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("noReferer",      ngx_js_referer_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("blockedReferer", ngx_js_referer_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("serverNames",    ngx_js_referer_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("hashMaxSize",    ngx_js_referer_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("hashBucketSize", ngx_js_referer_get, NULL, 4),
};


ngx_int_t
ngx_js_referer_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_referer_class_id,
                       &ngx_js_referer_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_referer_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_referer_proto_funcs,
                               countof(ngx_js_referer_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_referer_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_referer(JSContext *ctx, ngx_http_referer_conf_t *rlcf)
{
    JSValue                   obj;
    ngx_js_referer_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_referer_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rlcf = rlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_referer_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
