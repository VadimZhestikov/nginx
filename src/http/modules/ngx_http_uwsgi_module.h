
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_uwsgi_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_UWSGI_MODULE_H_INCLUDED_
#define _NGX_HTTP_UWSGI_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t  *flushes;
    ngx_array_t  *lengths;
    ngx_array_t  *values;
    ngx_uint_t    number;
    ngx_hash_t    hash;
} ngx_http_uwsgi_params_t;

typedef struct {
    ngx_http_upstream_conf_t   upstream;

    ngx_http_uwsgi_params_t    params;
#if (NGX_HTTP_CACHE)
    ngx_http_uwsgi_params_t    params_cache;
#endif
    ngx_array_t               *params_source;

    ngx_array_t               *uwsgi_lengths;
    ngx_array_t               *uwsgi_values;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t   cache_key;
#endif

    ngx_str_t                  uwsgi_string;

    ngx_uint_t                 modifier1;
    ngx_uint_t                 modifier2;

#if (NGX_HTTP_SSL)
    ngx_uint_t                 ssl;
    ngx_uint_t                 ssl_protocols;
    ngx_str_t                  ssl_ciphers;
    ngx_uint_t                 ssl_verify_depth;
    ngx_str_t                  ssl_trusted_certificate;
    ngx_str_t                  ssl_crl;
    ngx_array_t               *ssl_conf_commands;
#endif
} ngx_http_uwsgi_loc_conf_t;


extern ngx_module_t  ngx_http_uwsgi_module;


#endif /* _NGX_HTTP_UWSGI_MODULE_H_INCLUDED_ */
