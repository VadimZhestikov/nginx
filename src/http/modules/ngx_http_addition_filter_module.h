
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_addition_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_ADDITION_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_ADDITION_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t     before_body;
    ngx_str_t     after_body;

    ngx_hash_t    types;
    ngx_array_t  *types_keys;
} ngx_http_addition_conf_t;


extern ngx_module_t  ngx_http_addition_filter_module;


#endif /* _NGX_HTTP_ADDITION_FILTER_MODULE_H_INCLUDED_ */
