
/*
 * Copyright (C) nginx JS contributors
 *
 * Root COM namespace: installs the global `nginx` object and its
 * top-level properties (version, cpu_count, log, cycle, http).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "ngx_js_worker.h"
#include "ngx_js_sw.h"


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
JSClassID  ngx_js_proxy_class_id;
JSClassID  ngx_js_ssl_class_id;
JSClassID  ngx_js_gzip_class_id;
JSClassID  ngx_js_headers_class_id;
JSClassID  ngx_js_proxy_cache_class_id;
JSClassID  ngx_js_rewrite_class_id;
JSClassID  ngx_js_access_class_id;
JSClassID  ngx_js_auth_class_id;
JSClassID  ngx_js_limit_req_class_id;
JSClassID  ngx_js_limit_conn_class_id;
JSClassID  ngx_js_fastcgi_class_id;
JSClassID  ngx_js_log_class_id;
JSClassID  ngx_js_realip_class_id;
JSClassID  ngx_js_charset_class_id;
JSClassID  ngx_js_sub_filter_class_id;
JSClassID  ngx_js_autoindex_class_id;
JSClassID  ngx_js_referer_class_id;
JSClassID  ngx_js_dav_class_id;
JSClassID  ngx_js_ssi_class_id;
JSClassID  ngx_js_userid_class_id;
JSClassID  ngx_js_addition_class_id;


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
    int32_t       level;
    const char   *msg;

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

    ngx_log_error((ngx_uint_t) level, ngx_cycle->log, 0, "js: %s", msg);

    JS_FreeCString(ctx, msg);

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* nginx.setTimeout(ms) — resolves a Promise via an NGINX timer        */
/* ------------------------------------------------------------------ */

typedef struct {
    JSContext        *ctx;
    JSRuntime        *rt;
    ngx_js_worker_t  *w;
    JSValue           resolve;
    JSValue           reject;
    ngx_event_t       ev;       /* embedded; ev.data = this timer struct */
} ngx_js_timer_t;


static void
ngx_js_timer_handler(ngx_event_t *ev)
{
    ngx_js_timer_t  *t = ev->data;
    JSValue          ret;
    JSContext       *job_ctx;

    /* Resolve the awaited Promise, re-queuing the async body as a microtask */
    ret = JS_Call(t->ctx, t->resolve, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(t->ctx, ret);
    JS_FreeValue(t->ctx, t->resolve);
    JS_FreeValue(t->ctx, t->reject);

    /* Drain microtasks — async body runs, calls req.respond() */
    while (JS_ExecutePendingJob(t->rt, &job_ctx) > 0) { }

    /* Finalize any suspended nginx request whose promise has now settled */
    ngx_js_async_check(t->w);

    ngx_free(t);
}


static JSValue
ngx_js_nginx_set_timeout(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    uint32_t         ms;
    ngx_js_timer_t  *t;
    ngx_js_worker_t *w;
    JSValue          resolving[2], promise;

    if (argc < 1 || JS_ToUint32(ctx, &ms, argv[0])) {
        return JS_ThrowTypeError(ctx, "setTimeout(ms): ms required");
    }

    w = JS_GetContextOpaque(ctx);
    if (w == NULL) {
        return JS_ThrowInternalError(ctx, "setTimeout: no worker context");
    }

    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        return promise;
    }

    t = ngx_alloc(sizeof(ngx_js_timer_t), ngx_cycle->log);
    if (t == NULL) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "setTimeout: alloc failed");
    }

    t->ctx     = ctx;
    t->rt      = w->rt;
    t->w       = w;
    t->resolve = resolving[0];   /* JS_NewPromiseCapability gave us ownership */
    t->reject  = resolving[1];

    ngx_memzero(&t->ev, sizeof(ngx_event_t));
    t->ev.handler = ngx_js_timer_handler;
    t->ev.data    = t;
    t->ev.log     = ngx_cycle->log;

    ngx_add_timer(&t->ev, (ngx_msec_t) ms);

    return promise;
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
        JS_NewClassID(&ngx_js_proxy_class_id);
        JS_NewClassID(&ngx_js_ssl_class_id);
        JS_NewClassID(&ngx_js_gzip_class_id);
        JS_NewClassID(&ngx_js_headers_class_id);
        JS_NewClassID(&ngx_js_proxy_cache_class_id);
        JS_NewClassID(&ngx_js_rewrite_class_id);
        JS_NewClassID(&ngx_js_access_class_id);
        JS_NewClassID(&ngx_js_auth_class_id);
        JS_NewClassID(&ngx_js_limit_req_class_id);
        JS_NewClassID(&ngx_js_limit_conn_class_id);
        JS_NewClassID(&ngx_js_fastcgi_class_id);
        JS_NewClassID(&ngx_js_log_class_id);
        JS_NewClassID(&ngx_js_realip_class_id);
        JS_NewClassID(&ngx_js_charset_class_id);
        JS_NewClassID(&ngx_js_sub_filter_class_id);
        JS_NewClassID(&ngx_js_autoindex_class_id);
        JS_NewClassID(&ngx_js_referer_class_id);
        JS_NewClassID(&ngx_js_dav_class_id);
        JS_NewClassID(&ngx_js_ssi_class_id);
        JS_NewClassID(&ngx_js_userid_class_id);
        JS_NewClassID(&ngx_js_addition_class_id);
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

    /* nginx.setTimeout(ms) — returns a Promise resolved by an NGINX timer */
    JS_SetPropertyStr(ctx, nginx_obj, "setTimeout",
                      JS_NewCFunction(ctx, ngx_js_nginx_set_timeout,
                                      "setTimeout", 1));

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

    /* global Worker constructor */
    if (ngx_js_worker_install(ctx) != NGX_OK) {
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    /* global SharedWorker constructor */
    if (ngx_js_sw_install(ctx) != NGX_OK) {
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

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


/*
 * Compile and execute a JS source file as an ES module.
 *
 * Module evaluation in QuickJS always returns a Promise (even for
 * synchronous modules with no top-level await).  Errors are reported as
 * rejected promises, not as JS_EXCEPTION from JS_EvalFunction.  This
 * helper handles the full sequence:
 *
 *   1. Compile with JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
 *   2. Execute with JS_EvalFunction (returns a Promise)
 *   3. Drain the pending-jobs queue (required for async module bodies)
 *   4. Check the promise state; if rejected, log the reason and fail
 *
 * Returns NGX_CONF_OK on success, NGX_CONF_ERROR on any failure.
 */
char *
ngx_js_eval_module(JSContext *ctx, JSRuntime *rt,
    const u_char *src, size_t src_len, const u_char *filename,
    ngx_log_t *log)
{
    JSValue    fn, promise, reason;
    JSContext *job_ctx;

    fn = JS_Eval(ctx, (const char *) src, src_len, (const char *) filename,
                 JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(fn)) {
        ngx_js_log_exception(ctx, log);
        return NGX_CONF_ERROR;
    }

    promise = JS_EvalFunction(ctx, fn);

    if (JS_IsException(promise)) {
        ngx_js_log_exception(ctx, log);
        JS_FreeValue(ctx, promise);
        return NGX_CONF_ERROR;
    }

    /* Drain pending jobs — module body executes here for async modules */
    while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }

    /* Module evaluation errors land in the promise as rejections */
    if (JS_PromiseState(ctx, promise) == JS_PROMISE_REJECTED) {
        reason = JS_PromiseResult(ctx, promise);
        JS_Throw(ctx, reason);      /* install as current exception */
        ngx_js_log_exception(ctx, log);
        JS_FreeValue(ctx, promise);
        return NGX_CONF_ERROR;
    }

    JS_FreeValue(ctx, promise);
    return NGX_CONF_OK;
}
