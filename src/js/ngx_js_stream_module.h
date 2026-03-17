
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage E — Stream JS session handlers.
 *
 * Public interface for ngx_js_stream_module (NGX_STREAM_MODULE):
 *  - per-server JS config struct
 *  - stream content handler callback
 *  - NginxStreamSession class wrap helper
 */

#ifndef _NGX_JS_STREAM_MODULE_H_INCLUDED_
#define _NGX_JS_STREAM_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


typedef struct {
    ngx_int_t    handler_idx;   /* index into __ngx_handlers__; -1 = none */
} ngx_js_stream_srv_conf_t;


extern ngx_module_t  ngx_js_stream_module;


/*
 * Stream content handler — set as cscf->handler when server.handler is
 * assigned from JS.  Calls the stored __ngx_handlers__[idx] function with
 * a NginxStreamSession wrapper, then finalises the session.
 */
void  ngx_js_stream_content_handler(ngx_stream_session_t *s);


#endif /* _NGX_JS_STREAM_MODULE_H_INCLUDED_ */
