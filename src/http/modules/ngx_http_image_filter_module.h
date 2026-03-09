
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_image_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_IMAGE_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_IMAGE_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_IMAGE_OFF       0
#define NGX_HTTP_IMAGE_TEST      1
#define NGX_HTTP_IMAGE_SIZE      2
#define NGX_HTTP_IMAGE_RESIZE    3
#define NGX_HTTP_IMAGE_CROP      4
#define NGX_HTTP_IMAGE_ROTATE    5


typedef struct {
    ngx_uint_t                   filter;
    ngx_uint_t                   width;
    ngx_uint_t                   height;
    ngx_uint_t                   angle;
    ngx_uint_t                   jpeg_quality;
    ngx_uint_t                   webp_quality;
    ngx_uint_t                   sharpen;

    ngx_flag_t                   transparency;
    ngx_flag_t                   interlace;

    ngx_http_complex_value_t    *wcv;
    ngx_http_complex_value_t    *hcv;
    ngx_http_complex_value_t    *acv;
    ngx_http_complex_value_t    *jqcv;
    ngx_http_complex_value_t    *wqcv;
    ngx_http_complex_value_t    *shcv;

    size_t                       buffer_size;
} ngx_http_image_filter_conf_t;


extern ngx_module_t  ngx_http_image_filter_module;


#endif /* _NGX_HTTP_IMAGE_FILTER_MODULE_H_INCLUDED_ */
