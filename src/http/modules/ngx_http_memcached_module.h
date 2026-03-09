
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_memcached_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_MEMCACHED_MODULE_H_INCLUDED_
#define _NGX_HTTP_MEMCACHED_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_upstream_conf_t   upstream;
    ngx_int_t                  index;
    ngx_uint_t                 gzip_flag;
} ngx_http_memcached_loc_conf_t;


extern ngx_module_t  ngx_http_memcached_module;


#endif /* _NGX_HTTP_MEMCACHED_MODULE_H_INCLUDED_ */
