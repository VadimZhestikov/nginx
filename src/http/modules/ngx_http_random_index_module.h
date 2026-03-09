
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_random_index_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_RANDOM_INDEX_MODULE_H_INCLUDED_
#define _NGX_HTTP_RANDOM_INDEX_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_flag_t  enable;
} ngx_http_random_index_loc_conf_t;


extern ngx_module_t  ngx_http_random_index_module;


#endif /* _NGX_HTTP_RANDOM_INDEX_MODULE_H_INCLUDED_ */
