
/*
 * Copyright (C) nginx JS contributors
 *
 * Root COM namespace: installs the global `nginx` object and its
 * top-level properties (version, cpu_count, log, cycle, http).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* NginxCycle class                                                     */
/* ------------------------------------------------------------------ */

JSClassID  ngx_js_cycle_class_id;
JSClassID  ngx_js_http_class_id;
JSClassID  ngx_js_server_class_id;
JSClassID  ngx_js_location_class_id;
JSClassID  ngx_js_upstream_class_id;
JSClassID  ngx_js_peer_class_id;
JSClassID  ngx_js_rr_peer_class_id;
JSClassID  ngx_js_request_class_id;


typedef struct {
    ngx_cycle_t  *cycle;
} ngx_js_cycle_opaque_t;


static void
ngx_js_cycle_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_cycle_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_cycle_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_cycle_class = {
    "NginxCycle",
    .finalizer = ngx_js_cycle_finalizer
};


/*
 * Magic values for ngx_js_cycle_get / ngx_js_cycle_set:
 *   0 — hostname   (r/o)
 *   1 — prefix     (r/o)
 *   2 — workers    (r/w)
 */
static JSValue
ngx_js_cycle_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_cycle_opaque_t  *op;
    ngx_core_conf_t        *ccf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_cycle_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0:
        return JS_NewStringLen(ctx,
                               (const char *) op->cycle->hostname.data,
                               op->cycle->hostname.len);
    case 1:
        return JS_NewStringLen(ctx,
                               (const char *) op->cycle->conf_prefix.data,
                               op->cycle->conf_prefix.len);
    case 2: /* workers */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewInt32(ctx, (int32_t) ccf->worker_processes);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_cycle_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_cycle_opaque_t  *op;
    ngx_core_conf_t        *ccf;
    int32_t                 n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_cycle_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 2: /* workers */
        if (JS_ToInt32(ctx, &n, val)) {
            return JS_EXCEPTION;
        }
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        ccf->worker_processes = (ngx_int_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_cycle_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("hostname", ngx_js_cycle_get, NULL,              0),
    JS_CGETSET_MAGIC_DEF("prefix",   ngx_js_cycle_get, NULL,              1),
    JS_CGETSET_MAGIC_DEF("workers",  ngx_js_cycle_get, ngx_js_cycle_set,  2),
};


static JSValue
ngx_js_wrap_cycle(JSContext *ctx, ngx_cycle_t *cycle)
{
    JSValue                obj, proto;
    ngx_js_cycle_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_cycle_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->cycle = cycle;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_cycle_proto_funcs,
                               countof(ngx_js_cycle_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_cycle_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* nginx.log(level, message)                                            */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_log(JSContext *ctx, JSValueConst this_val,
           int argc, JSValueConst *argv)
{
    ngx_cycle_t  *cycle;
    int32_t       level;
    const char   *msg;

    cycle = JS_GetContextOpaque(ctx);

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "nginx.log: expected (level, message)");
    }

    if (JS_ToInt32(ctx, &level, argv[0])) {
        return JS_EXCEPTION;
    }

    msg = JS_ToCString(ctx, argv[1]);
    if (!msg) {
        return JS_EXCEPTION;
    }

    ngx_log_error((ngx_uint_t) level, cycle->log, 0, "js: %s", msg);

    JS_FreeCString(ctx, msg);

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Class registration                                                   */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_com_register_classes(JSRuntime *rt)
{
    static ngx_uint_t  initialised;

    /*
     * JSClassIDs are global integers that must be allocated once per
     * process, not once per runtime.  Guard with a flag so that a
     * second call (e.g. from a worker's init_process) doesn't reallocate.
     */
    if (!initialised) {
        JS_NewClassID(&ngx_js_cycle_class_id);
        JS_NewClassID(&ngx_js_http_class_id);
        JS_NewClassID(&ngx_js_server_class_id);
        JS_NewClassID(&ngx_js_location_class_id);
        JS_NewClassID(&ngx_js_upstream_class_id);
        JS_NewClassID(&ngx_js_peer_class_id);
        JS_NewClassID(&ngx_js_rr_peer_class_id);
        JS_NewClassID(&ngx_js_request_class_id);
        initialised = 1;
    }

    /* Register class definitions in THIS runtime */
    if (JS_NewClass(rt, ngx_js_cycle_class_id, &ngx_js_cycle_class) < 0) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* ngx_js_com_init — main entry point called from ngx_js_module.c      */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle)
{
    JSValue  global, nginx_obj, cycle_obj;

    /* Register classes for this runtime first */
    if (ngx_js_com_register_classes(JS_GetRuntime(ctx)) != NGX_OK) {
        return NGX_ERROR;
    }

    /*
     * Store the cycle pointer in the context so callbacks (nginx.log,
     * getters, etc.) can reach it without a global variable.
     */
    JS_SetContextOpaque(ctx, cycle);

    global    = JS_GetGlobalObject(ctx);
    nginx_obj = JS_NewObject(ctx);

    /* nginx.version  (string, read-only) */
    JS_DefinePropertyValueStr(ctx, nginx_obj, "version",
                              JS_NewString(ctx, NGINX_VERSION),
                              JS_PROP_ENUMERABLE);

    /* nginx.cpu_count  (number, read-only) */
    JS_DefinePropertyValueStr(ctx, nginx_obj, "cpu_count",
                              JS_NewInt32(ctx, (int32_t) ngx_ncpu),
                              JS_PROP_ENUMERABLE);

    /* nginx.log(level, msg) */
    JS_SetPropertyStr(ctx, nginx_obj, "log",
                      JS_NewCFunction(ctx, ngx_js_log, "log", 2));

    /* nginx.cycle */
    cycle_obj = ngx_js_wrap_cycle(ctx, cycle);
    if (JS_IsException(cycle_obj)) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, nginx_obj, "cycle", cycle_obj);

    /* nginx.http — servers[], upstreams[] (read-only Phase 1) */
    if (ngx_js_http_com_install(ctx, nginx_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, global, "nginx", nginx_obj);

    JS_FreeValue(ctx, global);

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Exception logging helper                                             */
/* ------------------------------------------------------------------ */

void
ngx_js_log_exception(JSContext *ctx, ngx_log_t *log)
{
    JSValue      exc, str;
    const char  *cstr;

    exc  = JS_GetException(ctx);
    str  = JS_ToString(ctx, exc);
    cstr = JS_ToCString(ctx, str);

    if (cstr) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "js exception: %s", cstr);
        JS_FreeCString(ctx, cstr);
    }

    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, exc);
}
