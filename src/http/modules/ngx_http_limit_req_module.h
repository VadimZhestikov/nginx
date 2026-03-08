
/*
 * Minimal public interface to ngx_http_limit_req_module internals.
 * Only the fields required by the JS COM layer are exposed here.
 */

#ifndef _NGX_HTTP_LIMIT_REQ_MODULE_H_INCLUDED_
#define _NGX_HTTP_LIMIT_REQ_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_shm_zone_t  *shm_zone;
    ngx_uint_t       burst;
    ngx_uint_t       delay;   /* NGX_MAX_UINT32_VALUE means nodelay */
} ngx_http_limit_req_limit_t;

typedef struct {
    ngx_array_t      limits;           /* embedded ngx_http_limit_req_limit_t[] */
    ngx_uint_t       limit_log_level;
    ngx_uint_t       delay_log_level;
    ngx_uint_t       status_code;
    ngx_flag_t       dry_run;
} ngx_http_limit_req_conf_t;

extern ngx_module_t  ngx_http_limit_req_module;


#endif /* _NGX_HTTP_LIMIT_REQ_MODULE_H_INCLUDED_ */
