
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public header for ngx_http_access_module.
 * Include AFTER <ngx_http.h>.
 */

#ifndef _NGX_HTTP_ACCESS_MODULE_H_INCLUDED_
#define _NGX_HTTP_ACCESS_MODULE_H_INCLUDED_


typedef struct {
    in_addr_t         mask;
    in_addr_t         addr;
    ngx_uint_t        deny;      /* unsigned  deny:1; */
} ngx_http_access_rule_t;

#if (NGX_HAVE_INET6)
typedef struct {
    struct in6_addr   addr;
    struct in6_addr   mask;
    ngx_uint_t        deny;      /* unsigned  deny:1; */
} ngx_http_access_rule6_t;
#endif

#if (NGX_HAVE_UNIX_DOMAIN)
typedef struct {
    ngx_uint_t        deny;      /* unsigned  deny:1; */
} ngx_http_access_rule_un_t;
#endif

typedef struct {
    ngx_array_t      *rules;     /* array of ngx_http_access_rule_t */
#if (NGX_HAVE_INET6)
    ngx_array_t      *rules6;    /* array of ngx_http_access_rule6_t */
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    ngx_array_t      *rules_un;  /* array of ngx_http_access_rule_un_t */
#endif
} ngx_http_access_loc_conf_t;


extern ngx_module_t  ngx_http_access_module;


#endif /* _NGX_HTTP_ACCESS_MODULE_H_INCLUDED_ */
