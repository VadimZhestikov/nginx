
/*
 * Copyright (C) nginx JS contributors
 */

#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/*
 * Per-cycle configuration owned by ngx_js_module (NGX_CORE_MODULE).
 * Allocated in cycle->pool via create_conf; populated by js_include
 * directives during ngx_conf_parse(), executed by init_conf().
 */
typedef struct {
    ngx_array_t   includes;    /* ngx_str_t: resolved paths from js_include */
    JSRuntime    *rt;          /* master-process QuickJS runtime             */
    JSContext    *ctx;         /* master-process QuickJS context             */
    void         *worker;     /* ngx_js_worker_t* after fork (in workers)   */
} ngx_js_conf_t;


/*
 * Per-worker JS runtime created in init_process().
 * Workers never share a JSRuntime — QuickJS is not thread-safe.
 */
typedef struct {
    JSRuntime    *rt;
    JSContext    *ctx;
} ngx_js_worker_t;


extern ngx_module_t  ngx_js_module;


/* COM initialisation — installs nginx.* into ctx's global object */
ngx_int_t  ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle);

/* Log a pending JS exception to the NGINX error log, then clear it */
void       ngx_js_log_exception(JSContext *ctx, ngx_log_t *log);


#endif /* _NGX_JS_H_INCLUDED_ */
