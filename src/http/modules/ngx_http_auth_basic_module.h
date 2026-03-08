
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public header for ngx_http_auth_basic_module.
 * Include AFTER <ngx_http.h> (needs ngx_http_complex_value_t).
 */

#ifndef _NGX_HTTP_AUTH_BASIC_MODULE_H_INCLUDED_
#define _NGX_HTTP_AUTH_BASIC_MODULE_H_INCLUDED_


typedef struct {
    ngx_http_complex_value_t  *realm;
    ngx_http_complex_value_t  *user_file;
} ngx_http_auth_basic_loc_conf_t;


extern ngx_module_t  ngx_http_auth_basic_module;


#endif /* _NGX_HTTP_AUTH_BASIC_MODULE_H_INCLUDED_ */
