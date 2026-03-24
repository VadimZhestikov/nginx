
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
JSClassID  ngx_js_upstream_class_id;
JSClassID  ngx_js_peer_class_id;
JSClassID  ngx_js_rr_peer_class_id;
JSClassID  ngx_js_request_class_id;
JSClassID  ngx_js_req_vars_class_id;
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
        JS_NewClassID(&ngx_js_req_vars_class_id);
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
        JS_NewClassID(&ngx_js_stream_server_class_id);
        JS_NewClassID(&ngx_js_stream_listener_class_id);
        JS_NewClassID(&ngx_js_stream_proxy_class_id);
        JS_NewClassID(&ngx_js_stream_upstream_class_id);
        JS_NewClassID(&ngx_js_stream_peer_class_id);
        JS_NewClassID(&ngx_js_stream_rr_peer_class_id);
        JS_NewClassID(&ngx_js_stream_access_class_id);
        JS_NewClassID(&ngx_js_stream_ssl_class_id);
        JS_NewClassID(&ngx_js_stream_session_class_id);
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

    (void) recv(conn->fd, &ack, 1, MSG_DONTWAIT);

    /* Resolve the Promise — async handler body resumes as a microtask */
    ret = JS_Call(actx->ctx, actx->resolve, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(actx->ctx, ret);
    JS_FreeValue(actx->ctx, actx->resolve);
    JS_FreeValue(actx->ctx, actx->reject);

    while (JS_ExecutePendingJob(actx->rt, &job_ctx) > 0) { }
    ngx_js_async_check(actx->w);
    ngx_js_bf_async_check(actx->w);
    ngx_js_sf_async_check(actx->w);

    /* Clean up the reply fd connection */
    ngx_del_event(conn->read, NGX_READ_EVENT, NGX_CLOSE_EVENT);
    ngx_free_connection(conn);
    conn->fd = (ngx_socket_t) -1;

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


/* ------------------------------------------------------------------ */
/* ngx_js_com_init — main entry point called from ngx_js_module.c      */
/* ------------------------------------------------------------------ */

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

    /* nginx.broadcast(fn) — per-worker startup callbacks */
    JS_SetPropertyStr(ctx, nginx_obj, "broadcast",
                      JS_NewCFunction(ctx, ngx_js_broadcast, "broadcast", 1));
    JS_SetPropertyStr(ctx, nginx_obj, "__broadcast_queue",
                      JS_NewArray(ctx));

    /* nginx.setTimeout(ms) — returns a Promise resolved by an NGINX timer */
    JS_SetPropertyStr(ctx, nginx_obj, "setTimeout",
                      JS_NewCFunction(ctx, ngx_js_nginx_set_timeout,
                                      "setTimeout", 1));

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

    JS_SetPropertyStr(ctx, global, "nginx", nginx_obj);

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
