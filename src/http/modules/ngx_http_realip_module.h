
/*
 * Minimal public interface to ngx_http_realip_module internals.
 * Only the fields required by the JS COM layer are exposed here.
 * Guarded by NGX_HTTP_REALIP — only defined when --with-http_realip_module.
 */

#ifndef _NGX_HTTP_REALIP_MODULE_H_INCLUDED_
#define _NGX_HTTP_REALIP_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_HTTP_REALIP)

#define NGX_HTTP_REALIP_XREALIP  0
#define NGX_HTTP_REALIP_XFWD     1
#define NGX_HTTP_REALIP_HEADER   2
#define NGX_HTTP_REALIP_PROXY    3

typedef struct {
    ngx_array_t  *from;      /* ngx_cidr_t[] — trusted proxy CIDRs */
    ngx_uint_t    type;      /* NGX_HTTP_REALIP_* */
    ngx_uint_t    hash;
    ngx_str_t     header;    /* custom header name (lowercased), type==HEADER */
    ngx_flag_t    recursive;
} ngx_http_realip_loc_conf_t;

extern ngx_module_t  ngx_http_realip_module;

#endif /* NGX_HTTP_REALIP */

#endif /* _NGX_HTTP_REALIP_MODULE_H_INCLUDED_ */
