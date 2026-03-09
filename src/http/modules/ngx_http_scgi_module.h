
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_scgi_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_SCGI_MODULE_H_INCLUDED_
#define _NGX_HTTP_SCGI_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t  *flushes;
    ngx_array_t  *lengths;
    ngx_array_t  *values;
    ngx_uint_t    number;
    ngx_hash_t    hash;
} ngx_http_scgi_params_t;

typedef struct {
    ngx_http_upstream_conf_t   upstream;

    ngx_http_scgi_params_t     params;
#if (NGX_HTTP_CACHE)
    ngx_http_scgi_params_t     params_cache;
#endif
    ngx_array_t               *params_source;

    ngx_array_t               *scgi_lengths;
    ngx_array_t               *scgi_values;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t   cache_key;
#endif
} ngx_http_scgi_loc_conf_t;


extern ngx_module_t  ngx_http_scgi_module;


#endif /* _NGX_HTTP_SCGI_MODULE_H_INCLUDED_ */
