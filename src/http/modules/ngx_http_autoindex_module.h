
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_autoindex_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_AUTOINDEX_MODULE_H_INCLUDED_
#define _NGX_HTTP_AUTOINDEX_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_AUTOINDEX_HTML   0
#define NGX_HTTP_AUTOINDEX_JSON   1
#define NGX_HTTP_AUTOINDEX_JSONP  2
#define NGX_HTTP_AUTOINDEX_XML    3


typedef struct {
    ngx_flag_t     enable;
    ngx_uint_t     format;
    ngx_flag_t     localtime;
    ngx_flag_t     exact_size;
} ngx_http_autoindex_loc_conf_t;


extern ngx_module_t  ngx_http_autoindex_module;


#endif /* _NGX_HTTP_AUTOINDEX_MODULE_H_INCLUDED_ */
