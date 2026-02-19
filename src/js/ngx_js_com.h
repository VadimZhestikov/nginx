
/*
 * Copyright (C) nginx JS contributors
 *
 * COM class IDs and shared declarations.
 *
 * Each wrappable NGINX C struct gets a unique JSClassID allocated once
 * (lazily, on first JS_NewRuntime call) via JS_NewClassID().  All COM
 * source files share these IDs through this header.
 */

#ifndef _NGX_JS_COM_H_INCLUDED_
#define _NGX_JS_COM_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/* ---- Class IDs ---- */

extern JSClassID  ngx_js_cycle_class_id;
extern JSClassID  ngx_js_http_class_id;
extern JSClassID  ngx_js_server_class_id;
extern JSClassID  ngx_js_location_class_id;
extern JSClassID  ngx_js_upstream_class_id;
extern JSClassID  ngx_js_peer_class_id;


/*
 * Register all COM classes into a newly-created JSRuntime.
 * Must be called before any COM object is created in that runtime.
 */
ngx_int_t  ngx_js_com_register_classes(JSRuntime *rt);

/*
 * Install nginx.http subtree into the nginx_obj JS object.
 * Called from ngx_js_com_init() in ngx_js_com.c.
 */
ngx_int_t  ngx_js_http_com_install(JSContext *ctx, JSValue nginx_obj,
    ngx_cycle_t *cycle);

/*
 * Install nginx.http.upstreams[] into the http_obj JS object.
 * Called from ngx_js_http_com_install().
 */
ngx_int_t  ngx_js_upstream_com_install(JSContext *ctx, JSValue http_obj,
    ngx_cycle_t *cycle);


#endif /* _NGX_JS_COM_H_INCLUDED_ */
