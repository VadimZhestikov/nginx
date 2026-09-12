
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

    result = JS_UNDEFINED;

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used
            && ngx_strcmp(entries[i].key, key) == 0)
        {
            if (entries[i].expires != 0 && now >= entries[i].expires) {
                /* expired — reclaim lazily and report absent */
                ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
                hdr->count--;
            } else {
                result = JS_NewString(ctx, entries[i].val);
            }
            break;
        }
    }

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
    int                     found;

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

    ngx_spinlock(&hdr->lock, 1, 2048);

    found = 0;
    free_slot = (ngx_uint_t) -1;

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used) {
            /* reclaim any expired entry we pass, freeing its slot for reuse */
            if (entries[i].expires != 0 && now >= entries[i].expires) {
                ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
                hdr->count--;
                if (free_slot == (ngx_uint_t) -1) {
                    free_slot = i;
                }
                continue;
            }
            if (ngx_strcmp(entries[i].key, key) == 0) {
                ngx_cpystrn((u_char *) entries[i].val, (u_char *) val,
                            NGX_JS_SHARED_VAL_LEN);
                entries[i].expires = expires;
                found = 1;
                break;
            }
        } else if (free_slot == (ngx_uint_t) -1) {
            free_slot = i;
        }
    }

    if (!found) {
        if (free_slot == (ngx_uint_t) -1) {
            ngx_unlock(&hdr->lock);
            JS_FreeCString(ctx, val);
            JS_FreeCString(ctx, key);
            return JS_ThrowInternalError(ctx, "nginx.shared: store full");
        }

        entries[free_slot].used = 1;
        ngx_cpystrn((u_char *) entries[free_slot].key, (u_char *) key,
                    NGX_JS_SHARED_KEY_LEN);
        ngx_cpystrn((u_char *) entries[free_slot].val, (u_char *) val,
                    NGX_JS_SHARED_VAL_LEN);
        entries[free_slot].expires = expires;
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

    deleted = 0;

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used
            && ngx_strcmp(entries[i].key, key) == 0)
        {
            ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
            hdr->count--;
            deleted = 1;
            break;
        }
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

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used) {
            if (entries[i].expires != 0 && now >= entries[i].expires) {
                ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
                hdr->count--;
                continue;
            }
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, entries[i].key));
        }
    }

    ngx_unlock(&hdr->lock);

    return arr;
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
    int                     found;

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

    ngx_spinlock(&hdr->lock, 1, 2048);

    found = 0;
    free_slot = (ngx_uint_t) -1;
    cur = 0;

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used) {
            /* an expired counter is reclaimed and restarts from zero */
            if (entries[i].expires != 0 && now >= entries[i].expires) {
                ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
                hdr->count--;
                if (free_slot == (ngx_uint_t) -1) {
                    free_slot = i;
                }
                continue;
            }
            if (ngx_strcmp(entries[i].key, key) == 0) {
                cur = ngx_atoi((u_char *) entries[i].val,
                               ngx_strlen(entries[i].val));
                if (cur == NGX_ERROR) {
                    cur = 0;
                }
                cur += delta;
                ngx_snprintf((u_char *) entries[i].val,
                             NGX_JS_SHARED_VAL_LEN - 1, "%l", cur);
                entries[i].val[NGX_JS_SHARED_VAL_LEN - 1] = '\0';
                found = 1;
                break;
            }
        } else if (free_slot == (ngx_uint_t) -1) {
            free_slot = i;
        }
    }

    if (!found) {
        if (free_slot == (ngx_uint_t) -1) {
            ngx_unlock(&hdr->lock);
            JS_FreeCString(ctx, key);
            return JS_ThrowInternalError(ctx, "nginx.shared: store full");
        }

        cur = delta;
        entries[free_slot].used = 1;
        entries[free_slot].expires = 0;   /* a fresh counter never expires */
        ngx_cpystrn((u_char *) entries[free_slot].key, (u_char *) key,
                    NGX_JS_SHARED_KEY_LEN);
        ngx_snprintf((u_char *) entries[free_slot].val,
                     NGX_JS_SHARED_VAL_LEN - 1, "%l", cur);
        entries[free_slot].val[NGX_JS_SHARED_VAL_LEN - 1] = '\0';
        hdr->count++;
    }

    ngx_unlock(&hdr->lock);

    JS_FreeCString(ctx, key);

    (void) buf;  /* silence unused-variable warning */

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

    result = JS_NULL;

    for (i = 0; i < hdr->capacity; i++) {
        if (entries[i].used
            && ngx_strcmp(entries[i].key, key) == 0)
        {
            if (entries[i].expires == 0) {
                result = JS_NewInt64(ctx, -1);          /* permanent */
            } else if (now >= entries[i].expires) {
                ngx_memzero(&entries[i], sizeof(ngx_js_shared_entry_t));
                hdr->count--;                           /* expired -> absent */
            } else {
                result = JS_NewInt64(ctx,
                                     (int64_t) (entries[i].expires - now));
            }
            break;
        }
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


static ngx_uint_t
ngx_js_admit_in_imports(ngx_js_admit_check_t *c, const char *name)
{
    uint32_t     i;
    JSValue      v;
    const char  *s;
    ngx_uint_t   match = 0;

    for (i = 0; i < c->imports_len && !match; i++) {
        v = JS_GetPropertyUint32(c->ctx, c->imports, i);
        s = JS_ToCString(c->ctx, v);
        if (s != NULL && ngx_strcmp(s, name) == 0) {
            match = 1;
        }
        if (s != NULL) {
            JS_FreeCString(c->ctx, s);
        }
        JS_FreeValue(c->ctx, v);
    }

    return match;
}


static void
ngx_js_admit_free_cb(void *ud, const char *name)
{
    ngx_js_admit_check_t  *c = ud;

    if (c->bad) {
        return;
    }

    if (ngx_js_admit_name_denied(name) || !ngx_js_admit_in_imports(c, name)) {
        c->bad = 1;
        ngx_cpystrn((u_char *) c->badname, (u_char *) name, sizeof(c->badname));
    }
}


static JSValue
ngx_js_admit_verdict(JSContext *ctx, ngx_uint_t certified, const char *reject)
{
    JSValue  o;

    o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "certified", JS_NewBool(ctx, certified ? 1 : 0));
    if (!certified && reject != NULL) {
        JS_SetPropertyStr(ctx, o, "reject", JS_NewString(ctx, reject));
    }
    return o;
}


/*
 * The C3 admission gate, factored so both the `admit` operator and
 * `comcon.include` (which composes admission on the fragment it compiles in the
 * confined compartment) share ONE implementation. Runs entirely in `ctx`:
 * `fn` and `imports` (a JS array, or JS_UNDEFINED) must belong to `ctx`.
 * Returns NGX_OK (certified) or NGX_ERROR with `reason` filled. Grants are
 * closure var-refs on `fn`, not free globals, so they are auto-excluded from
 * the free-name check — only genuine global lookups must be in `imports`.
 */
ngx_int_t
ngx_js_comcon_admit_check(JSContext *ctx, JSValueConst fn, JSValueConst imports,
    int check_request, char *reason, size_t reason_len)
{
    JSValue               len;
    ngx_js_admit_check_t  chk;
    char                  field[128];

    if (!JS_IsFunction(ctx, fn)) {
        ngx_snprintf((u_char *) reason, reason_len,
                     "admit: arg0 must be a function%Z");
        return NGX_ERROR;
    }

    /* C3: no direct eval / with */
    if (js_comcon_uses_dynamic_code(fn)) {
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

    if (js_comcon_collect_free_globals(ctx, fn, ngx_js_admit_free_cb, &chk)
        != 0)
    {
        ngx_snprintf((u_char *) reason, reason_len,
                     "admit: arg0 not a bytecode function%Z");
        return NGX_ERROR;
    }

    if (chk.bad) {
        ngx_snprintf((u_char *) reason, reason_len,
                     "free name not granted: %s%Z", chk.badname);
        return NGX_ERROR;
    }

    /* optional C3: sealed-Request field check */
    if (check_request
        && js_comcon_check_request_fields(ctx, fn, field, sizeof(field)))
    {
        ngx_snprintf((u_char *) reason, reason_len,
                     "request field not in sealed schema: %s%Z", field);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static JSValue
ngx_js_comcon_admit(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValueConst  fn, contract;
    JSValue       imports, cr;
    int           check_request = 0;
    char          reason[256];
    ngx_int_t     rc;

    fn = argc > 0 ? argv[0] : JS_UNDEFINED;
    contract = argc > 1 ? argv[1] : JS_UNDEFINED;

    imports = JS_IsObject(contract)
              ? JS_GetPropertyStr(ctx, contract, "imports") : JS_UNDEFINED;

    if (JS_IsObject(contract)) {
        cr = JS_GetPropertyStr(ctx, contract, "checkRequest");
        check_request = JS_ToBool(ctx, cr);
        JS_FreeValue(ctx, cr);
    }

    rc = ngx_js_comcon_admit_check(ctx, fn, imports, check_request,
                                   reason, sizeof(reason));
    JS_FreeValue(ctx, imports);

    return ngx_js_admit_verdict(ctx, rc == NGX_OK, rc == NGX_OK ? NULL : reason);
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
    "  C.mediate=function(cap,interceptor){var f={};"
    "    f[FACET]={cap:cap,interceptor:interceptor};return f;};"
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
    "  C.include=function(source,contract){"
    "    contract=contract||{};"
    "    var FM={address:1,port:2,fd:4,listener:8},FULL=15;"
    "    var g=contract.grants||{},names=[],caps=[],pols=[];"
    "    for(var k in g){if(Object.prototype.hasOwnProperty.call(g,k)){"
    "      var v=g[k],cap=v,pol={kind:0,mask:FULL};"
    "      if(v&&v[FACET]){var it=v[FACET].interceptor||{};cap=v[FACET].cap;"
    "        if(it.flavor==='revoke')continue;"       /* narrow to zero: withhold */
    "        else if(it.flavor==='allow'){var m=0;"
    "          (it.fields||[]).forEach(function(f){m|=(FM[f]||0);});"
    "          pol={kind:0,mask:m>>>0};}"
    "        else if(it.flavor==='redact'){var r=FULL;"
    "          (it.fields||[]).forEach(function(f){r&=~(FM[f]||0);});"
    "          pol={kind:0,mask:r>>>0};}"
    "        else if(it.flavor==='routes'){"
    "          pol={kind:1,glob:String(it.glob||'*')};}}"
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
    "    if(contract.imports!==undefined||contract.identity"
    "       ||contract.checkRequest||contract.tests){"
    "      admit={imports:contract.imports||[],"
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
    "    var bound=function(arg){return C.__invokeConfined(h,arg,ms);};"
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
    "  function pomName(name,g){"
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
    "  function pomView(rootFn,path){"
    "    var raw=C.__pomNodeAt(rootFn,path);"
    "    if(raw===undefined)return null;"
    "    var ps=path.join('.'),v={};"
    "    v.kind=raw.kind;v.name=raw.name;v.hash=raw.hash;"
    "    v.line0=raw.line0;v.line1=raw.line1;v.childCount=raw.childCount;"
    "    v.span={line0:raw.line0,line1:raw.line1};"
    "    v.id=pomId(raw.hash,ps);"
    "    v.binding={epoch:0,profile:'unbound'};"
    "    v.text=function(){return C.quote(raw.source);};"
    "    v.quote=function(){return C.quote(raw.source);};"
    "    v.query=function(sel){return pomQuery(v,sel);};"
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
    "  C.pom=function(rootFn){return pomView(rootFn,[]);};"
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
    "    h.replace=function(q2){"
    "      if(!q2||!q2[QUOTE])throw new TypeError("
    "        'replace: arg0 must be a comcon.quote() description');"
    "      hist.push({epoch:epoch,callable:cur});"
    "      while(hist.length>BINDCAP)pomFreeFrag(hist.shift().callable);"
    "      cur=make(q2);epoch++;tomb=false;site(cur,epoch);return epoch;};"
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
