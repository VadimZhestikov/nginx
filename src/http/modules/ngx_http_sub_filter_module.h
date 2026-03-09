
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_sub_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_SUB_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_SUB_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_complex_value_t   match;
    ngx_http_complex_value_t   value;
} ngx_http_sub_pair_t;


/* Forward declaration — we never dereference this */
typedef struct ngx_http_sub_tables_s  ngx_http_sub_tables_t;


typedef struct {
    ngx_uint_t                 dynamic; /* unsigned dynamic:1 */

    ngx_array_t               *pairs;   /* ngx_http_sub_pair_t[] */
    ngx_http_sub_tables_t     *tables;  /* internal; must keep for layout */

    ngx_hash_t                 types;

    ngx_flag_t                 once;
    ngx_flag_t                 last_modified;

    ngx_array_t               *types_keys;
    ngx_array_t               *matches;
} ngx_http_sub_loc_conf_t;


extern ngx_module_t  ngx_http_sub_filter_module;


#endif /* _NGX_HTTP_SUB_FILTER_MODULE_H_INCLUDED_ */
