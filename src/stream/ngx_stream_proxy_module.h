
/*
 * Copyright (C) nginx JS contributors
 *
 * Public interface for ngx_stream_proxy_module — exposes the srv conf
 * struct so JS COM files can access proxy configuration without having
 * to include the monolithic module .c file.
 */

#ifndef _NGX_STREAM_PROXY_MODULE_H_INCLUDED_
#define _NGX_STREAM_PROXY_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


typedef struct {
    ngx_addr_t                      *addr;
    ngx_stream_complex_value_t      *value;
#if (NGX_HAVE_TRANSPARENT_PROXY)
    ngx_uint_t                       transparent; /* unsigned transparent:1; */
#endif
} ngx_stream_upstream_local_t;


typedef struct {
    ngx_msec_t                       connect_timeout;
    ngx_msec_t                       timeout;
    ngx_msec_t                       next_upstream_timeout;
    size_t                           buffer_size;
    ngx_stream_complex_value_t      *upload_rate;
    ngx_stream_complex_value_t      *download_rate;
    ngx_uint_t                       requests;
    ngx_uint_t                       responses;
    ngx_uint_t                       next_upstream_tries;
    ngx_flag_t                       next_upstream;
    ngx_flag_t                       proxy_protocol;
    ngx_flag_t                       half_close;
    ngx_stream_upstream_local_t     *local;
    ngx_flag_t                       socket_keepalive;

#if (NGX_STREAM_SSL)
    ngx_flag_t                       ssl_enable;
    ngx_flag_t                       ssl_session_reuse;
    ngx_uint_t                       ssl_protocols;
    ngx_str_t                        ssl_ciphers;
    ngx_stream_complex_value_t      *ssl_name;
    ngx_flag_t                       ssl_server_name;

    ngx_flag_t                       ssl_verify;
    ngx_uint_t                       ssl_verify_depth;
    ngx_str_t                        ssl_trusted_certificate;
    ngx_str_t                        ssl_crl;
    ngx_stream_complex_value_t      *ssl_certificate;
    ngx_stream_complex_value_t      *ssl_certificate_key;
    ngx_ssl_cache_t                 *ssl_certificate_cache;
    ngx_array_t                     *ssl_passwords;
    ngx_array_t                     *ssl_conf_commands;

    ngx_ssl_t                       *ssl;
#endif

    ngx_stream_upstream_srv_conf_t  *upstream;
    ngx_stream_complex_value_t      *upstream_value;
} ngx_stream_proxy_srv_conf_t;


extern ngx_module_t  ngx_stream_proxy_module;


#endif /* _NGX_STREAM_PROXY_MODULE_H_INCLUDED_ */
