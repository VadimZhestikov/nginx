
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_sw.h — SharedWorker class: long-lived JS thread in the master
 * process, reachable by all nginx worker processes via pre-allocated pipes.
 */

#ifndef _NGX_JS_SW_H_INCLUDED_
#define _NGX_JS_SW_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>
#include "ngx_js.h"


/*
 * Install the global SharedWorker constructor into ctx.
 * Called once per process from ngx_js_com_init().
 */
ngx_int_t  ngx_js_sw_install(JSContext *ctx);

/*
 * Release per-worker resources (epoll connection, on_message JSValue).
 * Called from ngx_js_exit_process() before JS_FreeContext().
 */
void  ngx_js_sw_exit_process(ngx_cycle_t *cycle, ngx_js_conf_t *jcf);

/*
 * Terminate all SW threads and free all SharedWorker state.
 * Called from ngx_js_exit_master() before JS_FreeContext().
 */
void  ngx_js_sw_exit_master(ngx_js_conf_t *jcf);

/*
 * Create the command socketpair and term pipe, then start the SW manager
 * thread.  The manager handles new SharedWorker(url) requests from worker
 * processes, creating SW threads in the master on demand.
 * Called once at the end of ngx_js_init_conf().
 */
ngx_int_t  ngx_js_sw_manager_start(ngx_js_conf_t *jcf,
    ngx_cycle_t *cycle);


#endif /* _NGX_JS_SW_H_INCLUDED_ */
