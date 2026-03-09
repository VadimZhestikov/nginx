
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_mp4_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_MP4_MODULE_H_INCLUDED_
#define _NGX_HTTP_MP4_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    size_t      buffer_size;
    size_t      max_buffer_size;
    ngx_flag_t  start_key_frame;
} ngx_http_mp4_conf_t;


extern ngx_module_t  ngx_http_mp4_module;


#endif /* _NGX_HTTP_MP4_MODULE_H_INCLUDED_ */
