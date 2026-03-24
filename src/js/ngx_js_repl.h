
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_repl — REPL primitives exposed as nginx.repl.*
 */

#ifndef _NGX_JS_REPL_H_INCLUDED_
#define _NGX_JS_REPL_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>

/*
 * Install nginx.repl.{eval, attach, detach, listen, _writeFd} into ctx.
 * nginx_obj must be the "nginx" JS object (not owned — caller keeps ref).
 * Called from ngx_js_com_init after nginx_obj is populated.
 */
ngx_int_t  ngx_js_repl_install(JSContext *ctx, JSValue nginx_obj);

#endif /* _NGX_JS_REPL_H_INCLUDED_ */
