
/*
 * Copyright (C) nginx JS contributors
 */

#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/* Forward declaration for SharedWorker state list */
struct ngx_js_sw_state_s;
typedef struct ngx_js_sw_state_s ngx_js_sw_state_t;

/*
 * Per-cycle configuration owned by ngx_js_module (NGX_CORE_MODULE).
 * Allocated in cycle->pool via create_conf; populated by js_include
 * directives during ngx_conf_parse(), executed by init_conf().
 */
typedef struct {
    ngx_array_t         includes;    /* ngx_str_t: resolved paths from js_include */
    JSRuntime          *rt;          /* master-process QuickJS runtime             */
    JSContext          *ctx;         /* master-process QuickJS context             */
    void               *worker;      /* ngx_js_worker_t* after fork (in workers)  */
    ngx_js_sw_state_t  *sw_list;     /* linked list of SharedWorker states        */
} ngx_js_conf_t;


/*
 * Saved state for one suspended async nginx request.
 * Allocated in r->pool; freed automatically when the request pool is torn down.
 */
struct ngx_http_request_s;

typedef struct {
    struct ngx_http_request_s  *r;
    JSValue                     req_obj;   /* DupValue'd from content handler */
    JSValue                     promise;   /* outer handler Promise */
} ngx_js_async_ctx_t;


/*
 * Per-worker JS runtime created in init_process().
 * Workers never share a JSRuntime — QuickJS is not thread-safe.
 */
typedef struct {
    JSRuntime           *rt;
    JSContext           *ctx;
    ngx_js_async_ctx_t  *async_pending;  /* NULL or one suspended request */
} ngx_js_worker_t;


/*
 * Per-location JS handler config owned by ngx_js_http_module.
 * handler_idx == -1 means no JS handler is set for this location.
 * Otherwise it is an index into the global __ngx_handlers__ array
 * that was populated by location.handler = <function> assignments
 * during the config phase.
 */
typedef struct {
    ngx_int_t  handler_idx;
} ngx_js_loc_conf_t;


extern ngx_module_t  ngx_js_module;
extern ngx_module_t  ngx_js_http_module;


/* COM initialisation — installs nginx.* into ctx's global object */
ngx_int_t  ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle);

/*
 * config.write(text) — feeds config text back into ngx_conf_parse() via a
 * temporary file.  ctx's opaque must be a valid ngx_conf_t*.  Usable from
 * both js_preprocess (core-level) and js_init_http (http-level) handlers.
 */
JSValue    ngx_js_config_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);

/* Read a file into a NUL-terminated cycle->pool buffer. */
u_char    *ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path,
    size_t *len);

/* Log a pending JS exception to the NGINX error log, then clear it */
void       ngx_js_log_exception(JSContext *ctx, ngx_log_t *log);

/* Register NginxRequest class in rt (called once per new runtime) */
ngx_int_t  ngx_js_request_register_class(JSRuntime *rt);

/* Content-phase handler; installed in clcf->handler by the JS setter */
struct ngx_http_request_s;
ngx_int_t  ngx_js_content_handler(struct ngx_http_request_s *r);

/*
 * Inspect the promise of a suspended async request and finalize it if
 * the promise has settled.  Called from the timer handler in ngx_js_com.c.
 */
void ngx_js_async_check(ngx_js_worker_t *w);


#endif /* _NGX_JS_H_INCLUDED_ */
