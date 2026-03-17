
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
extern JSClassID  ngx_js_req_vars_class_id; /* r.variables exotic (Stage 28) */
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
extern JSClassID  ngx_js_auth_request_class_id; /* auth_request loc conf (Stage 13) */
extern JSClassID  ngx_js_gzip_static_class_id;  /* gzip_static loc conf  (Stage 13) */
extern JSClassID  ngx_js_memcached_class_id;    /* memcached loc conf    (Stage 13) */
extern JSClassID  ngx_js_scgi_class_id;         /* scgi loc conf         (Stage 13) */
extern JSClassID  ngx_js_uwsgi_class_id;        /* uwsgi loc conf        (Stage 13) */
extern JSClassID  ngx_js_mirror_class_id;       /* mirror loc conf       (Stage 13) */
extern JSClassID  ngx_js_events_class_id;       /* nginx.events          (Stage 14) */
extern JSClassID  ngx_js_socket_class_id;             /* nginx.createSocket()     (Stage 52) */
extern JSClassID  ngx_js_http_listener_class_id;     /* nginx.http.attach()      (Stage 52) */
extern JSClassID  ngx_js_stream_server_class_id;     /* nginx.stream.servers[]   (Stage 52G) */
extern JSClassID  ngx_js_stream_listener_class_id;   /* nginx.stream.attach()    (Stage 52G) */
extern JSClassID  ngx_js_stream_proxy_class_id;      /* server.proxy             (Stage 53)  */
extern JSClassID  ngx_js_stream_upstream_class_id;   /* nginx.stream.upstreams[] (Stage 53)  */
extern JSClassID  ngx_js_stream_peer_class_id;       /* stream config-phase peer (Stage 53)  */
extern JSClassID  ngx_js_stream_rr_peer_class_id;    /* stream runtime RR peer   (Stage 53)  */
extern JSClassID  ngx_js_stream_access_class_id;    /* stream server access     (Stage C)   */
extern JSClassID  ngx_js_stream_ssl_class_id;       /* stream server SSL        (Stage D)   */
extern JSClassID  ngx_js_stream_session_class_id;   /* stream session handler   (Stage E)   */


/*
 * Register all COM classes into a newly-created JSRuntime.
 * Must be called before any COM object is created in that runtime.
 */
ngx_int_t  ngx_js_com_register_classes(JSRuntime *rt);
ngx_int_t  ngx_js_http_register_classes(JSRuntime *rt);
ngx_int_t  ngx_js_upstream_register_classes(JSRuntime *rt);

/*
 * Install shared prototypes for all COM classes into a JSContext.
 * Must be called once per context after classes are registered.
 */
ngx_int_t  ngx_js_com_install_protos(JSContext *ctx);

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
ngx_int_t  ngx_js_auth_request_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_gzip_static_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_memcached_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_scgi_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_uwsgi_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_mirror_register_class(JSRuntime *rt);


/*
 * Install shared prototype for each COM class into a JSContext.
 */
ngx_int_t  ngx_js_location_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_server_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_upstream_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_peer_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_rr_peer_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_proxy_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_ssl_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_gzip_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_headers_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_proxy_cache_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_rewrite_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_access_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_auth_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_limit_req_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_limit_conn_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_fastcgi_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_log_install_proto(JSContext *ctx);
#if (NGX_HTTP_REALIP)
ngx_int_t  ngx_js_realip_install_proto(JSContext *ctx);
#endif
ngx_int_t  ngx_js_charset_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_sub_filter_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_autoindex_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_referer_install_proto(JSContext *ctx);
#if (NGX_HTTP_DAV)
ngx_int_t  ngx_js_dav_install_proto(JSContext *ctx);
#endif
ngx_int_t  ngx_js_ssi_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_userid_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_addition_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_gunzip_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_slice_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_image_filter_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_xslt_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_secure_link_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_mp4_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_random_index_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_auth_request_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_gzip_static_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_memcached_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_scgi_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_uwsgi_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_mirror_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_events_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_events_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_socket_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_socket_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_socket_install(JSContext *ctx, JSValue nginx_obj);
ngx_int_t  ngx_js_listener_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_listener_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_listener_install(JSContext *ctx, JSValue http_obj);
ngx_int_t  ngx_js_stream_listener_register_classes(JSRuntime *rt);
ngx_int_t  ngx_js_stream_listener_install_protos(JSContext *ctx);
ngx_int_t  ngx_js_stream_install(JSContext *ctx, JSValue nginx_obj,
    ngx_cycle_t *cycle);
ngx_int_t  ngx_js_stream_upstream_register_classes(JSRuntime *rt);
ngx_int_t  ngx_js_stream_upstream_com_install(JSContext *ctx,
    JSValue stream_obj, ngx_cycle_t *cycle);
ngx_int_t  ngx_js_stream_access_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_stream_access_install_proto(JSContext *ctx);
/* forward-declare to avoid requiring ngx_stream_access_module.h everywhere */
struct ngx_stream_access_srv_conf_s;
JSValue    ngx_js_wrap_stream_access(JSContext *ctx,
    struct ngx_stream_access_srv_conf_s *ascf);
ngx_int_t  ngx_js_stream_ssl_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_stream_ssl_install_proto(JSContext *ctx);
/* ngx_js_wrap_stream_ssl() requires ngx_stream_ssl_srv_conf_t (anonymous struct,
 * no struct tag); callers must #include "../../stream/ngx_stream_ssl_module.h"
 * and then declare: JSValue ngx_js_wrap_stream_ssl(JSContext *,
 *                              ngx_stream_ssl_srv_conf_t *); */

ngx_int_t  ngx_js_stream_session_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_stream_session_install_proto(JSContext *ctx);

/* ngx_stream_session_t is a tagged struct — forward declaration works. */
struct ngx_stream_session_s;
JSValue    ngx_js_wrap_stream_session(JSContext *ctx,
    struct ngx_stream_session_s *s,
    ngx_int_t *pending_code_p, ngx_uint_t *did_finalize_p);


/*
 * Wrap a single ngx_http_core_loc_conf_t into a NginxLocation JS object.
 * Used by ngx_js_http_module.c to implement r.location.
 * ngx_http_core_loc_conf_t is defined in <ngx_http.h>; callers must
 * include it before this header.
 */
struct ngx_http_core_loc_conf_s;
JSValue  ngx_js_wrap_location(JSContext *ctx,
    struct ngx_http_core_loc_conf_s *clcf);


/*
 * ngx_js_com_events.c — create a NginxEvents wrapper.
 * Include <ngx_event.h> before this header to get the full prototype;
 * otherwise it is declared with void * for files that don't need events.
 */
JSValue  ngx_js_wrap_events(JSContext *ctx, void *ecf);


#endif /* _NGX_JS_COM_H_INCLUDED_ */
