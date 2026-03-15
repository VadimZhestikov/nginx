
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
 * Magic values for ngx_js_rewrite_get_core:
 *   0 — log
 *   1 — uninitializedVariableWarn
 *   2 — stackSize
 *   3 — hasRules
 */
static JSValue
ngx_js_rewrite_get_core(JSContext *ctx, JSValueConst this_val, int magic)
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


/*
 * Magic values for ngx_js_rewrite_set:
 *   0 — log
 *   1 — uninitializedVariableWarn
 *   2 — stackSize
 *   (3 — hasRules is read-only: reflects compiled bytecode)
 */
static JSValue
ngx_js_rewrite_set_core(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_rewrite_opaque_t  *op;
    int64_t                   n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rewrite_class_id);
    if (!op) { return JS_EXCEPTION; }

    switch (magic) {
    case 0: /* log */
        op->rlcf->log = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 1: /* uninitializedVariableWarn */
        op->rlcf->uninitialized_variable_warn = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 2: /* stackSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        op->rlcf->stack_size = (ngx_uint_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * Snapshot-aware getter wrapper.
 */
static JSValue
ngx_js_rewrite_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_rewrite_opaque_t       *op;
    ngx_js_worker_t               *w;
    ngx_http_request_t            *r;
    ngx_js_req_ctx_t              *rctx;
    ngx_http_rewrite_loc_conf_t   *orig_rlcf;
    JSValue                        ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rewrite_class_id);
    if (!op) { return JS_EXCEPTION; }

    w    = JS_GetContextOpaque(ctx);
    r    = (w != NULL) ? w->current_request : NULL;
    rctx = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;

    if (rctx != NULL && (rctx->read_mode & NGX_JS_WRITE_LOCAL)
        && ngx_js_is_own_conf(r, rctx,
               ngx_http_rewrite_module.ctx_index, op->rlcf))
    {
        orig_rlcf = op->rlcf;
        op->rlcf  = r->loc_conf[ngx_http_rewrite_module.ctx_index];
        ret       = ngx_js_rewrite_get_core(ctx, this_val, magic);
        op->rlcf  = orig_rlcf;
        return ret;
    }

    return ngx_js_rewrite_get_core(ctx, this_val, magic);
}


/*
 * Snapshot-aware setter wrapper.
 */
static JSValue
ngx_js_rewrite_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_rewrite_opaque_t       *op;
    ngx_js_worker_t               *w;
    ngx_http_request_t            *r;
    ngx_js_req_ctx_t              *rctx;
    ngx_http_rewrite_loc_conf_t   *orig_rlcf;
    uint32_t                       wmode;
    int                            need_global, need_local;
    JSValue                        ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rewrite_class_id);
    if (!op) { return JS_EXCEPTION; }

    w     = JS_GetContextOpaque(ctx);
    r     = (w != NULL) ? w->current_request : NULL;
    rctx  = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;
    wmode = (rctx != NULL) ? rctx->write_mode : NGX_JS_WRITE_GLOBAL;

    need_global = (wmode & NGX_JS_WRITE_GLOBAL) != 0;
    need_local  = (wmode & NGX_JS_WRITE_LOCAL)  != 0;

    if (need_local) {
        if (!ngx_js_is_own_conf(r, rctx,
                ngx_http_rewrite_module.ctx_index, op->rlcf))
        {
            need_local = 0;  /* cross-location: global only */
        }
    }

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_rewrite_module,
                sizeof(ngx_http_rewrite_loc_conf_t)) != NGX_OK) {
            return JS_ThrowInternalError(ctx, "rewrite snapshot alloc failed");
        }
        orig_rlcf = op->rlcf;
        op->rlcf  = r->loc_conf[ngx_http_rewrite_module.ctx_index];
        ret       = ngx_js_rewrite_set_core(ctx, this_val, val, magic);
        op->rlcf  = orig_rlcf;
        if (!need_global || JS_IsException(ret)) { return ret; }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        return ngx_js_rewrite_set_core(ctx, this_val, val, magic);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_rewrite_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("log",                       ngx_js_rewrite_get, ngx_js_rewrite_set, 0),
    JS_CGETSET_MAGIC_DEF("uninitializedVariableWarn", ngx_js_rewrite_get, ngx_js_rewrite_set, 1),
    JS_CGETSET_MAGIC_DEF("stackSize",                 ngx_js_rewrite_get, ngx_js_rewrite_set, 2),
    JS_CGETSET_MAGIC_DEF("hasRules",                  ngx_js_rewrite_get, NULL,               3),
};


ngx_int_t
ngx_js_rewrite_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_rewrite_proto_funcs,
                               countof(ngx_js_rewrite_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_rewrite_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_rewrite(JSContext *ctx, ngx_http_rewrite_loc_conf_t *rlcf)
{
    JSValue                  obj;
    ngx_js_rewrite_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_rewrite_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rlcf = rlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_rewrite_class_id);
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
