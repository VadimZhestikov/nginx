
/*
 * Minimal public interface to ngx_http_fastcgi_module internals.
 * Only the fields required by the JS COM layer are exposed here.
 */

#ifndef _NGX_HTTP_FASTCGI_MODULE_H_INCLUDED_
#define _NGX_HTTP_FASTCGI_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_uint_t                     number;
    ngx_hash_t                     hash;
} ngx_http_fastcgi_params_t;

typedef struct {
    ngx_http_upstream_conf_t       upstream;

    ngx_str_t                      index;

    ngx_http_fastcgi_params_t      params;
#if (NGX_HTTP_CACHE)
    ngx_http_fastcgi_params_t      params_cache;
#endif

    ngx_array_t                   *params_source;  /* ngx_keyval_t[]  */
    ngx_array_t                   *catch_stderr;   /* ngx_str_t[]     */

    ngx_array_t                   *fastcgi_lengths;
    ngx_array_t                   *fastcgi_values;

    ngx_flag_t                     keep_conn;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t       cache_key;
#endif

#if (NGX_PCRE)
    ngx_regex_t                   *split_regex;
    ngx_str_t                      split_name;
#endif
} ngx_http_fastcgi_loc_conf_t;

extern ngx_module_t  ngx_http_fastcgi_module;


#endif /* _NGX_HTTP_FASTCGI_MODULE_H_INCLUDED_ */
