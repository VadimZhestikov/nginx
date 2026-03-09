
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_slice_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_SLICE_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_SLICE_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    size_t               size;
} ngx_http_slice_loc_conf_t;


extern ngx_module_t  ngx_http_slice_filter_module;


#endif /* _NGX_HTTP_SLICE_FILTER_MODULE_H_INCLUDED_ */
