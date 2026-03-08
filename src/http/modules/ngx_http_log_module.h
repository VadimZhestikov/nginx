
/*
 * Minimal public interface to ngx_http_log_module internals.
 * Only the fields required by the JS COM layer are exposed here.
 */

#ifndef _NGX_HTTP_LOG_MODULE_H_INCLUDED_
#define _NGX_HTTP_LOG_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t                   name;
    ngx_array_t                *flushes;
    ngx_array_t                *ops;        /* array of ngx_http_log_op_t */
} ngx_http_log_fmt_t;

typedef struct {
    ngx_array_t                *lengths;
    ngx_array_t                *values;
} ngx_http_log_script_t;

typedef struct {
    ngx_open_file_t            *file;
    ngx_http_log_script_t      *script;
    time_t                      disk_full_time;
    time_t                      error_log_time;
    ngx_syslog_peer_t          *syslog_peer;
    ngx_http_log_fmt_t         *format;
    ngx_http_complex_value_t   *filter;
} ngx_http_log_t;

typedef struct {
    ngx_array_t                *logs;   /* ngx_http_log_t[] */

    ngx_open_file_cache_t      *open_file_cache;
    time_t                      open_file_cache_valid;
    ngx_uint_t                  open_file_cache_min_uses;

    ngx_uint_t                  off;    /* unsigned off:1 */
} ngx_http_log_loc_conf_t;

extern ngx_module_t  ngx_http_log_module;


#endif /* _NGX_HTTP_LOG_MODULE_H_INCLUDED_ */
