
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

/*
 * Zone context — lives in cf->pool, one per limit_req_zone directive.
 * COM needs shpool (for the mutex) and rate (settable).
 * The remaining fields (sh, key, node) are opaque to the COM layer.
 */
typedef struct ngx_http_limit_req_shctx_s  ngx_http_limit_req_shctx_t;

typedef struct {
    ngx_http_limit_req_shctx_t  *sh;
    ngx_slab_pool_t             *shpool;
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   rate;
    /* remaining fields (key, node) are not used by the COM layer */
} ngx_http_limit_req_ctx_t;

extern ngx_module_t  ngx_http_limit_req_module;


#endif /* _NGX_HTTP_LIMIT_REQ_MODULE_H_INCLUDED_ */
