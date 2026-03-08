
/*
 * Minimal public interface to ngx_http_charset_filter_module internals.
 * Only the fields required by the JS COM layer are exposed here.
 */

#ifndef _NGX_HTTP_CHARSET_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_CHARSET_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_CHARSET_OFF    -2

typedef struct {
    u_char                    **tables;
    ngx_str_t                   name;
    unsigned                    length:16;
    unsigned                    utf8:1;
} ngx_http_charset_t;

typedef struct {
    ngx_array_t                 charsets;   /* ngx_http_charset_t[] */
    ngx_array_t                 tables;
    ngx_array_t                 recodes;
} ngx_http_charset_main_conf_t;

typedef struct {
    ngx_int_t                   charset;         /* index or NGX_CONF_UNSET */
    ngx_int_t                   source_charset;  /* index or NGX_CONF_UNSET */
    ngx_flag_t                  override_charset;

    ngx_hash_t                  types;
    ngx_array_t                *types_keys;
} ngx_http_charset_loc_conf_t;

extern ngx_module_t  ngx_http_charset_filter_module;


#endif /* _NGX_HTTP_CHARSET_FILTER_MODULE_H_INCLUDED_ */
