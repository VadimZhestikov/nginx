
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_referer_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_REFERER_MODULE_H_INCLUDED_
#define _NGX_HTTP_REFERER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_hash_combined_t      hash;

#if (NGX_PCRE)
    ngx_array_t             *regex;
    ngx_array_t             *server_name_regex;
#endif

    ngx_flag_t               no_referer;
    ngx_flag_t               blocked_referer;
    ngx_flag_t               server_names;

    ngx_hash_keys_arrays_t  *keys;

    ngx_uint_t               referer_hash_max_size;
    ngx_uint_t               referer_hash_bucket_size;
} ngx_http_referer_conf_t;


extern ngx_module_t  ngx_http_referer_module;


#endif /* _NGX_HTTP_REFERER_MODULE_H_INCLUDED_ */
