
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_secure_link_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_SECURE_LINK_MODULE_H_INCLUDED_
#define _NGX_HTTP_SECURE_LINK_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_complex_value_t  *variable;
    ngx_http_complex_value_t  *md5;
    ngx_str_t                  secret;
} ngx_http_secure_link_conf_t;


extern ngx_module_t  ngx_http_secure_link_module;


#endif /* _NGX_HTTP_SECURE_LINK_MODULE_H_INCLUDED_ */
