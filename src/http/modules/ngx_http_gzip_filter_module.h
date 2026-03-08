
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public header for ngx_http_gzip_filter_module.
 * Extracts only ngx_http_gzip_conf_t and the module symbol so that
 * external modules can read per-location gzip configuration without
 * duplicating the struct definition.
 *
 * Include AFTER <ngx_http.h>.
 */

#ifndef _NGX_HTTP_GZIP_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_GZIP_FILTER_MODULE_H_INCLUDED_


typedef struct {
    ngx_flag_t           enable;
    ngx_flag_t           no_buffer;

    ngx_hash_t           types;

    ngx_bufs_t           bufs;

    size_t               postpone_gzipping;
    ngx_int_t            level;
    size_t               wbits;
    size_t               memlevel;
    ssize_t              min_length;

    ngx_array_t         *types_keys;
} ngx_http_gzip_conf_t;


extern ngx_module_t  ngx_http_gzip_filter_module;


#endif /* _NGX_HTTP_GZIP_FILTER_MODULE_H_INCLUDED_ */
