
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_dav_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_DAV_MODULE_H_INCLUDED_
#define _NGX_HTTP_DAV_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#if (NGX_HTTP_DAV)

#define NGX_HTTP_DAV_OFF  2


typedef struct {
    ngx_uint_t  methods;            /* bitmask of NGX_HTTP_PUT|DELETE|MKCOL|COPY|MOVE */
    ngx_uint_t  access;             /* dav_access permissions */
    ngx_uint_t  min_delete_depth;   /* min_delete_depth */
    ngx_flag_t  create_full_put_path;
} ngx_http_dav_loc_conf_t;


extern ngx_module_t  ngx_http_dav_module;

#endif /* NGX_HTTP_DAV */


#endif /* _NGX_HTTP_DAV_MODULE_H_INCLUDED_ */
