
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_userid_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_USERID_MODULE_H_INCLUDED_
#define _NGX_HTTP_USERID_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_USERID_OFF   0
#define NGX_HTTP_USERID_LOG   1
#define NGX_HTTP_USERID_V1    2
#define NGX_HTTP_USERID_ON    3

#define NGX_HTTP_USERID_MAX_EXPIRES  2145916555


typedef struct {
    ngx_uint_t  enable;
    ngx_uint_t  flags;

    ngx_int_t   service;

    ngx_str_t   name;
    ngx_str_t   domain;
    ngx_str_t   path;
    ngx_str_t   p3p;

    time_t      expires;

    u_char      mark;
} ngx_http_userid_conf_t;


extern ngx_module_t  ngx_http_userid_filter_module;


#endif /* _NGX_HTTP_USERID_MODULE_H_INCLUDED_ */
