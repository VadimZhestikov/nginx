
/*
 * Copyright (C) nginx JS contributors
 */

#ifndef _NGX_JS_WORKER_H_INCLUDED_
#define _NGX_JS_WORKER_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/*
 * Register the global Worker constructor in ctx.
 * Called once from ngx_js_com_init (master process, before fork).
 */
ngx_int_t ngx_js_worker_install(JSContext *ctx);


#endif /* _NGX_JS_WORKER_H_INCLUDED_ */
