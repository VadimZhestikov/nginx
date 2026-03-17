
/*
 * Copyright (C) nginx JS contributors
 *
 * Public interface for ngx_stream_access_module — exposes the srv conf
 * struct so JS COM files can access stream access configuration without
 * including the monolithic module .c file.
 */

#ifndef _NGX_STREAM_ACCESS_MODULE_H_INCLUDED_
#define _NGX_STREAM_ACCESS_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


typedef struct {
    in_addr_t    mask;
    in_addr_t    addr;
    ngx_uint_t   deny;    /* unsigned deny:1; */
} ngx_stream_access_rule_t;

#if (NGX_HAVE_INET6)

typedef struct {
    struct in6_addr   addr;
    struct in6_addr   mask;
    ngx_uint_t        deny;
} ngx_stream_access_rule6_t;

#endif

#if (NGX_HAVE_UNIX_DOMAIN)

typedef struct {
    ngx_uint_t   deny;
} ngx_stream_access_rule_un_t;

#endif

typedef struct ngx_stream_access_srv_conf_s {
    ngx_array_t  *rules;      /* ngx_stream_access_rule_t[]   */
#if (NGX_HAVE_INET6)
    ngx_array_t  *rules6;     /* ngx_stream_access_rule6_t[]  */
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    ngx_array_t  *rules_un;   /* ngx_stream_access_rule_un_t[] */
#endif
} ngx_stream_access_srv_conf_t;


extern ngx_module_t  ngx_stream_access_module;


#endif /* _NGX_STREAM_ACCESS_MODULE_H_INCLUDED_ */
