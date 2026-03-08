
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public header for ngx_http_rewrite_module.
 * Exposes only ngx_http_rewrite_loc_conf_t so that external modules
 * can read per-location rewrite configuration.
 *
 * Include AFTER <ngx_http.h>.
 */

#ifndef _NGX_HTTP_REWRITE_MODULE_H_INCLUDED_
#define _NGX_HTTP_REWRITE_MODULE_H_INCLUDED_


typedef struct {
    ngx_array_t  *codes;          /* compiled script bytecode (uintptr_t[]) */

    ngx_uint_t    stack_size;

    ngx_flag_t    log;
    ngx_flag_t    uninitialized_variable_warn;
} ngx_http_rewrite_loc_conf_t;


extern ngx_module_t  ngx_http_rewrite_module;


#endif /* _NGX_HTTP_REWRITE_MODULE_H_INCLUDED_ */
