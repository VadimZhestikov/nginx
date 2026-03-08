
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
extern JSClassID  ngx_js_peer_class_id;     /* config-phase peer (Phase 1/2) */
extern JSClassID  ngx_js_rr_peer_class_id;  /* runtime RR peer   (Phase 3)   */
extern JSClassID  ngx_js_request_class_id;  /* per-request object (Phase 4)  */
extern JSClassID  ngx_js_proxy_class_id;    /* proxy_pass conf    (Stage 4)  */
extern JSClassID  ngx_js_ssl_class_id;      /* SSL server conf    (Stage 6)  */
extern JSClassID  ngx_js_gzip_class_id;     /* gzip loc conf      (Stage 7)  */


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

/*
 * Register NginxProxy class with a runtime.
 * Called from ngx_js_http_register_classes().
 */
ngx_int_t  ngx_js_proxy_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_ssl_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_gzip_register_class(JSRuntime *rt);


#endif /* _NGX_JS_COM_H_INCLUDED_ */
