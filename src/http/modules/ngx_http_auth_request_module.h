
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_auth_request_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_AUTH_REQUEST_MODULE_H_INCLUDED_
#define _NGX_HTTP_AUTH_REQUEST_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t     uri;
    ngx_array_t  *vars;
} ngx_http_auth_request_conf_t;


extern ngx_module_t  ngx_http_auth_request_module;


#endif /* _NGX_HTTP_AUTH_REQUEST_MODULE_H_INCLUDED_ */
