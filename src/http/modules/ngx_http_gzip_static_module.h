
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_gzip_static_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_GZIP_STATIC_MODULE_H_INCLUDED_
#define _NGX_HTTP_GZIP_STATIC_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_GZIP_STATIC_OFF     0
#define NGX_HTTP_GZIP_STATIC_ON      1
#define NGX_HTTP_GZIP_STATIC_ALWAYS  2

typedef struct {
    ngx_uint_t  enable;
} ngx_http_gzip_static_conf_t;


extern ngx_module_t  ngx_http_gzip_static_module;


#endif /* _NGX_HTTP_GZIP_STATIC_MODULE_H_INCLUDED_ */
