
/*
 * Copyright (C) nginx JS contributors
 *
 * Root COM namespace: installs the global `nginx` object and its
 * top-level properties (version, cpu_count, log, cycle, http).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>
#include <sys/socket.h>
#include <cutils.h>
#include "ngx_js.h"
#include <math.h>
#include "ngx_js_com.h"
#include "ngx_js_worker.h"
#include "ngx_js_sw.h"
#include "ngx_js_compartment.h"
#include "ngx_js_socket.h"
#include "ngx_js_listener.h"
#include "ngx_js_stream_listener.h"
#include "ngx_js_repl.h"


/* ------------------------------------------------------------------ */
/* NginxCycle class                                                     */
/* ------------------------------------------------------------------ */

JSClassID  ngx_js_cycle_class_id;
JSClassID  ngx_js_http_class_id;
JSClassID  ngx_js_server_class_id;
JSClassID  ngx_js_location_class_id;
JSClassID  ngx_js_com_facet_class_id;   /* COMCON mediate: attenuated COM cap */
JSClassID  ngx_js_upstream_class_id;
JSClassID  ngx_js_peer_class_id;
JSClassID  ngx_js_rr_peer_class_id;
JSClassID  ngx_js_request_class_id;
JSClassID  ngx_js_req_vars_class_id;
JSClassID  ngx_js_body_chunks_class_id;
JSClassID  ngx_js_proxy_class_id;
JSClassID  ngx_js_ssl_class_id;
JSClassID  ngx_js_gzip_class_id;
JSClassID  ngx_js_headers_class_id;
JSClassID  ngx_js_proxy_cache_class_id;
JSClassID  ngx_js_rewrite_class_id;
JSClassID  ngx_js_access_class_id;
JSClassID  ngx_js_stream_access_class_id;
JSClassID  ngx_js_stream_ssl_class_id;
JSClassID  ngx_js_stream_session_class_id;
JSClassID  ngx_js_auth_class_id;
JSClassID  ngx_js_limit_req_class_id;
JSClassID  ngx_js_limit_req_limit_class_id;
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
JSClassID  ngx_js_gunzip_class_id;
JSClassID  ngx_js_slice_class_id;
JSClassID  ngx_js_image_filter_class_id;
JSClassID  ngx_js_xslt_class_id;
JSClassID  ngx_js_secure_link_class_id;
JSClassID  ngx_js_mp4_class_id;
JSClassID  ngx_js_random_index_class_id;
JSClassID  ngx_js_auth_request_class_id;
JSClassID  ngx_js_gzip_static_class_id;
JSClassID  ngx_js_memcached_class_id;
JSClassID  ngx_js_scgi_class_id;
JSClassID  ngx_js_uwsgi_class_id;
JSClassID  ngx_js_mirror_class_id;
JSClassID  ngx_js_snapshot_class_id;


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
 * serverByName(name) — JSCFunctionData, bound per socket entry.
 * func_data[0] = { lowercaseName: serverWrapper, ... }
 */
JSValue
ngx_js_socket_server_by_name_fn(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv, int magic, JSValue *func_data)
{
    const char  *cstr;
    size_t       clen;
    u_char      *lc;
    JSValue      result;

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        return JS_NULL;
    }

    cstr = JS_ToCStringLen(ctx, &clen, argv[0]);
    if (!cstr) {
        return JS_EXCEPTION;
    }

    lc = js_malloc(ctx, clen + 1);
    if (!lc) {
        JS_FreeCString(ctx, cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_strlow(lc, (u_char *) cstr, clen);
    lc[clen] = '\0';
    JS_FreeCString(ctx, cstr);

    result = JS_GetPropertyStr(ctx, func_data[0], (const char *) lc);
    js_free(ctx, lc);

    if (JS_IsUndefined(result)) {
        JS_FreeValue(ctx, result);
        return JS_NULL;
    }

    return result;
}


/*
 * Magic values for ngx_js_cycle_get / ngx_js_cycle_set:
 *   0 — hostname          (r/o string)
 *   1 — prefix            (r/o string, conf_prefix — config directory)
 *   2 — workers           (r/w number)
 *   3 — confFile          (r/o string, full config file path)
 *   4 — errorLog          (r/o string, error log path)
 *   5 — installPrefix     (r/o string, installation prefix, cycle->prefix)
 *   6 — pid               (r/o string, pid file path)
 *   7 — connectionN       (r/o number, max connections per worker)
 *   8 — daemon            (r/o boolean)
 *   9 — master            (r/o boolean, master_process on/off)
 *  10 — timerResolution   (r/o ms, timer_resolution; 0 = disabled)
 *  11 — shutdownTimeout   (r/o ms, worker_shutdown_timeout; 0 = disabled)
 *  12 — priority          (r/o number, worker_priority nice value)
 *  13 — rlimitNofile      (r/o number, worker_rlimit_nofile; -1 if unset)
 *  14 — workingDirectory  (r/o string, working_directory; "" if unset)
 *  15 — sockets           (r/o array, snapshot of cycle->listening)
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

    case 3: /* confFile */
        return JS_NewStringLen(ctx,
                               (const char *) op->cycle->conf_file.data,
                               op->cycle->conf_file.len);

    case 4: /* errorLog */
        return JS_NewStringLen(ctx,
                               (const char *) op->cycle->error_log.data,
                               op->cycle->error_log.len);

    case 5: /* installPrefix — cycle->prefix (installation directory) */
        return JS_NewStringLen(ctx,
                               (const char *) op->cycle->prefix.data,
                               op->cycle->prefix.len);

    case 6: /* pid — pid file path */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewStringLen(ctx,
                               (const char *) ccf->pid.data,
                               ccf->pid.len);

    case 7: /* connectionN — max connections per worker */
        return JS_NewInt64(ctx, (int64_t) op->cycle->connection_n);

    case 8: /* daemon */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewBool(ctx, (int) ccf->daemon);

    case 9: /* master — master_process on/off */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewBool(ctx, (int) ccf->master);

    case 10: /* timerResolution — ms (0 = disabled) */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewInt64(ctx, (int64_t) ccf->timer_resolution);

    case 11: /* shutdownTimeout — ms (0 = disabled) */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewInt64(ctx, (int64_t) ccf->shutdown_timeout);

    case 12: /* priority — worker_priority (nice value, 0 = default) */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewInt32(ctx, (int32_t) ccf->priority);

    case 13: /* rlimitNofile — worker_rlimit_nofile (-1 if unset) */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewInt64(ctx, (int64_t) ccf->rlimit_nofile);

    case 14: /* workingDirectory — working_directory ("" if unset) */
        ccf = (ngx_core_conf_t *) ngx_get_conf(op->cycle->conf_ctx,
                                               ngx_core_module);
        return JS_NewStringLen(ctx,
                               (const char *) ccf->working_directory.data,
                               ccf->working_directory.len);

    case 15: /* sockets — snapshot array from cycle->listening */
    {
        JSValue     arr;
        ngx_uint_t  idx;

        arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) {
            return arr;
        }

        idx = 0;
        ngx_js_http_socket_entries(ctx, arr, op->cycle, &idx);
        ngx_js_stream_socket_entries(ctx, arr, op->cycle, &idx);

        return arr;
    }
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
    JS_CGETSET_MAGIC_DEF("hostname",      ngx_js_cycle_get, NULL,             0),
    JS_CGETSET_MAGIC_DEF("prefix",        ngx_js_cycle_get, NULL,             1),
    JS_CGETSET_MAGIC_DEF("workers",       ngx_js_cycle_get, ngx_js_cycle_set, 2),
    JS_CGETSET_MAGIC_DEF("confFile",      ngx_js_cycle_get, NULL,             3),
    JS_CGETSET_MAGIC_DEF("errorLog",      ngx_js_cycle_get, NULL,             4),
    JS_CGETSET_MAGIC_DEF("installPrefix", ngx_js_cycle_get, NULL,             5),
    JS_CGETSET_MAGIC_DEF("pid",           ngx_js_cycle_get, NULL,             6),
    JS_CGETSET_MAGIC_DEF("connectionN",       ngx_js_cycle_get, NULL,             7),
    JS_CGETSET_MAGIC_DEF("daemon",            ngx_js_cycle_get, NULL,             8),
    JS_CGETSET_MAGIC_DEF("master",            ngx_js_cycle_get, NULL,             9),
    JS_CGETSET_MAGIC_DEF("timerResolution",   ngx_js_cycle_get, NULL,            10),
    JS_CGETSET_MAGIC_DEF("shutdownTimeout",   ngx_js_cycle_get, NULL,            11),
    JS_CGETSET_MAGIC_DEF("priority",          ngx_js_cycle_get, NULL,            12),
    JS_CGETSET_MAGIC_DEF("rlimitNofile",      ngx_js_cycle_get, NULL,            13),
    JS_CGETSET_MAGIC_DEF("workingDirectory",  ngx_js_cycle_get, NULL,            14),
    JS_CGETSET_MAGIC_DEF("sockets",           ngx_js_cycle_get, NULL,            15),
};


static ngx_int_t
ngx_js_cycle_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_cycle_proto_funcs,
                               countof(ngx_js_cycle_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_cycle_class_id, proto);
    return NGX_OK;
}


static JSValue
ngx_js_wrap_cycle(JSContext *ctx, ngx_cycle_t *cycle)
{
    JSValue                obj;
    ngx_js_cycle_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_cycle_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->cycle = cycle;

    obj = JS_NewObjectClass(ctx, ngx_js_cycle_class_id);
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

    /* Finalize any suspended nginx requests whose promise has now settled */
    ngx_js_async_check(t->w);
    ngx_js_bf_async_check(t->w);
    ngx_js_sf_async_check(t->w);
    ngx_js_l4_async_check(t->w);

    ngx_free(t);
}


static JSValue
ngx_js_nginx_gc(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}


static JSValue
ngx_js_nginx_js_mem_usage(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSMemoryUsage  s;
    JSValue        obj;

    JS_ComputeMemoryUsage(JS_GetRuntime(ctx), &s);

    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "mallocSize",  JS_NewInt64(ctx, s.malloc_size));
    JS_SetPropertyStr(ctx, obj, "mallocCount", JS_NewInt64(ctx, s.malloc_count));
    JS_SetPropertyStr(ctx, obj, "objectCount", JS_NewInt64(ctx, s.obj_count));
    return obj;
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
        JS_NewClassID(&ngx_js_com_facet_class_id);
        JS_NewClassID(&ngx_js_upstream_class_id);
        JS_NewClassID(&ngx_js_peer_class_id);
        JS_NewClassID(&ngx_js_rr_peer_class_id);
        JS_NewClassID(&ngx_js_request_class_id);
        JS_NewClassID(&ngx_js_req_vars_class_id);
        JS_NewClassID(&ngx_js_body_chunks_class_id);
        JS_NewClassID(&ngx_js_proxy_class_id);
        JS_NewClassID(&ngx_js_ssl_class_id);
        JS_NewClassID(&ngx_js_gzip_class_id);
        JS_NewClassID(&ngx_js_headers_class_id);
        JS_NewClassID(&ngx_js_proxy_cache_class_id);
        JS_NewClassID(&ngx_js_rewrite_class_id);
        JS_NewClassID(&ngx_js_access_class_id);
        JS_NewClassID(&ngx_js_auth_class_id);
        JS_NewClassID(&ngx_js_limit_req_class_id);
        JS_NewClassID(&ngx_js_limit_req_limit_class_id);
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
        JS_NewClassID(&ngx_js_gunzip_class_id);
        JS_NewClassID(&ngx_js_slice_class_id);
        JS_NewClassID(&ngx_js_image_filter_class_id);
        JS_NewClassID(&ngx_js_xslt_class_id);
        JS_NewClassID(&ngx_js_secure_link_class_id);
        JS_NewClassID(&ngx_js_mp4_class_id);
        JS_NewClassID(&ngx_js_random_index_class_id);
        JS_NewClassID(&ngx_js_auth_request_class_id);
        JS_NewClassID(&ngx_js_gzip_static_class_id);
        JS_NewClassID(&ngx_js_memcached_class_id);
        JS_NewClassID(&ngx_js_scgi_class_id);
        JS_NewClassID(&ngx_js_uwsgi_class_id);
        JS_NewClassID(&ngx_js_mirror_class_id);
        JS_NewClassID(&ngx_js_events_class_id);
        JS_NewClassID(&ngx_js_socket_class_id);
        JS_NewClassID(&ngx_js_http_listener_class_id);
        JS_NewClassID(&ngx_js_connection_class_id);
        JS_NewClassID(&ngx_js_stream_server_class_id);
        JS_NewClassID(&ngx_js_stream_listener_class_id);
        JS_NewClassID(&ngx_js_stream_proxy_class_id);
        JS_NewClassID(&ngx_js_stream_upstream_class_id);
        JS_NewClassID(&ngx_js_stream_peer_class_id);
        JS_NewClassID(&ngx_js_stream_rr_peer_class_id);
        JS_NewClassID(&ngx_js_stream_access_class_id);
        JS_NewClassID(&ngx_js_stream_ssl_class_id);
        JS_NewClassID(&ngx_js_stream_session_class_id);
        JS_NewClassID(&ngx_js_snapshot_class_id);
        initialised = 1;
    }

    /* Register class definitions in THIS runtime */
    if (JS_NewClass(rt, ngx_js_cycle_class_id, &ngx_js_cycle_class) < 0) {
        return NGX_ERROR;
    }

    if (ngx_js_events_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_socket_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_com_facet_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_listener_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_stream_listener_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_stream_access_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_stream_ssl_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_stream_session_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* nginx.broadcast(fn)                                                  */
/* ------------------------------------------------------------------ */

/*
 * nginx.broadcast(fn)
 *
 * Registers a function to be called in every nginx worker process.
 *
 * MASTER / INIT-CONF CONTEXT (called from top-level js_source code):
 *   fn is appended to nginx.__broadcast_queue.  After the master process
 *   forks, each worker calls every queued function during init_process
 *   (with the worker context opaque already set).  This lets js_source
 *   scripts perform per-worker initialisation that requires the full
 *   worker environment (e.g. using nginx.setTimeout, inspecting
 *   request-level objects).
 *
 * WORKER CONTEXT (called from a request handler):
 *   fn is called immediately in the current worker.  No deferred
 *   execution or IPC to other workers is performed.
 *
 * Returns undefined.
 */
static JSValue
ngx_js_broadcast(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue   queue, fn, len_val, ret;
    uint32_t  len;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
            "broadcast: argument must be a function");
    }

    fn = argv[0];

    /* Worker context: call fn immediately in this worker */
    if (ngx_process == NGX_PROCESS_WORKER) {
        ret = JS_Call(ctx, fn, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(ret)) {
            return ret;
        }
        JS_FreeValue(ctx, ret);
        return JS_UNDEFINED;
    }

    /* Master/init context: append fn to nginx.__broadcast_queue */
    queue = JS_GetPropertyStr(ctx, this_val, "__broadcast_queue");
    if (JS_IsException(queue)) {
        return queue;
    }

    len_val = JS_GetPropertyStr(ctx, queue, "length");
    if (JS_IsException(len_val)) {
        JS_FreeValue(ctx, queue);
        return len_val;
    }

    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    JS_SetPropertyUint32(ctx, queue, len, JS_DupValue(ctx, fn));
    JS_FreeValue(ctx, queue);

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/*
 * nginx.jitStatus(fn) -> {functions, compiled}   [D4c's aotStatus, for host JS]
 *
 * Which TIER a host function's tree is on, read-only: asking must never compile.
 * jitCompile() cannot answer it -- calling it to find out changes the answer,
 * which is how a measurement of "interpreted vs compiled" ends up comparing
 * compiled with compiled. Added while gathering M5 evidence, when the A/B's
 * validity check said the interpreted arm already had native code and nothing
 * could say when it got it.
 */
static JSValue
ngx_js_jit_status(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue  r;
#ifdef CONFIG_JIT
    int      n = 0, c;
#endif

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "jitStatus(fn): function required");
    }

    r = JS_NewObject(ctx);
    if (JS_IsException(r)) {
        return r;
    }

#ifdef CONFIG_JIT
    c = js_comcon_aot_status(ctx, argv[0], &n);
    JS_SetPropertyStr(ctx, r, "jit", JS_TRUE);
    JS_SetPropertyStr(ctx, r, "functions", JS_NewInt32(ctx, n));
    JS_SetPropertyStr(ctx, r, "compiled", JS_NewInt32(ctx, c < 0 ? 0 : c));
#else
    JS_SetPropertyStr(ctx, r, "jit", JS_FALSE);
    JS_SetPropertyStr(ctx, r, "functions", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, r, "compiled", JS_NewInt32(ctx, 0));
#endif

    return r;
}


/*
 * nginx.jitCompile(fn[, opts]) -> report   [AOT-A, opt-in]
 *
 * Lower a host-JS function AND its nested functions to native C at LOAD time,
 * the same way COMCON C5 does for an admitted comcon.include fragment.
 *
 * Why it exists: host JS loaded via js_source is not compiled otherwise. C5's
 * server-AOT covers admitted fragments only, and the engine's automatic path
 * merely ENQUEUES -- nothing in src/js ever drains it -- so every
 * location.handler and mirror rule runs interpreted.
 *
 * WHY LOAD TIME, AND WHY IT MUST BE: the gcc worker is a pthread, and a pthread
 * does not survive fork(). jit_atfork_child() zeroes jit_worker.started, so
 * every enqueue path is inert in a WORKER. nginx creates the JS runtime in the
 * master at init_conf and workers fork from it, so compiling here -- pre-fork,
 * in the master -- is the only thing that works, and the resulting .so mapping
 * and jit_func pointers are inherited by every worker through COW. That is
 * exactly why C5 works.
 *
 * OPT-IN, and not a directive: pilgrim wraps an existing nginx and adds no
 * nginx configuration (see the project's foundational principle). The policy
 * decides, in JS, at load.
 *
 * BUDGET: compiling costs wall-clock on the config-load path, so
 *   opts.maxFunctions  cap on functions enqueued  (default: unbounded)
 *   opts.maxMillis     stop once this much elapsed (default: unbounded)
 * The engine drains in chunks, so maxMillis bounds the whole call.
 *
 * RETURNS A REPORT, NOT A BOOLEAN, deliberately:
 *   { walked, attempted, installed, skipped, budgetHit, ms }
 * `installed` is the only field that means compiled code exists. The previous
 * version of this function returned js_comcon_aot_compile() == 0, which is true
 * as soon as the argument is a bytecode function -- and that hid a real bug for
 * a month: codegen emitted a stale extern, gcc rejected every direct-call
 * function, and this still reported success. A compiler that falls back
 * silently must report counts, not success.
 */
static JSValue
ngx_js_jit_compile(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
#ifdef CONFIG_JIT
    JSJITCompileReport  rep;
    JSValue             out, v;
    int32_t             max_funcs = 0;
    double              max_ms = 0;
#endif

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "jitCompile(fn[, opts]): function required");
    }

#ifdef CONFIG_JIT
    if (argc > 1 && JS_IsObject(argv[1])) {
        v = JS_GetPropertyStr(ctx, argv[1], "maxFunctions");
        if (!JS_IsUndefined(v) && JS_ToInt32(ctx, &max_funcs, v) < 0) {
            JS_FreeValue(ctx, v);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, argv[1], "maxMillis");
        if (!JS_IsUndefined(v) && JS_ToFloat64(ctx, &max_ms, v) < 0) {
            JS_FreeValue(ctx, v);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, v);
    }

    if (js_jit_compile_tree(ctx, argv[0], (int) max_funcs, max_ms, &rep) < 0) {
        return JS_ThrowTypeError(ctx,
                                 "jitCompile(fn): not a bytecode function");
    }

    out = JS_NewObject(ctx);
    if (JS_IsException(out)) {
        return out;
    }
    JS_SetPropertyStr(ctx, out, "walked",    JS_NewInt32(ctx, rep.walked));
    JS_SetPropertyStr(ctx, out, "attempted", JS_NewInt32(ctx, rep.attempted));
    JS_SetPropertyStr(ctx, out, "installed", JS_NewInt32(ctx, rep.installed));
    JS_SetPropertyStr(ctx, out, "skipped",   JS_NewInt32(ctx, rep.skipped));
    JS_SetPropertyStr(ctx, out, "budgetHit", JS_NewBool(ctx, rep.budget_hit));
    JS_SetPropertyStr(ctx, out, "ms",        JS_NewFloat64(ctx, rep.ms));
    return out;
#else
    /* Built without -DCONFIG_JIT. Report honestly rather than silently
     * pretending, so a benchmark arm cannot be mislabelled. */
    {
        JSValue out = JS_NewObject(ctx);
        if (JS_IsException(out)) {
            return out;
        }
        JS_SetPropertyStr(ctx, out, "walked",    JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, out, "attempted", JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, out, "installed", JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, out, "skipped",   JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, out, "budgetHit", JS_FALSE);
        JS_SetPropertyStr(ctx, out, "ms",        JS_NewFloat64(ctx, 0));
        return out;
    }
#endif
}


/* nginx.suspendAcceptance() / nginx.resumeAcceptance()                */
/* Phase 1: per-worker connection acceptance control.                   */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_disable_accept_events — removes all listening socket read events
 * from this worker's event loop.  Equivalent to the static
 * ngx_disable_accept_events(cycle, 1) in ngx_event_accept.c.
 *
 * New TCP connections continue to queue in the OS SYN backlog (up to
 * net.core.somaxconn) and will be accepted once resumeAcceptance() is
 * called.  With multiple workers, other workers continue accepting
 * normally.
 */
ngx_int_t
ngx_js_disable_accept_events(ngx_cycle_t *cycle)
{
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    ls = cycle->listening.elts;

    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;

        if (c == NULL || !c->read->active) {
            continue;
        }

        if (ngx_del_event(c->read, NGX_READ_EVENT, NGX_DISABLE_EVENT)
            == NGX_ERROR)
        {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


/*
 * nginx.suspendAcceptance()
 *
 * Removes all listening socket read events from this worker's nginx event
 * loop.  Incoming TCP connections queue in the OS backlog; other workers
 * (if any) continue accepting normally.  Only callable from worker context.
 *
 * Use-case: wrap a batch of COM mutations that must be applied atomically
 * from the request handler's perspective:
 *
 *   nginx.suspendAcceptance();
 *   nginx.http.upstreams[0].peers[0].weight = 200;
 *   nginx.http.servers[0].ssl.cert = '/etc/ssl/new.pem';
 *   nginx.resumeAcceptance();
 *
 * For cross-worker atomic batches use Phase 2 (nginx.suspendAllWorkers).
 */
static JSValue
ngx_js_suspend_acceptance(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowTypeError(ctx,
            "suspendAcceptance: only callable from worker process");
    }

    if (ngx_js_disable_accept_events((ngx_cycle_t *) ngx_cycle) != NGX_OK) {
        return JS_ThrowInternalError(ctx, "suspendAcceptance: failed");
    }

    return JS_UNDEFINED;
}


/*
 * nginx.resumeAcceptance()
 *
 * Re-adds listening socket read events that were removed by
 * suspendAcceptance().  No-op if acceptance was not suspended.
 * Only callable from worker context.
 */
static JSValue
ngx_js_resume_acceptance(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowTypeError(ctx,
            "resumeAcceptance: only callable from worker process");
    }

    if (ngx_enable_accept_events((ngx_cycle_t *) ngx_cycle) != NGX_OK) {
        return JS_ThrowInternalError(ctx, "resumeAcceptance: failed");
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Phase 2 — nginx.suspendAllWorkers() / nginx.resumeAllWorkers()      */
/* ------------------------------------------------------------------ */

/*
 * Context for the accept-control reply fd event handler.
 * Allocated with ngx_alloc; freed by the handler after the Promise resolves.
 */
typedef struct {
    JSContext        *ctx;
    JSRuntime        *rt;
    ngx_js_worker_t  *w;
    JSValue           resolve;
    JSValue           reject;
    ngx_connection_t *conn;
} ngx_js_accept_ctrl_ctx_t;


/*
 * Called when the manager has broadcast the suspend/resume command to all
 * workers, collected their acks, and written 1 byte on reply_fd.
 * Resolves the awaited Promise so the async handler resumes.
 */
static void
ngx_js_accept_ctrl_reply_handler(ngx_event_t *ev)
{
    ngx_connection_t          *conn;
    ngx_js_accept_ctrl_ctx_t  *actx;
    uint8_t                    ack;
    JSValue                    ret;
    JSContext                 *job_ctx;

    conn = ev->data;
    actx = conn->data;

    ack = 0;

    if (recv(conn->fd, &ack, 1, MSG_DONTWAIT) != 1) {
        ack = 0;
    }

    /*
     * The status byte is the number of workers that did NOT ack within the
     * manager's timeout.  Resolve with it so `await nginx.suspendAllWorkers()`
     * can be checked: 0 means every worker really did suspend.  Resolving
     * with a value rather than rejecting keeps existing callers -- which
     * ignore the result -- working unchanged.
     */
    {
        JSValue  res = JS_NewObject(actx->ctx);

        JS_SetPropertyStr(actx->ctx, res, "unacked",
                          JS_NewInt32(actx->ctx, (int32_t) ack));
        JS_SetPropertyStr(actx->ctx, res, "ok",
                          JS_NewBool(actx->ctx, ack == 0));

        ret = JS_Call(actx->ctx, actx->resolve, JS_UNDEFINED, 1, &res);
        JS_FreeValue(actx->ctx, res);
    }
    JS_FreeValue(actx->ctx, ret);
    JS_FreeValue(actx->ctx, actx->resolve);
    JS_FreeValue(actx->ctx, actx->reject);

    while (JS_ExecutePendingJob(actx->rt, &job_ctx) > 0) { }
    ngx_js_async_check(actx->w);
    ngx_js_bf_async_check(actx->w);
    ngx_js_sf_async_check(actx->w);
    ngx_js_l4_async_check(actx->w);

    /*
     * Clean up the reply fd connection.
     *
     * This used to del the event with NGX_CLOSE_EVENT, free the connection and
     * set fd = -1 -- but never close() the fd.  NGX_CLOSE_EVENT tells the
     * epoll module to SKIP epoll_ctl(DEL) precisely because the fd is about to
     * be closed and closing drops it from the set implicitly.  With no close()
     * the descriptor leaked AND stayed registered in the worker's epoll set,
     * while ngx_free_connection() handed its ngx_connection_t back to the free
     * list.  Two consequences, both bad:
     *
     *   - the manager closes its end, so the worker's end sits permanently
     *     readable-at-EOF; epoll_wait then returns immediately forever and the
     *     worker spins (observed: both workers in state R at 60%+ CPU, the
     *     listen queue backing up, no requests served);
     *   - the stale epoll entry still carries the freed connection pointer, so
     *     once that slot is recycled for a real request the event fires
     *     against the WRONG connection.
     *
     * ngx_close_connection() is the canonical teardown: it removes the event,
     * frees the connection and closes the descriptor, in that order.
     */
    ngx_close_connection(conn);

    ngx_free(actx);
}


/*
 * Common implementation for suspendAllWorkers() and resumeAllWorkers().
 * Sends cmd_type to the manager, registers the reply fd in the event loop,
 * and returns a Promise that resolves when all workers have acked.
 */
static JSValue
ngx_js_make_accept_ctrl_promise(JSContext *ctx, uint32_t cmd_type)
{
    ngx_js_worker_t           *w;
    int                        reply_fd;
    JSValue                    resolving[2], promise;
    ngx_connection_t          *conn;
    ngx_js_accept_ctrl_ctx_t  *actx;

    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowTypeError(ctx,
            "suspendAllWorkers/resumeAllWorkers: only callable from worker");
    }

    w = JS_GetContextOpaque(ctx);
    if (w == NULL) {
        return JS_ThrowInternalError(ctx, "no worker context");
    }

    reply_fd = ngx_js_mgr_accept_control(cmd_type);
    if (reply_fd < 0) {
        return JS_ThrowInternalError(ctx,
            "accept control: manager unavailable");
    }

    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        close(reply_fd);
        return promise;
    }

    actx = ngx_alloc(sizeof(ngx_js_accept_ctrl_ctx_t), ngx_cycle->log);
    if (actx == NULL) {
        close(reply_fd);
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "accept control: alloc failed");
    }

    conn = ngx_get_connection(reply_fd, ngx_cycle->log);
    if (conn == NULL) {
        close(reply_fd);
        ngx_free(actx);
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "accept control: no connection slot");
    }

    actx->ctx     = ctx;
    actx->rt      = w->rt;
    actx->w       = w;
    actx->resolve = resolving[0];
    actx->reject  = resolving[1];
    actx->conn    = conn;

    conn->data          = actx;
    conn->read->handler = ngx_js_accept_ctrl_reply_handler;
    conn->read->log     = ngx_cycle->log;

    if (ngx_add_event(conn->read, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_free_connection(conn);
        conn->fd = (ngx_socket_t) -1;
        /* The three failure paths above this one all close reply_fd; this one
         * did not, leaking one socketpair descriptor per failed
         * suspendAllWorkers()/resumeAllWorkers().  Nothing else can close it
         * afterwards: actx is freed just below and conn->fd has been cleared,
         * so the number is lost.  (No epoll entry to worry about here -- the
         * add_event we are handling the failure of never registered one.) */
        close(reply_fd);
        ngx_free(actx);
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "accept control: add event failed");
    }

    return promise;
}


/*
 * nginx.suspendAllWorkers()
 *
 * Returns a Promise that resolves after every worker has disabled its
 * accept events.  The manager broadcasts the suspend command and waits
 * for acks (with a 500ms timeout) before resolving.
 *
 *   await nginx.suspendAllWorkers();
 *   // all workers suspended; make batch COM mutations here
 *   await nginx.resumeAllWorkers();
 *
 * The promise resolves with { ok, unacked }.  `unacked` is the number of
 * workers that did not ack inside the manager's window; those workers have
 * NOT suspended yet and are still accepting, so `ok === false` means the
 * batch was not actually atomic.  Check it if that matters.  (It used to
 * resolve with undefined and report success unconditionally.)
 *
 * RESUME MUST TRAVEL THE SAME CHANNEL AS THE SUSPEND.
 *
 * suspendAllWorkers/resumeAllWorkers both go through the manager, which sends
 * on a per-worker STREAM socket, so delivery is ordered: a worker that acked
 * late still receives its SUSPEND and then the RESUME queued behind it, and
 * ends up accepting.  nginx.withSuspendedAcceptance() is built on that pair
 * and additionally resumes from a `finally`, so it is safe.
 *
 * Resuming by some OTHER route -- a SharedWorker broadcast that calls
 * nginx.resumeAcceptance() in each worker, say -- travels a different
 * descriptor, and nothing orders the two against each other.  A SUSPEND
 * delayed past that resume then arrives with nothing left to undo it, and the
 * worker stops accepting for good.  If every worker is hit, the process stops
 * accepting entirely.  Prefer resumeAllWorkers(); if a cross-channel pattern
 * is unavoidable, make the resume idempotent and re-issue it.
 */
static JSValue
ngx_js_suspend_all_workers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return ngx_js_make_accept_ctrl_promise(ctx,
                                           NGX_JS_MGR_CMD_SUSPEND_ACCEPT);
}


/*
 * nginx.resumeAllWorkers()
 *
 * Returns a Promise that resolves after every worker has re-enabled its
 * accept events.
 */
static JSValue
ngx_js_resume_all_workers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return ngx_js_make_accept_ctrl_promise(ctx,
                                           NGX_JS_MGR_CMD_RESUME_ACCEPT);
}


/*
 * ngx_js_com_num_range — read a bounded number out of a JS value.
 *
 * The one place this check lives.  JS_ToInt32()/JS_ToInt64() are CASTS: they
 * answer 0 for NaN, for {} and for "abc" without reporting an error, and they
 * happily hand back a negative that the caller then stores in an ngx_uint_t.
 * Both halves of that produced real defects -- `peers[0].weight = -1` stored
 * ~1.8e19 into the load balancer (5186565a1), `ssl.verifyDepth = {}` silently
 * set certificate verification depth to 0 (0ebac7e47), and `respond(-1)` put
 * "HTTP/1.1 18446744073709551615" on the wire.
 *
 * Those were found one at a time, in code that had been written twice: a
 * config-phase setter and its runtime twin, each casting, neither checking.
 * A shared function is the fix for that, not a shared convention -- the
 * convention is what drifted.
 *
 * `name` is the full property name for the message ("peer.weight").  Policy is
 * plain ToNumber, then refuse non-finite and out-of-range, so the coercions
 * ToNumber itself admits (Number([]) is 0, Number("600") is 600) still pass.
 */
int
ngx_js_com_num_range(JSContext *ctx, JSValueConst val, int64_t min,
    int64_t max, const char *name, int64_t *out)
{
    double  d;

    if (JS_ToFloat64(ctx, &d, val) < 0) {
        return -1;
    }

    if (isnan(d) || isinf(d)) {
        JS_ThrowRangeError(ctx, "%s must be a number", name);
        return -1;
    }

    if (d < (double) min || d > (double) max) {
        JS_ThrowRangeError(ctx, "%s must be between %lld and %lld, got %g",
                           name, (long long) min, (long long) max, d);
        return -1;
    }

    *out = (int64_t) d;
    return 0;
}


/* ------------------------------------------------------------------ */
/* nginx.get / nginx.set / nginx.settable — generic path accessors      */
/* ------------------------------------------------------------------ */

/*
 * Normalise a path string in-place: replace '[' with '.' and remove ']'.
 * tmp must be at least path_len + 1 bytes.
 */
static void
ngx_js_path_normalise(char *tmp, const char *path, size_t path_len)
{
    char  *p;

    ngx_memcpy(tmp, path, path_len);
    tmp[path_len] = '\0';

    for (p = tmp; *p; p++) {
        if (*p == '[') {
            *p = '.';
        } else if (*p == ']') {
            ngx_memmove(p, p + 1, ngx_strlen(p));
            p--;   /* re-examine at same position */
        }
    }
}


/*
 * Traverse a normalised dot-path from root.
 *
 * stop_at_parent == 0: traverse every segment, return the final value.
 * stop_at_parent != 0: traverse every segment except the last one;
 *   copy the last segment name into last_key (must be ≥ 256 bytes).
 *   Returns the parent object so the caller can set the final property.
 *
 * Returns the traversed object (caller owns it); JS_EXCEPTION on error.
 */
static JSValue
ngx_js_path_traverse(JSContext *ctx, JSValueConst root,
    char *path, ngx_uint_t stop_at_parent, char *last_key)
{
    JSValue        cur, next;
    char          *seg, *dot;
    char          *endptr;
    unsigned long  n;

    cur = JS_DupValue(ctx, root);
    seg = path;

    while (*seg == '.') { seg++; }   /* skip leading dots */

    for (;;) {
        if (!*seg) { break; }

        dot = strchr(seg, '.');

        if (stop_at_parent && dot == NULL) {
            /*
             * seg is the last segment (no more dots).
             * Store it as the key for the caller and stop WITHOUT traversing.
             */
            if (last_key) {
                ngx_cpystrn((u_char *) last_key, (u_char *) seg,
                            ngx_strlen(seg) + 1);
            }
            break;
        }

        /* Advance past this segment */
        if (dot) { *dot = '\0'; }

        n = strtoul(seg, &endptr, 10);
        if (*endptr == '\0' && endptr != seg) {
            next = JS_GetPropertyUint32(ctx, cur, (uint32_t) n);
        } else {
            next = JS_GetPropertyStr(ctx, cur, seg);
        }

        JS_FreeValue(ctx, cur);
        cur = next;

        if (JS_IsException(cur)) {
            return cur;
        }

        if (dot == NULL) {
            break;
        }

        seg = dot + 1;
        while (*seg == '.') { seg++; }
    }

    return cur;
}


/*
 * nginx.get(path) — read any property in the nginx.* object tree by path.
 *
 * Examples:
 *   nginx.get("http.upstreams[0].peers[0].weight")
 *   nginx.get("http.servers[0].locations[0].root")
 */
static JSValue
ngx_js_nginx_fn_get(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char  *path;
    size_t       plen;
    char         tmp[512];
    JSValue      result;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "nginx.get: path string required");
    }

    path = JS_ToCStringLen(ctx, &plen, argv[0]);
    if (!path) { return JS_EXCEPTION; }

    if (plen + 1 > sizeof(tmp)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "nginx.get: path too long");
    }

    ngx_js_path_normalise(tmp, path, plen);
    JS_FreeCString(ctx, path);

    result = ngx_js_path_traverse(ctx, this_val, tmp, 0, NULL);
    return result;
}


/*
 * nginx.set(path, value) — write any settable property in the nginx.* tree.
 *
 * Examples:
 *   nginx.set("http.upstreams[0].peers[0].weight", 10)
 *   nginx.set("http.servers[0].locations[0].root", "/new/path")
 */
static JSValue
ngx_js_nginx_fn_set(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char  *path;
    size_t       plen;
    char         tmp[512];
    char         last_key[256];
    JSValue      parent;
    char        *endptr;
    unsigned long n;
    int          rc;

    if (argc < 2 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "nginx.set: path string and value required");
    }

    path = JS_ToCStringLen(ctx, &plen, argv[0]);
    if (!path) { return JS_EXCEPTION; }

    if (plen + 1 > sizeof(tmp)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "nginx.set: path too long");
    }

    ngx_js_path_normalise(tmp, path, plen);
    JS_FreeCString(ctx, path);

    last_key[0] = '\0';
    parent = ngx_js_path_traverse(ctx, this_val, tmp, 1, last_key);
    if (JS_IsException(parent)) { return parent; }

    if (last_key[0] == '\0') {
        JS_FreeValue(ctx, parent);
        return JS_ThrowTypeError(ctx,
            "nginx.set: path must have at least one segment");
    }

    n = strtoul(last_key, &endptr, 10);
    if (*endptr == '\0' && endptr != last_key) {
        rc = JS_SetPropertyUint32(ctx, parent, (uint32_t) n,
                                  JS_DupValue(ctx, argv[1]));
    } else {
        rc = JS_SetPropertyStr(ctx, parent, last_key,
                               JS_DupValue(ctx, argv[1]));
    }

    JS_FreeValue(ctx, parent);

    return (rc < 0) ? JS_EXCEPTION : JS_UNDEFINED;
}


/*
 * nginx.settable(path_or_obj) — return an array of settable property names
 * for the COM object at the given path (string) or directly (object).
 *
 * Examples:
 *   nginx.settable("http.upstreams[0].peers[0]")
 *   // → ["weight","maxFails","down","failTimeout","maxConns"]
 *
 *   nginx.settable(nginx.http.servers[0].locations[0])
 *   // → ["root","sendfile","tcpNopush", ...]
 */
static JSValue
ngx_js_nginx_fn_settable(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue     obj;
    const char *path;
    size_t      plen;
    char        tmp[512];
    JSValue     result;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "nginx.settable: path or object required");
    }

    if (JS_IsString(argv[0])) {
        path = JS_ToCStringLen(ctx, &plen, argv[0]);
        if (!path) { return JS_EXCEPTION; }

        if (plen + 1 > sizeof(tmp)) {
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "nginx.settable: path too long");
        }

        ngx_js_path_normalise(tmp, path, plen);
        JS_FreeCString(ctx, path);

        obj = ngx_js_path_traverse(ctx, this_val, tmp, 0, NULL);
        if (JS_IsException(obj)) { return obj; }

        result = ngx_js_settable_props(ctx, obj);
        JS_FreeValue(ctx, obj);
        return result;
    }

    /* Object passed directly */
    return ngx_js_settable_props(ctx, argv[0]);
}


/*
 * nginx.describe(path [, name]) — return mutation safety metadata for the COM
 * object at path (string) or passed directly (object).
 *
 *   nginx.describe("http.upstreams[0].peers[0]")
 *   // → [{name:"weight", class:"safe", propagation:"zoned-shared", …}, …]
 *
 *   nginx.describe("http.servers[0].locations[0]", "handler")
 *   // → {name:"handler", class:"guarded", requestScoped:false, …}  (or null)
 *
 * See js_com_docs/js-com-safety-classes.adoc.  Unregistered classes yield an
 * empty array (or null for the single-member form), mirroring settable().
 */
/*
 * nginx.describeType(name [, member]) — describe a COM class by TYPE NAME.
 *
 *   nginx.describeType("NginxLocation")            -> Descriptor[]
 *   nginx.describeType("NginxLocation", "addHook") -> Descriptor | null
 *
 * describe() needs an object: it answers "what can I do to THIS value". A
 * static check has no object — to validate `nginx.addServer(..).addLocation(..)`
 * it must ask what a NginxLocation offers before any location exists. That is
 * the M4 return-type binding, and the reason comcon.reviewCalls() could only
 * check the FIRST call in a chain.
 *
 * Deliberately a separate function rather than an overload: describe()'s string
 * form is a COM PATH ("http.servers[0]"), so a type name there would be
 * ambiguous with a path that happens to match.
 */
static JSValue
ngx_js_nginx_fn_describe_type(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue      result;
    const char  *type, *name;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "nginx.describeType(name[, member]): "
                                 "type name required");
    }

    type = JS_ToCString(ctx, argv[0]);
    if (type == NULL) {
        return JS_EXCEPTION;
    }

    name = NULL;
    if (argc >= 2 && JS_IsString(argv[1])) {
        name = JS_ToCString(ctx, argv[1]);
        if (name == NULL) {
            JS_FreeCString(ctx, type);
            return JS_EXCEPTION;
        }
    }

    result = ngx_js_describe_type(ctx, type, name);

    JS_FreeCString(ctx, type);
    if (name != NULL) {
        JS_FreeCString(ctx, name);
    }

    return result;
}


static JSValue
ngx_js_nginx_fn_describe(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue      obj, result;
    const char  *path, *name;
    size_t       plen;
    char         tmp[512];

    if (argc < 1 || JS_IsUndefined(argv[0])) {
        /* discovery root: list every classifiable COM class + its members */
        return ngx_js_describe_catalog(ctx);
    }

    if (JS_IsString(argv[0])) {
        path = JS_ToCStringLen(ctx, &plen, argv[0]);
        if (!path) { return JS_EXCEPTION; }

        if (plen + 1 > sizeof(tmp)) {
            JS_FreeCString(ctx, path);
            return JS_ThrowTypeError(ctx, "nginx.describe: path too long");
        }

        ngx_js_path_normalise(tmp, path, plen);
        JS_FreeCString(ctx, path);

        obj = ngx_js_path_traverse(ctx, this_val, tmp, 0, NULL);
        if (JS_IsException(obj)) { return obj; }

    } else {
        obj = JS_DupValue(ctx, argv[0]);
    }

    if (argc >= 2 && JS_IsString(argv[1])) {
        name = JS_ToCString(ctx, argv[1]);
        if (!name) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        result = ngx_js_describe_member(ctx, obj, name);
        JS_FreeCString(ctx, name);

    } else {
        result = ngx_js_describe_members(ctx, obj);
    }

    JS_FreeValue(ctx, obj);
    return result;
}


/* ------------------------------------------------------------------ */
/* ngx_js_com_init — main entry point called from ngx_js_module.c      */
/* ------------------------------------------------------------------ */

/*
 * nginx.on(event, fn)
 *
 * Registers fn as a handler for the named master-process lifecycle event.
 * Valid event names: 'terminate', 'quit', 'reload', 'reloaded', 'reopen',
 * 'workerSpawned', 'workerExited'.
 *
 * Multiple handlers per event are supported.
 * Returns nginx (this_val) for chaining.
 * Only callable from master (init_conf) context — throws in workers.
 */
static JSValue
ngx_js_nginx_on(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_cycle_t    *cycle;
    ngx_js_conf_t  *jcf;
    const char     *event;
    JSValue         arr, len_val;
    uint32_t        len;

    if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
            "nginx.on(event, fn): string event name and function required");
    }

    if (ngx_process != NGX_PROCESS_MASTER && ngx_process != NGX_PROCESS_SINGLE) {
        return JS_ThrowInternalError(ctx,
            "nginx.on: only callable in master process (init_conf context)");
    }

    cycle = JS_GetContextOpaque(ctx);
    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.on: no cycle context");
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || JS_IsUninitialized(jcf->master_handlers)) {
        return JS_ThrowInternalError(ctx,
            "nginx.on: master_handlers not initialised");
    }

    event = JS_ToCString(ctx, argv[0]);
    if (!event) {
        return JS_EXCEPTION;
    }

    /* Get or create the per-event handler array */
    arr = JS_GetPropertyStr(ctx, jcf->master_handlers, event);
    if (!JS_IsArray(ctx, arr)) {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, jcf->master_handlers, event,
                          JS_DupValue(ctx, arr));
    }

    /* arr.push(fn) */
    len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));

    JS_FreeValue(ctx, arr);
    JS_FreeCString(ctx, event);

    return JS_DupValue(ctx, this_val);   /* return nginx for chaining */
}


/*
 * ngx_js_channel_send — write NGX_CMD_JS_MESSAGE header + payload to `fd`.
 * Uses a single sendmsg() with two iov vectors (header + payload) so that
 * both land in the kernel buffer atomically.  The channel is SOCK_STREAM
 * AF_UNIX; both ends are O_NONBLOCK (set by ngx_spawn_process).
 * Returns NGX_OK or NGX_ERROR.
 */
ngx_int_t
ngx_js_channel_send(ngx_socket_t fd, ngx_uint_t command, ngx_uint_t slot_arg,
    const uint8_t *buf, size_t len, ngx_log_t *log)
{
    ngx_channel_t   ch;
    struct iovec    iov[2];
    struct msghdr   mh;
    ssize_t         n;

    ngx_memzero(&ch, sizeof(ch));
    ch.command = command;
    ch.pid     = ngx_pid;
    ch.slot    = (ngx_int_t) slot_arg;
    ch.fd      = (ngx_fd_t) len;   /* payload_len */

    iov[0].iov_base = &ch;
    iov[0].iov_len  = sizeof(ch);
    iov[1].iov_base = (void *) buf;
    iov[1].iov_len  = len;

    ngx_memzero(&mh, sizeof(mh));
    mh.msg_iov    = iov;
    mh.msg_iovlen = 2;

    n = sendmsg(fd, &mh, 0);
    if (n < 0) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_errno,
                      "js: channel sendmsg() failed");
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * nginx.sendToWorker(slot, data)
 *
 * Serializes `data` with JS_WriteObject and sends it over the nginx channel
 * (ngx_processes[slot].channel[0]) to worker `slot` using the new
 * NGX_CMD_JS_MESSAGE command.  Only callable from the master process.
 * The worker's ngx_channel_handler dispatches it to nginx.on('message') handlers.
 */
static JSValue
ngx_js_send_to_worker(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_cycle_t    *cycle;
    int32_t         slot;
    uint8_t        *buf;
    size_t          len;

    if (ngx_process != NGX_PROCESS_MASTER && ngx_process != NGX_PROCESS_SINGLE) {
        return JS_ThrowInternalError(ctx,
            "nginx.sendToWorker: only callable in master process");
    }

    if (argc < 2 || JS_ToInt32(ctx, &slot, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx,
            "nginx.sendToWorker(slot, data): integer slot required");
    }

    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);
    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.sendToWorker: no cycle");
    }

    if (slot < 0 || slot >= ngx_last_process
        || ngx_processes[slot].channel[0] < 0)
    {
        return JS_ThrowRangeError(ctx,
            "nginx.sendToWorker: slot %d has no active channel", slot);
    }

    buf = JS_WriteObject(ctx, &len, argv[1], JS_WRITE_OBJ_REFERENCE);
    if (buf == NULL) {
        return JS_EXCEPTION;
    }

    if (len > NGX_JS_MSG_MAX) {
        js_free(ctx, buf);
        return JS_ThrowRangeError(ctx,
            "nginx.sendToWorker: message too large (%zu > %d)", len,
            NGX_JS_MSG_MAX);
    }

    ngx_js_channel_send(ngx_processes[slot].channel[0],
                        NGX_CMD_JS_MESSAGE, (ngx_uint_t) slot,
                        buf, len, cycle->log);
    js_free(ctx, buf);

    return JS_UNDEFINED;
}


/*
 * nginx.broadcastToWorkers(data)
 *
 * Sends `data` to every active worker channel.
 * Equivalent to calling sendToWorker(slot, data) for each slot.
 */
static JSValue
ngx_js_broadcast_to_workers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_cycle_t    *cycle;
    uint8_t        *buf;
    size_t          len;
    ngx_int_t       k;

    if (ngx_process != NGX_PROCESS_MASTER && ngx_process != NGX_PROCESS_SINGLE) {
        return JS_ThrowInternalError(ctx,
            "nginx.broadcastToWorkers: only callable in master process");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "nginx.broadcastToWorkers(data): data required");
    }

    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);
    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.broadcastToWorkers: no cycle");
    }

    buf = JS_WriteObject(ctx, &len, argv[0], JS_WRITE_OBJ_REFERENCE);
    if (buf == NULL) {
        return JS_EXCEPTION;
    }

    if (len > NGX_JS_MSG_MAX) {
        js_free(ctx, buf);
        return JS_ThrowRangeError(ctx,
            "nginx.broadcastToWorkers: message too large (%zu > %d)",
            len, NGX_JS_MSG_MAX);
    }

    for (k = 0; k < ngx_last_process; k++) {
        if (ngx_processes[k].channel[0] >= 0) {
            ngx_js_channel_send(ngx_processes[k].channel[0],
                                NGX_CMD_JS_MESSAGE, (ngx_uint_t) k,
                                buf, len, cycle->log);
        }
    }

    js_free(ctx, buf);

    return JS_UNDEFINED;
}


/*
 * nginx.sendToMaster(data)
 *
 * Worker → master JS message.  Serializes `data` and sends it over the
 * existing nginx channel (ngx_channel = channel[1]) using NGX_CMD_JS_WORKER_MSG.
 * The master receives it on SIGIO, reads from channel[0], and dispatches
 * to nginx.on('workerMessage', fn(slot, data)) handlers.
 * Only callable from worker processes.
 */
static JSValue
ngx_js_send_to_master(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_cycle_t  *cycle;
    uint8_t      *buf;
    size_t        len;

    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "nginx.sendToMaster: only callable in worker process");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "nginx.sendToMaster(data): data required");
    }

    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);
    if (cycle == NULL) {
        /* worker context opaque is ngx_js_worker_t*, not cycle */
        cycle = (ngx_cycle_t *) ngx_cycle;
    }

    if (ngx_channel < 0) {
        return JS_ThrowInternalError(ctx,
            "nginx.sendToMaster: channel not available");
    }

    buf = JS_WriteObject(ctx, &len, argv[0], JS_WRITE_OBJ_REFERENCE);
    if (buf == NULL) {
        return JS_EXCEPTION;
    }

    if (len > NGX_JS_MSG_MAX) {
        js_free(ctx, buf);
        return JS_ThrowRangeError(ctx,
            "nginx.sendToMaster: message too large (%zu > %d)", len,
            NGX_JS_MSG_MAX);
    }

    ngx_js_channel_send(ngx_channel, NGX_CMD_JS_WORKER_MSG,
                        (ngx_uint_t) ngx_worker, buf, len,
                        (cycle != NULL) ? cycle->log : ngx_cycle->log);
    js_free(ctx, buf);

    return JS_UNDEFINED;
}


/*
 * ------------------------------------------------------------------ *
 * P18: Online package registry helpers                                *
 * ------------------------------------------------------------------ *
 *
 * ngx_js_is_registry_ref(path)
 *   Returns 1 if path looks like a registry reference of the form
 *   "vendor/plugin[@version]" rather than a filesystem path.
 *
 *   A registry reference:
 *     - does NOT start with '/', './', or '../'
 *     - contains exactly ONE '/' (the vendor/plugin separator)
 *     - has no embedded spaces
 *
 * ngx_js_resolve_cache_dir(cycle, out, len)
 *   Fills out[0..len-1] with the absolute path of the ngxjs package
 *   cache directory.  Resolution order:
 *     1. $NGXJS_CACHE              — explicit override
 *     2. $HOME/.cache/ngxjs        — XDG-style user cache
 *     3. {cycle->prefix}ngxjs_cache — nginx prefix fallback
 *   Returns NGX_OK on success, NGX_ERROR if out is too small.
 *
 * ngx_js_resolve_registry_pkg(cycle, ref, out, out_len)
 *   Resolves a registry reference "vendor/plugin[@version]" to the
 *   on-disk cache directory:
 *     {cache_dir}/packages/vendor/plugin/{version}/
 *   If @version is omitted, reads {cache}/packages/vendor/plugin/.latest
 *   for the installed-latest version string.
 *   Returns NGX_OK with out filled, NGX_ERROR (with log message) on
 *   failure (package not installed, .latest missing, dir absent).
 */

static ngx_int_t
ngx_js_is_registry_ref(const char *path)
{
    const char  *p, *slash;
    int          slashes;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    /* filesystem absolute or relative paths */
    if (path[0] == '/') { return 0; }
    if (path[0] == '.' && (path[1] == '/' ||
        (path[1] == '.' && path[2] == '/'))) { return 0; }

    /* must contain exactly one '/' and no spaces */
    slashes = 0;
    slash   = NULL;
    for (p = path; *p; p++) {
        if (*p == ' ') { return 0; }
        if (*p == '/') { slashes++; slash = p; }
        if (slashes > 1) { return 0; }
    }

    /* vendor/plugin — slash must not be first or last char */
    return (slashes == 1 && slash != path && *(slash + 1) != '\0');
}


static ngx_int_t
ngx_js_resolve_cache_dir(ngx_cycle_t *cycle, char *out, size_t len)
{
    const char  *env;
    u_char      *p;

    env = getenv("NGXJS_CACHE");
    if (env && env[0] != '\0') {
        p = ngx_snprintf((u_char *) out, len - 1, "%s", env);
        *p = '\0';
        return NGX_OK;
    }

    env = getenv("HOME");
    if (env && env[0] != '\0') {
        p = ngx_snprintf((u_char *) out, len - 1, "%s/.cache/ngxjs", env);
        *p = '\0';
        return NGX_OK;
    }

    /* fallback: nginx prefix */
    p = ngx_snprintf((u_char *) out, len - 1,
                     "%Vngxjs_cache", &cycle->prefix);
    *p = '\0';
    return NGX_OK;
}


static ngx_int_t
ngx_js_resolve_registry_pkg(ngx_cycle_t *cycle, const char *ref,
    char *out, size_t out_len)
{
    char         cache_dir[NGX_MAX_PATH];
    char         name_buf[256];   /* vendor/plugin (without @version) */
    const char  *at, *version;
    char         ver_buf[64];
    char         latest_path[NGX_MAX_PATH];
    char         pkg_dir[NGX_MAX_PATH];
    u_char      *p;
    ngx_fd_t     fd;
    ssize_t      n;
    size_t       name_len;

    if (ngx_js_resolve_cache_dir(cycle, cache_dir, sizeof(cache_dir))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* Split "vendor/plugin@version" into name + version */
    at = strchr(ref, '@');
    if (at != NULL) {
        name_len = (size_t) (at - ref);
        if (name_len == 0 || name_len >= sizeof(name_buf)) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: nginx.use: invalid package name in '%s'", ref);
            return NGX_ERROR;
        }
        ngx_memcpy(name_buf, ref, name_len);
        name_buf[name_len] = '\0';

        version = at + 1;
        if (version[0] == '\0') {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: nginx.use: empty version in '%s'; "
                          "use 'vendor/plugin' or 'vendor/plugin@X.Y.Z'",
                          ref);
            return NGX_ERROR;
        }

    } else {
        /* No @version — read .latest */
        ngx_memcpy(name_buf, ref, ngx_strlen(ref) + 1);

        p = ngx_snprintf((u_char *) latest_path, sizeof(latest_path) - 1,
                         "%s/packages/%s/.latest", cache_dir, name_buf);
        *p = '\0';

        fd = ngx_open_file((u_char *) latest_path, NGX_FILE_RDONLY,
                           NGX_FILE_OPEN, 0);
        if (fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: nginx.use: package '%s' has no installed "
                          "version; run: ngxjs install %s",
                          name_buf, name_buf);
            return NGX_ERROR;
        }

        n = read(fd, ver_buf, sizeof(ver_buf) - 1);
        ngx_close_file(fd);

        if (n <= 0) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: nginx.use: could not read .latest for '%s'",
                          name_buf);
            return NGX_ERROR;
        }

        /* strip trailing newline/whitespace */
        while (n > 0 && (ver_buf[n - 1] == '\n' || ver_buf[n - 1] == '\r'
                          || ver_buf[n - 1] == ' '))
        {
            n--;
        }
        ver_buf[n] = '\0';
        version = ver_buf;
    }

    /* Build the expected cache path */
    p = ngx_snprintf((u_char *) pkg_dir, sizeof(pkg_dir) - 1,
                     "%s/packages/%s/%s", cache_dir, name_buf, version);
    *p = '\0';

    /* Verify the directory exists */
    fd = ngx_open_file((u_char *) pkg_dir, NGX_FILE_RDONLY,
                       NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "js: nginx.use: package '%s@%s' not installed; "
                      "run: ngxjs install %s@%s",
                      name_buf, version, name_buf, version);
        return NGX_ERROR;
    }
    ngx_close_file(fd);

    if (ngx_strlen(pkg_dir) >= out_len) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "js: nginx.use: resolved package path too long");
        return NGX_ERROR;
    }

    ngx_memcpy(out, pkg_dir, ngx_strlen(pkg_dir) + 1);
    return NGX_OK;
}


/*
 * ngx_js_load_plugin — P16 shared helper.
 *
 * Load a JS plugin from absolute directory path `dir` into ctx/rt.
 * config_json: optional JSON string used as nginx.pluginConfig (NULL → "{}").
 *
 * Called from both the master (via nginx.use) and from workers (via the
 * NGX_CMD_JS_LOAD_PLUGIN broadcast handler).
 *
 * Returns NGX_OK on success, NGX_ERROR on failure.
 */
ngx_int_t
ngx_js_load_plugin(JSContext *ctx, JSRuntime *rt, ngx_cycle_t *cycle,
    const char *dir, const char *config_json)
{
    const char   *main_str;
    u_char       *src, *pkg_src;
    size_t        src_len, pkg_len;
    ngx_str_t     entry_path, pkg_path;
    JSValue       config, global, nginx_obj;
    JSValue       pkg_val, ngxjs_val, main_val;
    char          entry_buf[NGX_MAX_PATH];
    char          pkg_buf[NGX_MAX_PATH];

    /* default entry point: <dir>/index.js */
    {
        u_char *p = ngx_snprintf((u_char *) entry_buf, sizeof(entry_buf) - 1,
                                 "%s/index.js", dir);
        *p = '\0';
    }

    /* check package.json for a custom entry point */
    {
        u_char *p = ngx_snprintf((u_char *) pkg_buf, sizeof(pkg_buf) - 1,
                                 "%s/package.json", dir);
        *p = '\0';
    }

    pkg_path.data = (u_char *) pkg_buf;
    pkg_path.len  = ngx_strlen(pkg_buf);

    /*
     * package.json is optional — try to open it quietly.
     * ngx_js_read_file logs NGX_LOG_EMERG on open failure, which is
     * too noisy for an absent optional file, so check existence first.
     */
    pkg_src = NULL;
    pkg_len = 0;
    {
        ngx_fd_t  fd = ngx_open_file(pkg_path.data, NGX_FILE_RDONLY,
                                     NGX_FILE_OPEN, 0);
        if (fd != NGX_INVALID_FILE) {
            ngx_close_file(fd);
            pkg_src = ngx_js_read_file(cycle, &pkg_path, &pkg_len);
        }
        /* ENOENT (or any other open error) → skip package.json silently */
    }

    if (pkg_src != NULL) {
        pkg_val = JS_ParseJSON(ctx, (const char *) pkg_src, pkg_len,
                               "package.json");
        if (!JS_IsException(pkg_val)) {
            /* prefer ngxjs.main key over top-level main */
            ngxjs_val = JS_GetPropertyStr(ctx, pkg_val, "ngxjs");
            if (JS_IsObject(ngxjs_val)) {
                main_val = JS_GetPropertyStr(ctx, ngxjs_val, "main");
            } else {
                main_val = JS_GetPropertyStr(ctx, pkg_val, "main");
            }
            JS_FreeValue(ctx, ngxjs_val);

            if (JS_IsString(main_val)) {
                main_str = JS_ToCString(ctx, main_val);
                if (main_str) {
                    u_char *p = ngx_snprintf(
                        (u_char *) entry_buf, sizeof(entry_buf) - 1,
                        "%s/%s", dir, main_str);
                    *p = '\0';
                    JS_FreeCString(ctx, main_str);
                }
            }
            JS_FreeValue(ctx, main_val);
            JS_FreeValue(ctx, pkg_val);
        } else {
            JS_FreeValue(ctx, JS_GetException(ctx));
        }
    }

    /* read the plugin entry point */
    entry_path.data = (u_char *) entry_buf;
    entry_path.len  = ngx_strlen(entry_buf);

    src = ngx_js_read_file(cycle, &entry_path, &src_len);
    if (src == NULL) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "js: nginx.use: failed to read '%s'", entry_buf);
        return NGX_ERROR;
    }

    /* expose config as nginx.pluginConfig before plugin runs */
    if (config_json != NULL && config_json[0] != '\0') {
        config = JS_ParseJSON(ctx, config_json, strlen(config_json),
                              "<pluginConfig>");
        if (JS_IsException(config)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            config = JS_NewObject(ctx);
        }
    } else {
        config = JS_NewObject(ctx);
    }

    global    = JS_GetGlobalObject(ctx);
    nginx_obj = JS_GetPropertyStr(ctx, global, "nginx");
    JS_SetPropertyStr(ctx, nginx_obj, "pluginConfig", config);
    JS_FreeValue(ctx, nginx_obj);
    JS_FreeValue(ctx, global);

    /* evaluate the plugin as an ES module */
    if (ngx_js_eval_module(ctx, rt, src, src_len,
                           (const u_char *) entry_buf, cycle->log)
        != NGX_CONF_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * nginx.use(path[, config]) — JS-Pilgrim P7 / P16
 *
 * Load a JS plugin package from the filesystem.
 *
 * path   — directory path; may be absolute or relative to cycle->prefix.
 *          If the directory contains a package.json with an "ngxjs" object
 *          whose "main" key names the entry file, that file is loaded.
 *          Otherwise the directory's index.js is loaded.
 * config — optional JS value exposed as nginx.pluginConfig before the
 *          plugin script is evaluated.  Defaults to {}.
 *
 * P7:  callable before fork (master process) — loads the plugin into the
 *      master runtime so workers inherit it via COW.
 * P16: also callable from worker request handlers — loads the plugin into
 *      this worker's runtime immediately and broadcasts the load to all
 *      other workers via the master (NGX_CMD_JS_USE_PLUGIN → master →
 *      NGX_CMD_JS_LOAD_PLUGIN → each other worker).
 *
 * Returns nginx (for chaining).
 */
static JSValue
ngx_js_use(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_cycle_t   *cycle;
    const char    *path_str;
    char           dir_buf[NGX_MAX_PATH];
    const char    *config_json;
    JSValue        config_json_val;
    JSRuntime     *rt;
    uint8_t        payload[NGX_JS_MSG_MAX];
    size_t         path_payload_len, cfg_payload_len, payload_len;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "nginx.use(path[, config]): string path required");
    }

    /*
     * In master/single mode, context opaque is ngx_cycle_t*.
     * In worker mode, context opaque is ngx_js_worker_t* — use ngx_cycle.
     */
    if (ngx_process == NGX_PROCESS_MASTER
        || ngx_process == NGX_PROCESS_SINGLE)
    {
        cycle = JS_GetContextOpaque(ctx);
        if (cycle == NULL) {
            return JS_ThrowInternalError(ctx, "nginx.use: no cycle context");
        }
    } else {
        cycle = (ngx_cycle_t *) ngx_cycle;
        if (cycle == NULL) {
            return JS_ThrowInternalError(ctx, "nginx.use: ngx_cycle is NULL");
        }
    }

    path_str = JS_ToCString(ctx, argv[0]);
    if (!path_str) {
        return JS_EXCEPTION;
    }

    /*
     * P18: detect registry references ("vendor/plugin[@version]") and
     * resolve them to the local cache directory before falling through
     * to the normal filesystem load path.
     *
     * Filesystem paths (absolute, ./, ../) are resolved as before.
     */
    if (ngx_js_is_registry_ref(path_str)) {
        if (ngx_js_resolve_registry_pkg(cycle, path_str,
                                        dir_buf, sizeof(dir_buf))
            != NGX_OK)
        {
            JS_FreeCString(ctx, path_str);
            return JS_EXCEPTION;
        }

    } else {
        /* resolve path relative to cycle->prefix if not absolute */
        u_char *p;
        if (path_str[0] == '/') {
            p = ngx_snprintf((u_char *) dir_buf, sizeof(dir_buf) - 1,
                             "%s", path_str);
        } else {
            p = ngx_snprintf((u_char *) dir_buf, sizeof(dir_buf) - 1,
                             "%V%s", &cycle->prefix, path_str);
        }
        *p = '\0';
    }
    JS_FreeCString(ctx, path_str);

    /*
     * Serialize config to JSON string so it can be transmitted over the
     * channel payload (P16 worker broadcast) or used directly (master).
     */
    config_json      = NULL;
    config_json_val  = JS_UNDEFINED;

    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        config_json_val = JS_JSONStringify(ctx, argv[1],
                                           JS_UNDEFINED, JS_UNDEFINED);
        if (!JS_IsException(config_json_val)) {
            config_json = JS_ToCString(ctx, config_json_val);
        }
    }

    /* Load the plugin in this process's runtime */
    rt = JS_GetRuntime(ctx);
    if (ngx_js_load_plugin(ctx, rt, cycle, dir_buf, config_json) != NGX_OK) {
        JS_FreeCString(ctx, config_json);
        JS_FreeValue(ctx, config_json_val);
        return JS_EXCEPTION;
    }

    /* P19: record resolved plugin path in nginx.plugins */
    {
        JSValue  global_p, nginx_p, plugins_arr, push_fn, path_val, res;

        global_p    = JS_GetGlobalObject(ctx);
        nginx_p     = JS_GetPropertyStr(ctx, global_p, "nginx");
        plugins_arr = JS_GetPropertyStr(ctx, nginx_p, "plugins");
        push_fn     = JS_GetPropertyStr(ctx, plugins_arr, "push");
        path_val    = JS_NewString(ctx, dir_buf);

        if (JS_IsFunction(ctx, push_fn)) {
            res = JS_Call(ctx, push_fn, plugins_arr, 1,
                          (JSValueConst *) &path_val);
            JS_FreeValue(ctx, res);
        }

        JS_FreeValue(ctx, path_val);
        JS_FreeValue(ctx, push_fn);
        JS_FreeValue(ctx, plugins_arr);
        JS_FreeValue(ctx, nginx_p);
        JS_FreeValue(ctx, global_p);
    }

    /*
     * P16: if we are a worker, broadcast the plugin load to all other
     * workers via the master.
     *
     * Payload format: dir_buf\0config_json\0
     * Sent as NGX_CMD_JS_USE_PLUGIN to master (via ngx_channel = channel[1]).
     * Master handles it in ngx_js_handle_master_channel_msgs and forwards as
     * NGX_CMD_JS_LOAD_PLUGIN to every OTHER worker.
     */
    if (ngx_process == NGX_PROCESS_WORKER && ngx_channel >= 0) {
        path_payload_len = strlen(dir_buf) + 1;          /* include NUL */
        cfg_payload_len  = config_json
                           ? strlen(config_json) + 1     /* include NUL */
                           : 1;                          /* just "\0" */
        payload_len = path_payload_len + cfg_payload_len;

        if (payload_len <= sizeof(payload)) {
            ngx_memcpy(payload, dir_buf, path_payload_len);
            if (config_json) {
                ngx_memcpy(payload + path_payload_len,
                           config_json, cfg_payload_len);
            } else {
                payload[path_payload_len] = '\0';
            }

            ngx_js_channel_send(ngx_channel,
                                NGX_CMD_JS_USE_PLUGIN,
                                (ngx_uint_t) ngx_worker,
                                payload, payload_len,
                                cycle->log);
        } else {
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "js: nginx.use: plugin path+config too large"
                          " for broadcast (%uz > %uz)",
                          payload_len, sizeof(payload));
        }
    }

    JS_FreeCString(ctx, config_json);
    JS_FreeValue(ctx, config_json_val);

    return JS_DupValue(ctx, this_val);  /* return nginx for chaining */
}


/*
 * nginx.install(plugin[, config]) — JS-Pilgrim P7
 *
 * Invoke an inline (in-process) plugin.
 *
 * plugin — a function OR an object with an .install method.
 *          Called with (config || {}) as the sole argument.
 * config — optional config value.  Defaults to {}.
 *
 * Returns the result of the plugin call.
 */
static JSValue
ngx_js_install(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  plugin, install_fn, config, args[1], ret;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "nginx.install(plugin[, config]): plugin required");
    }

    plugin = argv[0];
    config = (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
             ? JS_DupValue(ctx, argv[1])
             : JS_NewObject(ctx);
    args[0] = config;

    if (JS_IsFunction(ctx, plugin)) {
        ret = JS_Call(ctx, plugin, JS_UNDEFINED, 1, args);

    } else if (JS_IsObject(plugin)) {
        install_fn = JS_GetPropertyStr(ctx, plugin, "install");
        if (!JS_IsFunction(ctx, install_fn)) {
            JS_FreeValue(ctx, install_fn);
            JS_FreeValue(ctx, config);
            return JS_ThrowTypeError(ctx,
                "nginx.install: plugin must be a function or have an "
                ".install method");
        }
        ret = JS_Call(ctx, install_fn, plugin, 1, args);
        JS_FreeValue(ctx, install_fn);

    } else {
        JS_FreeValue(ctx, config);
        return JS_ThrowTypeError(ctx,
            "nginx.install: plugin must be a function or object");
    }

    JS_FreeValue(ctx, config);
    return ret;
}


/* ------------------------------------------------------------------ */
/* P11 — nginx.shared: cross-worker shared key/value store              */
/* ------------------------------------------------------------------ */

static ngx_js_shared_hdr_t *
ngx_js_shared_get_hdr(JSContext *ctx)
{
    ngx_js_worker_t  *w;
    ngx_js_conf_t    *jcf;

    w = JS_GetContextOpaque(ctx);

    if (w == NULL || ngx_cycle->conf_ctx == NULL) {
        JS_ThrowInternalError(ctx, "nginx.shared not available at config time");
        return NULL;
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);

    if (jcf->shared_zone == NULL || jcf->shared_zone->data == NULL) {
        JS_ThrowInternalError(ctx, "nginx.shared zone not initialised");
        return NULL;
    }

    return (ngx_js_shared_hdr_t *) jcf->shared_zone->data;
}


/* ------------------------------------------------------------------ */
/* HOST-PERF: the shared store is a hash table, not a list to be scanned */
/* ------------------------------------------------------------------ */

#define NGX_JS_SHARED_NOSLOT  ((ngx_uint_t) -1)


static uint32_t
ngx_js_shared_hash(const char *key)
{
    uint32_t  h = 2166136261u;              /* FNV-1a, 32-bit */

    while (*key) {
        h ^= (uint32_t) (u_char) *key++;
        h *= 16777619u;
    }

    return h;
}


/*
 * Remove the entry at slot `i` by BACKWARD SHIFT (Knuth 6.4 alg. R), not by
 * leaving a tombstone.
 *
 * Tombstones would have been less code, but they accumulate: a rate limiter
 * that churns keys would fill the table with dead markers and start reporting
 * "store full" while holding almost nothing, and recovering from that needs a
 * compaction pass with a copy of the table -- 166 KB memcpy'd under the
 * spinlock every worker shares. Shifting the cluster back keeps the table
 * dense and self-maintaining at the cost of a bounded memmove here, on the
 * cold path (delete / expiry), rather than on the hot one (incr).
 *
 * The invariant being preserved: every entry must remain reachable by probing
 * forward from its OWN home slot. An entry may only move back into the hole if
 * its home does not lie cyclically inside (hole, entry] -- moving it past its
 * own home would put it where a probe starting there never looks.
 */
static void
ngx_js_shared_remove(ngx_js_shared_hdr_t *hdr, ngx_js_shared_entry_t *entries,
    ngx_uint_t i)
{
    ngx_uint_t  j, k, cap;

    cap = hdr->capacity;
    hdr->count--;

    for ( ;; ) {
        ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));

        j = i;

        for ( ;; ) {
            j = (j + 1) % cap;

            if (!entries[j].used) {
                return;              /* cluster ended: nothing left to shift */
            }

            k = (ngx_uint_t) (entries[j].hash % cap);

            if (i <= j) {
                if (!(i < k && k <= j)) {
                    break;
                }
            } else {
                if (!(i < k || k <= j)) {
                    break;
                }
            }
        }

        entries[i] = entries[j];
        i = j;
    }
}


/*
 * Probe for `key`. Returns its slot, or NGX_JS_SHARED_NOSLOT when absent.
 *
 * `*stop` receives the slot the probe stopped on — the hole at the end of the
 * cluster, which is exactly where an insert of this key belongs — or
 * NGX_JS_SHARED_NOSLOT when the table is full.
 *
 * Expiry is reclaimed HERE, when the key is probed, which is what the old full
 * scan did incidentally for every slot it walked past. After a removal the
 * probe RESTARTS: backward shift may have moved a live entry into the slot we
 * just cleared, so a `stop` remembered from before the removal could point at
 * an occupied slot and an insert would overwrite a live key. Each restart
 * removes one entry, so it terminates.
 */
static ngx_uint_t
ngx_js_shared_find(ngx_js_shared_hdr_t *hdr, ngx_js_shared_entry_t *entries,
    const char *key, uint32_t hash, time_t now, ngx_uint_t *stop)
{
    ngx_uint_t  i, n, cap;

    cap = hdr->capacity;

    if (stop != NULL) {
        *stop = NGX_JS_SHARED_NOSLOT;
    }

    if (cap == 0) {
        return NGX_JS_SHARED_NOSLOT;
    }

restart:

    i = (ngx_uint_t) (hash % cap);

    for (n = 0; n < cap; n++) {

        if (!entries[i].used) {
            if (stop != NULL) {
                *stop = i;
            }
            return NGX_JS_SHARED_NOSLOT;
        }

        if (entries[i].hash == hash
            && ngx_strcmp(entries[i].key, key) == 0)
        {
            if (entries[i].expires != 0 && now >= entries[i].expires) {
                ngx_js_shared_remove(hdr, entries, i);
                goto restart;
            }

            return i;
        }

        i = (i + 1) % cap;
    }

    return NGX_JS_SHARED_NOSLOT;             /* full, and the key is not here */
}


static void
ngx_js_shared_insert(ngx_js_shared_entry_t *e, const char *key, uint32_t hash,
    const char *val, time_t expires)
{
    e->used = 1;
    e->hash = hash;
    e->expires = expires;
    ngx_cpystrn((u_char *) e->key, (u_char *) key, NGX_JS_SHARED_KEY_LEN);
    ngx_cpystrn((u_char *) e->val, (u_char *) val, NGX_JS_SHARED_VAL_LEN);
}


/*
 * COMCON M-LIB: charge one use against a named budget (the `uses` mediation).
 *
 * A FIXED WINDOW, and that is a semantic choice worth stating rather than
 * discovering: the counter is created on the first use with an expiry `window`
 * seconds out, and every use until then charges against it. At the boundary a
 * caller can therefore spend `limit` at the end of one window and `limit` again
 * at the start of the next -- up to 2*limit across a straddling interval. A
 * sliding window costs per-use timestamps in shared memory; the honest fix is to
 * say which one this is, not to imply the other.
 *
 * The counter lives in nginx.shared, so it is FLEET-WIDE. A per-worker budget
 * would be `limit` times the number of workers, which is not the number the
 * operator wrote down -- the same class of defect as the per-process mode switch
 * (v5.56). Cost: one hash probe under the store's spinlock per charged use,
 * ~0.06 µs since HOST-PERF (v5.63); it was ~0.42 µs on a miss before that, which
 * is the kind of per-request cost that decides whether a feature is affordable.
 *
 * Returns NGX_OK (within budget), NGX_DECLINED (exhausted), NGX_ERROR (no store).
 */
ngx_int_t
ngx_js_shared_budget_charge(JSContext *ctx, const char *key, uint32_t limit,
    uint32_t window)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    ngx_uint_t              i, slot;
    uint32_t                hash;
    int64_t                 cur;
    time_t                  now;
    u_char                  buf[32];
    ngx_int_t               rc;

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return NGX_ERROR;
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);
    hash = ngx_js_shared_hash(key);
    now = ngx_time();

    ngx_spinlock(&hdr->lock, 1, 2048);

    i = ngx_js_shared_find(hdr, entries, key, hash, now, &slot);

    if (i != NGX_JS_SHARED_NOSLOT) {
        cur = ngx_atoi((u_char *) entries[i].val,
                       ngx_strlen(entries[i].val));
        if (cur == NGX_ERROR) {
            cur = 0;
        }

        /*
         * CHARGE FIRST, THEN COMPARE. Counting the refused attempt too is
         * deliberate: the audit wants to see how far over a tenant ran, and a
         * counter that stops at the limit cannot tell "just reached it" from
         * "hammering it a million times".
         */
        cur++;
        ngx_snprintf((u_char *) entries[i].val, NGX_JS_SHARED_VAL_LEN - 1,
                     "%L", cur);
        entries[i].val[NGX_JS_SHARED_VAL_LEN - 1] = '\0';

        rc = (cur <= (int64_t) limit) ? NGX_OK : NGX_DECLINED;

    } else if (slot == NGX_JS_SHARED_NOSLOT) {
        /*
         * The store is full. FAIL CLOSED: a budget that cannot be counted is a
         * budget that is not enforced, and the safe reading of "I cannot tell"
         * is "no".
         */
        rc = NGX_DECLINED;

    } else {
        ngx_snprintf(buf, sizeof(buf) - 1, "1%Z");
        ngx_js_shared_insert(&entries[slot], key, hash, (char *) buf,
                             now + (time_t) window);
        hdr->count++;
        rc = (limit >= 1) ? NGX_OK : NGX_DECLINED;
    }

    ngx_unlock(&hdr->lock);

    return rc;
}


static JSValue
ngx_js_shared_fn_get(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *key;
    ngx_uint_t              i;
    time_t                  now;
    JSValue                 result;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "shared.get(key): key must be a string");
    }

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_ToCString(ctx, argv[0]);
    if (key == NULL) {
        return JS_EXCEPTION;
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    now = ngx_time();

    ngx_spinlock(&hdr->lock, 1, 2048);

    /* find() reclaims the entry if it has expired, and reports it absent */
    i = ngx_js_shared_find(hdr, entries, key, ngx_js_shared_hash(key), now,
                           NULL);

    result = (i == NGX_JS_SHARED_NOSLOT)
             ? JS_UNDEFINED
             : JS_NewString(ctx, entries[i].val);

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, key);

    return result;
}


static JSValue
ngx_js_shared_fn_set(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *key, *val;
    ngx_uint_t              i, free_slot;
    int64_t                 ttl;
    time_t                  now, expires;
    uint32_t                hash;

    if (argc < 2 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "shared.set(key, val): key must be a string");
    }

    /* optional 3rd arg: ttl in seconds (0 / omitted / <=0 = never expires) */
    ttl = 0;
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        if (JS_ToInt64(ctx, &ttl, argv[2]) < 0) {
            return JS_EXCEPTION;
        }
    }

    now     = ngx_time();
    expires = (ttl > 0) ? now + (time_t) ttl : 0;

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_ToCString(ctx, argv[0]);
    if (key == NULL) {
        return JS_EXCEPTION;
    }

    val = JS_ToCString(ctx, argv[1]);
    if (val == NULL) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    if (ngx_strlen(key) >= NGX_JS_SHARED_KEY_LEN) {
        JS_FreeCString(ctx, val);
        JS_FreeCString(ctx, key);
        return JS_ThrowRangeError(ctx, "shared.set: key too long (max %d)",
                                  NGX_JS_SHARED_KEY_LEN - 1);
    }

    if (ngx_strlen(val) >= NGX_JS_SHARED_VAL_LEN) {
        JS_FreeCString(ctx, val);
        JS_FreeCString(ctx, key);
        return JS_ThrowRangeError(ctx, "shared.set: value too long (max %d)",
                                  NGX_JS_SHARED_VAL_LEN - 1);
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    hash = ngx_js_shared_hash(key);

    ngx_spinlock(&hdr->lock, 1, 2048);

    i = ngx_js_shared_find(hdr, entries, key, hash, now, &free_slot);

    if (i != NGX_JS_SHARED_NOSLOT) {
        ngx_cpystrn((u_char *) entries[i].val, (u_char *) val,
                    NGX_JS_SHARED_VAL_LEN);
        entries[i].expires = expires;

    } else {
        if (free_slot == NGX_JS_SHARED_NOSLOT) {
            ngx_unlock(&hdr->lock);
            JS_FreeCString(ctx, val);
            JS_FreeCString(ctx, key);
            return JS_ThrowInternalError(ctx, "nginx.shared: store full");
        }

        ngx_js_shared_insert(&entries[free_slot], key, hash, val, expires);
        hdr->count++;
    }

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, val);
    JS_FreeCString(ctx, key);

    return JS_UNDEFINED;
}


static JSValue
ngx_js_shared_fn_delete(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *key;
    ngx_uint_t              i;
    int                     deleted;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "shared.delete(key): key must be a string");
    }

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_ToCString(ctx, argv[0]);
    if (key == NULL) {
        return JS_EXCEPTION;
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    ngx_spinlock(&hdr->lock, 1, 2048);

    /*
     * ngx_time(), not 0: an expired entry is already absent, so deleting it
     * must report false rather than true. find() reclaims it either way.
     */
    i = ngx_js_shared_find(hdr, entries, key, ngx_js_shared_hash(key),
                           ngx_time(), NULL);

    deleted = 0;

    if (i != NGX_JS_SHARED_NOSLOT) {
        ngx_js_shared_remove(hdr, entries, i);
        deleted = 1;
    }

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, key);

    return JS_NewBool(ctx, deleted);
}


static JSValue
ngx_js_shared_fn_keys(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    JSValue                 arr;
    ngx_uint_t              i, idx;
    time_t                  now;

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    arr = JS_NewArray(ctx);
    idx = 0;

    now = ngx_time();

    ngx_spinlock(&hdr->lock, 1, 2048);

    /*
     * Enumeration stays a full scan -- keys() is not a hot path, and the table
     * has no order to walk. It is done in TWO phases because removal now
     * shifts a cluster BACKWARD: dropping an expired entry mid-scan can move an
     * entry the cursor has already passed, and it would be missed from the very
     * list this call exists to produce. So: report first, reclaim after.
     */
    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used
            && !(entries[i].expires != 0 && now >= entries[i].expires))
        {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, entries[i].key));
        }
    }

    for ( ;; ) {
        for (i = 0; i < hdr->capacity; i++) {
            if (entries[i].used
                && entries[i].expires != 0 && now >= entries[i].expires)
            {
                ngx_js_shared_remove(hdr, entries, i);
                break;
            }
        }

        if (i == hdr->capacity) {
            break;
        }
    }

    ngx_unlock(&hdr->lock);

    return arr;
}


/*
 * nginx.__benchStub(mode, arg) -> number   [M5 EVIDENCE INSTRUMENT, not an API]
 *
 * Decomposes what a host call costs, so the M5 question -- "is the payoff in the
 * typed stub ABI?" -- can be answered with numbers instead of a thesis.
 * `shared.incr` is the host call the candidate policies make, and it currently
 * does THREE things per call: a JS->C dispatch with boxed args, a
 * JS_ToCString() of the key, and a LINEAR SCAN of up to 256 slots comparing
 * 128-byte keys under a spinlock. A "typed stub" removes the first two. Only
 * measuring separates them:
 *
 *   mode 0  return immediately            -- the JS->C call floor
 *   mode 1  JS_ToCString(arg) and free    -- adds string marshalling
 *   mode 2  increment slot `arg` directly -- a typed stub: int in, no scan
 *   mode 3  as 2 without the spinlock     -- what the lock costs
 *
 * Against the real `shared.incr(key, 1)` these give the whole breakdown. It is
 * deliberately double-underscored and documented as an instrument: it writes to
 * a slot by index with no key, which is not a thing any policy should be able to
 * do. See t/tools/policy-compute-split.t and ROADMAP's M5 evidence block.
 */
static JSValue
ngx_js_bench_stub(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *s;
    int32_t                 mode = 0;
    int64_t                 slot = 0;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "__benchStub(mode[, arg])");
    }
    JS_ToInt32(ctx, &mode, argv[0]);

    if (mode == 0) {
        return JS_NewInt64(ctx, 0);
    }

    if (mode == 1) {
        if (argc < 2) {
            return JS_ThrowTypeError(ctx, "__benchStub(1, str)");
        }
        s = JS_ToCString(ctx, argv[1]);
        if (s == NULL) {
            return JS_EXCEPTION;
        }
        JS_FreeCString(ctx, s);
        return JS_NewInt64(ctx, 0);
    }

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }
    entries = (ngx_js_shared_entry_t *) (hdr + 1);

    if (argc >= 2) {
        JS_ToInt64(ctx, &slot, argv[1]);
    }
    if (slot < 0 || (ngx_uint_t) slot >= hdr->capacity) {
        return JS_ThrowRangeError(ctx, "__benchStub: slot out of range");
    }

    /* The typed-stub shape: the key was resolved to a slot at bind time, so the
     * per-call work is an add. This is what M1's hand-written C did with a slab
     * atomic -- no key, no scan, no lock. */
    if (mode == 3) {
        int64_t  v;
        ngx_memcpy(&v, entries[slot].val, sizeof(int64_t));
        v += 1;
        ngx_memcpy(entries[slot].val, &v, sizeof(int64_t));
        return JS_NewInt64(ctx, v);
    }

    {
        int64_t  v;
        ngx_spinlock(&hdr->lock, 1, 2048);
        ngx_memcpy(&v, entries[slot].val, sizeof(int64_t));
        v += 1;
        ngx_memcpy(entries[slot].val, &v, sizeof(int64_t));
        ngx_unlock(&hdr->lock);
        return JS_NewInt64(ctx, v);
    }
}


static JSValue
ngx_js_shared_fn_incr(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *key;
    ngx_uint_t              i, free_slot;
    int64_t                 delta, cur;
    time_t                  now;
    char                    buf[32];
    uint32_t                hash;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "shared.incr(key[, delta]): key must be a string");
    }

    delta = 1;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &delta, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
    }

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_ToCString(ctx, argv[0]);
    if (key == NULL) {
        return JS_EXCEPTION;
    }

    if (ngx_strlen(key) >= NGX_JS_SHARED_KEY_LEN) {
        JS_FreeCString(ctx, key);
        return JS_ThrowRangeError(ctx, "shared.incr: key too long (max %d)",
                                  NGX_JS_SHARED_KEY_LEN - 1);
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    now = ngx_time();

    hash = ngx_js_shared_hash(key);

    ngx_spinlock(&hdr->lock, 1, 2048);

    /* an expired counter is reclaimed by find() and restarts from zero */
    i = ngx_js_shared_find(hdr, entries, key, hash, now, &free_slot);

    if (i != NGX_JS_SHARED_NOSLOT) {
        cur = ngx_atoi((u_char *) entries[i].val,
                       ngx_strlen(entries[i].val));
        if (cur == NGX_ERROR) {
            cur = 0;
        }
        cur += delta;
        ngx_snprintf((u_char *) entries[i].val,
                     NGX_JS_SHARED_VAL_LEN - 1, "%l", cur);
        entries[i].val[NGX_JS_SHARED_VAL_LEN - 1] = '\0';

    } else {
        if (free_slot == NGX_JS_SHARED_NOSLOT) {
            ngx_unlock(&hdr->lock);
            JS_FreeCString(ctx, key);
            return JS_ThrowInternalError(ctx, "nginx.shared: store full");
        }

        cur = delta;
        ngx_snprintf((u_char *) buf, sizeof(buf) - 1, "%l%Z", cur);

        /* a fresh counter never expires */
        ngx_js_shared_insert(&entries[free_slot], key, hash, buf, 0);
        hdr->count++;
    }

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, key);

    return JS_NewInt64(ctx, cur);
}


/*
 * shared.ttl(key) — remaining lifetime of a key, in whole seconds.
 *   null  — key absent (or already expired)
 *   -1    — key present but has no expiry (permanent)
 *   >= 0  — seconds until the key expires
 * (Redis-style semantics.)
 */
static JSValue
ngx_js_shared_fn_ttl(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_shared_hdr_t    *hdr;
    ngx_js_shared_entry_t  *entries;
    const char             *key;
    ngx_uint_t              i;
    time_t                  now;
    JSValue                 result;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "shared.ttl(key): key must be a string");
    }

    hdr = ngx_js_shared_get_hdr(ctx);
    if (hdr == NULL) {
        return JS_EXCEPTION;
    }

    key = JS_ToCString(ctx, argv[0]);
    if (key == NULL) {
        return JS_EXCEPTION;
    }

    entries = (ngx_js_shared_entry_t *)(hdr + 1);

    now = ngx_time();

    ngx_spinlock(&hdr->lock, 1, 2048);

    /* an expired key is ABSENT: find() reclaims it and reports NOSLOT */
    i = ngx_js_shared_find(hdr, entries, key, ngx_js_shared_hash(key), now,
                           NULL);

    if (i == NGX_JS_SHARED_NOSLOT) {
        result = JS_NULL;

    } else if (entries[i].expires == 0) {
        result = JS_NewInt64(ctx, -1);                  /* permanent */

    } else {
        result = JS_NewInt64(ctx, (int64_t) (entries[i].expires - now));
    }

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, key);

    return result;
}


/*
 * COMCON A2.1 (reduced): nginx.grantToTenant(name) — DECLARE that `name` is
 * granted, for the wanted-vs-granted delta in nginx.tenantLearning().
 *
 * IT DOES NOT GRANT ANYTHING, and the name is a misnomer kept for
 * compatibility.  It once published a socket into the tenant compartment, which
 * the tenant eval re-wrapped under `name`; the M-CFG convergence removed that
 * compartment and nothing replaced the read.  What was left validated a socket
 * argument, stored its handle, and never looked at it again — so a caller was
 * told a capability had been conferred when none had, and the harvest report
 * then listed the name as granted.  An API that reports success for work it no
 * longer does is worse than one that was deleted.
 *
 * To actually confer a capability use `comcon.grant(env, name, cap)` and
 * `comcon.include(source, {grants})`, which is what the confined path consumes.
 *
 * A second argument is still ACCEPTED AND IGNORED so existing host JS keeps
 * working.  It is deliberately not validated: checking a value that is then
 * discarded implies it is used.
 */
static JSValue
ngx_js_grant_to_tenant(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_str_t               name;
    ngx_cycle_t            *cycle;
    ngx_js_conf_t          *jcf;
    ngx_js_tenant_grant_t  *grant;
    const char             *s;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "nginx.grantToTenant(name): a name string is required");
    }

    cycle = JS_GetContextOpaque(ctx);
    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.grantToTenant: no cycle");
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.grantToTenant: no jcf");
    }

    s = JS_ToCString(ctx, argv[0]);
    if (s == NULL) {
        return JS_EXCEPTION;
    }

    name.len = ngx_strlen(s);
    name.data = ngx_pnalloc(cycle->pool, name.len + 1);   /* NUL for prop name */
    if (name.data == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowInternalError(ctx, "nginx.grantToTenant: alloc failed");
    }
    ngx_memcpy(name.data, s, name.len);
    name.data[name.len] = '\0';
    JS_FreeCString(ctx, s);

    grant = ngx_array_push(&jcf->tenant_grants);
    if (grant == NULL) {
        return JS_ThrowInternalError(ctx, "nginx.grantToTenant: alloc failed");
    }

    grant->name = name;

    return JS_UNDEFINED;
}


/*
 * COMCON A4: nginx.tenantDenials() — the host-side denial report that closes
 * the audit→enforce loop: {mode, total, byOp:{op: n}}. Counters are exact
 * regardless of the TM-1 log-record quota/sampling. Per-process (call it from
 * the worker that served the traffic).
 */
static JSValue
ngx_js_tenant_denials(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue                obj, by;
    ngx_uint_t             i;
    ngx_js_denial_code_t   code;

    obj = JS_NewObject(ctx);
    by  = JS_NewObject(ctx);

    for (i = 0; i < NGX_JS_DENIAL_LAST; i++) {
        code = (ngx_js_denial_code_t) i;
        JS_SetPropertyStr(ctx, by, ngx_js_denial_code_name(code),
                          JS_NewInt64(ctx,
                              (int64_t) ngx_js_compartment_denial_count(code)));
    }

    JS_SetPropertyStr(ctx, obj, "mode",
                      JS_NewString(ctx, ngx_js_tenant_mode_name()));
    JS_SetPropertyStr(ctx, obj, "total",
                      JS_NewInt64(ctx,
                          (int64_t) ngx_js_compartment_denial_total()));
    JS_SetPropertyStr(ctx, obj, "byOp", by);

    return obj;
}


/*
 * COMCON B0: nginx.tenantLearning() — the onboarding harvest. In learn mode
 * the tenant's references into the withheld host surface are recorded as
 * access paths; this host-only report returns {mode, wants:[{path, hits}]} —
 * the exact wishlist the operator grants the safe subset of, then enforces.
 */
static JSValue
ngx_js_tenant_learning(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue                 obj, arr, grants, e;
    ngx_uint_t              i, n;
    ngx_cycle_t            *cycle;
    ngx_js_conf_t          *jcf;
    ngx_js_tenant_grant_t  *g;

    obj = JS_NewObject(ctx);
    arr = JS_NewArray(ctx);

    n = ngx_js_learn_count();

    for (i = 0; i < n; i++) {
        e = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, e, "path",
                          JS_NewString(ctx, ngx_js_learn_path(i)));
        JS_SetPropertyStr(ctx, e, "hits",
                          JS_NewInt64(ctx, (int64_t) ngx_js_learn_hits(i)));
        JS_SetPropertyUint32(ctx, arr, i, e);
    }

    /* The names already granted — the "current environment" side of the
     * contract, so a generator can emit the wanted-vs-granted delta.
     * NB: use ngx_cycle, not JS_GetContextOpaque(ctx) — in a worker request
     * handler the context opaque is the ngx_js_worker_t, not the cycle. */
    grants = JS_NewArray(ctx);
    cycle = (ngx_cycle_t *) ngx_cycle;

    if (cycle != NULL) {
        jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

        if (jcf != NULL) {
            g = jcf->tenant_grants.elts;

            for (i = 0; i < jcf->tenant_grants.nelts; i++) {
                JS_SetPropertyUint32(ctx, grants, i,
                    JS_NewStringLen(ctx, (const char *) g[i].name.data,
                                    g[i].name.len));
            }
        }
    }

    JS_SetPropertyStr(ctx, obj, "mode",
                      JS_NewString(ctx, ngx_js_tenant_mode_name()));
    JS_SetPropertyStr(ctx, obj, "wants", arr);
    JS_SetPropertyStr(ctx, obj, "grants", grants);

    return obj;
}


/*
 * COMCON M-CFG step 2: comcon.admit(fn, contract) — the admission GATE.
 *
 * The host-JS realization of FOUNDATION §4 `admit`: the C3 static gate over a
 * compiled fragment function, returning { certified, reject? }. This is the
 * gate only — no compartment creation / bind / lowering (those are bind /
 * include, later slices). Interim (pre-M2) schema per OPERATOR_API §8.6: the
 * existing C3 structural checks — no dynamic code, and every free-global name
 * must be in the contract's `imports` manifest (eval/Function/globalThis/…
 * always denied). Static: no fragment code runs (js_comcon_* analyse bytecode).
 */

typedef struct {
    JSContext    *ctx;
    JSValueConst  imports;      /* array of allowed free-name strings, or undefined */
    uint32_t      imports_len;
    /*
     * The contract's INTRINSICS NARROWING (absent => the full allowance).
     * `intr_present` distinguishes "not asked for" from "asked for nothing":
     * {intrinsics: []} is the strictest contract expressible -- no free names at
     * all, not even language values -- and it is the setting that {imports: []}
     * used to mean before the allowance existed.
     */
    JSValueConst  intrinsics;
    uint32_t      intr_len;
    ngx_uint_t    intr_present;
    ngx_uint_t    bad;
    char          badname[128];
} ngx_js_admit_check_t;


static ngx_uint_t
ngx_js_admit_name_denied(const char *name)
{
    return ngx_strcmp(name, "eval") == 0
        || ngx_strcmp(name, "Function") == 0
        || ngx_strcmp(name, "globalThis") == 0
        || ngx_strcmp(name, "global") == 0
        || ngx_strcmp(name, "self") == 0;
}


/*
 * C3 free-name categories, and the reason there are three rather than two.
 *
 *   DENIED       eval / Function / globalThis / global / self. Ambient-authority
 *                reach; no manifest re-admits them.
 *   INTRINSIC    the list below: language values a fragment computes WITH, not
 *                authority it acts through. Allowed WITHOUT declaration.
 *   DECLARABLE   everything else, including every host name: must appear in
 *                `imports`, which is what makes the manifest meaningful.
 *
 * Before this list existed there were only two categories, so `undefined`,
 * `JSON` and `Object` were "ungranted host names" and an ordinary
 * `x !== undefined` was refused unless the operator declared `undefined` -- a
 * name nothing is granted for. Worse, the gate refused to let a fragment DECLARE
 * intrinsics that the compartment PROVIDES, and that a fragment with admission
 * OFF used freely: the gate was stricter than the boundary it guards. Found by
 * the V3 kernel oracle (t/comcon_v3_oracle.t) disagreeing with the engine, and
 * decided deliberately rather than inherited -- widening an admission gate is a
 * decision (user, 2026-09-12).
 *
 * WHAT IS DELIBERATELY *NOT* HERE, each for its own reason, and each still
 * usable by DECLARING it:
 *
 *   Date, Math    clock and RNG -- the side channels M-SES left open
 *                 ("clock/RNG doubles during the run" is still an open item).
 *                 A fragment that wants them should say so.
 *   Promise       scheduling: continuations that outlive the invocation the
 *                 deadline is measured against.
 *   Symbol        Symbol.for is a RUNTIME-WIDE registry -- a channel between
 *                 fragments, not a value.
 *   Proxy,        object-graph tampering over values a fragment holds,
 *   Reflect       including granted capabilities.
 *   ArrayBuffer,  SharedArrayBuffer is a communication channel; the buffer
 *   typed arrays  family travels with it under one rule rather than a
 *                 case-by-case one.
 *
 * The cost of omitting something is one word in a manifest. The cost of
 * wrongly including one is a silently wider gate, so the list is short on
 * purpose and grows only by decision.
 *
 * KEEP IN SYNC with the same list in t/tools/kernel-oracle.js -- two copies of a
 * closed enumeration is exactly the drift V7 exists to catch, and
 * t/tools/check-enumerations.py compares them on every test run.
 */
static const char *ngx_js_admit_intrinsics[] = {
    "undefined", "NaN", "Infinity",
    "Object", "Array", "String", "Number", "Boolean", "BigInt", "JSON",
    "RegExp", "Map", "Set", "WeakMap", "WeakSet",
    "Error", "TypeError", "RangeError", "SyntaxError", "ReferenceError",
    "EvalError", "URIError",
    "parseInt", "parseFloat", "isNaN", "isFinite",
    "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
    NULL
};


static ngx_uint_t
ngx_js_admit_name_intrinsic(const char *name)
{
    const char **p;

    for (p = ngx_js_admit_intrinsics; *p != NULL; p++) {
        if (ngx_strcmp(name, *p) == 0) {
            return 1;
        }
    }

    return 0;
}


static ngx_uint_t
ngx_js_admit_in_list(JSContext *ctx, JSValueConst arr, uint32_t len,
    const char *name)
{
    uint32_t     i;
    JSValue      v;
    const char  *s;
    ngx_uint_t   match = 0;

    for (i = 0; i < len && !match; i++) {
        v = JS_GetPropertyUint32(ctx, arr, i);
        s = JS_ToCString(ctx, v);
        if (s != NULL && ngx_strcmp(s, name) == 0) {
            match = 1;
        }
        if (s != NULL) {
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, v);
    }

    return match;
}


/*
 * The EFFECTIVE allowance for this contract: the static list MEET the contract's
 * narrowing, which is why `intrinsics` can only ever attenuate.
 *
 * `imports` DECLARES (it can add any name, including Date and Math);
 * `intrinsics` NARROWS (it can only remove from the list above, and naming
 * something outside that list is refused rather than quietly ignored -- see the
 * validation in ngx_js_comcon_admit_check). Two knobs pointing in one direction
 * each, so the monotonicity story survives: nothing here widens authority.
 */
static ngx_uint_t
ngx_js_admit_name_allowed_intrinsic(ngx_js_admit_check_t *c, const char *name)
{
    if (!ngx_js_admit_name_intrinsic(name)) {
        return 0;
    }

    if (!c->intr_present) {
        return 1;                          /* no narrowing asked for */
    }

    return ngx_js_admit_in_list(c->ctx, c->intrinsics, c->intr_len, name);
}


static ngx_uint_t
ngx_js_admit_in_imports(ngx_js_admit_check_t *c, const char *name)
{
    return ngx_js_admit_in_list(c->ctx, c->imports, c->imports_len, name);
}


static void
ngx_js_admit_free_cb(void *ud, const char *name)
{
    ngx_js_admit_check_t  *c = ud;

    if (c->bad) {
        return;
    }

    /* denied wins over intrinsic: no manifest, and no category, re-admits the
       ambient-authority names. */
    if (ngx_js_admit_name_denied(name)) {
        c->bad = 1;
        ngx_cpystrn((u_char *) c->badname, (u_char *) name, sizeof(c->badname));
        return;
    }

    if (ngx_js_admit_name_allowed_intrinsic(c, name)) {
        return;                    /* a value to compute with, not authority */
    }

    if (!ngx_js_admit_in_imports(c, name)) {
        c->bad = 1;
        ngx_cpystrn((u_char *) c->badname, (u_char *) name, sizeof(c->badname));
    }
}


static JSValue
ngx_js_admit_verdict(JSContext *ctx, ngx_uint_t certified, const char *reject,
    ngx_js_refusal_code_t code)
{
    JSValue  o;

    o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "certified", JS_NewBool(ctx, certified ? 1 : 0));
    if (!certified && reject != NULL) {
        /*
         * `code` before `reject`: the verdict is read by machines first
         * ([TBD-2] — MANUAL §3.2 tells tenants to pin CI to the code) and by
         * a person second. Both are always present on a refusal; a verdict
         * with prose and no code is the thing this enum was added to end.
         */
        JS_SetPropertyStr(ctx, o, "code",
                          JS_NewString(ctx, ngx_js_refusal_name(code)));
        JS_SetPropertyStr(ctx, o, "reject", JS_NewString(ctx, reject));
    }
    return o;
}


/*
 * Throw a coded refusal: the message keeps its prose and gains a bracketed
 * code at the END (so every existing reader and every grep still finds the
 * text it knew), and the Error object carries `.code` — the half a deny-suite
 * should actually assert on, since it survives rewording.
 */
JSValue
ngx_js_comcon_refuse(JSContext *ctx, ngx_js_refusal_code_t code,
    const char *fmt, ...)
{
    u_char       *p, buf[512];
    JSValue       exc;
    va_list       args;
    const char   *name;

    name = ngx_js_refusal_name(code);

    va_start(args, fmt);
    p = ngx_vslprintf(buf, buf + sizeof(buf) - 1, fmt, args);
    va_end(args);
    *p = '\0';

    JS_ThrowTypeError(ctx, "%s [%s]", (char *) buf, name);

    exc = JS_GetException(ctx);
    if (JS_IsObject(exc)) {
        JS_SetPropertyStr(ctx, exc, "code", JS_NewString(ctx, name));
    }

    return JS_Throw(ctx, exc);
}


/*
 * The C3 admission gate, factored so both the `admit` operator and
 * `comcon.include` (which composes admission on the fragment it compiles in the
 * confined compartment) share ONE implementation. Runs entirely in `ctx`:
 * `fn` and `imports` (a JS array, or JS_UNDEFINED) must belong to `ctx`.
 * Returns NGX_OK (certified) or NGX_ERROR with `reason` filled and `*code` set
 * to the refusal code ([TBD-2]; ngx_js_compartment.h). Grants are
 * closure var-refs on `fn`, not free globals, so they are auto-excluded from
 * the free-name check — only genuine global lookups must be in `imports`.
 *
 * `reason` is the prose and `*code` is the CONTRACT: the two are set together
 * at every refusal site, because a reason without a code is what MANUAL §3.2
 * told tenants not to pin to, and a code without a reason is a lookup table
 * the operator reading the error log does not have.
 */
ngx_int_t
ngx_js_comcon_admit_check(JSContext *ctx, JSValueConst fn, JSValueConst imports,
    JSValueConst intrinsics, int check_request, char *reason, size_t reason_len,
    ngx_js_refusal_code_t *code)
{
    JSValue               len;
    ngx_js_admit_check_t  chk;
    char                  field[128];
    uint32_t              i;

    *code = NGX_JS_REFUSAL_NONE;

    if (!JS_IsFunction(ctx, fn)) {
        *code = NGX_JS_REFUSAL_ADMIT_ARG;
        ngx_snprintf((u_char *) reason, reason_len,
                     "admit: arg0 must be a function%Z");
        return NGX_ERROR;
    }

    /* C3: no direct eval / with */
    if (js_comcon_uses_dynamic_code(fn)) {
        *code = NGX_JS_REFUSAL_ADMIT_DYNCODE;
        ngx_snprintf((u_char *) reason, reason_len,
                     "dynamic-code: eval or with%Z");
        return NGX_ERROR;
    }

    /* C3: every free-global name must be in imports (deny-by-default) */
    ngx_memzero(&chk, sizeof(ngx_js_admit_check_t));
    chk.ctx = ctx;
    chk.imports = imports;

    if (JS_IsObject(imports)) {
        len = JS_GetPropertyStr(ctx, imports, "length");
        JS_ToUint32(ctx, &chk.imports_len, len);
        JS_FreeValue(ctx, len);
    }

    /*
     * The contract's intrinsics narrowing. PRESENT-but-malformed reads as the
     * NARROWEST setting (no intrinsics), the same fail-closed direction a
     * malformed `imports` takes: a contract that looks stricter than it is, is
     * worse than an absent one.
     */
    chk.intrinsics = intrinsics;
    chk.intr_present = !JS_IsUndefined(intrinsics) && !JS_IsNull(intrinsics);

    if (chk.intr_present && JS_IsObject(intrinsics)) {
        len = JS_GetPropertyStr(ctx, intrinsics, "length");
        JS_ToUint32(ctx, &chk.intr_len, len);
        JS_FreeValue(ctx, len);
    }

    /*
     * NARROWING ONLY, enforced rather than assumed: a name here that is not in
     * the allowance cannot widen anything, so accepting it silently would leave
     * a contract word that reads like policy and does nothing. `imports` is the
     * way to add a name; say so in the refusal.
     */
    for (i = 0; i < chk.intr_len; i++) {
        JSValue      iv = JS_GetPropertyUint32(ctx, intrinsics, i);
        const char  *is = JS_ToCString(ctx, iv);
        ngx_uint_t   known = (is != NULL) ? ngx_js_admit_name_intrinsic(is) : 0;

        if (!known) {
            *code = NGX_JS_REFUSAL_ADMIT_INTRINSIC;
            ngx_snprintf((u_char *) reason, reason_len,
                         "intrinsics: %s is not in the intrinsics allowance; "
                         "`intrinsics` only narrows it -- declare the name in "
                         "`imports` instead%Z", is ? is : "?");
            if (is != NULL) {
                JS_FreeCString(ctx, is);
            }
            JS_FreeValue(ctx, iv);
            return NGX_ERROR;
        }

        JS_FreeCString(ctx, is);
        JS_FreeValue(ctx, iv);
    }

    if (js_comcon_collect_free_globals(ctx, fn, ngx_js_admit_free_cb, &chk)
        != 0)
    {
        *code = NGX_JS_REFUSAL_ADMIT_NOTBYTECODE;
        ngx_snprintf((u_char *) reason, reason_len,
                     "admit: arg0 not a bytecode function%Z");
        return NGX_ERROR;
    }

    if (chk.bad) {
        /*
         * "not granted" sent a reader looking for a missing grant, when what is
         * missing is a DECLARATION: this gate checks the free-name manifest, and
         * `imports` is a whitelist of names, not a set of capabilities. There is
         * no intrinsics allowance either, so an ordinary `x !== undefined`
         * refuses unless `undefined` is declared -- found by the V3 oracle
         * (t/comcon_v3_oracle.t), which modelled the rule as stated and
         * disagreed with the rule as implemented. The wording now says which of
         * the two things to fix; whether intrinsics should be allowed without
         * declaration is a policy question recorded in VERIFICATION.md V3, not
         * something to widen silently.
         */
        *code = NGX_JS_REFUSAL_ADMIT_FREENAME;
        ngx_snprintf((u_char *) reason, reason_len,
                     "free name not declared in imports: %s%Z", chk.badname);
        return NGX_ERROR;
    }

    /* optional C3: sealed-Request field check */
    if (check_request
        && js_comcon_check_request_fields(ctx, fn, field, sizeof(field)))
    {
        *code = NGX_JS_REFUSAL_ADMIT_SCHEMA;
        ngx_snprintf((u_char *) reason, reason_len,
                     "request field not in sealed schema: %s%Z", field);
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * COMCON [TBD-2]: comcon.refusalCodes() — the closed refusal set, read from the
 * same table the refusals are thrown from.
 *
 * V7's rule (enumerations are generated, never maintained) applies with force
 * here: this is the list a tenant's CI enumerates to discover what it may be
 * refused with, so a hand-kept copy would be a promise that drifts. There are
 * no counters — a refusal happens at ADMISSION, so it is a load-time event with
 * an exception to catch, not a per-request statistic like a denial counter.
 */
static JSValue
ngx_js_comcon_refusal_codes(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue     a;
    uint32_t    n = 0;
    ngx_uint_t  i;

    a = JS_NewArray(ctx);

    /* from 1: NONE is the success value, not a code (see the names table) */
    for (i = NGX_JS_REFUSAL_NONE + 1; i < NGX_JS_REFUSAL_LAST; i++) {
        JS_SetPropertyUint32(ctx, a, n++,
            JS_NewString(ctx,
                ngx_js_refusal_name((ngx_js_refusal_code_t) i)));
    }

    return a;
}


static JSValue
ngx_js_comcon_admit(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValueConst  fn, contract;
    JSValue       imports, intrinsics, cr;
    int           check_request = 0;
    char          reason[256];
    ngx_int_t     rc;
    ngx_js_refusal_code_t  code;

    fn = argc > 0 ? argv[0] : JS_UNDEFINED;
    contract = argc > 1 ? argv[1] : JS_UNDEFINED;

    imports = JS_IsObject(contract)
              ? JS_GetPropertyStr(ctx, contract, "imports") : JS_UNDEFINED;
    intrinsics = JS_IsObject(contract)
                 ? JS_GetPropertyStr(ctx, contract, "intrinsics") : JS_UNDEFINED;

    if (JS_IsObject(contract)) {
        cr = JS_GetPropertyStr(ctx, contract, "checkRequest");
        check_request = JS_ToBool(ctx, cr);
        JS_FreeValue(ctx, cr);
    }

    rc = ngx_js_comcon_admit_check(ctx, fn, imports, intrinsics, check_request,
                                   reason, sizeof(reason), &code);
    JS_FreeValue(ctx, imports);
    JS_FreeValue(ctx, intrinsics);

    return ngx_js_admit_verdict(ctx, rc == NGX_OK, rc == NGX_OK ? NULL : reason,
                                code);
}




/*
 * COMCON M-CFG: the capability layer — env/grant/mediate/bind/meter — as JS on
 * the `comcon` object. `env()` a fresh deny-by-default environment; `grant`
 * places a held cap into it; `mediate`/`meter` build attenuation membranes; and
 * `bind(env, source)` is the real kernel bind — it COMPILES the source in the
 * confined compartment under the env (delegating to `include`), so free names
 * resolve only through the env's grants + intrinsics. bind is the env-first
 * spelling of include.
 */
static const char  ngx_js_comcon_bootstrap[] =
    "(function(){"
    "  var C=comcon, ENV='__comconEnv__', METER='__comconMeter__',"
    "      FACET='__comconFacet__', QUOTE='__comconQuote__';"
    "  C.env=function(){var e={grants:Object.create(null)};"
    "    Object.defineProperty(e,ENV,{value:true});return e;};"
    "  C.grant=function(env,name,cap){"
    "    if(!env||!env[ENV])throw new TypeError('grant: arg0 must be comcon.env()');"
    "    env.grants[name]=cap;return env;};"
    "  C.meter=function(opts){var m={};m[METER]=opts||{};return m;};"
    /* The mediation vocabulary is CLOSED, and an unknown member is refused here
       -- at the producer, stage 0 -- for the same reason a splice is checked at
       quote() time: the alternative is discovering it where it cannot be
       reported.  It used to fall through include()'s flavor translation and
       leave the default `{kind:0, mask:FULL}` in place, so a descriptor the
       enforcement layer does not implement (`allowHosts`) or a one-letter typo
       (`redcat` for `redact`) granted the capability IN FULL -- a misspelling
       that widened authority. Measured before fixing: the fragment read
       s.address as a string through both, where redact() correctly hid it.
       Adding a vocabulary word therefore means adding it in BOTH places -- the
       point of a closed set is that the two cannot drift silently. */
    /* M-LIB: `uses` joins the closed vocabulary. It is normalized in mediate()
       into an allow-everything mask PLUS a budget, so the field lattice and its
       meet are untouched -- a budget attenuates HOW MANY TIMES, not WHAT. */
    /* [TBD-2], second tranche: the capability layer throws with a CODE too.
       Mirrors ngx_js_comcon_refuse() in C -- `.code` on the error (the contract)
       and the code bracketed at the END of the message (so an error log is
       greppable and no existing reader of the prose stops matching). Kept in
       one function so the two halves of the platform cannot drift in shape. */
    "  function capRefuse(code,msg){"
    "    var e=new TypeError(msg+' ['+code+']');"
    "    e.code=code;throw e;}"
    "  var FLAVORS={revoke:1,redact:1,allow:1,routes:1,uses:1};"
    /* One definition of the socket field lattice, used by the meet here and by
       include()'s translation below -- two copies of a bitmask mapping is how a
       "narrower" membrane ends up wider than the one it attenuates. */
    "  var FMASK={address:1,port:2,fd:4,listener:8},FMASK_FULL=15;"
    "  function jsMask(it){var m,i,fs=it.fields||[];"
    "    if(it.flavor==='allow'){m=0;"
    "      for(i=0;i<fs.length;i++)m|=(FMASK[fs[i]]||0);}"
    "    else if(it.flavor==='redact'){m=FMASK_FULL;"
    "      for(i=0;i<fs.length;i++)m&=~(FMASK[fs[i]]||0);}"
    "    else if(it.flavor==='revoke')m=0;"
    "    else m=FMASK_FULL;"
    "    return m>>>0;}"
    "  function maskFields(m){var out=[],k;"
    "    for(k in FMASK)if(Object.prototype.hasOwnProperty.call(FMASK,k))"
    "      if(m&FMASK[k])out.push(k);"
    "    return out;}"
    "  function budgetMeet(a,b){"
    "    if(!a)return b||null;if(!b)return a;"
    "    if(a.key!==b.key||a.limit!==b.limit||a.window!==b.window)"
    "      capRefuse('E_CAP_ESCALATE','mediate: cannot re-mediate a budgeted "
               "capability with a DIFFERENT budget -- budgets are not ordered "
               "(10/min vs 100/hour), so a meet would have to guess, and the "
               "guess would widen one of them');"
    "    return a;}"
    "  C.mediate=function(cap,interceptor){var f={};"
    "    if(!interceptor||typeof interceptor!=='object')throw new TypeError("
    "      'mediate: arg1 must be an interceptor descriptor "
                 "(revoke/redact/allow/routes)');"
    "    if(!FLAVORS[interceptor.flavor])capRefuse('E_CAP_FLAVOR',"
    "      'mediate: unknown interceptor flavor '+String(interceptor.flavor)+"
    "      '; the vocabulary is closed (revoke, redact, allow, routes, uses) -- "
                 "an unrecognized one used to mean FULL authority');"
    /* SNAPSHOT, do not hold the caller's object.  Validating here and reading it
       at include() time is a time-of-check/time-of-use gap: the descriptor is an
       ordinary object the caller still holds, so
         var it = redact(['address']); var m = mediate(sock, it);
         it.flavor = 'redcat';
       passed the check and then reached the translation as an unknown flavor --
       which is the fail-open this increment closed, reopened from the other end.
       A frozen copy of the fields the translation reads means the value that was
       checked is the value that is used. */
    "    var snap={flavor:interceptor.flavor};"
    "    if(interceptor.fields!==undefined)"
    "      snap.fields=Array.prototype.slice.call(interceptor.fields);"
    "    if(interceptor.glob!==undefined)snap.glob=String(interceptor.glob);"
    /* `uses` is validated HERE, at the producer, and normalized away: what the
       rest of the pipeline sees is an allow-everything mask carrying a budget.
       Every field is refused rather than defaulted -- a budget with a missing
       limit is not "unlimited", it is a mistake, and the one direction a
       mediation may never take is toward more authority. */
    "    if(snap.flavor==='uses'){"
    "      var bk=String(interceptor.key||''),"
    "          bl=Number(interceptor.limit),bw=Number(interceptor.window);"
    "      if(!bk)throw new TypeError('mediate: uses() needs a key naming the "
                 "counter -- two capabilities share a budget only when the "
                 "operator says so');"
    "      if(bk.length>48)throw new TypeError('mediate: uses() key too long "
                 "(max 48 chars)');"
    "      if(!(bl>=1)||bl!==Math.floor(bl))throw new TypeError("
    "        'mediate: uses() needs an integer limit >= 1; a missing limit is a "
             "mistake, not `unlimited`');"
    "      if(!(bw>=1)||bw!==Math.floor(bw))throw new TypeError("
    "        'mediate: uses() needs an integer window >= 1 (seconds)');"
    "      snap={flavor:'allow',fields:maskFields(FMASK_FULL),"
    "            budget:{key:bk,limit:bl,window:bw}};}"
    /* V4 — ATTENUATION MEET, and the lattice inclusion asserted rather than
       argued.  Re-mediating an already-mediated capability used to fail with
       "grant is not a NginxSocket", because the translation unwraps one facet
       level and found another: fail-closed, but by accident and with a message
       about the wrong thing.

       The rule is A(cap'') = A(cap') MEET A(cap) -- an outer membrane may only
       NARROW what an inner one already allows, never restore a field it hid.
       Field masks form a lattice, so the meet is an AND and the inclusion is
       checkable; the assertion below is V4's point: a future logic bug here
       becomes a loud failure instead of a silent widening.  Globs do not form a
       computable meet, so a routes facet is re-mediated only by an identical
       glob and otherwise REFUSED -- guessing would be the widening this exists
       to prevent. */
    "    if(cap&&cap[FACET]){"
    "      var ii=cap[FACET].interceptor,oi=snap;"
    "      if(ii.flavor==='revoke'||oi.flavor==='revoke'){"
    "        snap=Object.freeze({flavor:'revoke'});}"
    "      else if(ii.flavor==='routes'||oi.flavor==='routes'){"
    "        if(ii.flavor!==oi.flavor||ii.glob!==oi.glob)capRefuse("
    "          'E_CAP_ESCALATE',"
    "          'mediate: cannot re-mediate a routes facet with a different "
                 "glob -- a glob meet is not computable, and guessing would "
                 "widen');"
    "        snap=Object.freeze({flavor:'routes',glob:ii.glob});}"
    "      else{"
    "        var mi=jsMask(ii),mo=jsMask(oi),mm=(mi&mo)>>>0;"
    "        if((mm&~mi)!==0||(mm&~mo)!==0)capRefuse('E_CAP_ESCALATE',"
    "          'mediate: attenuation meet widened authority (V4)');"
    /* Budgets do not form a computable meet either: 10-per-minute and
       100-per-hour are not ordered, and picking the smaller of each field
       yields 600-per-hour -- WIDER than one of the inputs. So the routes rule
       applies unchanged: an identical budget composes, a different one is
       REFUSED rather than guessed. */
    "        var bb=budgetMeet(ii.budget,oi.budget);"
    "        var ns={flavor:'allow',fields:maskFields(mm)};"
    "        if(bb)ns.budget=bb;"
    "        snap=Object.freeze(ns);}"
    "      cap=cap[FACET].cap;}"
    "    f[FACET]={cap:cap,interceptor:Object.freeze(snap)};return f;};"
    /* interceptor library — attenuation-only membranes over a cap. For a
       NginxSocket the fields are address/port/fd/listener; the membrane is
       realized as a C-side field mask on the re-wrapped cap. */
    "  C.revoke=function(){return {flavor:'revoke'};};"
    "  C.redact=function(fields){"
    "    return {flavor:'redact',fields:fields||[]};};"
    "  C.allow=function(fields){"
    "    return {flavor:'allow',fields:fields||[]};};"
    /* routes(glob): attenuate a granted COM server to a route glob. The
       fragment receives a NginxComFacet (never the stateful server wrapper). */
    "  C.routes=function(glob){return {flavor:'routes',glob:String(glob)};};"
    /* uses(key, limit, window): a fleet-wide FIXED-WINDOW budget on a capability.
       `key` NAMES the counter, so two capabilities share a budget exactly when
       the operator says they do -- deriving a key would make that unsayable and
       make the counter's identity depend on wrapping order. */
    "  C.uses=function(key,limit,window){"
    "    return {flavor:'uses',key:String(key||''),"
    "            limit:Number(limit),window:Number(window)};};"
    /* stone check (increment D3): a splice may carry only DEEP cap-free plain
       data — primitives + frozen records/arrays; no functions, no capabilities
       (facet/quote/confined), no getters/setters (a getter could mint a cap
       lazily; TOCTOU-unsound). A violation is a stage-0 error at the PRODUCER.
       Makes cap-freeness stable, so the closure/quotation split stays checkable
       (SEMANTICS §4.4, rule QUOTE / the cap-free rule R2). */
    "  function pomStone(v,name){var t=typeof v;"
    "    if(v===null||t==='number'||t==='string'||t==='boolean')return;"
    "    if(t!=='object')throw new TypeError("
    "      'quote: splice '+name+' is not cap-free (stone) data');"
    "    if(v[FACET]||v[QUOTE]||v.confined)throw new TypeError("
    "      'quote: splice '+name+' carries a capability');"
    "    var ks=Object.keys(v);"
    "    for(var i=0;i<ks.length;i++){"
    "      var d=Object.getOwnPropertyDescriptor(v,ks[i]);"
    "      if(d.get||d.set)throw new TypeError("
    "        'quote: splice '+name+' has an accessor (not stone)');"
    "      pomStone(v[ks[i]],name);}"
    "    Object.freeze(v);}"
    /* quote(source, splices?): an inert, cap-free DESCRIPTION of a policy/
       fragment — the quotation half of closure-vs-quotation (FOUNDATION §6).
       Zero authority. Optional `splices` = producer data spliced into the
       description; each is deep-checked STONE (cap-free) at quote time, so a
       quotation is structurally immune to injecting a capability. Splices bind
       at data positions (never as text — see realize), the same reason
       parameterized SQL kills injection. Frozen + marked so realize() can tell a
       description from a closure. Structured POM-node splices (splice into a
       parsed subtree) await stmt/expr nodes (D5). */
    "  C.quote=function(source,splices){"
    "    var q={source:String(source)};"
    "    if(splices!==undefined&&splices!==null){"
    "      if(typeof splices!=='object')throw new TypeError("
    "        'quote: splices must be an object');"
    "      var sk=Object.keys(splices),sp=Object.create(null);"
    "      for(var i=0;i<sk.length;i++){pomStone(splices[sk[i]],sk[i]);"
    "        sp[sk[i]]=splices[sk[i]];}"
    "      q.splices=Object.freeze(sp);}"
    "    Object.defineProperty(q,QUOTE,{value:true});"
    "    return Object.freeze(q);};"
    /* reviewDeclarative(source) — increment D5b-1: the syntax_allowed declarative
       profile checker + descriptor-table normal form (SEMANTICS §4.4, FOUNDATION
       §8). A config/policy SENTENCE is declarative iff it is a straight-line
       sequence of fluent call-chains over dotted name paths, with literal /
       nested-chain / free-name-ref arguments — NO loops, conditionals, operators,
       assignments, computed access, or functions. A SOUND rejecter: it parses ONLY
       that grammar and throws on anything else, so what it accepts is exactly what
       reduces to the returned descriptor tables (diffable JSON — the review
       artifact). This is the one platform hook the config-language pattern needs
       (PATTERN_config_language.md): an untrusted proposal becomes soundly
       reviewable, not merely runtime-validated. Full CST / source-rewrite is D5b-2+. */
    "  C.reviewDeclarative=function(source){"
    "    var s=String(source),i=0,N=s.length;"
    "    function isIdS(c){return !!c&&(c>='a'&&c<='z'||c>='A'&&c<='Z'||c==='_'||c==='$');}"
    "    function isId(c){return isIdS(c)||c>='0'&&c<='9';}"
    "    function isD(c){return c>='0'&&c<='9';}"
    "    var KW={'for':1,'while':1,'do':1,'if':1,'else':1,'switch':1,'function':1,"
    "      'return':1,'var':1,'let':1,'const':1,'new':1,'throw':1,'try':1,'catch':1,"
    "      'with':1,'class':1,'yield':1,'await':1,'typeof':1,'delete':1,'void':1,"
    "      'in':1,'instanceof':1,'this':1,'super':1};"
    "    function fail(m){throw new TypeError('not declarative: '+m+' (@'+i+')');}"
    /* A line comment ends at ANY JS LineTerminator -- LF, CR, U+2028, U+2029 --
       not only at LF. Scanning for LF alone let a BARE CR (or LS/PS) hide the
       rest of the line from the review while the engine still compiled it:
       `a(1); //<CR>for(;;){}` was accepted, its descriptor table listed the one
       call a(1), and the admitted program ran the loop. That is the escape this
       profile exists to prevent, so the terminator set must match the engine's.
       LS/PS end a comment but are NOT accepted as whitespace: outside a comment
       they still refuse, which is the conservative direction. */
    "    function isNL(c){return c==='\\n'||c==='\\r'"
    "      ||c==='\\u2028'||c==='\\u2029';}"
    "    var ce=0;"
    "    function nlBetween(a,b){for(var k=a;k<b;k++)if(isNL(s[k]))return true;"
    "      return false;}"
    "    function ws(){for(;;){var c=s[i];"
    "      if(c===' '||c==='\\t'||c==='\\n'||c==='\\r'){i++;continue;}"
    "      if(c==='/'&&s[i+1]==='/'){i+=2;while(i<N&&!isNL(s[i]))i++;continue;}"
    "      break;}}"
    "    function ident(){ws();var st=i;if(!isIdS(s[i]))fail('expected name');"
    "      while(i<N&&isId(s[i]))i++;var w=s.slice(st,i);"
    "      if(KW[w])fail(\"keyword '\"+w+\"'\");return w;}"
    "    function path(){var p=[ident()];for(;;){ws();"
    "      if(s[i]==='.'){var sv=i;i++;ws();"
    "        if(isIdS(s[i]))p.push(ident());else{i=sv;break;}}else break;}"
    "      return p.join('.');}"
    /* An unescaped LF or CR inside a string literal is a SyntaxError to the
       engine, so a source carrying one is not a program at all and must not be
       reviewed as if it were. A line continuation (backslash + newline) is
       legal and stays legal: the escape branch consumes it before this test. */
    "    function str(){var q=s[i++],o='';"
    "      while(i<N&&s[i]!==q){"
    "        if(s[i]==='\\n'||s[i]==='\\r')fail('line terminator in string');"
    "        if(s[i]==='\\\\'){o+='\\\\'+s[i+1];i+=2;}else o+=s[i++];}"
    "      if(s[i]!==q)fail('unterminated string');i++;"
    "      try{return JSON.parse('\"'+o+'\"');}catch(e){return o;}}"
    /* A numeric literal must actually be one. Scanning optimistically and
       handing back Number(slice) minted values the source never contained: a
       bare '-' and a truncated exponent ('1e') both produced NaN, which the
       descriptor table then reported as JSON null -- a literal the reviewer
       sees but the source does not have, in a source the engine rejects. A
       non-finite value (1e400) is refused for the same reason: the table is
       advertised as a diffable JSON artifact, and JSON cannot carry it. */
    "    function num(){var st=i,nd=0;if(s[i]==='-')i++;"
    "      while(i<N&&isD(s[i])){i++;nd++;}"
    "      if(s[i]==='.'){i++;while(i<N&&isD(s[i])){i++;nd++;}}"
    "      if(nd===0)fail('malformed number');"
    "      if(s[i]==='e'||s[i]==='E'){i++;if(s[i]==='+'||s[i]==='-')i++;"
    "        var ne=0;while(i<N&&isD(s[i])){i++;ne++;}"
    "        if(ne===0)fail('malformed exponent');}"
    "      var v=Number(s.slice(st,i));"
    "      if(!isFinite(v))fail('number literal is not finite');return v;}"
    "    function obj(){i++;var o={};ws();if(s[i]==='}'){i++;return o;}"
    "      for(;;){ws();var k=(s[i]==='\"'||s[i]===\"'\")?str():ident();ws();"
    "        if(s[i]!==':')fail(\"expected ':'\");i++;o[k]=value();ws();"
    "        if(s[i]===','){i++;continue;}if(s[i]!=='}')fail(\"expected '}'\");i++;break;}"
    "      return o;}"
    "    function arr(){i++;var a=[];ws();if(s[i]===']'){i++;return a;}"
    "      for(;;){a.push(value());ws();if(s[i]===','){i++;continue;}"
    "        if(s[i]!==']')fail(\"expected ']'\");i++;break;}return a;}"
    "    function args(){var a=[];ws();if(s[i]===')')return a;"
    "      for(;;){a.push(value());ws();if(s[i]===','){i++;continue;}break;}return a;}"
    /* `ce` tracks the index just past the ')' that closed the most recent call,
       i.e. where the statement really ended. chain() skips trailing whitespace
       itself while looking for a further '.' step, so the statement separator
       below cannot be decided from a flag set during that skip -- it needs the
       span of source between the closing ')' and the next token. */
    "    function chain(){var steps=[],p=path();ws();"
    "      if(s[i]!=='(')fail(\"expected '(' after '\"+p+\"'\");i++;"
    "      steps.push({op:p,args:args()});ws();"
    "      if(s[i]!==')')fail(\"expected ')'\");i++;ce=i;"
    "      for(;;){ws();if(s[i]==='.'){var sv=i;i++;ws();"
    "        if(!isIdS(s[i])){i=sv;break;}var p2=path();ws();"
    "        if(s[i]!=='(')fail(\"chain step '\"+p2+\"' is not a call\");i++;"
    "        steps.push({op:p2,args:args()});ws();"
    "        if(s[i]!==')')fail(\"expected ')'\");i++;ce=i;continue;}break;}"
    "      return steps;}"
    "    function value(){ws();var c=s[i];"
    "      if(c==='\"'||c===\"'\")return str();"
    "      if(c==='-'||isD(c))return num();"
    "      if(c==='{')return obj();"
    "      if(c==='[')return arr();"
    "      if(isIdS(c)){var sv=i,p=path();ws();"
    "        if(s[i]==='('){i=sv;return {chain:chain()};}"
    "        return {ref:p};}"
    "      fail(\"unexpected '\"+(c||'<eof>')+\"'\");}"
    /* Statements must be SEPARATED, by ';' or by a line break. Treating the
       ';' as merely optional accepted `one() two()`, which is not a program in
       any grammar -- the engine rejects it outright -- so the table described
       something that could never run. This is the engine's own rule (explicit
       semicolon, or automatic insertion at a line terminator), kept strict:
       nothing legal is lost, since a proposal that omits both is not JS. */
    "    var out=[];ws();"
    "    while(i<N){var sv=i;path();ws();"
    "      if(s[i]!=='(')fail('statement must be a call');i=sv;"
    "      out.push(chain());var ep=ce;ws();"
    "      if(s[i]===';'){i++;ws();}"
    "      else if(i<N&&!nlBetween(ep,i))"
    "        fail(\"expected ';' between statements\");}"
    "    return {declarative:true,statements:out};};"
    /* reviewCalls(source, grants) — the admission-time CALL check.
       reviewDeclarative() proves a proposal has the declarative SHAPE, but its
       descriptor table was discarded by the admit path, so nothing ever verified
       that the calls inside it are real: a typo'd member or a wrong-arity call
       was admitted and only failed later, at request time, inside the tenant.
       This walks the table and validates every call whose receiver it can
       resolve statically against the typed describe() registry — unknown member
       or wrong arity is refused HERE, at admission, where the refusal can be
       charged to the realizer and reported with the offending call path.
       M4 RETURN-TYPE BINDING: a chained step is a member of the PREVIOUS step's
       return type, so it used to be unconditionally `unchecked` -- only the
       first call in `nginx.addServer(..).addLocation(..)` was ever verified.
       The typed registry records returns (M2b: addLocation -> handle<Location>),
       so the chain's receiver TYPE is now carried across steps and checked with
       nginx.describeType(), which answers "what does this type offer" without
       an instance. Only `handle<X>` names a receiver; void/bool/str end the
       chain's type knowledge and the rest falls back to `unchecked`.
       SOUNDNESS IS PRESERVED -- it still never guesses. A receiver it cannot
       resolve (a non-COM grant; an un-granted root, which is the free-name
       gate's job; a chain step after a non-handle return; a dotted chained step
       whose accessor is not typed in the tables yet) is reported in `unchecked`
       rather than rejected, so this
       can only turn runtime failures into admission failures, never reject a
       valid proposal. Arity is checked as the RANGE required..declared,
       matching how describe() records optional parameters. */
    "  C.reviewCalls=function(source,grants){"
    "    var r=C.reviewDeclarative(source),checked=[],unchecked=[];"
    "    function fail(m){throw new TypeError('admission refused: '+m);}"
    /* M4 return-type binding: handle<X> is the only return that names a
       receiver for the NEXT step in a chain. Anything else (void, bool, str)
       ends the chain's type knowledge, and the rest goes to `unchecked`. */
    "    function hnd(t){var m=/^handle<([A-Za-z_$][A-Za-z0-9_$]*)>$/"
    "      .exec(String(t===undefined||t===null?'':t));return m?m[1]:null;}"
    "    var st=r.statements;"
    "    for(var si=0;si<st.length;si++){var ch=st[si];"
    "      var ctype=null;"
    "      for(var k=0;k<ch.length;k++){var step=ch[k];"
    "        var segs=String(step.op).split('.');"
    "        var mem=segs[segs.length-1];"
    "        var d=null;"
    "        if(k===0){"
    "          if(!grants||segs.length<2){"
    "            unchecked.push(step.op);ctype=null;continue;}"
    "          var root=segs[0];"
    "          if(!Object.prototype.hasOwnProperty.call(grants,root)){"
    "            unchecked.push(step.op);ctype=null;continue;}"
    "          var recv=grants[root];"
    "          for(var j=1;j<segs.length-1&&recv;j++)recv=recv[segs[j]];"
    "          if(!recv||typeof recv!=='object'){"
    "            unchecked.push(step.op);ctype=null;continue;}"
    "          try{d=nginx.describe(recv,mem);}catch(e){d=null;}"
    /* A chained step is a member of the PREVIOUS step's return type, so it can
       only be checked once that type is known. A DOTTED chained step
       (`.proxy.setPass(..)`) additionally walks sub-object accessors, which a
       static check cannot do by following a prototype — hence the WRAP rows in
       the describe() tables, whose handle<X> type names each accessor's class.
       If any segment fails to resolve the step is left UNCHECKED, never
       guessed: an accessor absent from the table (a class whose wrappers are
       not typed yet) must not become a refusal. */
    "        }else if(ctype){"
    "          var t=ctype;"
    "          for(var q=0;q<segs.length-1&&t;q++){"
    "            var nd=null;try{nd=nginx.describeType(t,segs[q]);}catch(e){nd=null;}"
    "            t=nd?hnd(nd.type):null;}"
    "          if(!t){unchecked.push(step.op);ctype=null;continue;}"
    "          try{d=nginx.describeType(t,mem);}catch(e){d=null;}"
    "          if(d===null||d===undefined)"
    "            fail(\"unknown member '\"+mem+\"' on \"+t+"
    "                 \" in chained call '\"+step.op+\"'\");"
    "        }else{unchecked.push(step.op);ctype=null;continue;}"
    "        if(d===null||d===undefined)"
    "          fail(\"unknown member '\"+mem+\"' in call '\"+step.op+\"'\");"
    "        if(d.params){var lo=0,hi=d.params.length;"
    "          for(var p=0;p<hi;p++){if(d.params[p].optional)break;lo++;}"
    "          var n=step.args.length;"
    "          if(n<lo||n>hi)"
    "            fail(\"'\"+step.op+\"' expects \"+lo+\"..\"+hi+\" argument(s), got \"+n);}"
    "        ctype=hnd(d.returns);"
    "        checked.push(step.op);}}"
    "    return {ok:true,checked:checked,unchecked:unchecked};};"
    /* realize(q, contract, realizerEnv): give a quotation force under the
       REALIZER's authority — the operator-realizes-a-tenant-proposal path
       (showcases 46-47). Distinct from bind/include (which use the PRODUCER's
       env, closure discipline). Least-authority realization (R6): the contract
       is MANDATORY, and the realization environment is the realizer's grants
       RESTRICTED to the quotation's declared free-name manifest (contract.
       imports) — rho_R |^ manifest — so a proposal reviewed as "needs a,b,c"
       cannot touch anything else the operator's session holds (confused-deputy
       fix). The existing admit gate then enforces free-names subset of imports,
       charging E_CAP_UNRESOLVED at the realizer for anything undeclared. */
    "  C.realize=function(q,contract,renv){"
    "    if(!q||!q[QUOTE])throw new TypeError("
    "      'realize: arg0 must be a comcon.quote() description, not a closure');"
    "    if(!contract||typeof contract!=='object')throw new TypeError("
    "      'realize: a contract is mandatory (least-authority realization)');"
    "    if(!renv||!renv[ENV])throw new TypeError("
    "      'realize: arg2 must be the realizer comcon.env()');"
    "    var manifest=contract.imports||[],rg=Object.create(null);"
    "    for(var i=0;i<manifest.length;i++){var n=manifest[i];"
    "      if(Object.prototype.hasOwnProperty.call(renv.grants,n))"
    "        rg[n]=renv.grants[n];}"
    /* V4 — the lattice inclusion, ASSERTED at admission rather than argued.
       A*(child) SUBSET-OF A*(parent): the restricted environment a fragment is
       realized under may only ever be a sub-map of the REALIZER's, name by name
       and value by value.  It is true by construction two lines above, which is
       exactly why it is worth checking: the theorem holds given an unforgeable
       TCB, and a TCB bug here -- a substituted cap, an extra name -- would
       otherwise fail silently and confer authority nobody granted.  Cheap
       (environments are finite), and it converts that class into a loud one. */
    "    for(var rn in rg)if(Object.prototype.hasOwnProperty.call(rg,rn)){"
    "      if(!Object.prototype.hasOwnProperty.call(renv.grants,rn)"
    "         ||rg[rn]!==renv.grants[rn])capRefuse('E_CAP_ESCALATE',"
    "        'realize: restricted env is not a sub-map of the realizer (V4): '"
    "        +rn);}"
    "    var c={grants:rg,imports:manifest};"
    /* D5b-1: an operator can require the proposal be in the declarative profile —
       soundly reviewable (reduces to descriptor tables), no loops/dynamic. The
       check runs on the ORIGINAL source before any splice wrapping; refusal is
       charged as admission at the realizer. */
    "    if(contract.profile==='declarative'){"
    "      try{C.reviewDeclarative(q.source);}"
    "      catch(e){throw new Error('admission refused: '+e.message);}}"
    /* contract.checkCalls: OPT-IN so this cannot reject a proposal that admitted
       before. Requires the declarative profile (reviewCalls parses with it). The
       receiver set is the RESTRICTED grants rg, so the check sees exactly the
       authority the fragment will actually run with. */
    "    if(contract.checkCalls){"
    "      if(contract.profile!=='declarative')"
    "        throw new Error('admission refused: checkCalls requires "
    "profile:\\'declarative\\'');"
    "      C.reviewCalls(q.source,rg);}"
    "    if(contract.intrinsics!==undefined)c.intrinsics=contract.intrinsics;"
    "    if(contract.meter)c.meter=contract.meter;"
    "    if(contract.tests)c.tests=contract.tests;"
    "    if(contract.identity)c.identity=contract.identity;"
    "    if(contract.checkRequest)c.checkRequest=contract.checkRequest;"
    /* splices bind as DATA, never as text: each is emitted as a JSON literal
       into an enclosing IIFE var, so the quoted code's reference to the splice
       name resolves to escaped producer data — a spliced string cannot smuggle
       code (JSON.stringify escaping = the parameterized-SQL defense), and the
       names become bound closure vars (invisible to the admit free-name gate).
       Stone was already enforced at quote() time. */
    "    var src=q.source;"
    "    if(q.splices){var sk=Object.keys(q.splices);"
    "      if(sk.length){var pre='';"
    "        for(var j=0;j<sk.length;j++)"
    "          pre+=(j?',':'')+sk[j]+'='+JSON.stringify(q.splices[sk[j]]);"
    "        src='(function(){var '+pre+';return('+q.source+');})()';}}"
    "    return C.include(src,c);};"
    /* bind(env, source, opts): attach the env over a fragment — the real kernel
       bind, realized by COMPILING the source in the confined compartment under
       the env (you cannot re-bind an already-compiled host closure to a
       restricted env; confinement requires compile-in-env). This is the
       env-first spelling of include: `bind(grant(env(),"x",cap), src, {meter})`
       ≡ `include(src, {grants:{x:cap}, meter})`. Returns the confined callable. */
    "  C.bind=function(env,source,opts){"
    "    if(!env||!env[ENV])throw new TypeError('bind: arg0 must be comcon.env()');"
    "    opts=opts||{};"
    "    var contract={grants:env.grants};"
    "    if(opts.imports)contract.imports=opts.imports;"
    "    if(opts.meter)contract.meter=opts.meter;"
    "    if(opts.tests)contract.tests=opts.tests;"
    "    if(opts.identity)contract.identity=opts.identity;"
    "    if(opts.checkRequest)contract.checkRequest=opts.checkRequest;"
    "    return C.include(String(source),contract);};"
    /* include(source, contract): compile the fragment in the confined
       compartment (own runtime) and hold it there; the returned callable
       marshals arg/result by JSON round-trip in C — no live object crosses.
       AUTHORITY isolation (host unreachable) + RESOURCE (the meter).
       contract.grants maps a name -> a live host capability (a NginxSocket);
       each is re-wrapped compartment-native and injected as a closure binding
       of that name (attenuation-only: the cap stays reach-gated). */
    /* ---- the audit/enforce/learn mode, FLEET-WIDE -----------------------
       comcon.mode() sets a per-process static, so the rollout verbs switched
       only the worker that served the request.  Measured on four workers: one
       shadow() call, then 24 requests -> 16 audit and 8 enforce.  THE FLEET SAT
       IN MIXED MODES, and the dangerous direction is the common one -- an
       operator calls enforce(), gets "enforce" back, and some workers keep
       AUDITING: still allowing what they believe they have begun denying.

       Fixed with D4b's transport rather than a new one: the mode lives in
       nginx.shared as {epoch, mode}, and each worker RECONCILES LAZILY -- one
       shared read before a fragment runs, and before the mode is reported.
       Lazy pull, not eager push: no broadcast, no stop-the-world, and a worker
       that was busy during the switch picks it up on its next fragment.

       nginx.shared does not exist at config-eval time (only once workers run),
       so every access here is guarded: a config-time comcon.mode() sets the
       local mode and publishes nothing, which is right -- the value is already
       in jcf->tenant_mode and every worker inherits it across fork(). */
    "  var MODEK='__comconMode__',modeEpoch=-1,__modeC=C.mode;"
    "  function modeShared(){"
    "    try{return (typeof nginx!=='undefined'&&nginx.shared)?nginx.shared:null;}"
    "    catch(e){return null;}}"
    "  function modeReconcile(){"
    "    var sh=modeShared();if(!sh)return;"
    "    var raw;try{raw=sh.get(MODEK);}catch(e){return;}"
    "    if(raw===undefined||raw===null)return;"
    "    var st;try{st=JSON.parse(raw);}catch(e){return;}"
    "    if(!st||st.epoch===modeEpoch)return;"
    /* Apply LOCALLY through the C setter, never through C.mode -- publishing
       here would bump the epoch on every reconcile and the fleet would chase
       its own tail. */
    "    __modeC(st.mode);modeEpoch=st.epoch;}"
    "  C.mode=function(m){"
    "    var eff=__modeC(m);"
    "    var sh=modeShared();"
    "    if(sh){try{"
    "      var raw=sh.get(MODEK);"
    "      var prev=0;"
    "      if(raw!==undefined&&raw!==null){"
    "        try{prev=(JSON.parse(raw).epoch|0);}catch(e2){prev=0;}}"
    "      modeEpoch=prev+1;"
    "      sh.set(MODEK,JSON.stringify({epoch:modeEpoch,mode:eff}));"
    "    }catch(e){}}"
    "    return eff;};"
    "  C.__modeReconcile=modeReconcile;"
    "  C.include=function(source,contract){"
    "    contract=contract||{};"
    "    var g=contract.grants||{},names=[],caps=[],pols=[];"
    "    for(var k in g){if(Object.prototype.hasOwnProperty.call(g,k)){"
    "      var v=g[k],cap=v,pol={kind:0,mask:FMASK_FULL};"
    "      if(v&&v[FACET]){var it=v[FACET].interceptor||{};cap=v[FACET].cap;"
    "        if(it.flavor==='revoke')continue;"       /* narrow to zero: withhold */
    /* ONE mask definition (jsMask), shared with mediate()'s attenuation meet:
       two copies of a bitmask mapping is how a "narrower" membrane ends up
       wider than the one it attenuates. */
    "        else if(it.flavor==='allow'||it.flavor==='redact'){"
    "          pol={kind:0,mask:jsMask(it)};"
    "          if(it.budget)pol.budget=it.budget;}"
    "        else if(it.flavor==='routes'){"
    "          pol={kind:1,glob:String(it.glob||'*')};}"
    /* No fall-through to the FULL default.  NOTE it is not reachable through the
       public API any more -- mediate() refuses an unknown flavor and snapshots
       the descriptor -- so no test drives this line, and it is kept anyway as a
       deliberate exception to "delete what no control can break".  What it
       guards is DRIFT: this translation and mediate()'s FLAVORS set are two
       lists of the same closed vocabulary, and if a future word is added to one
       and not the other, the fall-through decides whether that mistake means
       REFUSE or FULL AUTHORITY.  The cost of being wrong here is silent full
       authority, so the default direction is the whole point. */
    "        else throw new TypeError("
    "          'include: unknown mediation flavor '+String(it.flavor)+"
    "          ' for grant '+k+'; refusing rather than granting in full');}"
    "      names.push(String(k));caps.push(cap);pols.push(pol);}}"
    /* P1 (CONVERGE): opt-in C3 admission + identity pin — present iff the
       contract asks (imports/identity/checkRequest). Absent => no admission
       (backward compatible with un-admitted include fragments). */
    /* admit(node, contract): (i) free-names, (ii) syntactic predicates, and
       (iii) run contract.tests in the compartment against the fragment (zero
       blast radius — no host authority is in scope). A String tests fn refuses
       admission if it throws. Present iff the contract asks. */
    "    var admit=null;"
    /* PRESENCE, not truthiness, for imports: `{imports: ''}` is a caller
       who meant to declare a manifest and got it wrong, not one who
       declined admission, and reading it as "declined" left the fragment
       ungated.  The other three keep truthiness so that an explicit
       `checkRequest: false` does not newly switch admission on. */
    /* `intrinsics` joins the presence test for the same reason `imports` is
       there: a contract that narrows the allowance has asked for admission, and
       a narrowing with the gate switched off would be a policy word that does
       nothing. */
    "    if(contract.imports!==undefined||contract.identity"
    "       ||contract.checkRequest||contract.tests"
    "       ||contract.intrinsics!==undefined){"
    "      admit={imports:contract.imports||[],"
    "             intrinsics:contract.intrinsics,"
    "             checkRequest:!!contract.checkRequest,"
    "             identity:contract.identity,"
    "             tests:(typeof contract.tests==='function'"
    "                    ?String(contract.tests):contract.tests)};}"
    /* P3 (CONVERGE): pinned pure-library deps, bound as fragment closure
       params (per-fragment) — retires js_tenant_dependency onto include. */
    "    var deps=[];"
    "    if(contract.deps){for(var di=0;di<contract.deps.length;di++){"
    "      var d=contract.deps[di];deps.push({name:String(d.name),"
    "        path:String(d.path),sha256:String(d.sha256)});}}"
    "    var h=C.__includeConfined(String(source),names,caps,pols,admit,deps);"
    "    var ms=(contract.meter&&contract.meter[METER]"
    "            &&contract.meter[METER].timeoutMs)|0;"
    "    var bound=function(arg){modeReconcile();"
    "      return C.__invokeConfined(h,arg,ms);};"
    "    bound.confined=true;bound.handle=h;bound.meterMs=ms;return bound;};"
    /* pom(fragment): the reflective Program Object Model surface (increment D1).
       A lazy NodeView tree over a compiled fragment (module/function granularity
       — the bytecode tree; POM.md). Reads ALWAYS return quotations: text()/
       quote() hand back C.quote() values (cap-free descriptions), never raw
       source, so a read cannot leak authority (SEMANTICS REFLECT). children/
       parent materialize lazily on access. `id` is creation-ordered + stable
       within a process (keyed by content hash + path); `binding` is REDACTED by
       default (epoch/profile only — names[] needs the inspect-binding op). The
       node is frozen (immutable view). Backed by C.__pomNodeAt (single node at
       a path); the root fragment is kept alive by this closure (GC-safe). */
    "  var pomN=0, pomIds=Object.create(null);"
    "  function pomId(hash,ps){var k=hash+'@'+ps;"
    "    if(pomIds[k]===undefined)pomIds[k]=++pomN;return pomIds[k];}"
    /* query(sel): the target sub-language (POM.md §6 Q2) at coarse granularity.
       selector := term ('within' term)*  ;  term := factor+ (AND)
       factor  := 'module' | 'function' | '*' | 'name(' glob ')'
       glob    := exact | pre* | *suf | *mid* | * .  `A within B` selects nodes
       matching A that have an ANCESTOR matching B (intensional composition, the
       hardening pattern). A value-level DSL interpreted here, under the handle —
       no new grammar in the host language. Matches over the node's whole subtree
       (self + descendants); recomputed live on every call (born-bound, R9). */
    /* Quotes are stripped here, centrally, for EVERY glob factor.  MANUAL.md
       spells this one anchors('name') while call(fetch)/name(foo)/type(T) are
       unquoted, so a reader who follows the manual would otherwise match zero
       nodes and be told nothing -- a selector that silently finds no sites is
       the worst answer a hardening query can give.  No glob can legitimately
       contain a quote (anchor names are [A-Za-z0-9_$.-], identifiers and
       ESTree type names likewise), so accepting both spellings is unambiguous
       rather than merely lenient. */
    "  function pomName(name,g){"
    "    var q=g.charCodeAt(0);"          /* 39 = apostrophe, 34 = quote */
    "    if((q===39||q===34)&&g.length>1&&g.charCodeAt(g.length-1)===q)"
    "      g=g.slice(1,-1);"
    "    if(g==='*')return true;"
    "    var s=g.charAt(0)==='*',e=g.charAt(g.length-1)==='*';"
    "    if(s&&e)return name.indexOf(g.slice(1,-1))>=0;"
    "    if(s){var t=g.slice(1);return name.slice(name.length-t.length)===t;}"
    "    if(e){var u=g.slice(0,-1);return name.slice(0,u.length)===u;}"
    "    return name===g;}"
    "  function pomFactor(node,f){"
    "    if(f==='*')return true;"
    "    if(f==='module')return node.kind===1;"
    "    if(f==='function')return node.kind===2;"
    /* D5b-2: CST kinds. The p_symbol schema reserved block=3/stmt=4/expr=5 at
       D0 for exactly this; they only appear inside a cst() view. */
    "    if(f==='block')return node.kind===3;"
    "    if(f==='stmt')return node.kind===4;"
    "    if(f==='expr')return node.kind===5;"
    "    if(f.slice(0,5)==='call('&&f.charAt(f.length-1)===')')"
    "      return node.type==='CallExpression'&&pomName(node.name,f.slice(5,-1));"
    "    if(f.slice(0,5)==='type('&&f.charAt(f.length-1)===')')"
    "      return pomName(String(node.type||''),f.slice(5,-1));"
    /* anchors(glob): nodes carrying a matching inert site marker. The point of
       the anchors model -- a policy targets a NAMED SITE, not a line number,
       so edits above it do not move the target. */
    "    if(f.slice(0,8)==='anchors('&&f.charAt(f.length-1)===')'){"
    "      if(!Array.isArray(node.anchors))"
    "        throw new TypeError('query: anchors() needs a cst() view; the "
                               "bytecode tier does not parse');"
    "      var ag=f.slice(8,-1),as=node.anchors;"
    "      for(var ai=0;ai<as.length;ai++)if(pomName(as[ai],ag))return true;"
    "      return false;}"
    /* line(N) / line(N-M): a span predicate over the node's START line. Brittle
       by nature (any edit above shifts it), which is exactly why anchors exist;
       offered for interactive/LSP use, where a caller has a cursor position. */
    "    if(f.slice(0,5)==='line('&&f.charAt(f.length-1)===')'){"
    /* Validate with a regex, not parseInt: parseInt('5x') is 5, so a typo
       would be silently READ AS a line number and answer a question the
       caller did not ask.  An inverted range throws for the same reason --
       matching nothing is indistinguishable from "no sites here". */
    "      var lm=/^([0-9]+)(?:-([0-9]+))?$/.exec(f.slice(5,-1));"
    "      if(!lm)throw new TypeError('query: bad line(): '+f);"
    "      var a0=+lm[1],a1=lm[2]===undefined?a0:+lm[2];"
    "      if(a1<a0)throw new TypeError('query: inverted line range: '+f);"
    "      return node.line0>=a0&&node.line0<=a1;}"
    "    if(f.slice(0,5)==='name('&&f.charAt(f.length-1)===')')"
    "      return pomName(node.name,f.slice(5,-1));"
    "    throw new TypeError('query: bad selector factor: '+f);}"
    "  function pomTerm(node,t){var fs=t.trim().split(/\\s+/);"
    "    for(var i=0;i<fs.length;i++)if(fs[i]&&!pomFactor(node,fs[i]))return false;"
    "    return fs.length>0;}"
    "  function pomDesc(node,acc){acc.push(node);var ch=node.children;"
    "    for(var i=0;i<ch.length;i++)pomDesc(ch[i],acc);return acc;}"
    "  function pomQuery(root,sel){"
    "    var stages=String(sel).split(' within ');"
    "    if(stages[0].trim()==='')throw new TypeError('query: empty selector');"
    "    var primary=stages[0],anc=stages.slice(1);"
    "    var all=pomDesc(root,[]),out=[];"
    "    for(var i=0;i<all.length;i++){var n=all[i];"
    "      if(!pomTerm(n,primary))continue;"
    "      var ok=true,p=n.parent,ai=0;"
    "      while(ai<anc.length){"
    "        while(p&&!pomTerm(p,anc[ai]))p=p.parent;"
    "        if(!p){ok=false;break;}"
    "        ai++;p=p.parent;}"
    "      if(ok)out.push(n);}"
    "    return out;}"
    /* ---- D5b-2: ESTree -> POM CST nodes -------------------------------
       A POM node's source is parsed with the vendored acorn and mapped onto the
       kinds D0 reserved (block=3, stmt=4, expr=5), so children descend BELOW
       function granularity -- the residual D5a's bytecode scan cannot reach
       (locally-bound callees, method calls, per-site positions).

       SPANS ARE RELATIVE TO THE OWNING NODE'S `source`, not to a file. That is
       what composes with D4: harden() rewrites A NODE'S SOURCE and rebuilds via
       bindAt/replace, so offsets local to that node are exactly what
       rebuild-on-write consumes, and no absolute-offset plumbing is needed. Each
       view carries its own `src`, so a caller slices without guessing a base.

       ADDITIVE ON PURPOSE. D1's bytecode children/childCount contract is
       unchanged -- D1/D2 and the F/X ops are built on it. The CST hangs off
       cst(), whose own children descend fully. INCREMENT_D.md words D5b-2 as
       "node.children descends below function granularity"; doing that in place
       would redefine childCount under D1/D2, so it is offered beside them. */
    "  function cstFnv(str){var h=0x811c9dc5;"
    "    for(var i=0;i<str.length;i++){h^=str.charCodeAt(i)&0xff;"
    "      h=(h+((h<<1)+(h<<4)+(h<<7)+(h<<8)+(h<<24)))>>>0;}"
    "    return ('00000000'+h.toString(16)).slice(-8);}"
    /* ESTree type -> the reserved p_symbol kind. Unknown types still get a node
       (kind 5) rather than being dropped: a CST that silently omits a construct
       would let a hardening query miss a site, which is the failure that
       matters here. */
    /* Functions are checked BEFORE the Statement/Declaration suffix rule: a
       FunctionDeclaration ends in "Declaration", but the p_symbol schema
       reserves kind 2 for FUNCTION, and the bytecode tier already uses it.  If
       the CST called it a stmt instead, `query('function ...')` would match at
       one tier and silently return nothing at the other -- for the same
       function. Cross-tier agreement on `function` matters more than ESTree's
       (also true) claim that a function declaration is a statement; anything
       wanting the narrower reading has type(FunctionDeclaration). */
    "  function cstKind(t){"
    "    if(t==='Program'||t==='BlockStatement')return 3;"
    "    if(t==='FunctionDeclaration'||t==='FunctionExpression'"
    "       ||t==='ArrowFunctionExpression')return 2;"
    "    if(/(Statement|Declaration)$/.test(t))return 4;"
    "    return 5;}"
    /* The name a selector matches on: for a call it is the CALLEE (so
       call(fetch) matches obj.fetch() too), for a function/identifier its own
       name, else ''. */
    "  function cstName(n){"
    "    if(n.type==='CallExpression'||n.type==='NewExpression'){"
    "      var c=n.callee;"
    "      if(!c)return '';"
    "      if(c.type==='Identifier')return c.name;"
    "      if(c.type==='MemberExpression'&&c.property)"
    "        return c.property.name||String(c.property.value||'');"
    "      return '';}"
    "    if(n.type==='Identifier')return n.name;"
    "    if(n.id&&n.id.name)return n.id.name;"
    "    if(n.key&&(n.key.name||n.key.value!==undefined))"
    "      return n.key.name||String(n.key.value);"
    "    return '';}"
    /* Child ESTree nodes in SOURCE ORDER. Walks own enumerable properties rather
       than a per-type visitor table: a table goes stale the moment the parser
       learns new syntax, and a missed child is a missed hardening site. */
    "  function cstKids(n){var out=[];"
    "    for(var k in n){"
    "      if(k==='loc'||k==='range'||k==='start'||k==='end'||k==='type')continue;"
    "      var val=n[k];"
    "      if(val&&typeof val==='object'){"
    "        if(Array.isArray(val)){for(var i=0;i<val.length;i++){"
    "          if(val[i]&&typeof val[i].type==='string'&&val[i].range)"
    "            out.push(val[i]);}}"
    "        else if(typeof val.type==='string'&&val.range)out.push(val);}}"
    "    out.sort(function(a,b){return a.range[0]-b.range[0];});"
    "    return out;}"
    /* ANCHORS (FOUNDATION, "Inline binding -- anchors, not policy text").
       An anchor is an INERT MARKER NAMING A SITE -- "use comcon: checkout"; --
       a directive-prologue string, which is a no-op statement in plain JS.  The
       policy itself lives in a separate unit that references the anchor by name,
       so policy text is never trapped inside a string literal and can be
       swapped without touching the code it governs.  POM.md: anchors are
       queryable ATTRIBUTES of a node, not nodes of their own.

       Read off the directive prologue of whatever body this node owns, so both
       a function and its block report the anchors declared inside it -- a policy
       author names a function, not the block bracket inside it.

       The block-comment anchor form is NOT here: it needs comment trivia
       threaded through the parse and mapped to the following node.  Noted as
       remaining rather than half-done, because an anchor form that silently
       fails to register is worse than one that does not exist. */
    /* Reuse acorn's OWN prologue determination (`st.directive`, set only on
       real directive-prologue statements of a Program or function body) rather
       than re-deriving the rule here.  Re-deriving it got the parenthesized
       case wrong: `("use comcon: x");` is an ExpressionStatement wrapping a
       string Literal, so a hand-rolled type check accepts it, while the
       language does not treat it as a directive at all.  Same class of bug as
       [[bug-declarative-comment-terminator-escape]] -- a reviewer that lexes
       the source its own way eventually disagrees with the engine.

       Match the RAW directive text (`directive` is the source slice with
       escapes UNDECODED), never `expression.value`.  That one choice is what
       makes the name a human reads the name that binds: an escaped spelling
       such as "use comcon: check\u006fut" decodes to exactly `checkout`, but
       its raw form carries a backslash, which the name charset below excludes,
       so it registers nothing rather than silently binding under a spelling no
       reviewer would recognize.  The charset does that work on its own -- an
       explicit backslash test next to it looked like the enforcement and could
       not be made to fail, so it is not here.  Anything outside the charset is
       simply not an anchor, like any other non-matching directive. */
    "  function cstAnchorsIn(body){var out=[];"
    "    if(!body||!body.length)return out;"
    "    for(var i=0;i<body.length;i++){var st=body[i];"
    "      if(!st||typeof st.directive!=='string')break;"
    "      var m=/^use\\s+comcon:\\s*([A-Za-z0-9_$.-]+)$/.exec(st.directive);"
    "      if(m)out.push(m[1]);}"
    "    return out;}"
    "  function cstAnchors(n){"
    "    if(Array.isArray(n.body))return cstAnchorsIn(n.body);"
    "    if(n.body&&n.body.type==='BlockStatement')"
    "      return cstAnchorsIn(n.body.body);"
    "    return [];}"
    /* Parse a NODE'S source.  A fragment's source is `function(req){...}` -- a
       function EXPRESSION, not a Program -- so a plain parse fails at the
       anonymous `function` and we retry in expression mode.  NOT via a paren
       wrapper: that shifts every range by one, and ranges are the load-bearing
       output.  Guarded two ways so the retry cannot become a fail-open: the
       expression parse must consume the WHOLE input, and if it does not the
       ORIGINAL Program error is rethrown.  A half-parsed source must never
       present a CST covering only the part that parsed -- "no sites" for code
       the parser never read is the worst answer a hardening query can give.
       Shared by node.cst() and harden(), which re-parses its own output. */
    "  function cstParse(src){"
    "    try{return C.__parse(src);}catch(e){"
    "      var ex=C.__parse(src,true);"
    "      if(!ex||ex.range[1]!==src.length)throw e;"
    "      return ex;}}"
    "  function cstView(src,n,path,getParent,org){"
    "    var v={},r=n.range,slice=src.slice(r[0],r[1]);"
    "    v.kind=cstKind(n.type);v.type=n.type;v.name=cstName(n);"
    "    v.hash=cstFnv(slice);"
    "    v.range=[r[0],r[1]];"
    "    v.line0=n.loc?n.loc.start.line:0;v.line1=n.loc?n.loc.end.line:0;"
    /* D5b-4: a span SAYS WHICH BASE IT COUNTS IN.  These are NODE-LOCAL (line 1
       is the first line of this view's own source), while a bytecode-tier span
       is FILE-relative -- the same field name meaning two things is how a
       denial record ends up pointing at the wrong place.  base:'node' here,
       base:'file' there, and origin() converts. */
    "    v.span={base:'node',line0:v.line0,line1:v.line1,"
    "            col0:n.loc?n.loc.start.column:0,"
    "            col1:n.loc?n.loc.end.column:0,range:[r[0],r[1]]};"
    "    v.id=pomId(v.hash,path.join('.'));"
    "    v.src=src;"
    "    v.anchors=Object.freeze(cstAnchors(n));"
    /* origin(): this node's position in the FILE the view came from, or null if
       the view has no origin (plain text handed to comcon.cst with no opts).
       NULL IS THE POINT -- inventing a plausible absolute location for a node
       whose origin is unknown is the source-map lie, and a denial record that
       names the wrong file:line is worse than one that says it does not know.

       Only line 1 of the node's source starts at the origin's column; every
       later line starts at column 0 of its own line, so the column shift
       applies to line 1 alone.  An absolute `range` needs the node's byte
       OFFSET in the file, which the bytecode tier does not carry (pc2line maps
       lines, not offsets) -- so it is reported only when the origin supplies
       one, and is null otherwise rather than guessed. */
    "    v.origin=function(){"
    "      if(!org)return null;"
    "      var l0=org.line0+v.line0-1,l1=org.line0+v.line1-1;"
    "      var c0=(v.line0===1)?org.col0+v.span.col0:v.span.col0;"
    "      var c1=(v.line1===1)?org.col0+v.span.col1:v.span.col1;"
    "      var rg=(org.offset===null||org.offset===undefined)?null:"
    "             [org.offset+r[0],org.offset+r[1]];"
    "      return Object.freeze({base:'file',file:org.file,line0:l0,line1:l1,"
    "                            col0:c0,col1:c1,range:rg});};"
    "    v.binding={epoch:0,profile:'unbound'};"
    "    v.text=function(){return C.quote(slice);};"
    "    v.quote=function(){return C.quote(slice);};"
    "    v.query=function(sel){return pomQuery(v,sel);};"
    /* Unlike D5a's bytecode scan these see LOCALLY-BOUND callees and method
       calls, which is what D5b-2 exists for. Read (class R); enforcement stays
       the capability kernel's. */
    "    v.references=function(nm){"
    "      return v.query('* name('+String(nm)+')').map(function(x){"
    "        return {name:x.name,line:x.line0,"
    "                call:x.type==='CallExpression',range:x.range};});};"
    "    v.callsites=function(nm){"
    "      return v.query('call('+String(nm)+')').map(function(x){"
    "        return {name:x.name,line:x.line0,call:true,range:x.range};});};"
    "    v.describe=function(){return {kind:v.kind,type:v.type,ops:["
    "      {name:'text',op:'read',cls:'R'},{name:'quote',op:'read',cls:'R'},"
    "      {name:'query',op:'read',cls:'R'},"
    "      {name:'references',op:'read',cls:'R'},"
    "      {name:'callsites',op:'read',cls:'R'},"
    "      {name:'describe',op:'read',cls:'R'},"
    "      {name:'children',op:'read',cls:'R'},"
    "      {name:'parent',op:'read',cls:'R'}]};};"
    "    var kids=cstKids(n);"
    "    v.childCount=kids.length;"
    "    Object.defineProperty(v,'parent',{enumerable:true,get:getParent});"
    "    Object.defineProperty(v,'children',{enumerable:true,get:function(){"
    "      var a=[];for(var i=0;i<kids.length;i++){"
    "        (function(j){a.push(cstView(src,kids[j],path.concat([j]),"
    "          function(){return v;},org));})(i);}"
    "      return a;}});"
    "    return Object.freeze(v);}"
    "  function pomView(rootFn,path){"
    "    var raw=C.__pomNodeAt(rootFn,path);"
    "    if(raw===undefined)return null;"
    "    var ps=path.join('.'),v={};"
    "    v.kind=raw.kind;v.name=raw.name;v.hash=raw.hash;"
    "    v.line0=raw.line0;v.line1=raw.line1;v.childCount=raw.childCount;"
    /* FILE-relative (find_line_num walks pc2line), so the span carries the file
       and says base:'file'. col1 is NOT known at this tier -- line1 is derived
       by counting newlines in the source slice, which says nothing about where
       the last line ends -- so it is null, not 0. */
    "    v.file=raw.file;"
    "    v.span={base:'file',file:raw.file,line0:raw.line0,line1:raw.line1,"
    "            col0:raw.col0,col1:null};"
    "    v.id=pomId(raw.hash,ps);"
    "    v.binding={epoch:0,profile:'unbound'};"
    "    v.text=function(){return C.quote(raw.source);};"
    "    v.quote=function(){return C.quote(raw.source);};"
    "    v.query=function(sel){return pomQuery(v,sel);};"
    /* D5b-2: the CST view of THIS node's source. Parsed on demand -- most nodes
       are never hardened, and the parser itself loads lazily. Throws on a source
       that will not parse (FAIL CLOSED: an unparseable node must not present an
       empty CST, or a hardening query would report "no sites" for code it never
       managed to read). */
    "    v.cst=function(){"
    "      return cstView(raw.source,cstParse(raw.source),"
    "                     path.concat(['cst']),function(){return v;},"
    "                     {file:raw.file,line0:raw.line0,col0:raw.col0,"
    "                      offset:null});};"
    /* references(name)/callsites(name): D5a call-site audit — every reference to
       a free name or method `name` in this fragment (whole subtree), with line
       numbers; callsites = the references that are the callee of a call. Read
       (class R); the ENFORCEMENT side is the capability kernel (mediate a grant).
       Locally-bound callees need the CST (D5b). */
    "    v.references=function(nm){"
    "      return C.__pomCallsites(rootFn,String(nm))||[];};"
    "    v.callsites=function(nm){"
    "      return (C.__pomCallsites(rootFn,String(nm))||[])"
    "        .filter(function(r){return r.call;});};"
    "    v.describe=function(){return {kind:v.kind,ops:["
    "      {name:'text',op:'read',cls:'R'},{name:'quote',op:'read',cls:'R'},"
    "      {name:'query',op:'read',cls:'R'},"
    "      {name:'references',op:'read',cls:'R'},"
    "      {name:'callsites',op:'read',cls:'R'},"
    "      {name:'describe',op:'read',cls:'R'},"
    "      {name:'children',op:'read',cls:'R'},"
    "      {name:'parent',op:'read',cls:'R'}]};};"
    "    Object.defineProperty(v,'parent',{enumerable:true,get:function(){"
    "      return path.length?pomView(rootFn,path.slice(0,-1)):null;}});"
    "    Object.defineProperty(v,'children',{enumerable:true,get:function(){"
    "      var a=[];for(var i=0;i<raw.childCount;i++)"
    "        a.push(pomView(rootFn,path.concat([i])));return a;}});"
    "    return Object.freeze(v);};"
    /* A confined fragment's `bound` wrapper is NOT the fragment.  Handing it to
       pom() used to describe the WRAPPER -- a two-line closure in
       <comcon-bootstrap> -- and answer every query about it: a wrong answer that
       looks exactly like a right one (it has a kind, a hash, a source, children).
       The fragment itself lives in the other compartment, so refuse and say what
       to do instead.  The caller already holds the source it passed to
       include(), and comcon.cst(source, {file:'<comcon-fragment>'}) gives the
       fragment's own tree with its synthetic origin (POM.md §6 Q3). */
    /* aotStatus(fragment) -- increment D4c: the TIER a live fragment runs on.
       Takes the bound wrapper include() returned (the thing an operator
       actually holds) and reports {jit, functions, compiled}: `compiled` is the
       only field that means native code exists, and 0 means the BYTECODE
       FALLBACK is what runs.  Read-only (class R): asking never compiles.

       Why an operator needs it: a live rewrite's new epoch is compiled in a
       WORKER, post-fork, where there is no gcc thread -- so it runs bytecode,
       correctly but interpreted, until a process with a compiler lowers it.
       Before this, nothing could distinguish that from a re-AOT'd epoch. */
    "  C.aotStatus=function(frag){"
    "    if(!frag||frag.confined!==true||typeof frag.handle!=='number')"
    "      throw new TypeError('aotStatus: arg0 must be a confined fragment "
                 "(the value comcon.include returned)');"
    "    return C.__aotStatus(frag.handle);};"
    /* ================= M-LIB: the standard policy library =================
       ROADMAP M-LIB: "the user-facing surface is not the kernel but the
       combinators."  Everything above is the kernel: 20 operators, correct and
       unusable by anyone who does not already know which four of them have to
       agree.  A PROFILE is a named bundle of contract fields for one use case,
       so the agreement is made once, here, instead of at every call site.

       ONE RULE GOVERNS WHAT MAY GO IN A PROFILE: only fields the kernel
       actually ENFORCES.  MANUAL.md is written as-if-shipped against
       `{profile:"restrictive", onViolation:"audit"}` and `std.postures.lockdown`,
       and those are NOT here, because nothing reads them -- realize() knows only
       profile:'declarative'.  A posture that sets ignored keys would read like a
       policy and do nothing, which is worse than its absence: it would be
       believed.  std.describe() lists, per field, what enforces it.

       IMPORTS ARE DERIVED FROM THE ENV, never written twice.  Writing both is
       the library's main reason to exist: omit a granted name from imports and
       admission refuses the fragment (fail-closed but baffling); list a name
       that is not granted and the fragment sees undefined at runtime.  The env
       already knows the answer. */
    "  var STD={version:'comcon-std-1'};"
    "  STD.profiles={};"
    /* tenant(env, opts): an untrusted fragment.  Admission ON (imports present),
       request-field checks ON, and BOUNDED BY DEFAULT -- the audit's §3 gap list
       names "host JS unbounded by default" as accepted residual risk, so a
       tenant profile that inherited only the 5s fragment ceiling would be
       repeating it on purpose. 100ms is a default, not a finding: override it. */
    "  STD.profiles.tenant=function(env,opts){"
    "    if(!env||!env[ENV])throw new TypeError("
    "      'std.profiles.tenant: arg0 must be a comcon.env()');"
    "    opts=opts||{};"
    "    var names=Object.keys(env.grants);"
    "    var c={grants:env.grants,imports:names,checkRequest:true,"
    "           meter:opts.meter||C.meter({timeoutMs:100})};"
    "    if(opts.identity!==undefined)c.identity=opts.identity;"
    "    if(opts.tests!==undefined)c.tests=opts.tests;"
    "    if(opts.deps!==undefined)c.deps=opts.deps;"
    /* opts.intrinsics NARROWS the C3 allowance (it can only remove). Passed
       through rather than defaulted: making a profile stricter than it shipped
       would change behaviour for anyone already using it. */
    "    if(opts.intrinsics!==undefined)c.intrinsics=opts.intrinsics;"
    "    return Object.freeze(c);};"
    /* pure_library(opts): a cap-free computation.  grants {} and imports [] --
       imports is PRESENT, which is what switches admission on, so any free name
       at all refuses the fragment.  The strongest profile that is fully enforced
       today, and the right default for third-party code that should only
       compute. */
    "  STD.profiles.pure_library=function(opts){"
    "    opts=opts||{};"
    "    var c={grants:{},imports:[],checkRequest:false,"
    "           meter:opts.meter||C.meter({timeoutMs:50})};"
    "    if(opts.identity!==undefined)c.identity=opts.identity;"
    "    if(opts.tests!==undefined)c.tests=opts.tests;"
    "    if(opts.deps!==undefined)c.deps=opts.deps;"
    /* `pure_library({intrinsics: []})` is the strictest contract expressible:
       no free names at all, not even language values. NOT the default, because
       this profile shipped with the full allowance and tightening it silently
       would break fragments that already compute with JSON. */
    "    if(opts.intrinsics!==undefined)c.intrinsics=opts.intrinsics;"
    "    return Object.freeze(c);};"
    /* describe(): the library's own honesty surface -- per contract field, WHAT
       ENFORCES IT.  So a reader can tell a field that bites from a field that is
       decoration, without reading the C.  `absent` rows are the vocabulary
       ROADMAP lists that is deliberately not shipped. */
    "  STD.describe=function(){return {version:STD.version,"
    "    profiles:Object.keys(STD.profiles),"
    "    enforced:["
    "      {field:'imports',by:'admit free-name gate (C3)',effect:'refuse'},"
    "      {field:'intrinsics',by:'admit free-name gate (C3), narrowing only',"
    "       effect:'refuse'},"
    "      {field:'grants',by:'include: caps re-wrapped compartment-native',"
    "       effect:'authority'},"
    "      {field:'checkRequest',by:'admit request-field predicate',"
    "       effect:'refuse'},"
    "      {field:'meter',by:'worker request deadline',effect:'interrupt'},"
    "      {field:'identity',by:'C4 artifact pin (sha256)',effect:'refuse'},"
    "      {field:'tests',by:'admit test phase, run in the compartment',"
    "       effect:'refuse'},"
    "      {field:'deps',by:'pinned dep eval, bound as closure params',"
    "       effect:'authority'}],"
    "    absent:["
    "      {name:'postures / onViolation',why:'nothing reads them yet -- a "
             "posture of ignored keys would be believed'},"
    "      {name:'allowHosts / uses / ttl / window / cosign / protocol',"
    "       why:'needs C-side enforcement; the mediation vocabulary is closed "
             "at revoke/redact/allow/routes'},"
    "      {name:'std.ops',why:'the comconctl verbs; not started'}]};};"
    /* ---------------- std.ops: administration as library code ----------------
       FOUNDATION §8a: THERE IS NO MANAGEMENT PLANE.  comconctl is a shell, not a
       tool; every verb is an ordinary library program over the four kernel
       operators plus the OPS-RESOURCE CAPABILITIES, so v2 §9.3's "the tooling
       never needs a backdoor" is derived rather than asserted.

       Which means a session TAKES ITS RESOURCES AS ARGUMENTS and reaches for no
       ambient authority: `std.ops({log: nginx.tenantDenials, mode: comcon.mode})`.
       A verb whose resource was not passed is ABSENT FROM THE SESSION, not
       present-and-throwing, so "what can this session do" is answerable by
       Object.keys() instead of by reading the implementation. A session given
       nothing has no verbs -- that is the no-backdoor property, visible.

       THE RESOURCE LIST IS THE THIRD CLOSED ENUMERATION (§8a, ROADMAP §12 V7:
       enumerations are generated, never maintained).  All seven are named here,
       including the two with no host spelling yet -- `host:null` is what makes
       the gap checkable instead of invisible, and the verbs needing them are
       reported as withheld with the reason. describe() walks the SAME table the
       session is built from, so the list and the reality cannot drift. */
    "  var OPS_RES={"
    "    log:{doc:'denial/observation log',host:'nginx.tenantDenials()'},"
    "    learn:{doc:'learning recorder',host:'nginx.tenantLearning()'},"
    "    mode:{doc:'audit/enforce/learn switch',host:'comcon.mode()'},"
    "    bindings:{doc:'binding/epoch store',"
    "              host:'the session\\'s own record of bindAt handles'},"
    "    snapshot:{doc:'snapshot store',"
    "              host:'the binding store\\'s quotation record "
                        "(snapshot = quote)'},"
    "    broadcast:{doc:'class-F broadcast channel',"
    "               host:'nginx.shared (via bindShared)'},"
    "    provenance:{doc:'grant-chain registry',host:null},"
    "    signing:{doc:'signing key',host:null},"
    /* The ninth (v5.65, TM-2 / FOUNDATION §8b): the session registry. It holds
       DESCRIPTORS, never environments -- so it carries no authority, which is
       what lets it live in nginx.shared and be fleet-wide. A session without
       this resource has no grant/revoke verbs at all. */
    "    sessions:{doc:'session registry (identity -> attenuation)',"
    "              host:'nginx.shared (comcon.session:*)'}};"
    "  STD.ops=function(res){"
    "    res=res||{};"
    "    var have={},k;"
    "    for(k in OPS_RES)if(Object.prototype.hasOwnProperty.call(OPS_RES,k))"
    "      have[k]=(res[k]!==undefined&&res[k]!==null&&res[k]!==false);"
    /* The snapshot store is not a separate host object: it IS the binding
       store's quotation record, so holding one is holding the other.  Written
       as an implication rather than by merging the two names, because §8a
       enumerates them separately and the enumeration is the checkable thing. */
    "    if(have.bindings)have.snapshot=true;"
    /* The session's own binding/epoch store. It holds bindAt handles the
       operator registered, plus the quotation each epoch was built from --
       which IS the snapshot store: "snapshot = quote" (§8a). */
    "    var reg={},names=[];"
    "    function need(n){var b=reg[n];"
    "      if(!b)throw new TypeError('ops: no binding named '+n);return b;}"
    "    var V=[];"
    "    function verb(name,needs,fn){V.push({name:name,needs:needs,fn:fn});}"

    "    verb('denials','log',function(){"
    "      C.__modeReconcile();"
    "      var d=res.log();return {mode:d.mode,total:d.total,byOp:d.byOp};});"
    "    verb('learn','learn',function(){return res.learn();});"
    /* shadow/enforce are the audit-first rollout, and they are REAL here because
       comcon.mode() is real: audit observes and records, enforce denies. */
    /* Each returns the EFFECTIVE mode, read back rather than assumed -- and if
       the session also holds the log cap, read back from the reporter, which is
       a different path from the setter.  comcon.mode() used to be silently inert
       at request time; a verb that reports its own argument back would have said
       'enforce' throughout. */
    "    function setMode(m){var got=res.mode(m);"
    "      if(have.log){var d=res.log();if(d&&d.mode)got=d.mode;}"
    "      return got;}"
    "    verb('shadow','mode',function(){return setMode('audit');});"
    "    verb('enforce','mode',function(){return setMode('enforce');});"
    "    verb('learnMode','mode',function(){return setMode('learn');});"

    "    verb('register','bindings',function(name,handle,quotation){"
    "      if(typeof name!=='string'||!name)throw new TypeError("
    "        'ops.register: arg0 must be a name');"
    "      if(!handle||typeof handle.replace!=='function')throw new TypeError("
    "        'ops.register: arg1 must be a comcon.bindAt() handle');"
    "      if(!quotation||!quotation[QUOTE])throw new TypeError("
    "        'ops.register: arg2 must be the quotation it was bound from');"
    "      if(reg[name])throw new TypeError('ops.register: duplicate '+name);"
    "      reg[name]={h:handle,q:quotation,hist:[quotation]};"
    "      names.push(name);return name;});"
    "    verb('bindings','bindings',function(){"
    "      return names.map(function(n){var b=reg[n];return {name:n,"
    "        epoch:b.h.epoch(),tombstoned:b.h.tombstoned(),"
    "        snapshots:b.hist.length};});});"
    /* snapshot = quote: the quotation the live epoch was built from. Inert,
       cap-free, and exactly what rollback/rebind consume. */
    "    verb('snapshot','snapshot',function(name){return need(name).q;});"
    "    verb('rebind','bindings',function(name,q){var b=need(name);"
    "      if(!q||!q[QUOTE])throw new TypeError("
    "        'ops.rebind: arg1 must be a comcon.quote() description');"
    "      var e=b.h.replace(q);b.q=q;b.hist.push(q);return e;});"
    "    verb('rollback','bindings',function(name){var b=need(name);"
    "      var e=b.h.rollback();"
    "      if(b.hist.length>1){b.hist.pop();b.q=b.hist[b.hist.length-1];}"
    "      return e;});"
    /* rewrite = harden + rebind: SHOWCASE §38 as one operator action. harden()
       confers nothing (text -> text), so it needs no capability of its own; the
       authority is in the rebind, which needs the binding store. */
    "    verb('rewrite','bindings',function(name,query,wrapper){"
    "      var b=need(name);"
    "      var rep=C.harden(C.cst(b.q.source),query,wrapper);"
    "      if(rep.count===0)return {count:0,epoch:b.h.epoch()};"
    "      var e=b.h.replace(rep.quotation);"
    "      b.q=rep.quotation;b.hist.push(rep.quotation);"
    "      return {count:rep.count,epoch:e,sites:rep.sites};});"
    /* remove is class X (POM.md §3): guarded by SNAPSHOT-FIRST plus a
       confirmation that NAMES the binding -- a blind {confirm:true} pasted from
       another call cannot remove the wrong one. */
    "    verb('remove','bindings',function(name,opts){var b=need(name);"
    "      if(!opts||opts.confirm!==name)throw new TypeError("
    "        'ops.remove is class X (irreversible): pass {confirm:\"'+name+"
    "        '\"} to say which binding you mean');"
    "      b.hist.push(b.q);return b.h.remove();});"
    "    verb('revive','bindings',function(name){return need(name).h.revive();});"

    /* trustReport: for each registered binding, what is actually holding -- the
       epoch, whether it is bounded, and which contract fields bite. Over the
       binding store; the operator-session half of §8a's trust-report needs the
       provenance registry, which does not exist (see withheld). */
    "    verb('trustReport','bindings',function(){"
    "      return {bindings:names.map(function(n){var b=reg[n];return {"
    "        name:n,epoch:b.h.epoch(),tombstoned:b.h.tombstoned(),"
    "        ops:b.h.describe().ops.map(function(o){"
    "          return o.name+':'+o.cls;})};}),"
    "        enforcedBy:STD.describe().enforced};});"

    "    var sess={},withheld=[],avail=[];"
    "    for(var i=0;i<V.length;i++){"
    "      if(have[V[i].needs]){sess[V[i].name]=V[i].fn;avail.push(V[i].name);}"
    "      else withheld.push({verb:V[i].name,needs:V[i].needs,"
    "        host:OPS_RES[V[i].needs]?OPS_RES[V[i].needs].host:null});}"
    "    sess.describe=function(){"
    "      var rs=[];for(var k2 in OPS_RES)"
    "        if(Object.prototype.hasOwnProperty.call(OPS_RES,k2))"
    "          rs.push({name:k2,doc:OPS_RES[k2].doc,host:OPS_RES[k2].host,"
    "                   held:!!have[k2]});"
    "      return {resources:rs,verbs:avail.slice(),withheld:withheld.slice(),"
    "        absent:["
    "          {verb:'revoke',needs:'provenance',"
    "           why:'no grant-chain registry: revocation walks chains'},"
    "          {verb:'cosign / office-hours',needs:'signing',"
    "           why:'no signing key capability'},"
    "          {verb:'propose',needs:'learn+heuristics',"
    "           why:'the harvest is here (learn); turning it into a quotation "
                   "is not'},"
    "          {verb:'diff / docs',needs:'describe cap',"
    "           why:'nginx.describe() is not yet handed out as a capability'}]};};"
    "    return Object.freeze(sess);};"
    /* ================= M-CFG: the config instance =========================
       ROADMAP M-CFG's deliverable -- "one tenant subtree onboarded through admit
       end to end" -- and the last named one of increment E. Config fragments are
       sentences of a restricted grammar, admitted like code: reviewed by the
       sound rejecter, typed against the registry, pinned by hash, applied by the
       OPERATOR.

       THE PROPOSAL NEVER EXECUTES. That is the whole design, and it is also what
       makes it implementable: a COM capability cannot cross into a compartment
       (only C-wrapped sockets and server facets do), so a config fragment could
       not be handed the tree even if we wanted to. Instead D5b-1's
       reviewDeclarative reduces the source to a DESCRIPTOR TABLE -- inert data,
       diffable -- and the operator applies the table with its own authority.
       "Tenant proposes what it cannot apply; the operator realizes" (FOUNDATION
       §2a) is then a property of the mechanism rather than a convention.

       REFUSAL IS BY SAFETY CLASS, NOT BY A BLOCKLIST. Every member carries its
       type and class in the describe() registry, so `handler` (class guarded,
       "rewires nginx dispatch") is refused because of what it IS, and a COM
       member added next year is classified the day it is added. A blocklist here
       would be a list that rots -- the failure mode V7 exists to prevent.

       Review is a pure function of (source, policy, registry): describeType()
       needs no live object, so a proposal can be reviewed before anything is
       touched. */
    "  STD.config={};"
    "  function cfgType(t){var m=/^handle<([A-Za-z_$][A-Za-z0-9_$]*)>$/.exec(t);"
    "    return m?m[1]:null;}"
    "  function cfgMember(typeName,name){"
    "    var rows=nginx.describeType(typeName);"
    "    if(!rows)return null;"
    "    for(var i=0;i<rows.length;i++)if(rows[i].name===name)return rows[i];"
    "    return null;}"
    "  function cfgArgOk(decl,v){"
    "    if(decl==='string')return typeof v==='string';"
    "    if(decl==='number')return typeof v==='number';"
    "    if(decl==='boolean')return typeof v==='boolean';"
    "    return false;}"
    /* Walk a dotted path through the type registry. Intermediate segments must
       be TRAVERSABLE (a handle<X>); only the last is assigned, and it must be
       read-write. A read-only handle is exactly right in the middle and exactly
       wrong at the end, which is why the position matters and not just the
       flag. */
    "  function cfgResolve(rootType,segs){"
    "    var t=rootType,i,row;"
    "    for(i=0;i<segs.length;i++){"
    "      row=cfgMember(t,segs[i]);"
    "      if(!row)return {err:'no such member: '+segs.slice(0,i+1).join('.')"
    "        +' on '+t};"
    "      if(i<segs.length-1){"
    "        var nt=cfgType(row.type);"
    "        if(!nt)return {err:'not traversable: '+segs.slice(0,i+1).join('.')"
    "          +' is '+row.type};"
    "        t=nt;continue;}"
    "      return {row:row,owner:t};}"
    "    return {err:'empty path'};}"
    "  STD.config.review=function(source,policy){"
    "    policy=policy||{};"
    "    var rootType=policy.type,rootName=policy.root;"
    "    if(typeof rootType!=='string'||typeof rootName!=='string')"
    "      throw new TypeError("
    "        'std.config.review: policy needs {type, root}');"
    "    var allow=policy.allow||['*'];"
    "    var okClasses=policy.allowClass||['safe'];"
    "    var plan={hash:String(cstFnv(String(source))),root:rootName,"
    "              type:rootType,ops:[],refused:[]};"
    /* The sound rejecter runs FIRST and its refusal is the whole answer: what it
       accepts is exactly what reduces to the table, so anything it rejects never
       becomes an op to argue about. */
    "    var table;"
    "    try{table=C.reviewDeclarative(String(source));}"
    "    catch(e){plan.ok=false;plan.rejected=String(e.message);"
    "      return Object.freeze(plan);}"
    "    plan.table=table;"
    "    for(var si=0;si<table.statements.length;si++){"
    "      var chain=table.statements[si];"
    "      for(var ci=0;ci<chain.length;ci++){"
    "        var st=chain[ci],segs=String(st.op).split('.');"
    "        var op={path:st.op,args:st.args};"
    "        if(segs.shift()!==rootName){"
    "          op.verdict='refused';"
    "          op.why='outside the subtree: expected '+rootName+'.*';"
    "          plan.refused.push(op);continue;}"
    "        var rel=segs.join('.');"
    "        var allowed=false;"
    "        for(var ai=0;ai<allow.length;ai++)"
    "          if(pomName(rel,allow[ai])){allowed=true;break;}"
    "        if(!allowed){op.verdict='refused';"
    "          op.why='not in the policy allow-list: '+rel;"
    "          plan.refused.push(op);continue;}"
    "        var r=cfgResolve(rootType,segs);"
    "        if(r.err){op.verdict='refused';op.why=r.err;"
    "          plan.refused.push(op);continue;}"
    "        op.member=rel;op.type=r.row.type;op.cls=r.row['class'];"
    "        op.reversible=!!r.row.reversible;"
    "        if(r.row.access!=='read-write'){op.verdict='refused';"
    "          op.why='not writable: '+rel+' is '+r.row.access+' ('+op.cls+')';"
    "          plan.refused.push(op);continue;}"
    "        if(!st.args||st.args.length!==1){op.verdict='refused';"
    "          op.why='expected exactly one argument for '+rel;"
    "          plan.refused.push(op);continue;}"
    "        if(!cfgArgOk(r.row.type,st.args[0])){op.verdict='refused';"
    "          op.why='type: '+rel+' is '+r.row.type+', got '+typeof st.args[0];"
    "          plan.refused.push(op);continue;}"
    "        op.value=st.args[0];"
    /* The class decides the gate. `safe` applies; anything else needs the
       operator to name it at apply time, which is COM class-3 semantics
       (POM.md §3 class X: snapshot-first, explicit confirmation, or reject). */
    "        var clsOk=false;"
    "        for(var ki=0;ki<okClasses.length;ki++)"
    "          if(okClasses[ki]===op.cls){clsOk=true;break;}"
    "        op.verdict=clsOk?'ok':'confirm';"
    "        if(!clsOk)op.why='class '+op.cls+': needs explicit confirmation';"
    "        plan.ops.push(op);}}"
    "    plan.ok=(plan.refused.length===0);"
    "    Object.freeze(plan.ops);Object.freeze(plan.refused);"
    "    return Object.freeze(plan);};"
    /* diff(plan, target): AUDIT-FIRST. What would change, read from the live
       subtree, with nothing applied -- the same shape the rollout verbs use for
       code (observe, then enforce). */
    "  function cfgWalk(target,segs){"
    "    var o=target,i;"
    "    for(i=0;i<segs.length-1;i++){o=o[segs[i]];if(o===undefined)return null;}"
    "    return o;}"
    "  STD.config.diff=function(plan,target){"
    "    if(!plan||!plan.ops)throw new TypeError('std.config.diff: arg0 must be a plan');"
    "    return plan.ops.map(function(op){"
    "      var segs=op.member.split('.'),owner=cfgWalk(target,segs);"
    "      var cur=owner?owner[segs[segs.length-1]]:undefined;"
    "      return {path:op.path,from:cur,to:op.value,"
    "              changes:(cur!==op.value),verdict:op.verdict};});};"
    /* apply(plan, target, opts): the operator's authority, not the tenant's.
       Refuses a plan with ANY refused op unless opts.partial, requires every
       'confirm' op to be named in opts.confirm, and snapshots before writing so
       rollback is exact. An irreversible member cannot be applied at all here --
       there would be nothing to roll back to. */
    "  STD.config.apply=function(plan,target,opts){"
    "    opts=opts||{};"
    "    if(!plan||!plan.ops)throw new TypeError('std.config.apply: arg0 must be a plan');"
    "    if(!target||typeof target!=='object')throw new TypeError("
    "      'std.config.apply: arg1 must be the subtree to apply to');"
    "    if(!plan.ok&&!opts.partial)throw new Error("
    "      'std.config.apply: the plan has '+plan.refused.length+' refused op(s);"
                 " pass {partial:true} to apply the rest deliberately');"
    "    var confirm=opts.confirm||[],applied=[],snap=[];"
    /* Every gate is checked BEFORE anything is written, so a refusal cannot
       leave the subtree half-configured. */
    "    for(var i=0;i<plan.ops.length;i++){"
    "      var op=plan.ops[i];"
    "      if(op.verdict==='confirm'){"
    "        var named=false,ci;"
    "        for(ci=0;ci<confirm.length;ci++)if(confirm[ci]===op.path)named=true;"
    "        if(!named)throw new Error('std.config.apply: '+op.path+' is class '"
    "          +op.cls+'; name it in {confirm:[...]} to apply it');}"
    "      if(!op.reversible)throw new Error('std.config.apply: '+op.path+"
    "        ' is irreversible; there would be nothing to roll back to');"
    "      if(!cfgWalk(target,op.member.split('.')))throw new Error("
    "        'std.config.apply: cannot reach '+op.member);}"
    /* ALL OR NOTHING. A COM setter can refuse a value the registry considered
       well-typed -- `proxy.pass` wants an upstream that EXISTS, and the type
       system cannot know that -- so the write loop can fail halfway through.
       Found by composing this end to end: `root` applied, `proxy.pass` threw,
       and the throw discarded the very snapshot the caller needed to undo it,
       leaving a live subtree half-configured with no way back. Now a failure
       restores what was already written, in reverse, and reports both. */
    "    var failed=null;"
    "    try{"
    "      for(var j=0;j<plan.ops.length;j++){"
    "        var o2=plan.ops[j],sg=o2.member.split('.');"
    "        var own=cfgWalk(target,sg),lst=sg[sg.length-1];"
    "        var was=own[lst];"
    "        failed=o2.path;"
    /* Snapshot only AFTER the write succeeds: an entry for a write that threw
       would be rolled back too, and the counts would describe something that
       never happened ("rolled back 2 of 1"). */
    "        own[lst]=o2.value;"
    "        snap.push({member:o2.member,was:was});"
    "        applied.push(o2.path);failed=null;}"
    "    }catch(e){"
    "      var undone=0;"
    "      for(var k=snap.length-1;k>=0;k--){"
    "        var sk=snap[k],sgk=sk.member.split('.');"
    "        var ok2=cfgWalk(target,sgk);"
    "        if(ok2){try{ok2[sgk[sgk.length-1]]=sk.was;undone++;}catch(e2){}}}"
    "      throw new Error('std.config.apply: '+failed+"
    "        ' was refused by the COM setter ('+e.message+'); rolled back '+"
    "        undone+' of '+snap.length+' write(s) -- the subtree is as it "
                 "was');}"
    "    return Object.freeze({applied:applied,hash:plan.hash,"
    "                          snapshot:Object.freeze(snap)});};"
    "  STD.config.rollback=function(result,target){"
    "    if(!result||!result.snapshot)throw new TypeError("
    "      'std.config.rollback: arg0 must be an apply() result');"
    "    var n=0;"
    "    for(var i=result.snapshot.length-1;i>=0;i--){"
    "      var s=result.snapshot[i],segs=s.member.split('.');"
    "      var owner=cfgWalk(target,segs);"
    "      if(owner){owner[segs[segs.length-1]]=s.was;n++;}}"
    "    return n;};"
    "  Object.freeze(STD.config);"
    /* ---------------------------------------------------------------
       std.sessions(res) — TM-2 / FOUNDATION §8b: identity -> environment.

       THE HOST AUTHENTICATES; THIS MAPS. Nothing here validates a principal:
       the caller asserts one (an mTLS subject, a verified JWT, a peer
       credential) and that assertion IS the trust transfer. Passing a
       client-supplied string here hands the client the session.

       The registry stores a cap-free DESCRIPTOR per principal -- never an env,
       never a capability -- so (a) stealing the table yields nothing, and
       (b) it can live in nginx.shared, which makes it FLEET-WIDE. The mode
       switch shipped per-process and put four workers in mixed modes (v5.56);
       a session table with that bug would authenticate on one worker and not
       on the next.

       resolve(principal, env) ATTENUATES THE CALLER'S OWN env. The registry
       cannot hand out authority the resolver did not hold, so a session env is
       <= the env of whoever resolved it -- monotonicity at the identity
       boundary, inherited rather than re-argued. An unknown or expired
       principal resolves to the EMPTY env, the same answer an undeclared free
       name gets. --------------------------------------------------------- */
    "  var SESS_PREFIX='comcon.session:';"
    "  STD.sessions=function(res){"
    "    res=res||{};"
    "    var store=res.sessions||null;"
    "    var S={};"
    "    function key(p){"
    "      if(typeof p!=='string'||!p)throw new TypeError("
    "        'std.sessions: principal must be a non-empty string');"
    /* the shared store's key is 128 bytes; refuse rather than truncate, because
       two principals that truncate to the same key are one principal. */
    "      if(SESS_PREFIX.length+p.length>=120)throw new TypeError("
    "        'std.sessions: principal too long (max '+(120-SESS_PREFIX.length)+"
    "        ' chars); truncating would merge two principals into one');"
    "      return SESS_PREFIX+p;}"
    "    S.describe=function(){"
    "      return Object.freeze({resource:'sessions',"
    "        held:!!store,"
    "        verbs:Object.keys(S).sort(),"
    "        stores:'descriptors only -- no capability is ever written here',"
    "        authenticates:false,"
    "        note:'the HOST asserts the principal; this maps it to an "
                 "attenuation of the env passed to resolve()'});};"
    "    if(!store)return Object.freeze(S);"
    "    S.grant=function(principal,descriptor){"
    "      var d=descriptor||{};"
    "      var rec={imports:[],routes:null};"
    "      if(d.imports!==undefined){"
    "        if(!d.imports||typeof d.imports.length!=='number')"
    "          throw new TypeError('std.sessions.grant: imports must be an "
                 "array of names');"
    "        for(var i=0;i<d.imports.length;i++)rec.imports.push(String("
    "          d.imports[i]));}"
    "      if(d.routes!==undefined&&d.routes!==null)rec.routes=String(d.routes);"
    /* A descriptor is DATA: anything function-shaped is a capability trying to
       cross, and would be silently dropped by JSON.stringify -- so refuse it
       where the caller can still see the mistake. */
    "      for(var k in d)if(Object.prototype.hasOwnProperty.call(d,k)){"
    "        if(typeof d[k]==='function')throw new TypeError("
    "          'std.sessions.grant: descriptor field '+k+' is a function; the "
               "registry holds DATA, never capabilities');}"
    "      var ttl=(d.ttl===undefined||d.ttl===null)?0:Number(d.ttl);"
    "      if(!(ttl>=0))throw new TypeError('std.sessions.grant: ttl must be "
                 "a non-negative number of seconds');"
    "      var json=JSON.stringify(rec);"
    "      if(ttl>0)store.set(key(principal),json,ttl);"
    "      else store.set(key(principal),json);"
    "      return Object.freeze({principal:principal,descriptor:"
    "        Object.freeze(rec),ttl:ttl});};"
    "    S.revoke=function(principal){"
    "      return store.delete(key(principal));};"
    "    S.lookup=function(principal){"
    "      var v=store.get(key(principal));"
    "      if(v===undefined)return null;"
    "      var rec;try{rec=JSON.parse(v);}catch(e){return null;}"
    "      var ttl=store.ttl(key(principal));"
    "      return Object.freeze({principal:principal,descriptor:"
    "        Object.freeze(rec),ttl:(ttl===null?0:ttl)});};"
    "    S.list=function(){"
    "      var out=[],ks=store.keys(),i;"
    "      for(i=0;i<ks.length;i++){"
    "        if(ks[i].indexOf(SESS_PREFIX)===0)"
    "          out.push(ks[i].slice(SESS_PREFIX.length));}"
    "      return out.sort();};"
    /* resolve(principal, env): the whole point. Deny-by-default, narrowing
       only, and the narrowing is REFUSED rather than trimmed when it names
       something the base env does not hold -- a mapping that quietly grants
       less than it says is a mapping nobody can audit. */
    "    S.resolve=function(principal,env){"
    "      if(!env||!env[ENV])throw new TypeError("
    "        'std.sessions.resolve: arg1 must be the comcon.env() to attenuate "
             "-- the registry holds no authority of its own');"
    "      var found=S.lookup(principal);"
    "      var out=C.env();"
    "      if(!found)return Object.freeze({principal:principal,env:out,"
    "        granted:[],reason:'no mapping (or the lease expired)'});"
    "      var want=found.descriptor.imports,i,n,cap;"
    "      for(i=0;i<want.length;i++){"
    "        n=want[i];"
    "        if(!Object.prototype.hasOwnProperty.call(env.grants,n))"
    "          throw new TypeError('std.sessions.resolve: the mapping for '+"
    "            principal+' names '+n+', which the environment being "
               "attenuated does not grant; refusing rather than granting less "
               "than the mapping says');"
    "        cap=env.grants[n];"
    "        if(found.descriptor.routes)"
    "          cap=C.mediate(cap,{flavor:'routes',glob:found.descriptor.routes});"
    "        C.grant(out,n,cap);}"
    "      return Object.freeze({principal:principal,env:out,"
    "        granted:want.slice().sort(),reason:null});};"
    "    return Object.freeze(S);};"

    "  C.std=Object.freeze(STD);"
    "  Object.freeze(STD.profiles);"
    "  C.pom=function(rootFn){"
    "    if(rootFn&&rootFn.confined===true)throw new TypeError("
    "      'pom: this is the BOUND WRAPPER of a confined fragment, not the "
                 "fragment; use comcon.cst(source,{file:"
                 "\"<comcon-fragment>\"}) for the fragment itself');"
    "    return pomView(rootFn,[]);};"
    /* cst(source): a CST view over plain SOURCE, with no live function behind it
       -- the §38 shape, where what you have is vendor TEXT you may not edit.
       pom() needs a compiled function (it reads the bytecode's source slice);
       this needs only a parse.  It is also what makes a rewrite REVIEWABLE
       before installation (`cst(rep.source).query(...)` checks the result
       structurally instead of by string compare) and what lets hardening passes
       compose, since harden() returns source rather than a node.  Pure analysis:
       it confers nothing, parses fail-closed, and the view is frozen like any
       other.  `parent` is null at the root -- there is no enclosing node. */
    "  C.cst=function(source,opts){var src=String(source);"
    "    var org=null;"
    "    if(opts&&typeof opts==='object'){"
    "      org={file:String(opts.file===undefined?'':opts.file),"
    "           line0:(opts.line0===undefined?1:opts.line0),"
    "           col0:(opts.col0===undefined?0:opts.col0),"
    "           offset:(opts.offset===undefined?null:opts.offset)};}"
    "    return cstView(src,cstParse(src),['cst'],"
    "                   function(){return null;},org);};"
    /* harden(node, query, wrapperQuotation) -- increment D5b-3: SOURCE-REWRITE
       hardening, the residual §38 case.  Every site the query matches is
       replaced by the wrapper with `$$` standing for the site's own source, and
       the result comes back as a QUOTATION, so installation stays D4's
       (`bindAt(site,q,contract)` / `epoch.replace(q)` -- rebuild-on-write, never
       an in-place edit of a running fragment).

       WHY IT IS NOT THE ENFORCEMENT STORY.  For a free name the capability
       kernel is strictly better: `grant(env,"fetch",mediate(cap,guard))` needs no
       parser and cannot be evaded by spelling.  harden() exists for what the
       kernel cannot name -- a LOCALLY-BOUND callee, a method call, one site out
       of many at a position -- which is also exactly what D5a's bytecode audit
       cannot see.  Reach for the kernel first; this is the fallback.

       WHY IT RETURNS A DESCRIPTION AND NOT AN INSTALL.  Rewriting is analysis
       (class R/L): it produces text.  Keeping the authority to install in D4
       means a hardening pass can be reviewed, diffed and admitted before it runs
       -- and that a rewrite cannot quietly become an install.

       $$ IS A CODE SPLICE, and that is the one place this differs from quote()'s
       data splices, which deliberately bind as JSON literals so a spliced string
       can never become code.  It is sound here only because the code spliced is
       the fragment's OWN source, read back out of the node it came from -- never
       caller text.

       The wrapper IS caller text, and is held to being a single
       `ExpressionStatement`.  What that buys, precisely: the splice stays at the
       expression position its range was measured at, so `guard($$); evil()` --
       which would splice to three VALID statements, and therefore cannot be
       caught by re-parsing the result -- is refused.  What it does NOT buy: any
       general guarantee about a wrapper, since `(guard($$), evil())` is also one
       expression.  A wrapper is code; what bounds it is the env it is realized
       under, never its syntax.  Do not read this check as an injection defence.

       The report's `count` is the only field that means anything happened --
       same discipline as jitCompile()'s `installed`.  A query matching nothing
       yields count 0 and the source unchanged; it does not throw, because
       hardening a corpus legitimately finds files with no sites, but a caller
       who does not read `count` cannot tell that from a rewrite. */
    "  var SITE='$$';"
    "  C.harden=function(node,query,wrapper){"
    "    if(!node||typeof node.query!=='function')throw new TypeError("
    "      'harden: arg0 must be a POM node');"
    "    var v=(typeof node.cst==='function')?node.cst():node;"
    "    if(typeof v.src!=='string'||typeof v.query!=='function')"
    "      throw new TypeError('harden: arg0 has no cst() view');"
    "    if(typeof query!=='string')throw new TypeError("
    "      'harden: arg1 must be a selector string');"
    "    if(!wrapper||!wrapper[QUOTE])throw new TypeError("
    "      'harden: arg2 must be a comcon.quote() description');"
    "    var w=String(wrapper.source).trim();"
    "    if(w.indexOf(SITE)<0)throw new TypeError("
    "      'harden: wrapper must contain '+SITE+', the matched site -- a "
                               "wrapper without it would DELETE the site');"
    /* "One expression" is checked by SHAPE -- the wrapper must parse as a
       Program of exactly one ExpressionStatement -- not by comparing an
       expression parse's end offset to the text length.  The offset test looked
       equivalent and was not: `(__guard($$), evil())` parses to a node whose
       range EXCLUDES the parentheses, so a perfectly good parenthesized wrapper
       was refused for the wrong reason, and the refusal would have been credited
       to the injection rule.

       Normalization then strips only a trailing `;` (spliced verbatim it would
       break the expression the site sits in).  NOT by slicing to the parsed
       expression's own range, which was the first attempt: for
       `(__guard($$), evil())` that range excludes the parentheses, and dropping
       them changes the precedence of the spliced result -- a rewrite that alters
       how the surrounding expression groups is exactly what must not happen. */
    "    var wp=null;"
    "    try{wp=C.__parse(w);}catch(we){wp=null;}"
    "    if(!wp||!wp.body||wp.body.length!==1"
    "       ||wp.body[0].type!=='ExpressionStatement')throw new TypeError("
    "      'harden: wrapper must be ONE expression, got: '+w);"
    "    w=w.replace(/[\\s;]+$/,'');"
    "    var hits=v.query(query).slice().sort(function(a,b){"
    "      return a.range[0]-b.range[0];});"
    /* Overlapping matches (a site nested inside another) cannot both be spliced:
       whichever is written second is built from the ORIGINAL text and discards
       the first rewrite. Refuse, loudly, rather than silently leaving the inner
       site unhardened -- the caller narrows the query instead. */
    "    for(var i=1;i<hits.length;i++)"
    "      if(hits[i].range[0]<hits[i-1].range[1])throw new Error("
    "        'harden: sites overlap ('+hits[i-1].type+'@'+hits[i-1].range[0]+"
    "        ' contains '+hits[i].type+'@'+hits[i].range[0]+"
    "        '), narrow the query');"
    /* Back to front, so every range still indexes the text it was measured in. */
    "    var out=v.src,recs=[];"
    "    for(var j=hits.length-1;j>=0;j--){"
    "      var r=hits[j].range,was=v.src.slice(r[0],r[1]);"
    "      var now=w.split(SITE).join(was);"
    "      out=out.slice(0,r[0])+now+out.slice(r[1]);"
    "      recs.unshift(Object.freeze({range:[r[0],r[1]],line:hits[j].line0,"
    "        type:hits[j].type,before:was,after:now}));}"
    /* FAIL CLOSED: a splice that produced something unparseable must never be
       handed back as a quotation -- realize() would be the first thing to find
       out, at install time, on a live site. */
    "    if(recs.length)cstParse(out);"
    "    return Object.freeze({count:recs.length,from:v.hash,source:out,"
    "      sites:Object.freeze(recs),quotation:C.quote(out)});};"
    /* bindAt(site, quotation, contract): install an admitted quotation at a live
       binding SITE and return an epoch handle (increment D4a — POM mutation is
       REBUILD-ON-WRITE: recompile the quotation, swap the site, new epoch). The
       site is an install(callable, epoch) function — the caller wires it to the
       existing COM setter (loc.handler = ...), so there is NO parallel install
       path (the shell fundament). Ops: replace(q) (admit+realize a new epoch,
       retain prior; class F), rollback() (restore the prior epoch), remove()
       (tombstone — class X, guarded by the caller's site), revive(). Rollback
       history is BOUNDED (BINDCAP): a superseded fragment beyond the window is
       freed (C.__freeConfined) so live rewrite does not accumulate fragments. */
    "  var BINDCAP=8;"
    "  function pomFreeFrag(cb){if(cb&&cb.handle!==undefined&&cb.handle>=0)"
    "    C.__freeConfined(cb.handle);}"
    "  C.bindAt=function(site,quotation,contract){"
    "    if(typeof site!=='function')throw new TypeError("
    "      'bindAt: arg0 must be an install(callable,epoch) function');"
    "    if(!quotation||!quotation[QUOTE])throw new TypeError("
    "      'bindAt: arg1 must be a comcon.quote() description');"
    "    contract=contract||{imports:[]};"
    "    var renv=contract.env||C.env();"
    "    function make(q){return C.realize(q,contract,renv);}"
    "    var cur=make(quotation),epoch=0,tomb=false,hist=[];"
    "    site(cur,epoch);"
    "    var h={};"
    "    h.epoch=function(){return epoch;};"
    "    h.tombstoned=function(){return tomb;};"
    "    h.call=function(arg){if(tomb)throw new Error('bindAt: tombstoned');"
    "      return cur(arg);};"
    /* Realize BEFORE touching history or the site, so a replacement that fails
       admission changes nothing.  Found composing D5b-3 with this: a hardening
       pass that asks for a capability it cannot have must not cost the live site
       a rollback slot -- with the push first, a failed replace consumed one and
       the next rollback() restored the CURRENT epoch instead of the previous. */
    "    h.replace=function(q2){"
    "      if(!q2||!q2[QUOTE])throw new TypeError("
    "        'replace: arg0 must be a comcon.quote() description');"
    "      var next=make(q2);"
    "      hist.push({epoch:epoch,callable:cur});"
    "      while(hist.length>BINDCAP)pomFreeFrag(hist.shift().callable);"
    "      cur=next;epoch++;tomb=false;site(cur,epoch);return epoch;};"
    "    h.rollback=function(){"
    "      if(!hist.length)throw new Error('bindAt: nothing to roll back');"
    "      var prev=hist.pop(),old=cur;"
    "      cur=prev.callable;epoch=prev.epoch;tomb=false;site(cur,epoch);"
    "      pomFreeFrag(old);return epoch;};"
    "    h.remove=function(){tomb=true;site(null,epoch);return epoch;};"
    "    h.revive=function(){if(tomb){tomb=false;site(cur,epoch);}return epoch;};"
    "    h.describe=function(){return {ops:["
    "      {name:'call',op:'invoke',cls:'R'},"
    "      {name:'replace',op:'rewrite',cls:'F'},"
    "      {name:'rollback',op:'rewrite',cls:'F'},"
    "      {name:'remove',op:'remove',cls:'X'},"
    "      {name:'revive',op:'revive',cls:'L'}]};};"
    "    return Object.freeze(h);};"
    /* bindShared(key, quotation, contract, onRequest): the class-F, MULTI-WORKER
       spelling of bindAt (increment D4b). The current {epoch, source} is the
       single source of truth in nginx.shared (lock-free, instantly visible to
       every worker), and each worker's request handler RECONCILES lazily:
       on each request it reads the shared epoch and, if newer than its locally
       compiled one, recompiles the shared source in ITS OWN compartment and
       swaps (rebuild-on-write, per worker). So a replace() in any one worker
       fans out coherently — no worker ever serves a torn state, and only the
       source string crosses (never a JSValue). Reuses nginx.shared as the
       transport (no new broadcast mechanism — the shell fundament). The old
       local fragment is freed on each reconcile, so no per-worker accumulation.
       h.handler(req) is installed at loc.handler; onRequest(req, callable,
       epoch) does the app response shaping (callable === null when removed). */
    "  C.bindShared=function(key,quotation,contract,onRequest){"
    "    if(typeof key!=='string')throw new TypeError("
    "      'bindShared: arg0 must be a string key');"
    "    if(!quotation||!quotation[QUOTE])throw new TypeError("
    "      'bindShared: arg1 must be a comcon.quote() description');"
    "    if(typeof onRequest!=='function')throw new TypeError("
    "      'bindShared: arg3 must be onRequest(req,callable,epoch)');"
    "    contract=contract||{imports:[]};"
    "    var renv=contract.env||C.env(),SK='__comconBind__:'+key;"
    "    var localEpoch=-1,cur=null,tomb=false;"
    /* nginx.shared is NOT available at config-eval time (only once workers run),
       so ALL shared access is deferred to request time: reconcile() seeds the
       shared state lazily on first touch (idempotent across workers) and runs
       from the per-request handler. The constructor touches nothing shared. */
    "    function reconcile(){"
    "      var raw=nginx.shared.get(SK);"
    "      if(raw===undefined){"
    "        nginx.shared.set(SK,"
    "          JSON.stringify({epoch:0,source:quotation.source}));"
    "        raw=nginx.shared.get(SK);if(raw===undefined)return;}"
    "      var st=JSON.parse(raw);if(st.epoch===localEpoch)return;"
    "      var old=cur;"
    "      if(st.removed){tomb=true;cur=null;}"
    "      else{tomb=false;cur=C.realize(C.quote(st.source),contract,renv);}"
    "      localEpoch=st.epoch;"
    "      if(old&&old.handle!==undefined&&old.handle>=0)"
    "        C.__freeConfined(old.handle);}"
    "    function bump(obj){"
    "      var st=JSON.parse(nginx.shared.get(SK)||'{\"epoch\":0}');"
    "      obj.epoch=(st.epoch|0)+1;"
    "      nginx.shared.set(SK,JSON.stringify(obj));reconcile();return obj.epoch;}"
    "    var h={};"
    "    h.epoch=function(){reconcile();return localEpoch;};"
    "    h.handler=function(req){reconcile();"
    "      onRequest(req,tomb?null:cur,localEpoch);};"
    "    h.replace=function(q2){"
    "      if(!q2||!q2[QUOTE])throw new TypeError("
    "        'replace: arg0 must be a comcon.quote() description');"
    "      return bump({source:q2.source});};"
    "    h.remove=function(){"
    "      var st=JSON.parse(nginx.shared.get(SK)||'{\"epoch\":0}');"
    "      return bump({removed:true,source:st.source||''});};"
    "    h.revive=function(){"
    "      var st=JSON.parse(nginx.shared.get(SK)||'{\"epoch\":0}');"
    "      return bump({source:st.source||''});};"
    "    h.describe=function(){return {shared:true,ops:["
    "      {name:'handler',op:'invoke',cls:'R'},"
    "      {name:'replace',op:'rewrite',cls:'F'},"
    "      {name:'remove',op:'remove',cls:'X'},"
    "      {name:'revive',op:'revive',cls:'L'}]};};"
    "    return Object.freeze(h);};"
    "})();";


ngx_int_t
ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle)
{
    JSRuntime  *rt;
    JSValue     global, nginx_obj, cycle_obj;

    rt = JS_GetRuntime(ctx);

    /* Register ALL classes before installing any prototypes */
    if (ngx_js_com_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_http_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_upstream_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_stream_upstream_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    /* Install shared prototypes now that all classes are registered */
    if (ngx_js_request_install_proto(ctx) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_com_install_protos(ctx) != NGX_OK) {
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

    /* nginx.gc() — force a QuickJS GC pass */
    JS_SetPropertyStr(ctx, nginx_obj, "gc",
                      JS_NewCFunction(ctx, ngx_js_nginx_gc, "gc", 0));

    /* nginx.jsMemUsage() — {mallocSize, mallocCount, objectCount} */
    JS_SetPropertyStr(ctx, nginx_obj, "jsMemUsage",
                      JS_NewCFunction(ctx, ngx_js_nginx_js_mem_usage,
                                      "jsMemUsage", 0));

    /* nginx.broadcast(fn) — per-worker startup callbacks */
    JS_SetPropertyStr(ctx, nginx_obj, "broadcast",
                      JS_NewCFunction(ctx, ngx_js_broadcast, "broadcast", 1));
    JS_SetPropertyStr(ctx, nginx_obj, "__broadcast_queue",
                      JS_NewArray(ctx));

    /* nginx.setTimeout(ms) — returns a Promise resolved by an NGINX timer */
    JS_SetPropertyStr(ctx, nginx_obj, "setTimeout",
                      JS_NewCFunction(ctx, ngx_js_nginx_set_timeout,
                                      "setTimeout", 1));

    /* nginx.jitCompile(fn[, opts]) — AOT-A: opt-in load-time compilation of
       host JS (see the function comment). Present in every build; reports
       installed:0 when nginx was built without -DCONFIG_JIT. */
    JS_SetPropertyStr(ctx, nginx_obj, "jitCompile",
                      JS_NewCFunction(ctx, ngx_js_jit_compile,
                                      "jitCompile", 2));

    /* nginx.suspendAcceptance() / nginx.resumeAcceptance() — Phase 1 */
    JS_SetPropertyStr(ctx, nginx_obj, "suspendAcceptance",
                      JS_NewCFunction(ctx, ngx_js_suspend_acceptance,
                                      "suspendAcceptance", 0));
    JS_SetPropertyStr(ctx, nginx_obj, "resumeAcceptance",
                      JS_NewCFunction(ctx, ngx_js_resume_acceptance,
                                      "resumeAcceptance", 0));

    /* nginx.suspendAllWorkers() / nginx.resumeAllWorkers() — Phase 2 */
    JS_SetPropertyStr(ctx, nginx_obj, "suspendAllWorkers",
                      JS_NewCFunction(ctx, ngx_js_suspend_all_workers,
                                      "suspendAllWorkers", 0));
    JS_SetPropertyStr(ctx, nginx_obj, "resumeAllWorkers",
                      JS_NewCFunction(ctx, ngx_js_resume_all_workers,
                                      "resumeAllWorkers", 0));

    /*
     * nginx.workerIdx — 0-based worker index, -1 in master / init_conf.
     * Writable so that init_process can update it to the real worker index.
     */
    JS_DefinePropertyValueStr(ctx, nginx_obj, "workerIdx",
                              JS_NewInt32(ctx,
                                  ngx_process == NGX_PROCESS_WORKER
                                  ? (int32_t) ngx_worker : -1),
                              JS_PROP_WRITABLE | JS_PROP_ENUMERABLE);

    /*
     * nginx.workerMemoryLimit — per-worker JS heap cap in bytes (0 = none).
     * js_source scripts write this value; init_process reads it and calls
     * JS_SetMemoryLimit on the worker's private runtime copy after fork.
     */
    JS_DefinePropertyValueStr(ctx, nginx_obj, "workerMemoryLimit",
                              JS_NewInt32(ctx, 0),
                              JS_PROP_WRITABLE | JS_PROP_ENUMERABLE
                              | JS_PROP_CONFIGURABLE);

    /*
     * nginx.workerRequestTimeout — per-request JS execution cap in ms (0 =
     * none).  js_source scripts write this value; init_process installs a
     * QuickJS interrupt handler that checks a monotonic-clock deadline set
     * around each JS_Call in the content handler.
     */
    JS_DefinePropertyValueStr(ctx, nginx_obj, "workerRequestTimeout",
                              JS_NewInt32(ctx, 0),
                              JS_PROP_WRITABLE | JS_PROP_ENUMERABLE
                              | JS_PROP_CONFIGURABLE);

    /* nginx.cycle */
    cycle_obj = ngx_js_wrap_cycle(ctx, cycle);
    if (JS_IsException(cycle_obj)) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, nginx_obj, "cycle", cycle_obj);

    /* nginx.events — events{} block configuration */
    {
        ngx_event_conf_t  *ecf;
        JSValue            events_obj;

        ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);
        events_obj = ngx_js_wrap_events(ctx, ecf);
        if (JS_IsException(events_obj)) {
            JS_FreeValue(ctx, nginx_obj);
            JS_FreeValue(ctx, global);
            return NGX_ERROR;
        }
        JS_SetPropertyStr(ctx, nginx_obj, "events", events_obj);
    }

    /* nginx.http — servers[], upstreams[] (read-only Phase 1) */
    if (ngx_js_http_com_install(ctx, nginx_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    /* nginx.createSocket('host:port') — Stage 52 */
    if (ngx_js_socket_install(ctx, nginx_obj) != NGX_OK) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    /* nginx.stream — servers[] + attach() — Stage 52G */
    if (ngx_js_stream_install(ctx, nginx_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    /* nginx.repl.{eval, attach, detach, listen, _writeFd} */
    if (ngx_js_repl_install(ctx, nginx_obj) != NGX_OK) {
        JS_FreeValue(ctx, nginx_obj);
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

    /* nginx.get / nginx.set / nginx.settable */
    JS_SetPropertyStr(ctx, nginx_obj, "get",
                      JS_NewCFunction(ctx, ngx_js_nginx_fn_get, "get", 1));
    JS_SetPropertyStr(ctx, nginx_obj, "set",
                      JS_NewCFunction(ctx, ngx_js_nginx_fn_set, "set", 2));
    JS_SetPropertyStr(ctx, nginx_obj, "settable",
                      JS_NewCFunction(ctx, ngx_js_nginx_fn_settable,
                                      "settable", 1));
    JS_SetPropertyStr(ctx, nginx_obj, "describeType",
                      JS_NewCFunction(ctx, ngx_js_nginx_fn_describe_type,
                                      "describeType", 2));
    JS_SetPropertyStr(ctx, nginx_obj, "describe",
                      JS_NewCFunction(ctx, ngx_js_nginx_fn_describe,
                                      "describe", 2));

    /* nginx.on(event, fn) — master lifecycle event handler registration */
    JS_SetPropertyStr(ctx, nginx_obj, "on",
                      JS_NewCFunction(ctx, ngx_js_nginx_on, "on", 2));

    /* nginx.sendToWorker(slot, data) — master → specific worker */
    JS_SetPropertyStr(ctx, nginx_obj, "sendToWorker",
                      JS_NewCFunction(ctx, ngx_js_send_to_worker,
                                      "sendToWorker", 2));

    /* nginx.broadcastToWorkers(data) — master → all workers */
    JS_SetPropertyStr(ctx, nginx_obj, "broadcastToWorkers",
                      JS_NewCFunction(ctx, ngx_js_broadcast_to_workers,
                                      "broadcastToWorkers", 1));

    /* nginx.sendToMaster(data) — worker → master */
    JS_SetPropertyStr(ctx, nginx_obj, "sendToMaster",
                      JS_NewCFunction(ctx, ngx_js_send_to_master,
                                      "sendToMaster", 1));

    /* nginx.use(path[, config]) — JS-Pilgrim P7: filesystem plugin loader */
    JS_SetPropertyStr(ctx, nginx_obj, "use",
                      JS_NewCFunction(ctx, ngx_js_use, "use", 1));

    /* COMCON A2.1: host-only grant primitive (tenants never see this). */
    JS_SetPropertyStr(ctx, nginx_obj, "grantToTenant",
                      JS_NewCFunction(ctx, ngx_js_grant_to_tenant,
                                      "grantToTenant", 1));

    /* M5 evidence instrument (see its comment): host-call cost decomposition. */
    JS_SetPropertyStr(ctx, nginx_obj, "__benchStub",
                      JS_NewCFunction(ctx, ngx_js_bench_stub, "__benchStub", 2));

    /* D4c/AOT-A: which tier a host function is on (read-only). */
    JS_SetPropertyStr(ctx, nginx_obj, "jitStatus",
                      JS_NewCFunction(ctx, ngx_js_jit_status, "jitStatus", 1));

    /* COMCON A4: host-only denial report (audit→enforce loop). */
    JS_SetPropertyStr(ctx, nginx_obj, "tenantDenials",
                      JS_NewCFunction(ctx, ngx_js_tenant_denials,
                                      "tenantDenials", 0));

    /* COMCON B0: host-only learning report (onboarding harvest). */
    JS_SetPropertyStr(ctx, nginx_obj, "tenantLearning",
                      JS_NewCFunction(ctx, ngx_js_tenant_learning,
                                      "tenantLearning", 0));

    /* nginx.install(plugin[, config]) — JS-Pilgrim P7: inline plugin caller */
    JS_SetPropertyStr(ctx, nginx_obj, "install",
                      JS_NewCFunction(ctx, ngx_js_install, "install", 1));

    /* nginx.shared — JS-Pilgrim P11: cross-worker key/value store */
    {
        JSValue  shared_obj;

        shared_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, shared_obj, "get",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_get, "get", 1));
        JS_SetPropertyStr(ctx, shared_obj, "set",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_set, "set", 3));
        JS_SetPropertyStr(ctx, shared_obj, "delete",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_delete,
                                          "delete", 1));
        JS_SetPropertyStr(ctx, shared_obj, "keys",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_keys, "keys", 0));
        JS_SetPropertyStr(ctx, shared_obj, "incr",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_incr, "incr", 1));
        JS_SetPropertyStr(ctx, shared_obj, "ttl",
                          JS_NewCFunction(ctx, ngx_js_shared_fn_ttl, "ttl", 1));
        JS_SetPropertyStr(ctx, nginx_obj, "shared", shared_obj);
    }

    /* nginx.plugins — JS-Pilgrim P19: array of loaded plugin paths */
    JS_SetPropertyStr(ctx, nginx_obj, "plugins", JS_NewArray(ctx));

    JS_SetPropertyStr(ctx, global, "nginx", nginx_obj);

    /*
     * COMCON M-CFG: the host-JS kernel-operator surface. Step 2 exposes the
     * `admit` gate; grant/mediate/bind/include/includeAt follow in later
     * slices. Host context only (HOST_ROOT holds the root handle).
     */
    {
        JSValue  comcon_obj = JS_NewObject(ctx);
        JSValue  r;

        JS_SetPropertyStr(ctx, comcon_obj, "admit",
                          JS_NewCFunction(ctx, ngx_js_comcon_admit, "admit", 2));
        JS_SetPropertyStr(ctx, comcon_obj, "__includeConfined",
                          JS_NewCFunction(ctx, ngx_js_comcon_include_confined,
                                          "__includeConfined", 6));
        JS_SetPropertyStr(ctx, comcon_obj, "__invokeConfined",
                          JS_NewCFunction(ctx, ngx_js_comcon_invoke_confined,
                                          "__invokeConfined", 3));
        /* D4a: free a superseded fragment (bounded rollback window). */
        JS_SetPropertyStr(ctx, comcon_obj, "__freeConfined",
                          JS_NewCFunction(ctx, ngx_js_comcon_free_confined,
                                          "__freeConfined", 1));
        /* comcon.mode() — the confined-compartment policy mode (used by the
           include compartment: learn seeding + the reach-gate mode). The former
           comcon.tenant/dependency/artifact operators were retired with the
           tenant path; the include contract ({deps,identity,imports}) carries
           their per-fragment equivalents. */
        JS_SetPropertyStr(ctx, comcon_obj, "mode",
                          JS_NewCFunction(ctx, ngx_js_comcon_op_mode, "mode", 1));
        /* increment D0 (POM substrate): diagnostic reflector — module/function
           node tree from a compiled fragment. Replaced by the lazy NodeView in
           D1. Internal (double-underscore), like __includeConfined. */
        JS_SetPropertyStr(ctx, comcon_obj, "__pomInspect",
                          JS_NewCFunction(ctx, ngx_js_comcon_pom_inspect,
                                          "__pomInspect", 1));
        /* increment D1 (lazy NodeView): single-node-at-path accessor backing
           comcon.pom(). Internal; the NodeView surface is built in JS below. */
        JS_SetPropertyStr(ctx, comcon_obj, "__pomNodeAt",
                          JS_NewCFunction(ctx, ngx_js_comcon_pom_node_at,
                                          "__pomNodeAt", 2));
        /* increment D5b-2: ESTree parse via the vendored acorn. Internal
           (double-underscore) and host-side only — trusted analysis over
           untrusted TEXT, never reachable from a confined fragment. Lazy: the
           parser is evaluated on first use, not at every startup. */
        JS_SetPropertyStr(ctx, comcon_obj, "__parse",
                          JS_NewCFunction(ctx, ngx_js_comcon_parse,
                                          "__parse", 1));
        /* increment D4c: which tier a fragment's code is actually on. */
        JS_SetPropertyStr(ctx, comcon_obj, "__aotStatus",
                          JS_NewCFunction(ctx, ngx_js_comcon_aot_status,
                                          "__aotStatus", 1));
        /* [TBD-2]: the closed refusal-code set, generated from the C table —
           what a tenant's deny-suite may be refused with. */
        JS_SetPropertyStr(ctx, comcon_obj, "refusalCodes",
                          JS_NewCFunction(ctx, ngx_js_comcon_refusal_codes,
                                          "refusalCodes", 0));
        /* increment D5a: call-site / reference enumeration (bytecode scan). */
        JS_SetPropertyStr(ctx, comcon_obj, "__pomCallsites",
                          JS_NewCFunction(ctx, ngx_js_comcon_pom_callsites,
                                          "__pomCallsites", 2));
        JS_SetPropertyStr(ctx, global, "comcon", comcon_obj);

        /* env/grant/mediate/meter/bind — the capability layer (JS) */
        r = JS_Eval(ctx, ngx_js_comcon_bootstrap,
                    sizeof(ngx_js_comcon_bootstrap) - 1,
                    "<comcon-bootstrap>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "js: comcon capability-layer bootstrap failed");
            JS_FreeValue(ctx, r);
            JS_FreeValue(ctx, global);
            return NGX_ERROR;
        }
        JS_FreeValue(ctx, r);
    }

    /*
     * Global console object: debug/log/warn/error forwarded to nginx.log.
     * nginx.repl.attach() overrides these to also stream to the REPL fd.
     */
    {
        static const char  script[] =
            "(function(){"
            "  var nl = nginx.log;"
            "  function fmt(a){"
            "    return Array.prototype.slice.call(a).map(function(x){"
            "      return (typeof x==='object'&&x!==null)?JSON.stringify(x):String(x);"
            "    }).join(' ');"
            "  }"
            "  globalThis.console = {"
            "    debug: function(){ nl(8, fmt(arguments)); },"
            "    log:   function(){ nl(6, fmt(arguments)); },"
            "    warn:  function(){ nl(4, fmt(arguments)); },"
            "    error: function(){ nl(3, fmt(arguments)); }"
            "  };"
            "})();";
        JSValue  ret;

        ret = JS_Eval(ctx, script, sizeof(script) - 1,
                      "<console-init>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ret)) {
            ngx_js_log_exception(ctx, cycle->log);
        }
        JS_FreeValue(ctx, ret);
    }

    /*
     * nginx.withSuspendedAcceptance(fn) — convenience wrapper (pure JS).
     * Suspends all workers, awaits fn(), then resumes regardless of throw.
     */
    {
        static const char  script[] =
            "(function(){"
            "  const s = nginx.suspendAllWorkers.bind(nginx);"
            "  const r = nginx.resumeAllWorkers.bind(nginx);"
            "  nginx.withSuspendedAcceptance = async function(fn){"
            "    await s();"
            "    try{ await fn(); }finally{ await r(); }"
            "  };"
            "})();";
        JSValue  ret;

        ret = JS_Eval(ctx, script, sizeof(script) - 1,
                      "<com-init>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ret)) {
            ngx_js_log_exception(ctx, cycle->log);
        }
        JS_FreeValue(ctx, ret);
    }

    /* P12: install L4 async source factory */
    if (ngx_js_l4_install_source_factory(ctx) != NGX_OK) {
        JS_FreeValue(ctx, global);
        return NGX_ERROR;
    }

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
/* Install shared prototypes for all COM classes                        */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_com_install_protos(JSContext *ctx)
{
    if (ngx_js_cycle_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_location_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_server_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_com_facet_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_upstream_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_peer_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_rr_peer_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_proxy_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_ssl_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_gzip_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_headers_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_proxy_cache_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_rewrite_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_access_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_auth_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_limit_req_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_limit_req_limit_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_limit_conn_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_fastcgi_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_log_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
#if (NGX_HTTP_REALIP)
    if (ngx_js_realip_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
#endif
    if (ngx_js_charset_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_sub_filter_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_autoindex_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_referer_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
#if (NGX_HTTP_DAV)
    if (ngx_js_dav_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
#endif
    if (ngx_js_ssi_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_userid_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_addition_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_gunzip_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_slice_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_image_filter_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_xslt_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_secure_link_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_mp4_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_random_index_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_auth_request_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_gzip_static_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_memcached_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_scgi_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_uwsgi_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_mirror_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_events_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_socket_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_listener_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_stream_listener_install_protos(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_stream_access_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_stream_ssl_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_stream_session_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
    if (ngx_js_snapshot_install_proto(ctx) != NGX_OK) { return NGX_ERROR; }
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
