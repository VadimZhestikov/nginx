
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
extern JSClassID  ngx_js_headers_class_id;  /* headers filter loc (Stage 8)  */
extern JSClassID  ngx_js_proxy_cache_class_id; /* proxy cache conf (Stage 9)  */
extern JSClassID  ngx_js_rewrite_class_id;     /* rewrite loc conf (Stage 10) */
extern JSClassID  ngx_js_access_class_id;      /* access loc conf  (Stage 11) */
extern JSClassID  ngx_js_auth_class_id;        /* auth_basic conf  (Stage 11) */
extern JSClassID  ngx_js_limit_req_class_id;   /* limit_req conf   (Stage 12) */
extern JSClassID  ngx_js_limit_conn_class_id;  /* limit_conn conf  (Stage 12) */
extern JSClassID  ngx_js_fastcgi_class_id;     /* fastcgi loc conf (Stage 13) */
extern JSClassID  ngx_js_log_class_id;         /* access log conf  (Stage 13) */
extern JSClassID  ngx_js_realip_class_id;      /* realip loc conf  (Stage 13) */
extern JSClassID  ngx_js_charset_class_id;     /* charset loc conf (Stage 13) */
extern JSClassID  ngx_js_sub_filter_class_id;  /* sub_filter loc conf (Stage 13) */
extern JSClassID  ngx_js_autoindex_class_id;   /* autoindex loc conf  (Stage 13) */
extern JSClassID  ngx_js_referer_class_id;     /* referer loc conf    (Stage 13) */
extern JSClassID  ngx_js_dav_class_id;         /* dav loc conf        (Stage 13) */
extern JSClassID  ngx_js_ssi_class_id;         /* ssi loc conf        (Stage 13) */
extern JSClassID  ngx_js_userid_class_id;      /* userid loc conf     (Stage 13) */
extern JSClassID  ngx_js_addition_class_id;    /* addition loc conf   (Stage 13) */
extern JSClassID  ngx_js_gunzip_class_id;      /* gunzip loc conf     (Stage 13) */
extern JSClassID  ngx_js_slice_class_id;       /* slice loc conf      (Stage 13) */
extern JSClassID  ngx_js_image_filter_class_id; /* image_filter loc conf (Stage 13) */
extern JSClassID  ngx_js_xslt_class_id;         /* xslt loc conf         (Stage 13) */
extern JSClassID  ngx_js_secure_link_class_id;  /* secure_link loc conf  (Stage 13) */
extern JSClassID  ngx_js_mp4_class_id;          /* mp4 loc conf          (Stage 13) */
extern JSClassID  ngx_js_random_index_class_id; /* random_index loc conf (Stage 13) */


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
ngx_int_t  ngx_js_headers_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_proxy_cache_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_rewrite_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_access_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_auth_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_limit_req_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_limit_conn_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_fastcgi_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_log_register_class(JSRuntime *rt);
#if (NGX_HTTP_REALIP)
ngx_int_t  ngx_js_realip_register_class(JSRuntime *rt);
#endif
ngx_int_t  ngx_js_charset_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_sub_filter_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_autoindex_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_referer_register_class(JSRuntime *rt);
#if (NGX_HTTP_DAV)
ngx_int_t  ngx_js_dav_register_class(JSRuntime *rt);
#endif
ngx_int_t  ngx_js_ssi_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_userid_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_addition_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_gunzip_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_slice_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_image_filter_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_xslt_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_secure_link_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_mp4_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_random_index_register_class(JSRuntime *rt);


#endif /* _NGX_JS_COM_H_INCLUDED_ */
