
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


#endif /* _NGX_JS_SW_H_INCLUDED_ */
