
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public header for ngx_http_headers_filter_module.
 * Extracts types needed by external modules to read per-location
 * add_header / add_trailer / expires configuration.
 *
 * Include AFTER <ngx_http.h> (which brings in ngx_http_complex_value_t).
 */

#ifndef _NGX_HTTP_HEADERS_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_HEADERS_FILTER_MODULE_H_INCLUDED_


#define NGX_HTTP_HEADERS_INHERIT_OFF    0
#define NGX_HTTP_HEADERS_INHERIT_ON     1
#define NGX_HTTP_HEADERS_INHERIT_MERGE  2


typedef enum {
    NGX_HTTP_EXPIRES_OFF,
    NGX_HTTP_EXPIRES_EPOCH,
    NGX_HTTP_EXPIRES_MAX,
    NGX_HTTP_EXPIRES_ACCESS,
    NGX_HTTP_EXPIRES_MODIFIED,
    NGX_HTTP_EXPIRES_DAILY,
    NGX_HTTP_EXPIRES_UNSET
} ngx_http_expires_t;


typedef struct {
    ngx_http_complex_value_t   value;
    ngx_str_t                  key;
    void                      *handler;   /* ngx_http_set_header_pt — opaque */
    ngx_uint_t                 offset;
    ngx_uint_t                 always;    /* unsigned  always:1 */
} ngx_http_header_val_t;


typedef struct {
    ngx_http_expires_t         expires;
    time_t                     expires_time;
    ngx_http_complex_value_t  *expires_value;
    ngx_array_t               *headers;
    ngx_array_t               *trailers;
    ngx_uint_t                 headers_inherit;
    ngx_uint_t                 trailers_inherit;
} ngx_http_headers_conf_t;


extern ngx_module_t  ngx_http_headers_filter_module;


#endif /* _NGX_HTTP_HEADERS_FILTER_MODULE_H_INCLUDED_ */
