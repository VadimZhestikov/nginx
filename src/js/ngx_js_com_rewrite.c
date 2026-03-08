
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 10: rewrite location configuration.
 *
 * Exposes location.rewrite as a NginxRewrite object:
 *
 *   log                      bool    rewrite_log on/off
 *   uninitializedVariableWarn bool   uninitialized_variable_warn on/off
 *   stackSize                number  script stack size
 *   hasRules                 bool    true when any rewrite/return/set/if
 *                                    directives are present
 *
 * The rewrite module compiles all rules (rewrite, return, set, if) into
 * opaque bytecode stored in ngx_http_rewrite_loc_conf_t.codes.  This
 * wrapper exposes the metadata flags and a hasRules indicator rather than
 * attempting to decode the bytecode.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_rewrite_module.h"


typedef struct {
    ngx_http_rewrite_loc_conf_t  *rlcf;
} ngx_js_rewrite_opaque_t;


static void
ngx_js_rewrite_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_rewrite_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_rewrite_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_rewrite_class = {
    "NginxRewrite",
    .finalizer = ngx_js_rewrite_finalizer
};


/*
 * Magic values for ngx_js_rewrite_get:
 *   0 — log
 *   1 — uninitializedVariableWarn
 *   2 — stackSize
 *   3 — hasRules
 */
static JSValue
ngx_js_rewrite_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_rewrite_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rewrite_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return JS_NewBool(ctx,  (int) op->rlcf->log);
    case 1: return JS_NewBool(ctx,  (int) op->rlcf->uninitialized_variable_warn);
    case 2: return JS_NewInt64(ctx, (int64_t) op->rlcf->stack_size);
    case 3:
        return JS_NewBool(ctx,
                          op->rlcf->codes != NULL && op->rlcf->codes->nelts > 0);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_rewrite_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("log",                       ngx_js_rewrite_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("uninitializedVariableWarn", ngx_js_rewrite_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("stackSize",                 ngx_js_rewrite_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("hasRules",                  ngx_js_rewrite_get, NULL, 3),
};


JSValue
ngx_js_wrap_rewrite(JSContext *ctx, ngx_http_rewrite_loc_conf_t *rlcf)
{
    JSValue                  obj, proto;
    ngx_js_rewrite_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_rewrite_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rlcf = rlcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_rewrite_proto_funcs,
                               countof(ngx_js_rewrite_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_rewrite_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_rewrite_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_rewrite_class_id, &ngx_js_rewrite_class) < 0
           ? NGX_ERROR : NGX_OK;
}
