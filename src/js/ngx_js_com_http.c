
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — read-only Phase 1.
 *
 * Exposes:
 *   nginx.http.servers[]           — NginxServer objects
 *   nginx.http.servers[i].name     — first server_name string
 *   nginx.http.servers[i].names[]  — all server_name strings
 *   nginx.http.servers[i].root     — default document root
 *   nginx.http.servers[i].locations[]
 *   nginx.http.servers[i].locations[j].path
 *   nginx.http.servers[i].locations[j].root
 *
 * nginx.http.upstreams[] is delegated to ngx_js_com_upstream.c.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_proxy_module.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"

#if (NGX_HTTP_SSL)
#include <ngx_http_ssl_module.h>
#endif


/* Forward declarations from ngx_js_com_proxy.c and ngx_js_com_ssl.c */
JSValue  ngx_js_wrap_proxy(JSContext *ctx, ngx_http_proxy_loc_conf_t *plcf);

#if (NGX_HTTP_SSL)
JSValue  ngx_js_wrap_ssl(JSContext *ctx, ngx_http_ssl_srv_conf_t *sscf);
#endif

/* Forward declaration from ngx_js_com_gzip.c */
#ifdef NGX_HTTP_GZIP
#include "../http/modules/ngx_http_gzip_filter_module.h"
JSValue  ngx_js_wrap_gzip(JSContext *ctx, ngx_http_gzip_conf_t *gcf,
    ngx_http_core_loc_conf_t *clcf);
#endif

/* Forward declaration from ngx_js_com_headers.c */
#include "../http/modules/ngx_http_headers_filter_module.h"
JSValue  ngx_js_wrap_headers(JSContext *ctx, ngx_http_headers_conf_t *hcf);

/* Forward declaration from ngx_js_com_rewrite.c */
#include "../http/modules/ngx_http_rewrite_module.h"
JSValue  ngx_js_wrap_rewrite(JSContext *ctx, ngx_http_rewrite_loc_conf_t *rlcf);

/* Forward declarations from ngx_js_com_access.c and ngx_js_com_auth.c */
#include "../http/modules/ngx_http_access_module.h"
#include "../http/modules/ngx_http_auth_basic_module.h"
JSValue  ngx_js_wrap_access(JSContext *ctx, ngx_http_access_loc_conf_t *alcf);
JSValue  ngx_js_wrap_auth(JSContext *ctx, ngx_http_auth_basic_loc_conf_t *alcf);

/* Forward declarations from ngx_js_com_limit_req.c and ngx_js_com_limit_conn.c */
#include "../http/modules/ngx_http_limit_req_module.h"
#include "../http/modules/ngx_http_limit_conn_module.h"
JSValue  ngx_js_wrap_limit_req(JSContext *ctx, ngx_http_limit_req_conf_t *lrcf);
JSValue  ngx_js_wrap_limit_conn(JSContext *ctx, ngx_http_limit_conn_conf_t *lccf);

/* Forward declaration from ngx_js_com_fastcgi.c */
#include "../http/modules/ngx_http_fastcgi_module.h"
JSValue  ngx_js_wrap_fastcgi(JSContext *ctx, ngx_http_fastcgi_loc_conf_t *flcf);

/* Forward declaration from ngx_js_com_log.c */
#include "../http/modules/ngx_http_log_module.h"
JSValue  ngx_js_wrap_log(JSContext *ctx, ngx_http_log_loc_conf_t *llcf);

/* Forward declaration from ngx_js_com_realip.c */
#include "../http/modules/ngx_http_realip_module.h"
#if (NGX_HTTP_REALIP)
JSValue  ngx_js_wrap_realip(JSContext *ctx, ngx_http_realip_loc_conf_t *rlcf);
#endif

/* Forward declaration from ngx_js_com_charset.c */
#include "../http/modules/ngx_http_charset_filter_module.h"
JSValue  ngx_js_wrap_charset(JSContext *ctx,
    ngx_http_charset_loc_conf_t *lcf, ngx_http_charset_main_conf_t *mcf);

/* Forward declaration from ngx_js_com_sub_filter.c */
#include "../http/modules/ngx_http_sub_filter_module.h"
JSValue  ngx_js_wrap_sub_filter(JSContext *ctx,
    ngx_http_sub_loc_conf_t *slcf);

/* Forward declaration from ngx_js_com_autoindex.c */
#include "../http/modules/ngx_http_autoindex_module.h"
JSValue  ngx_js_wrap_autoindex(JSContext *ctx,
    ngx_http_autoindex_loc_conf_t *alcf);

/* Forward declaration from ngx_js_com_referer.c */
#include "../http/modules/ngx_http_referer_module.h"
JSValue  ngx_js_wrap_referer(JSContext *ctx,
    ngx_http_referer_conf_t *rlcf);

/* Forward declaration from ngx_js_com_dav.c */
#if (NGX_HTTP_DAV)
#include "../http/modules/ngx_http_dav_module.h"
JSValue  ngx_js_wrap_dav(JSContext *ctx, ngx_http_dav_loc_conf_t *dlcf);
#endif

/* Forward declaration from ngx_js_com_ssi.c */
JSValue  ngx_js_wrap_ssi(JSContext *ctx, void *slcf);

/* Forward declaration from ngx_js_com_userid.c */
#include "../http/modules/ngx_http_userid_module.h"
JSValue  ngx_js_wrap_userid(JSContext *ctx, ngx_http_userid_conf_t *ucf);

/* Forward declaration from ngx_js_com_addition.c */
#include "../http/modules/ngx_http_addition_filter_module.h"
JSValue  ngx_js_wrap_addition(JSContext *ctx, ngx_http_addition_conf_t *acf);

/* Forward declaration from ngx_js_com_gunzip.c */
#include "../http/modules/ngx_http_gunzip_filter_module.h"
JSValue  ngx_js_wrap_gunzip(JSContext *ctx, ngx_http_gunzip_conf_t *gcf);

/* Forward declaration from ngx_js_com_slice.c */
#include "../http/modules/ngx_http_slice_filter_module.h"
JSValue  ngx_js_wrap_slice(JSContext *ctx, ngx_http_slice_loc_conf_t *scf);

/* Forward declaration from ngx_js_com_image_filter.c */
#include "../http/modules/ngx_http_image_filter_module.h"
JSValue  ngx_js_wrap_image_filter(JSContext *ctx,
    ngx_http_image_filter_conf_t *icf);

/* Forward declaration from ngx_js_com_xslt.c */
#include "../http/modules/ngx_http_xslt_filter_module.h"
JSValue  ngx_js_wrap_xslt(JSContext *ctx,
    ngx_http_xslt_filter_loc_conf_t *xcf);

/* Forward declaration from ngx_js_com_secure_link.c */
#include "../http/modules/ngx_http_secure_link_module.h"
JSValue  ngx_js_wrap_secure_link(JSContext *ctx,
    ngx_http_secure_link_conf_t *scf);

/* Forward declaration from ngx_js_com_mp4.c */
#include "../http/modules/ngx_http_mp4_module.h"
JSValue  ngx_js_wrap_mp4(JSContext *ctx, ngx_http_mp4_conf_t *mcf);

/* Forward declaration from ngx_js_com_random_index.c */
#include "../http/modules/ngx_http_random_index_module.h"
JSValue  ngx_js_wrap_random_index(JSContext *ctx,
    ngx_http_random_index_loc_conf_t *rcf);

/* Forward declaration from ngx_js_com_auth_request.c */
#include "../http/modules/ngx_http_auth_request_module.h"
JSValue  ngx_js_wrap_auth_request(JSContext *ctx,
    ngx_http_auth_request_conf_t *arcf);

/* Forward declaration from ngx_js_com_gzip_static.c */
#include "../http/modules/ngx_http_gzip_static_module.h"
JSValue  ngx_js_wrap_gzip_static(JSContext *ctx,
    ngx_http_gzip_static_conf_t *gcf);

/* Forward declaration from ngx_js_com_memcached.c */
#include "../http/modules/ngx_http_memcached_module.h"
JSValue  ngx_js_wrap_memcached(JSContext *ctx,
    ngx_http_memcached_loc_conf_t *mlcf);

/* Forward declaration from ngx_js_com_scgi.c */
#include "../http/modules/ngx_http_scgi_module.h"
JSValue  ngx_js_wrap_scgi(JSContext *ctx, ngx_http_scgi_loc_conf_t *scf);

/* Forward declaration from ngx_js_com_uwsgi.c */
#include "../http/modules/ngx_http_uwsgi_module.h"
JSValue  ngx_js_wrap_uwsgi(JSContext *ctx, ngx_http_uwsgi_loc_conf_t *ucf);

/* Forward declaration from ngx_js_com_mirror.c */
#include "../http/modules/ngx_http_mirror_module.h"
JSValue  ngx_js_wrap_mirror(JSContext *ctx,
    ngx_http_mirror_loc_conf_t *mlcf);

/* try_files module — used directly (no sub-object, plain array getter) */
#include "../http/modules/ngx_http_try_files_module.h"


/* ------------------------------------------------------------------ */
/* Persistent vhost map — built at init_conf, used by                  */
/* nginx.http.rebuildVhostDispatch()                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_addr_conf_t       *addr_conf;   /* runtime addr conf pointer   */
    ngx_http_core_srv_conf_t  **servers;     /* cscf pointer array          */
    ngx_uint_t                  nservers;
} ngx_js_addr_entry_t;

static ngx_uint_t           ngx_js_vhost_nentries;
static ngx_js_addr_entry_t *ngx_js_vhost_entries;
static ngx_uint_t           ngx_js_vhost_hash_max_size;
static ngx_uint_t           ngx_js_vhost_hash_bucket_size;


/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

JSValue ngx_js_wrap_location(JSContext *ctx,
    ngx_http_core_loc_conf_t *clcf);

static void ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx);

static JSValue ngx_js_build_locations(JSContext *ctx,
    ngx_http_core_loc_conf_t *root_clcf);

static JSValue ngx_js_wrap_server(JSContext *ctx,
    ngx_http_core_srv_conf_t *cscf, ngx_cycle_t *cycle);


/* ------------------------------------------------------------------ */
/* NginxLocation wrapper                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_core_loc_conf_t  *clcf;
    uint32_t                   write_mode;  /* NGX_JS_WRITE_GLOBAL/LOCAL/BOTH */
    uint32_t                   read_mode;   /* NGX_JS_WRITE_GLOBAL or LOCAL   */
} ngx_js_location_opaque_t;


static void
ngx_js_location_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_location_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_location_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_location_class = {
    "NginxLocation",
    .finalizer = ngx_js_location_finalizer
};


/*
 * Parse "global", "local", or "both" into an NGX_JS_WRITE_* bitmask.
 * Returns NGX_OK on success, NGX_ERROR (with JS exception set) on failure.
 */
static ngx_int_t
ngx_js_parse_rw_mode(JSContext *ctx, JSValueConst mode_val, uint32_t *out)
{
    const char  *s;
    size_t       slen;

    s = JS_ToCStringLen(ctx, &slen, mode_val);
    if (!s) {
        return NGX_ERROR;
    }

    if (slen == 6 && ngx_strncmp(s, "global", 6) == 0) {
        *out = NGX_JS_WRITE_GLOBAL;
    } else if (slen == 5 && ngx_strncmp(s, "local", 5) == 0) {
        *out = NGX_JS_WRITE_LOCAL;
    } else if (slen == 4 && ngx_strncmp(s, "both", 4) == 0) {
        *out = NGX_JS_WRITE_BOTH;
    } else {
        JS_ThrowTypeError(ctx, "invalid mode \"%s\": expected \"global\", "
                          "\"local\", or \"both\"", s);
        JS_FreeCString(ctx, s);
        return NGX_ERROR;
    }

    JS_FreeCString(ctx, s);
    return NGX_OK;
}


/*
 * Magic values for ngx_js_location_get / ngx_js_location_set:
 *   0  — path              (r/o: location name/pattern)
 *   1  — root              (r/w: document root)
 *   2  — handler           (r/w: JS content handler function)
 *   3  — internal          (r/o: bool)
 *   4  — sendfile          (r/w: bool)
 *   5  — tcpNopush         (r/w: bool)
 *   6  — tcpNodelay        (r/w: bool)
 *   7  — etag              (r/w: bool)
 *   8  — keepaliveTimeout  (r/w: ms)
 *   9  — keepaliveRequests (r/w: count)
 *   10 — clientMaxBodySize (r/w: bytes)
 *   11 — clientBodyTimeout (r/w: ms)
 *   12 — sendTimeout       (r/w: ms)
 *   13 — defaultType       (r/w: string)
 *   70 — keepaliveTime     (r/w: ms)
 *   14 — alias             (r/o: alias path, or null if root directive)
 *   15 — proxy             (r/o: NginxProxy for proxy_pass conf)
 *   16 — gzip              (r/o: NginxGzip for gzip conf, or null)
 *   17 — headers           (r/o: NginxHeaders for add_header/expires conf)
 *   18 — rewrite           (r/o: NginxRewrite for rewrite/set/return conf)
 *   19 — access            (r/o: NginxAccess for allow/deny rules)
 *   20 — auth              (r/o: NginxAuth for auth_basic realm/user_file)
 */
static JSValue
ngx_js_location_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_js_worker_t           *w;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    /*
     * Read-mode routing:
     *   LOCAL (default) — prefer the per-request snapshot when available;
     *                     falls back to the shared global struct automatically
     *                     because r->loc_conf[idx] == op->clcf until a snapshot
     *                     is taken.
     *   GLOBAL          — always read from the shared struct.
     */
    if (op->read_mode & NGX_JS_WRITE_LOCAL) {
        w = JS_GetContextOpaque(ctx);
        clcf = (w && w->current_request)
               ? w->current_request->loc_conf[ngx_http_core_module.ctx_index]
               : op->clcf;
    } else {
        clcf = op->clcf;
    }

    switch (magic) {
    case 0: /* path */
        return JS_NewStringLen(ctx, (const char *) clcf->name.data,
                               clcf->name.len);
    case 1: /* root */
        return JS_NewStringLen(ctx, (const char *) clcf->root.data,
                               clcf->root.len);
    case 2: /* handler — return the stored function or undefined */
    {
        ngx_js_loc_conf_t  *jlcf;
        JSValue             global, registry, fn;

        jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];

        if (jlcf->handler_idx < 0) {
            return JS_UNDEFINED;
        }

        global   = JS_GetGlobalObject(ctx);
        registry = JS_GetPropertyStr(ctx, global, "__ngx_handlers__");
        JS_FreeValue(ctx, global);

        fn = JS_GetPropertyUint32(ctx, registry,
                                  (uint32_t) jlcf->handler_idx);
        JS_FreeValue(ctx, registry);

        return fn;
    }

    case 3:  /* internal */
        return JS_NewBool(ctx, clcf->internal);

    case 4:  /* sendfile */
        return JS_NewBool(ctx, clcf->sendfile);

    case 5:  /* tcpNopush */
        return JS_NewBool(ctx, clcf->tcp_nopush);

    case 6:  /* tcpNodelay */
        return JS_NewBool(ctx, clcf->tcp_nodelay);

    case 7:  /* etag */
        return JS_NewBool(ctx, clcf->etag);

    case 8:  /* keepaliveTimeout — ngx_msec_t (ms) */
        return JS_NewInt64(ctx, (int64_t) clcf->keepalive_timeout);

    case 9:  /* keepaliveRequests */
        return JS_NewInt64(ctx, (int64_t) clcf->keepalive_requests);

    case 10: /* clientMaxBodySize — off_t (bytes) */
        return JS_NewInt64(ctx, (int64_t) clcf->client_max_body_size);

    case 11: /* clientBodyTimeout — ngx_msec_t (ms) */
        return JS_NewInt64(ctx, (int64_t) clcf->client_body_timeout);

    case 12: /* sendTimeout — ngx_msec_t (ms) */
        return JS_NewInt64(ctx, (int64_t) clcf->send_timeout);

    case 13: /* defaultType */
        return JS_NewStringLen(ctx, (const char *) clcf->default_type.data,
                               clcf->default_type.len);

    case 14: /* alias — null if root directive, alias path if alias */
        if (clcf->alias == 0) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx, (const char *) clcf->root.data,
                               clcf->root.len);

    case 15: /* proxy — NginxProxy wrapping the proxy_pass conf */
    {
        ngx_http_proxy_loc_conf_t  *plcf;

        plcf = clcf->loc_conf[ngx_http_proxy_module.ctx_index];
        if (plcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_proxy(ctx, plcf);
    }

    case 16: /* gzip — NginxGzip wrapping the gzip location conf */
    {
#ifdef NGX_HTTP_GZIP
        ngx_http_gzip_conf_t  *gcf;

        gcf = clcf->loc_conf[ngx_http_gzip_filter_module.ctx_index];
        if (gcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_gzip(ctx, gcf, clcf);
#else
        return JS_NULL;
#endif
    }

    case 17: /* headers — NginxHeaders wrapping add_header/expires conf */
    {
        ngx_http_headers_conf_t  *hcf;

        hcf = clcf->loc_conf[ngx_http_headers_filter_module.ctx_index];
        if (hcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_headers(ctx, hcf);
    }

    case 18: /* rewrite — NginxRewrite wrapping rewrite/set/return conf */
    {
        ngx_http_rewrite_loc_conf_t  *rlcf;

        rlcf = clcf->loc_conf[ngx_http_rewrite_module.ctx_index];
        if (rlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_rewrite(ctx, rlcf);
    }

    case 19: /* access — NginxAccess wrapping allow/deny rules */
    {
        ngx_http_access_loc_conf_t  *aclcf;

        aclcf = clcf->loc_conf[ngx_http_access_module.ctx_index];
        if (aclcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_access(ctx, aclcf);
    }

    case 20: /* auth — NginxAuth wrapping auth_basic conf */
    {
        ngx_http_auth_basic_loc_conf_t  *ablcf;

        ablcf = clcf->loc_conf[ngx_http_auth_basic_module.ctx_index];
        if (ablcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_auth(ctx, ablcf);
    }

    case 21: /* limitReq — NginxLimitReq wrapping limit_req conf */
    {
        ngx_http_limit_req_conf_t  *lrcf;

        lrcf = clcf->loc_conf[ngx_http_limit_req_module.ctx_index];
        if (lrcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_limit_req(ctx, lrcf);
    }

    case 22: /* limitConn — NginxLimitConn wrapping limit_conn conf */
    {
        ngx_http_limit_conn_conf_t  *lccf;

        lccf = clcf->loc_conf[ngx_http_limit_conn_module.ctx_index];
        if (lccf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_limit_conn(ctx, lccf);
    }

    case 23: /* fastcgi — NginxFastCGI wrapping fastcgi_pass conf */
    {
        ngx_http_fastcgi_loc_conf_t  *flcf;

        flcf = clcf->loc_conf[ngx_http_fastcgi_module.ctx_index];
        if (flcf == NULL || (flcf->upstream.upstream == NULL
                             && flcf->fastcgi_lengths == NULL))
        {
            return JS_NULL;
        }

        return ngx_js_wrap_fastcgi(ctx, flcf);
    }

    case 24: /* log — NginxLog wrapping access_log conf */
    {
        ngx_http_log_loc_conf_t  *llcf;

        llcf = clcf->loc_conf[ngx_http_log_module.ctx_index];
        if (llcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_log(ctx, llcf);
    }

#if (NGX_HTTP_REALIP)
    case 25: /* realip — NginxRealIP wrapping realip conf */
    {
        ngx_http_realip_loc_conf_t  *rlcf;

        rlcf = clcf->loc_conf[ngx_http_realip_module.ctx_index];
        if (rlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_realip(ctx, rlcf);
    }
#endif

    case 26: /* charset — NginxCharset wrapping charset filter conf */
    {
        ngx_http_charset_loc_conf_t   *cslcf;
        ngx_http_charset_main_conf_t  *csmcf;
        ngx_cycle_t                   *cycle;

        cslcf = clcf->loc_conf[ngx_http_charset_filter_module.ctx_index];
        if (cslcf == NULL) {
            return JS_NULL;
        }

        /*
         * Use ngx_cycle (the global pointer) rather than the context
         * opaque: at request time the opaque is ngx_js_worker_t*, not
         * ngx_cycle_t*, so dereferencing it as a cycle would crash.
         */
        (void) cycle;
        csmcf = ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                               ngx_http_charset_filter_module);

        return ngx_js_wrap_charset(ctx, cslcf, csmcf);
    }

    case 27: /* subFilter — NginxSubFilter wrapping sub_filter loc conf */
    {
        ngx_http_sub_loc_conf_t  *slcf;

        slcf = clcf->loc_conf[ngx_http_sub_filter_module.ctx_index];
        if (slcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_sub_filter(ctx, slcf);
    }

    case 28: /* autoindex — NginxAutoindex wrapping autoindex loc conf */
    {
        ngx_http_autoindex_loc_conf_t  *alcf;

        alcf = clcf->loc_conf[ngx_http_autoindex_module.ctx_index];
        if (alcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_autoindex(ctx, alcf);
    }

    case 29: /* referer — NginxReferer wrapping referer loc conf */
    {
        ngx_http_referer_conf_t  *rlcf;

        rlcf = clcf->loc_conf[ngx_http_referer_module.ctx_index];
        if (rlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_referer(ctx, rlcf);
    }

#if (NGX_HTTP_DAV)
    case 30: /* dav — NginxDav wrapping dav loc conf */
    {
        ngx_http_dav_loc_conf_t  *dlcf;

        dlcf = clcf->loc_conf[ngx_http_dav_module.ctx_index];
        if (dlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_dav(ctx, dlcf);
    }
#endif

    case 31: /* ssi — NginxSsi wrapping ssi loc conf */
    {
        void  *sslcf;

        sslcf = clcf->loc_conf[ngx_http_ssi_filter_module.ctx_index];
        if (sslcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_ssi(ctx, sslcf);
    }

    case 32: /* userid — NginxUserid wrapping userid conf */
    {
        ngx_http_userid_conf_t  *ucf;

        ucf = clcf->loc_conf[ngx_http_userid_filter_module.ctx_index];
        if (ucf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_userid(ctx, ucf);
    }

    case 33: /* addition — NginxAddition wrapping addition conf */
    {
        ngx_http_addition_conf_t  *acf;

        acf = clcf->loc_conf[ngx_http_addition_filter_module.ctx_index];
        if (acf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_addition(ctx, acf);
    }

    case 34: /* gunzip — NginxGunzip wrapping gunzip conf */
    {
        ngx_http_gunzip_conf_t  *gcf;

        gcf = clcf->loc_conf[ngx_http_gunzip_filter_module.ctx_index];
        if (gcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_gunzip(ctx, gcf);
    }

    case 35: /* slice — NginxSlice wrapping slice conf */
    {
        ngx_http_slice_loc_conf_t  *scf;

        scf = clcf->loc_conf[ngx_http_slice_filter_module.ctx_index];
        if (scf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_slice(ctx, scf);
    }

    case 36: /* imageFilter — NginxImageFilter wrapping image_filter conf */
    {
        ngx_http_image_filter_conf_t  *icf;

        icf = clcf->loc_conf[ngx_http_image_filter_module.ctx_index];
        if (icf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_image_filter(ctx, icf);
    }

    case 37: /* xslt — NginxXslt wrapping xslt loc conf */
    {
        ngx_http_xslt_filter_loc_conf_t  *xcf;

        xcf = clcf->loc_conf[ngx_http_xslt_filter_module.ctx_index];
        if (xcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_xslt(ctx, xcf);
    }

    case 38: /* secureLink — NginxSecureLink wrapping secure_link conf */
    {
        ngx_http_secure_link_conf_t  *scf;

        scf = clcf->loc_conf[ngx_http_secure_link_module.ctx_index];
        if (scf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_secure_link(ctx, scf);
    }

    case 39: /* mp4 — NginxMp4 wrapping mp4 loc conf */
    {
        ngx_http_mp4_conf_t  *mcf;

        mcf = clcf->loc_conf[ngx_http_mp4_module.ctx_index];
        if (mcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_mp4(ctx, mcf);
    }

    case 40: /* randomIndex — NginxRandomIndex wrapping random_index conf */
    {
        ngx_http_random_index_loc_conf_t  *rcf;

        rcf = clcf->loc_conf[ngx_http_random_index_module.ctx_index];
        if (rcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_random_index(ctx, rcf);
    }

    case 41: /* authRequest — NginxAuthRequest wrapping auth_request conf */
    {
        ngx_http_auth_request_conf_t  *arcf;

        arcf = clcf->loc_conf[ngx_http_auth_request_module.ctx_index];
        if (arcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_auth_request(ctx, arcf);
    }

    case 42: /* gzipStatic — NginxGzipStatic wrapping gzip_static conf */
    {
        ngx_http_gzip_static_conf_t  *gcf;

        gcf = clcf->loc_conf[ngx_http_gzip_static_module.ctx_index];
        if (gcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_gzip_static(ctx, gcf);
    }

    case 43: /* memcached — NginxMemcached wrapping memcached loc conf */
    {
        ngx_http_memcached_loc_conf_t  *mlcf;

        mlcf = clcf->loc_conf[ngx_http_memcached_module.ctx_index];
        if (mlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_memcached(ctx, mlcf);
    }

    case 44: /* scgi — NginxScgi wrapping scgi loc conf */
    {
        ngx_http_scgi_loc_conf_t  *scf;

        scf = clcf->loc_conf[ngx_http_scgi_module.ctx_index];
        if (scf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_scgi(ctx, scf);
    }

    case 45: /* uwsgi — NginxUwsgi wrapping uwsgi loc conf */
    {
        ngx_http_uwsgi_loc_conf_t  *ucf;

        ucf = clcf->loc_conf[ngx_http_uwsgi_module.ctx_index];
        if (ucf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_uwsgi(ctx, ucf);
    }

    case 46: /* mirror — NginxMirror wrapping mirror loc conf */
    {
        ngx_http_mirror_loc_conf_t  *mlcf;

        mlcf = clcf->loc_conf[ngx_http_mirror_module.ctx_index];
        if (mlcf == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_mirror(ctx, mlcf);
    }

    case 47: /* tryFiles — array of try_files argument strings */
    {
        JSValue                         arr;
        ngx_uint_t                      n;
        ngx_http_try_file_t            *tf;
        ngx_http_try_files_loc_conf_t  *tlcf;
        size_t                          len;

        tlcf = clcf->loc_conf[ngx_http_try_files_module.ctx_index];
        if (tlcf == NULL || tlcf->try_files == NULL) {
            return JS_NewArray(ctx);
        }

        arr = JS_NewArray(ctx);
        n   = 0;

        for (tf = tlcf->try_files;
             !(tf->lengths == NULL && tf->name.len == 0);
             tf++)
        {
            JSValue  s;

            /* static entries store len with trailing '\0' included */
            len = (tf->lengths == NULL) ? tf->name.len - 1 : tf->name.len;

            if (tf->test_dir) {
                /*
                 * nginx strips the trailing '/' and sets test_dir=1;
                 * reconstruct it so the user sees what they configured.
                 */
                u_char  *buf;

                buf = js_malloc(ctx, len + 2);
                if (buf == NULL) {
                    JS_FreeValue(ctx, arr);
                    return JS_EXCEPTION;
                }
                ngx_memcpy(buf, tf->name.data, len);
                buf[len]     = '/';
                buf[len + 1] = '\0';
                s = JS_NewStringLen(ctx, (char *) buf, len + 1);
                js_free(ctx, buf);

            } else {
                s = JS_NewStringLen(ctx, (char *) tf->name.data, len);
            }

            JS_SetPropertyUint32(ctx, arr, (uint32_t) n++, s);
        }

        return arr;
    }

    case 48: /* satisfy — "all" | "any" */
        return JS_NewString(ctx,
            clcf->satisfy == NGX_HTTP_SATISFY_ANY ? "any" : "all");

    case 49: /* limitExcept — array of allowed method name strings */
    {
        JSValue     arr;
        ngx_uint_t  n;

        static const struct { uint32_t bit; const char *name; }
        methods[] = {
            { NGX_HTTP_GET,       "GET"       },
            { NGX_HTTP_HEAD,      "HEAD"      },
            { NGX_HTTP_POST,      "POST"      },
            { NGX_HTTP_PUT,       "PUT"       },
            { NGX_HTTP_DELETE,    "DELETE"    },
            { NGX_HTTP_MKCOL,     "MKCOL"     },
            { NGX_HTTP_COPY,      "COPY"      },
            { NGX_HTTP_MOVE,      "MOVE"      },
            { NGX_HTTP_OPTIONS,   "OPTIONS"   },
            { NGX_HTTP_PROPFIND,  "PROPFIND"  },
            { NGX_HTTP_PROPPATCH, "PROPPATCH" },
            { NGX_HTTP_LOCK,      "LOCK"      },
            { NGX_HTTP_UNLOCK,    "UNLOCK"    },
            { NGX_HTTP_PATCH,     "PATCH"     },
            { 0, NULL }
        };
        ngx_uint_t  i;

        arr = JS_NewArray(ctx);
        n   = 0;

        if (clcf->limit_except) {
            for (i = 0; methods[i].bit; i++) {
                if (clcf->limit_except & methods[i].bit) {
                    JS_SetPropertyUint32(ctx, arr, (uint32_t) n++,
                        JS_NewString(ctx, methods[i].name));
                }
            }
        }

        return arr;
    }

    case 50: /* lingering — "off" | "on" | "always" */
        switch (clcf->lingering_close) {
        case NGX_HTTP_LINGERING_ON:     return JS_NewString(ctx, "on");
        case NGX_HTTP_LINGERING_ALWAYS: return JS_NewString(ctx, "always");
        default:                        return JS_NewString(ctx, "off");
        }

    case 51: /* lingeringTimeout — ms */
        return JS_NewUint32(ctx, (uint32_t) clcf->lingering_timeout);

    case 52: /* lingeringTime — ms */
        return JS_NewUint32(ctx, (uint32_t) clcf->lingering_time);

    case 53: /* resolverTimeout — ms */
        return JS_NewUint32(ctx, (uint32_t) clcf->resolver_timeout);

    case 54: /* chunkedTransferEncoding */
        return JS_NewBool(ctx, (int) clcf->chunked_transfer_encoding);

    case 55: /* msieRefresh */
        return JS_NewBool(ctx, (int) clcf->msie_refresh);

    case 56: /* logNotFound */
        return JS_NewBool(ctx, (int) clcf->log_not_found);

    case 57: /* logSubrequest */
        return JS_NewBool(ctx, (int) clcf->log_subrequest);

    case 58: /* recursiveErrorPages */
        return JS_NewBool(ctx, (int) clcf->recursive_error_pages);

    case 59: /* clientBodyBufferSize */
        return JS_NewInt64(ctx, (int64_t) clcf->client_body_buffer_size);

    case 60: /* clientBodyInFileOnly */
        switch (clcf->client_body_in_file_only) {
        case 0:  return JS_NewString(ctx, "off");
        case 1:  return JS_NewString(ctx, "on");
        case 2:  return JS_NewString(ctx, "clean");
        default: return JS_NewString(ctx, "off");
        }

    case 61: /* clientBodyInSingleBuffer */
        return JS_NewBool(ctx, (int) clcf->client_body_in_single_buffer);

    case 62: /* resetTimedoutConnection */
        return JS_NewBool(ctx, (int) clcf->reset_timedout_connection);

    case 63: /* absoluteRedirect */
        return JS_NewBool(ctx, (int) clcf->absolute_redirect);

    case 64: /* serverNameInRedirect */
        return JS_NewBool(ctx, (int) clcf->server_name_in_redirect);

    case 65: /* portInRedirect */
        return JS_NewBool(ctx, (int) clcf->port_in_redirect);

    case 66: /* msiePadding */
        return JS_NewBool(ctx, (int) clcf->msie_padding);

    case 67: /* ifModifiedSince */
        switch (clcf->if_modified_since) {
        case NGX_HTTP_IMS_OFF:    return JS_NewString(ctx, "off");
        case NGX_HTTP_IMS_EXACT:  return JS_NewString(ctx, "exact");
        case NGX_HTTP_IMS_BEFORE: return JS_NewString(ctx, "before");
        default:                  return JS_NewString(ctx, "exact");
        }

    case 68: /* maxRanges */
        return JS_NewInt64(ctx, (int64_t) clcf->max_ranges);

    case 69: /* authDelay */
        return JS_NewInt64(ctx, (int64_t) clcf->auth_delay);

    case 70: /* keepaliveTime */
        return JS_NewInt64(ctx, (int64_t) clcf->keepalive_time);

    case 71: /* sendLowat */
        return JS_NewInt64(ctx, (int64_t) clcf->send_lowat);

    case 72: /* postponeOutput */
        return JS_NewInt64(ctx, (int64_t) clcf->postpone_output);

    case 73: /* typesHashMaxSize */
        return JS_NewInt64(ctx, (int64_t) clcf->types_hash_max_size);

    case 74: /* keepaliveDisable — string[] of disabled browser names */
    {
        JSValue  arr;
        int      idx;

        arr = JS_NewArray(ctx);
        idx = 0;

        if (clcf->keepalive_disable & NGX_HTTP_KEEPALIVE_DISABLE_MSIE6) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, "msie6"));
        }

        if (clcf->keepalive_disable & NGX_HTTP_KEEPALIVE_DISABLE_SAFARI) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, "safari"));
        }

        return arr;
    }

    case 75: /* keepaliveMinTimeout — ms */
        return JS_NewInt64(ctx, (int64_t) clcf->keepalive_min_timeout);

    case 76: /* sendfileMaxChunk — bytes */
        return JS_NewInt64(ctx, (int64_t) clcf->sendfile_max_chunk);

    case 77: /* readAhead — bytes */
        return JS_NewInt64(ctx, (int64_t) clcf->read_ahead);

    case 78: /* directio — bytes, or "off" when disabled */
        if (clcf->directio == NGX_OPEN_FILE_DIRECTIO_OFF) {
            return JS_NewString(ctx, "off");
        }
        return JS_NewInt64(ctx, (int64_t) clcf->directio);

    case 79: /* directioAlignment — bytes */
        return JS_NewInt64(ctx, (int64_t) clcf->directio_alignment);

    case 80: /* matchType — location modifier as string */
#if (NGX_PCRE)
        if (clcf->regex) {
            return JS_NewString(ctx, clcf->nocase ? "regexCaseInsensitive"
                                                  : "regex");
        }
#endif
        if (clcf->exact_match) {
            return JS_NewString(ctx, "exact");
        }
        if (clcf->noregex) {
            return JS_NewString(ctx, "preferentialPrefix");
        }
        if (clcf->named) {
            return JS_NewString(ctx, "named");
        }
        return JS_NewString(ctx, "prefix");

    case 81: /* pattern — full pattern string including modifier prefix */
        /*
         * Unlike path (which returns clcf->name without modifier),
         * pattern reconstructs the complete pattern usable as the first
         * argument to addLocation() / removeLocation():
         *   prefix              → "/foo"
         *   exact match         → "= /foo"
         *   preferential-prefix → "^~ /foo"
         *   case-sensitive regex→ "~ /regex"
         *   case-insensitive    → "~* /regex"
         *   named               → "@name"  (@ is already in clcf->name)
         */
        {
            const char  *pfx;
            size_t       pfx_len;
            u_char      *buf;
            JSValue      s;

#if (NGX_PCRE)
            if (clcf->regex) {
                pfx     = clcf->nocase ? "~* " : "~ ";
                pfx_len = clcf->nocase ? 3 : 2;
                goto pattern_with_prefix;
            }
#endif
            if (clcf->exact_match) {
                pfx = "= "; pfx_len = 2;
                goto pattern_with_prefix;
            }
            if (clcf->noregex) {
                pfx = "^~ "; pfx_len = 3;
                goto pattern_with_prefix;
            }
            /* plain prefix or @named — name is already the full pattern */
            return JS_NewStringLen(ctx, (const char *) clcf->name.data,
                                   clcf->name.len);

        pattern_with_prefix:
            buf = js_malloc(ctx, pfx_len + clcf->name.len);
            if (buf == NULL) {
                return JS_EXCEPTION;
            }
            ngx_memcpy(buf, pfx, pfx_len);
            ngx_memcpy(buf + pfx_len, clcf->name.data, clcf->name.len);
            s = JS_NewStringLen(ctx, (const char *) buf,
                                pfx_len + clcf->name.len);
            js_free(ctx, buf);
            return s;
        }

    }

    return JS_UNDEFINED;
}


/*
 * location.errorPage — array of {status, overwrite, uri} objects,
 * one per error_page entry (nginx creates one entry per status code).
 * For dynamic URI expressions (containing nginx variables) the uri field
 * reflects the literal prefix only; use JS_NULL if completely dynamic.
 */
static JSValue
ngx_js_location_get_error_page(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_err_page_t       *ep;
    JSValue                    arr, obj, uri_val;
    ngx_uint_t                 i;
    uint32_t                   idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    clcf = op->clcf;

    if (clcf->error_pages == NULL) {
        return arr;
    }

    ep  = clcf->error_pages->elts;
    idx = 0;

    for (i = 0; i < clcf->error_pages->nelts; i++) {

        /*
         * value.value holds the literal URI; value.lengths != NULL means
         * the URI contains nginx variables — return empty string in that
         * case (the compiled expression is not easily recoverable as text).
         */
        if (ep[i].value.lengths == NULL) {
            uri_val = JS_NewStringLen(ctx,
                                      (const char *) ep[i].value.value.data,
                                      ep[i].value.value.len);
        } else {
            uri_val = JS_NewString(ctx, "");
        }

        obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "status",
                          JS_NewInt32(ctx, (int32_t) ep[i].status));
        JS_SetPropertyStr(ctx, obj, "overwrite",
                          JS_NewInt32(ctx, (int32_t) ep[i].overwrite));
        JS_SetPropertyStr(ctx, obj, "uri", uri_val);

        JS_SetPropertyUint32(ctx, arr, idx++, obj);
    }

    return arr;
}


/*
 * location.errorPage = [{status, overwrite?, uri}, ...]
 *
 * Replaces clcf->error_pages with a new pool-allocated array.
 * Each entry must have:
 *   status    — number, the error code to match
 *   uri       — string, the redirect URI (literal, no nginx variables)
 *   overwrite — number (optional, default 0); 0 means keep original status
 *
 * Setting [] clears all error page rules.
 * Pool memory only grows (old arrays are never freed).
 */
static JSValue
ngx_js_location_set_error_page(JSContext *ctx, JSValueConst this_val,
    JSValue val)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_array_t               *arr;
    ngx_http_err_page_t       *ep;
    JSValue                    entry, status_v, overwrite_v, uri_v, len_v;
    const char                *uri;
    size_t                     uri_len;
    int64_t                    len, i, status, overwrite;
    u_char                    *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (!JS_IsArray(ctx, val)) {
        return JS_ThrowTypeError(ctx, "errorPage must be an array");
    }

    len_v = JS_GetPropertyStr(ctx, val, "length");
    if (JS_ToInt64(ctx, &len, len_v) < 0) {
        JS_FreeValue(ctx, len_v);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_v);

    clcf = op->clcf;

    if (len == 0) {
        clcf->error_pages = NULL;
        return JS_UNDEFINED;
    }

    arr = ngx_array_create(ngx_cycle->pool, (ngx_uint_t) len,
                           sizeof(ngx_http_err_page_t));
    if (!arr) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < len; i++) {
        entry      = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
        status_v   = JS_GetPropertyStr(ctx, entry, "status");
        overwrite_v = JS_GetPropertyStr(ctx, entry, "overwrite");
        uri_v      = JS_GetPropertyStr(ctx, entry, "uri");
        JS_FreeValue(ctx, entry);

        if (JS_ToInt64(ctx, &status, status_v) < 0) {
            JS_FreeValue(ctx, status_v);
            JS_FreeValue(ctx, overwrite_v);
            JS_FreeValue(ctx, uri_v);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, status_v);

        /* overwrite is optional — default 0 */
        if (JS_IsUndefined(overwrite_v) || JS_IsNull(overwrite_v)) {
            overwrite = 0;
        } else if (JS_ToInt64(ctx, &overwrite, overwrite_v) < 0) {
            JS_FreeValue(ctx, overwrite_v);
            JS_FreeValue(ctx, uri_v);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, overwrite_v);

        uri = JS_ToCStringLen(ctx, &uri_len, uri_v);
        JS_FreeValue(ctx, uri_v);
        if (!uri) {
            return JS_EXCEPTION;
        }

        ep = ngx_array_push(arr);
        if (!ep) {
            JS_FreeCString(ctx, uri);
            return JS_EXCEPTION;
        }

        p = ngx_pnalloc(ngx_cycle->pool, uri_len);
        if (!p) {
            JS_FreeCString(ctx, uri);
            return JS_EXCEPTION;
        }

        ngx_memcpy(p, uri, uri_len);
        JS_FreeCString(ctx, uri);

        ep->status              = (ngx_int_t) status;
        ep->overwrite           = (ngx_int_t) overwrite;
        ep->value.value.data    = p;
        ep->value.value.len     = uri_len;
        ep->value.lengths       = NULL;
        ep->value.values        = NULL;
        ep->value.flushes       = NULL;
        ngx_memzero(&ep->args, sizeof(ep->args));
    }

    clcf->error_pages = arr;
    return JS_UNDEFINED;
}


/*
 * Core setter — writes to whatever op->clcf points at.  Never called
 * directly by QuickJS; the ngx_js_location_set wrapper below routes to
 * this function once or twice (for global and/or local targets).
 */
static JSValue
ngx_js_location_set_core(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    const char                *cstr;
    size_t                     len;
    u_char                    *data;
    int64_t                    n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf  = op->clcf;

    switch (magic) {
    case 1: /* root */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(ngx_cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        clcf->root.data    = data;
        clcf->root.len     = len;
        clcf->root_lengths = NULL;  /* mark as literal (no variables) */
        return JS_UNDEFINED;

    case 14: /* alias — only writable when the location uses alias directive */
        if (clcf->alias == 0) {
            return JS_ThrowTypeError(ctx,
                "alias: location uses root, not alias; set .root instead");
        }

        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(ngx_cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        clcf->root.data    = data;
        clcf->root.len     = len;
        clcf->root_lengths = NULL;
        return JS_UNDEFINED;

    case 2: /* handler — store function in __ngx_handlers__, save index */
    {
        JSValue            global, registry, len_val;
        uint32_t           idx;
        ngx_js_loc_conf_t *jlcf;

        if (!JS_IsFunction(ctx, val)) {
            return JS_ThrowTypeError(ctx,
                                     "location.handler: expected a function");
        }

        /*
         * Append the function to the __ngx_handlers__ array on the global
         * object.  This keeps the function GC-reachable even when it is an
         * anonymous or arrow function with no other JS-side reference.
         */
        global   = JS_GetGlobalObject(ctx);
        registry = JS_GetPropertyStr(ctx, global, "__ngx_handlers__");

        if (!JS_IsArray(ctx, registry)) {
            JS_FreeValue(ctx, registry);
            registry = JS_NewArray(ctx);
            JS_SetPropertyStr(ctx, global, "__ngx_handlers__",
                              JS_DupValue(ctx, registry));
        }

        JS_FreeValue(ctx, global);

        len_val = JS_GetPropertyStr(ctx, registry, "length");
        JS_ToUint32(ctx, &idx, len_val);
        JS_FreeValue(ctx, len_val);

        JS_SetPropertyUint32(ctx, registry, idx, JS_DupValue(ctx, val));
        JS_FreeValue(ctx, registry);

        jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];
        jlcf->handler_idx = (ngx_int_t) idx;

        /* Wire up the content handler pointer */
        clcf->handler = ngx_js_content_handler;
        return JS_UNDEFINED;
    }

    case 4: /* sendfile */
        clcf->sendfile = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 5: /* tcpNopush */
        clcf->tcp_nopush = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 6: /* tcpNodelay */
        clcf->tcp_nodelay = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 7: /* etag */
        clcf->etag = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 8: /* keepaliveTimeout — ngx_msec_t (ms) */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->keepalive_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 9: /* keepaliveRequests — ngx_uint_t */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->keepalive_requests = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 10: /* clientMaxBodySize — off_t (bytes) */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->client_max_body_size = (off_t) n;
        return JS_UNDEFINED;

    case 11: /* clientBodyTimeout — ngx_msec_t (ms) */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->client_body_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 12: /* sendTimeout — ngx_msec_t (ms) */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->send_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 13: /* defaultType — ngx_str_t (dup to pool) */
        cstr = JS_ToCStringLen(ctx, &len, val);
        if (!cstr) { return JS_EXCEPTION; }
        data = ngx_pnalloc(ngx_cycle->pool, len);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }
        ngx_memcpy(data, cstr, len);
        JS_FreeCString(ctx, cstr);
        clcf->default_type.data = data;
        clcf->default_type.len  = len;
        return JS_UNDEFINED;

    case 48: /* satisfy — "all" | "any" */
    {
        const char *s;
        size_t      slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "all", 3) == 0) {
            clcf->satisfy = NGX_HTTP_SATISFY_ALL;
        } else if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "any", 3) == 0) {
            clcf->satisfy = NGX_HTTP_SATISFY_ANY;
        } else {
            JS_ThrowTypeError(ctx, "satisfy: expected \"all\" or \"any\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }

    case 49: /* limitExcept — string[] → bitmask */
    {
        static const struct { const char *name; uint32_t bit; } methods[] = {
            { "GET",       NGX_HTTP_GET       },
            { "HEAD",      NGX_HTTP_HEAD      },
            { "POST",      NGX_HTTP_POST      },
            { "PUT",       NGX_HTTP_PUT       },
            { "DELETE",    NGX_HTTP_DELETE    },
            { "MKCOL",     NGX_HTTP_MKCOL     },
            { "COPY",      NGX_HTTP_COPY      },
            { "MOVE",      NGX_HTTP_MOVE      },
            { "OPTIONS",   NGX_HTTP_OPTIONS   },
            { "PROPFIND",  NGX_HTTP_PROPFIND  },
            { "PROPPATCH", NGX_HTTP_PROPPATCH },
            { "LOCK",      NGX_HTTP_LOCK      },
            { "UNLOCK",    NGX_HTTP_UNLOCK    },
            { "PATCH",     NGX_HTTP_PATCH     },
        };
        ngx_uint_t  mask;
        uint32_t    alen, i, j;
        JSValue     elem;
        const char *ms;
        size_t      mlen;

        if (!JS_IsArray(ctx, val)) {
            return JS_ThrowTypeError(ctx, "limitExcept: array expected");
        }

        {
            JSValue lv = JS_GetPropertyStr(ctx, val, "length");
            if (JS_ToUint32(ctx, &alen, lv) < 0) {
                JS_FreeValue(ctx, lv);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, lv);
        }

        mask = 0;

        for (i = 0; i < alen; i++) {
            elem = JS_GetPropertyUint32(ctx, val, i);
            if (JS_IsException(elem)) { return JS_EXCEPTION; }

            ms = JS_ToCStringLen(ctx, &mlen, elem);
            JS_FreeValue(ctx, elem);
            if (!ms) { return JS_EXCEPTION; }

            for (j = 0; j < countof(methods); j++) {
                if (strlen(methods[j].name) == mlen
                    && ngx_strncasecmp((u_char *) methods[j].name,
                                       (u_char *) ms, mlen) == 0)
                {
                    mask |= methods[j].bit;
                    break;
                }
            }

            if (j == countof(methods)) {
                JS_ThrowTypeError(ctx,
                    "limitExcept: unknown method \"%s\"", ms);
                JS_FreeCString(ctx, ms);
                return JS_EXCEPTION;
            }

            JS_FreeCString(ctx, ms);
        }

        clcf->limit_except = mask;
        return JS_UNDEFINED;
    }

    case 50: /* lingering — "off" | "on" | "always" */
    {
        const char *s;
        size_t      slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            clcf->lingering_close = NGX_HTTP_LINGERING_OFF;
        } else if (slen == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
            clcf->lingering_close = NGX_HTTP_LINGERING_ON;
        } else if (slen == 6 && ngx_strncasecmp((u_char *) s, (u_char *) "always", 6) == 0) {
            clcf->lingering_close = NGX_HTTP_LINGERING_ALWAYS;
        } else {
            JS_ThrowTypeError(ctx,
                "lingering: expected \"off\", \"on\", or \"always\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }

    case 51: /* lingeringTimeout — ms */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->lingering_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 52: /* lingeringTime — ms */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->lingering_time = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 53: /* resolverTimeout — ms */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->resolver_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 54: /* chunkedTransferEncoding */
        clcf->chunked_transfer_encoding = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 55: /* msieRefresh */
        clcf->msie_refresh = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 56: /* logNotFound */
        clcf->log_not_found = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 57: /* logSubrequest */
        clcf->log_subrequest = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 58: /* recursiveErrorPages */
        clcf->recursive_error_pages = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 59: /* clientBodyBufferSize — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->client_body_buffer_size = (size_t) n;
        return JS_UNDEFINED;

    case 60: /* clientBodyInFileOnly — "off" | "on" | "clean" */
    {
        const char *s;
        size_t      slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            clcf->client_body_in_file_only = 0;
        } else if (slen == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
            clcf->client_body_in_file_only = 1;
        } else if (slen == 5 && ngx_strncasecmp((u_char *) s, (u_char *) "clean", 5) == 0) {
            clcf->client_body_in_file_only = 2;
        } else {
            JS_ThrowTypeError(ctx,
                "clientBodyInFileOnly: expected \"off\", \"on\", or \"clean\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }

    case 61: /* clientBodyInSingleBuffer */
        clcf->client_body_in_single_buffer = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 62: /* resetTimedoutConnection */
        clcf->reset_timedout_connection = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 63: /* absoluteRedirect */
        clcf->absolute_redirect = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 64: /* serverNameInRedirect */
        clcf->server_name_in_redirect = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 65: /* portInRedirect */
        clcf->port_in_redirect = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 66: /* msiePadding */
        clcf->msie_padding = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 67: /* ifModifiedSince — "off" | "exact" | "before" */
    {
        const char *s;
        size_t      slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            clcf->if_modified_since = NGX_HTTP_IMS_OFF;
        } else if (slen == 5 && ngx_strncasecmp((u_char *) s, (u_char *) "exact", 5) == 0) {
            clcf->if_modified_since = NGX_HTTP_IMS_EXACT;
        } else if (slen == 6 && ngx_strncasecmp((u_char *) s, (u_char *) "before", 6) == 0) {
            clcf->if_modified_since = NGX_HTTP_IMS_BEFORE;
        } else {
            JS_ThrowTypeError(ctx,
                "ifModifiedSince: expected \"off\", \"exact\", or \"before\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }

    case 68: /* maxRanges */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->max_ranges = (ngx_uint_t) n;
        return JS_UNDEFINED;

    case 69: /* authDelay — ms */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->auth_delay = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 70: /* keepaliveTime — ngx_msec_t (ms) */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->keepalive_time = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 71: /* sendLowat — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->send_lowat = (size_t) n;
        return JS_UNDEFINED;

    case 72: /* postponeOutput — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->postpone_output = (size_t) n;
        return JS_UNDEFINED;

    case 74: /* keepaliveDisable — string[] → bitmask */
    {
        ngx_uint_t  mask;
        uint32_t    alen, i;
        JSValue     elem;
        const char *ms;
        size_t      mlen;

        if (!JS_IsArray(ctx, val)) {
            return JS_ThrowTypeError(ctx, "keepaliveDisable: array expected");
        }

        {
            JSValue lv = JS_GetPropertyStr(ctx, val, "length");
            if (JS_ToUint32(ctx, &alen, lv) < 0) {
                JS_FreeValue(ctx, lv);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, lv);
        }

        mask = 0;

        for (i = 0; i < alen; i++) {
            elem = JS_GetPropertyUint32(ctx, val, i);
            if (JS_IsException(elem)) { return JS_EXCEPTION; }

            ms = JS_ToCStringLen(ctx, &mlen, elem);
            JS_FreeValue(ctx, elem);
            if (!ms) { return JS_EXCEPTION; }

            if (mlen == 5 && ngx_strncasecmp((u_char *) ms,
                                              (u_char *) "msie6", 5) == 0) {
                mask |= NGX_HTTP_KEEPALIVE_DISABLE_MSIE6;
            } else if (mlen == 6 && ngx_strncasecmp((u_char *) ms,
                                                     (u_char *) "safari", 6) == 0) {
                mask |= NGX_HTTP_KEEPALIVE_DISABLE_SAFARI;
            } else {
                JS_ThrowTypeError(ctx,
                    "keepaliveDisable: unknown browser \"%s\"", ms);
                JS_FreeCString(ctx, ms);
                return JS_EXCEPTION;
            }

            JS_FreeCString(ctx, ms);
        }

        clcf->keepalive_disable = mask;
        return JS_UNDEFINED;
    }

    case 75: /* keepaliveMinTimeout — ms */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->keepalive_min_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;

    case 76: /* sendfileMaxChunk — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->sendfile_max_chunk = (off_t) n;
        return JS_UNDEFINED;

    case 77: /* readAhead — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->read_ahead = (size_t) n;
        return JS_UNDEFINED;

    case 78: /* directio — "off" | bytes */
    {
        const char *s;
        size_t      slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            JS_FreeCString(ctx, s);
            clcf->directio = NGX_OPEN_FILE_DIRECTIO_OFF;
        } else {
            JS_FreeCString(ctx, s);
            if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
            clcf->directio = (off_t) n;
        }

        return JS_UNDEFINED;
    }

    case 79: /* directioAlignment — bytes */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        clcf->directio_alignment = (off_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * Write-mode routing wrapper — called by QuickJS for every property
 * assignment on a NginxLocation object.
 *
 * Depending on op->write_mode it invokes ngx_js_location_set_core()
 * once (global only / local only) or twice (both), temporarily swapping
 * op->clcf to point at the appropriate target.
 *
 * "local" writes deep-copy ngx_http_core_loc_conf_t into r->pool first
 * (via ngx_js_ensure_core_snapshot); if there is no current request
 * context the mode falls back to "global".
 *
 * The handler setter (magic == 2) always writes globally because it
 * wires nginx's content-phase dispatch mechanism and must be visible
 * to all future requests, not just the current one.
 */
static JSValue
ngx_js_location_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *orig_clcf, *local_clcf;
    ngx_js_worker_t           *w;
    uint32_t                   wm;
    int                        need_global, need_local;
    JSValue                    ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    orig_clcf = op->clcf;
    wm        = op->write_mode;

    /* handler wires nginx dispatch — always global regardless of write_mode */
    if (magic == 2) {
        return ngx_js_location_set_core(ctx, this_val, val, magic);
    }

    need_global = (wm & NGX_JS_WRITE_GLOBAL) != 0;
    need_local  = (wm & NGX_JS_WRITE_LOCAL)  != 0;

    local_clcf = NULL;

    if (need_local) {
        w = JS_GetContextOpaque(ctx);

        if (w && w->current_request) {
            if (ngx_js_ensure_core_snapshot(w->current_request) != NGX_OK) {
                return JS_ThrowOutOfMemory(ctx);
            }
            local_clcf = w->current_request->loc_conf[
                             ngx_http_core_module.ctx_index];
        } else {
            /* no request context — silently fall back to global */
            need_local  = 0;
            need_global = 1;
        }
    }

    ret = JS_UNDEFINED;

    if (need_local) {
        op->clcf = local_clcf;
        ret = ngx_js_location_set_core(ctx, this_val, val, magic);
        op->clcf = orig_clcf;
        if (JS_IsException(ret)) {
            return ret;
        }
        JS_FreeValue(ctx, ret);
        ret = JS_UNDEFINED;
    }

    if (need_global) {
        /* op->clcf already points at the global struct */
        ret = ngx_js_location_set_core(ctx, this_val, val, magic);
    }

    return ret;
}


/*
 * setWriteMode(mode) — sets the default write target for '=' assignments.
 * mode: "global" | "local" | "both"
 */
static JSValue
ngx_js_location_fn_set_write_mode(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    uint32_t                   mode;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setWriteMode: mode argument required");
    }

    if (ngx_js_parse_rw_mode(ctx, argv[0], &mode) != NGX_OK) {
        return JS_EXCEPTION;
    }

    op->write_mode = mode;

    /* Propagate to request-level write_mode for sub-object inheritance */
    {
        ngx_js_worker_t   *w;
        ngx_js_req_ctx_t  *rctx;

        w = JS_GetContextOpaque(ctx);
        if (w && w->current_request) {
            rctx = ngx_http_get_module_ctx(w->current_request,
                                            ngx_js_http_module);
            if (rctx) {
                rctx->write_mode = mode;
            }
        }
    }

    return JS_UNDEFINED;
}


/*
 * setReadMode(mode) — sets the default read source for property access.
 * mode: "global" | "local"
 * ("both" is accepted but treated as "local" — reads return one value)
 */
static JSValue
ngx_js_location_fn_set_read_mode(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    uint32_t                   mode;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setReadMode: mode argument required");
    }

    if (ngx_js_parse_rw_mode(ctx, argv[0], &mode) != NGX_OK) {
        return JS_EXCEPTION;
    }

    op->read_mode = mode;

    /* Propagate to request-level read_mode for sub-object inheritance */
    {
        ngx_js_worker_t   *w;
        ngx_js_req_ctx_t  *rctx;

        w = JS_GetContextOpaque(ctx);
        if (w && w->current_request) {
            rctx = ngx_http_get_module_ctx(w->current_request,
                                            ngx_js_http_module);
            if (rctx) {
                rctx->read_mode = mode;
            }
        }
    }

    return JS_UNDEFINED;
}


/*
 * setProperty(name, value[, mode]) — explicit write with an optional mode
 * override.  Temporarily sets write_mode, invokes the normal setter via
 * JS_SetPropertyStr (which calls ngx_js_location_set through QuickJS),
 * then restores the original write_mode.
 */
static JSValue
ngx_js_location_fn_set_property(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    const char                *name;
    uint32_t                   saved_mode, mode;
    int                        rc;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setProperty: name and value required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    name = JS_ToCString(ctx, argv[0]);
    if (!name) {
        return JS_EXCEPTION;
    }

    saved_mode = op->write_mode;

    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (ngx_js_parse_rw_mode(ctx, argv[2], &mode) != NGX_OK) {
            JS_FreeCString(ctx, name);
            return JS_EXCEPTION;
        }
        op->write_mode = mode;
    }

    rc = JS_SetPropertyStr(ctx, this_val, name, JS_DupValue(ctx, argv[1]));
    op->write_mode = saved_mode;
    JS_FreeCString(ctx, name);

    if (rc < 0) {
        return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}


/*
 * getProperty(name[, mode]) — explicit read with an optional mode override.
 */
static JSValue
ngx_js_location_fn_get_property(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    const char                *name;
    uint32_t                   saved_mode, mode;
    JSValue                    ret;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "getProperty: name argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    name = JS_ToCString(ctx, argv[0]);
    if (!name) {
        return JS_EXCEPTION;
    }

    saved_mode = op->read_mode;

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ngx_js_parse_rw_mode(ctx, argv[1], &mode) != NGX_OK) {
            JS_FreeCString(ctx, name);
            return JS_EXCEPTION;
        }
        op->read_mode = mode;
    }

    ret = JS_GetPropertyStr(ctx, this_val, name);
    op->read_mode = saved_mode;
    JS_FreeCString(ctx, name);

    return ret;
}


static const JSCFunctionListEntry ngx_js_location_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("path",             ngx_js_location_get, NULL,                 0),
    JS_CGETSET_MAGIC_DEF("root",             ngx_js_location_get, ngx_js_location_set,  1),
    JS_CGETSET_MAGIC_DEF("handler",          ngx_js_location_get, ngx_js_location_set,  2),
    JS_CGETSET_MAGIC_DEF("internal",         ngx_js_location_get, NULL,                        3),
    JS_CGETSET_MAGIC_DEF("sendfile",         ngx_js_location_get, ngx_js_location_set,         4),
    JS_CGETSET_MAGIC_DEF("tcpNopush",        ngx_js_location_get, ngx_js_location_set,         5),
    JS_CGETSET_MAGIC_DEF("tcpNodelay",       ngx_js_location_get, ngx_js_location_set,         6),
    JS_CGETSET_MAGIC_DEF("etag",             ngx_js_location_get, ngx_js_location_set,         7),
    JS_CGETSET_MAGIC_DEF("keepaliveTimeout", ngx_js_location_get, ngx_js_location_set,         8),
    JS_CGETSET_MAGIC_DEF("keepaliveRequests",ngx_js_location_get, ngx_js_location_set,         9),
    JS_CGETSET_MAGIC_DEF("clientMaxBodySize",ngx_js_location_get, ngx_js_location_set,        10),
    JS_CGETSET_MAGIC_DEF("clientBodyTimeout",ngx_js_location_get, ngx_js_location_set,        11),
    JS_CGETSET_MAGIC_DEF("sendTimeout",      ngx_js_location_get, ngx_js_location_set,        12),
    JS_CGETSET_MAGIC_DEF("defaultType",      ngx_js_location_get, ngx_js_location_set,        13),
    JS_CGETSET_MAGIC_DEF("alias",            ngx_js_location_get, ngx_js_location_set, 14),
    JS_CGETSET_MAGIC_DEF("proxy",            ngx_js_location_get, NULL,                15),
    JS_CGETSET_MAGIC_DEF("gzip",             ngx_js_location_get, NULL,                16),
    JS_CGETSET_MAGIC_DEF("headers",          ngx_js_location_get, NULL,                17),
    JS_CGETSET_MAGIC_DEF("rewrite",          ngx_js_location_get, NULL,                18),
    JS_CGETSET_MAGIC_DEF("access",           ngx_js_location_get, NULL,                19),
    JS_CGETSET_MAGIC_DEF("auth",             ngx_js_location_get, NULL,                20),
    JS_CGETSET_MAGIC_DEF("limitReq",         ngx_js_location_get, NULL,                21),
    JS_CGETSET_MAGIC_DEF("limitConn",        ngx_js_location_get, NULL,                22),
    JS_CGETSET_MAGIC_DEF("fastcgi",          ngx_js_location_get, NULL,                23),
    JS_CGETSET_MAGIC_DEF("log",              ngx_js_location_get, NULL,                24),
#if (NGX_HTTP_REALIP)
    JS_CGETSET_MAGIC_DEF("realip",           ngx_js_location_get, NULL,                25),
#endif
    JS_CGETSET_MAGIC_DEF("charset",          ngx_js_location_get, NULL,                26),
    JS_CGETSET_MAGIC_DEF("subFilter",        ngx_js_location_get, NULL,                27),
    JS_CGETSET_MAGIC_DEF("autoindex",        ngx_js_location_get, NULL,                28),
    JS_CGETSET_MAGIC_DEF("referer",          ngx_js_location_get, NULL,                29),
#if (NGX_HTTP_DAV)
    JS_CGETSET_MAGIC_DEF("dav",             ngx_js_location_get, NULL,                30),
#endif
    JS_CGETSET_MAGIC_DEF("ssi",             ngx_js_location_get, NULL,                31),
    JS_CGETSET_MAGIC_DEF("userid",          ngx_js_location_get, NULL,                32),
    JS_CGETSET_MAGIC_DEF("addition",        ngx_js_location_get, NULL,                33),
    JS_CGETSET_MAGIC_DEF("gunzip",          ngx_js_location_get, NULL,                34),
    JS_CGETSET_MAGIC_DEF("slice",           ngx_js_location_get, NULL,                35),
    JS_CGETSET_MAGIC_DEF("imageFilter",     ngx_js_location_get, NULL,                36),
    JS_CGETSET_MAGIC_DEF("xslt",            ngx_js_location_get, NULL,                37),
    JS_CGETSET_MAGIC_DEF("secureLink",      ngx_js_location_get, NULL,                38),
    JS_CGETSET_MAGIC_DEF("mp4",             ngx_js_location_get, NULL,                39),
    JS_CGETSET_MAGIC_DEF("randomIndex",     ngx_js_location_get, NULL,                40),
    JS_CGETSET_MAGIC_DEF("authRequest",     ngx_js_location_get, NULL,                41),
    JS_CGETSET_MAGIC_DEF("gzipStatic",      ngx_js_location_get, NULL,                42),
    JS_CGETSET_MAGIC_DEF("memcached",       ngx_js_location_get, NULL,                43),
    JS_CGETSET_MAGIC_DEF("scgi",            ngx_js_location_get, NULL,                44),
    JS_CGETSET_MAGIC_DEF("uwsgi",           ngx_js_location_get, NULL,                45),
    JS_CGETSET_MAGIC_DEF("mirror",          ngx_js_location_get, NULL,                46),
    JS_CGETSET_MAGIC_DEF("tryFiles",        ngx_js_location_get, NULL,                47),
    JS_CGETSET_MAGIC_DEF("satisfy",         ngx_js_location_get, ngx_js_location_set, 48),
    JS_CGETSET_MAGIC_DEF("limitExcept",           ngx_js_location_get, ngx_js_location_set, 49),
    JS_CGETSET_MAGIC_DEF("lingering",             ngx_js_location_get, ngx_js_location_set, 50),
    JS_CGETSET_MAGIC_DEF("lingeringTimeout",      ngx_js_location_get, ngx_js_location_set, 51),
    JS_CGETSET_MAGIC_DEF("lingeringTime",         ngx_js_location_get, ngx_js_location_set, 52),
    JS_CGETSET_MAGIC_DEF("resolverTimeout",       ngx_js_location_get, ngx_js_location_set, 53),
    JS_CGETSET_MAGIC_DEF("chunkedTransferEncoding", ngx_js_location_get, ngx_js_location_set, 54),
    JS_CGETSET_MAGIC_DEF("msieRefresh",           ngx_js_location_get, ngx_js_location_set, 55),
    JS_CGETSET_MAGIC_DEF("logNotFound",           ngx_js_location_get, ngx_js_location_set, 56),
    JS_CGETSET_MAGIC_DEF("logSubrequest",         ngx_js_location_get, ngx_js_location_set, 57),
    JS_CGETSET_MAGIC_DEF("recursiveErrorPages",   ngx_js_location_get, ngx_js_location_set, 58),
    JS_CGETSET_MAGIC_DEF("clientBodyBufferSize",     ngx_js_location_get, ngx_js_location_set, 59),
    JS_CGETSET_MAGIC_DEF("clientBodyInFileOnly",     ngx_js_location_get, ngx_js_location_set, 60),
    JS_CGETSET_MAGIC_DEF("clientBodyInSingleBuffer", ngx_js_location_get, ngx_js_location_set, 61),
    JS_CGETSET_MAGIC_DEF("resetTimedoutConnection",  ngx_js_location_get, ngx_js_location_set, 62),
    JS_CGETSET_MAGIC_DEF("absoluteRedirect",         ngx_js_location_get, ngx_js_location_set, 63),
    JS_CGETSET_MAGIC_DEF("serverNameInRedirect",     ngx_js_location_get, ngx_js_location_set, 64),
    JS_CGETSET_MAGIC_DEF("portInRedirect",           ngx_js_location_get, ngx_js_location_set, 65),
    JS_CGETSET_MAGIC_DEF("msiePadding",              ngx_js_location_get, ngx_js_location_set, 66),
    JS_CGETSET_MAGIC_DEF("ifModifiedSince",          ngx_js_location_get, ngx_js_location_set, 67),
    JS_CGETSET_MAGIC_DEF("maxRanges",                ngx_js_location_get, ngx_js_location_set, 68),
    JS_CGETSET_MAGIC_DEF("authDelay",                ngx_js_location_get, ngx_js_location_set, 69),
    JS_CGETSET_MAGIC_DEF("keepaliveTime",            ngx_js_location_get, ngx_js_location_set, 70),
    JS_CGETSET_MAGIC_DEF("sendLowat",                ngx_js_location_get, ngx_js_location_set, 71),
    JS_CGETSET_MAGIC_DEF("postponeOutput",           ngx_js_location_get, ngx_js_location_set, 72),
    JS_CGETSET_MAGIC_DEF("typesHashMaxSize",         ngx_js_location_get, NULL,       73),
    JS_CGETSET_MAGIC_DEF("keepaliveDisable",         ngx_js_location_get, ngx_js_location_set, 74),
    JS_CGETSET_MAGIC_DEF("keepaliveMinTimeout",      ngx_js_location_get, ngx_js_location_set, 75),
    JS_CGETSET_MAGIC_DEF("sendfileMaxChunk",         ngx_js_location_get, ngx_js_location_set, 76),
    JS_CGETSET_MAGIC_DEF("readAhead",                ngx_js_location_get, ngx_js_location_set, 77),
    JS_CGETSET_MAGIC_DEF("directio",                 ngx_js_location_get, ngx_js_location_set, 78),
    JS_CGETSET_MAGIC_DEF("directioAlignment",        ngx_js_location_get, ngx_js_location_set, 79),
    JS_CGETSET_MAGIC_DEF("matchType",                ngx_js_location_get, NULL,                80),
    JS_CGETSET_MAGIC_DEF("pattern",                 ngx_js_location_get, NULL,                81),
    JS_CGETSET_DEF       ("errorPage",             ngx_js_location_get_error_page,
                                                   ngx_js_location_set_error_page),

    /* Per-request snapshot read/write control */
    JS_CFUNC_DEF("setWriteMode",  1, ngx_js_location_fn_set_write_mode),
    JS_CFUNC_DEF("setReadMode",   1, ngx_js_location_fn_set_read_mode),
    JS_CFUNC_DEF("setProperty",   2, ngx_js_location_fn_set_property),
    JS_CFUNC_DEF("getProperty",   1, ngx_js_location_fn_get_property),
};


ngx_int_t
ngx_js_location_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_location_proto_funcs,
                               countof(ngx_js_location_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_location_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_location(JSContext *ctx, ngx_http_core_loc_conf_t *clcf)
{
    JSValue                    obj;
    ngx_js_location_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_location_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->clcf       = clcf;
    op->write_mode = NGX_JS_WRITE_GLOBAL; /* default: global writes (backward-compat) */
    op->read_mode  = NGX_JS_WRITE_GLOBAL; /* default: read from global struct */

    obj = JS_NewObjectClass(ctx, ngx_js_location_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/*
 * Recursively walk the static location tree and append a NginxLocation
 * object for every exact or inclusive entry found.
 */
static void
ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx)
{
    if (node == NULL) {
        return;
    }

    /* left subtree */
    ngx_js_collect_locations(ctx, arr, node->left, idx);

    /* this node's exact match (= prefix) */
    if (node->exact) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location(ctx, node->exact));
    }

    /* this node's inclusive (prefix) match */
    if (node->inclusive) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location(ctx, node->inclusive));
    }

    /* child subtree (shared prefix children) */
    ngx_js_collect_locations(ctx, arr, node->tree, idx);

    /* right subtree */
    ngx_js_collect_locations(ctx, arr, node->right, idx);
}


/*
 * Build a JS Array of NginxLocation objects for a server's default
 * root location config (which holds the static_locations tree).
 */
static JSValue
ngx_js_build_locations(JSContext *ctx, ngx_http_core_loc_conf_t *root_clcf)
{
    JSValue   arr;
    uint32_t  idx;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    idx = 0;

#if (NGX_PCRE)
    if (root_clcf->regex_locations) {
        ngx_http_core_loc_conf_t  **rloc;

        for (rloc = root_clcf->regex_locations; *rloc; rloc++) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 ngx_js_wrap_location(ctx, *rloc));
        }
    }
#endif

    ngx_js_collect_locations(ctx, arr, root_clcf->static_locations, &idx);

    return arr;
}


/* ------------------------------------------------------------------ */
/* NginxServer wrapper                                                  */
/* ------------------------------------------------------------------ */

/*
 * One snapshot entry for a prefix / exact location.
 * Used to rebuild the static location BST after addLocation / removeLocation.
 */
typedef struct {
    ngx_http_core_loc_conf_t  *clcf;
    unsigned                   is_exact:1;  /* 1 = exact match (lq->exact) */
    unsigned                   dynamic:1;   /* 1 = added by addLocation */
} ngx_js_loc_entry_t;


#if (NGX_PCRE)
/*
 * One entry in the ordered regex location list.
 * caseless mirrors the ~* modifier; dynamic marks JS-added entries.
 */
typedef struct {
    ngx_http_core_loc_conf_t  *clcf;
    unsigned                   caseless:1;  /* 1 = ~* (case-insensitive) */
    unsigned                   dynamic:1;   /* 1 = added by addLocation */
} ngx_js_regex_entry_t;
#endif


typedef struct {
    ngx_http_core_srv_conf_t  *cscf;
    ngx_cycle_t               *cycle;
    /*
     * server_names.elts lives in cf->temp_pool, which is destroyed after
     * ngx_init_cycle() returns — before workers fork.  Copy the ngx_str_t
     * array into cycle->pool here so workers can safely read server names.
     * The individual name.data pointers are in cycle->pool already.
     */
    ngx_str_t                 *names;
    ngx_uint_t                 nnames;

    /*
     * Dynamic location infrastructure.
     *
     * dyn_pool   — long-lived pool for new clcf structs and loc_conf arrays;
     *              freed only at server finalizer time.
     * tree_pool  — rebuilt on every addLocation/removeLocation call; holds
     *              ngx_http_location_queue_t items and BST nodes only.
     *              NULL until the first addLocation is called.
     * prefix_locs — flat snapshot of ALL prefix/exact locations (static ones
     *              snapshotted at wrap time + dynamic ones appended at add
     *              time).  Used as the source for BST rebuilds.
     * regex_locs  — ordered list of ALL regex locations (static + dynamic).
     *              Element type: ngx_js_regex_entry_t (NGX_PCRE only).
     */
    ngx_pool_t                *dyn_pool;
    ngx_pool_t                *tree_pool;
    ngx_array_t                prefix_locs;  /* ngx_js_loc_entry_t[] */
    ngx_array_t                regex_locs;   /* ngx_js_regex_entry_t[] */
    ngx_array_t                named_locs;   /* ngx_js_loc_entry_t[] (@name) */
} ngx_js_server_opaque_t;


/* ------------------------------------------------------------------ *
 * Location tree building helpers                                       *
 *                                                                      *
 * These are independent re-implementations of the three static         *
 * functions in ngx_http.c that build the static location BST.         *
 * They use ngx_pool_t* / ngx_log_t* instead of ngx_conf_t*.           *
 * ------------------------------------------------------------------ */

static ngx_int_t
ngx_js_cmp_locations(const ngx_queue_t *one, const ngx_queue_t *two)
{
    ngx_int_t                   rc;
    ngx_http_core_loc_conf_t   *first, *second;
    ngx_http_location_queue_t  *lq1, *lq2;

    lq1 = (ngx_http_location_queue_t *) one;
    lq2 = (ngx_http_location_queue_t *) two;

    first  = lq1->exact ? lq1->exact : lq1->inclusive;
    second = lq2->exact ? lq2->exact : lq2->inclusive;

    if (first->noname && !second->noname) {
        return 1;
    }

    if (!first->noname && second->noname) {
        return -1;
    }

    if (first->noname || second->noname) {
        return 0;
    }

    if (first->named && !second->named) {
        return 1;
    }

    if (!first->named && second->named) {
        return -1;
    }

    if (first->named && second->named) {
        return ngx_strcmp(first->name.data, second->name.data);
    }

#if (NGX_PCRE)

    if (first->regex && !second->regex) {
        return 1;
    }

    if (!first->regex && second->regex) {
        return -1;
    }

    if (first->regex || second->regex) {
        return 0;
    }

#endif

    rc = ngx_filename_cmp(first->name.data, second->name.data,
                          ngx_min(first->name.len, second->name.len) + 1);

    if (rc == 0 && !first->exact_match && second->exact_match) {
        return 1;
    }

    return rc;
}


static ngx_int_t
ngx_js_join_exact_locations(ngx_log_t *log, ngx_queue_t *locations)
{
    ngx_queue_t                *q, *x;
    ngx_http_location_queue_t  *lq, *lx;

    q = ngx_queue_head(locations);

    while (q != ngx_queue_last(locations)) {

        x = ngx_queue_next(q);

        lq = (ngx_http_location_queue_t *) q;
        lx = (ngx_http_location_queue_t *) x;

        if (lq->name->len == lx->name->len
            && ngx_filename_cmp(lq->name->data, lx->name->data, lx->name->len)
               == 0)
        {
            if ((lq->exact && lx->exact) || (lq->inclusive && lx->inclusive)) {
                ngx_log_error(NGX_LOG_WARN, log, 0,
                              "duplicate dynamic location \"%V\" ignored",
                              lx->name);
                ngx_queue_remove(x);
                continue;
            }

            lq->inclusive = lx->inclusive;

            ngx_queue_remove(x);

            continue;
        }

        q = ngx_queue_next(q);
    }

    return NGX_OK;
}


static void
ngx_js_create_locations_list(ngx_queue_t *locations, ngx_queue_t *q)
{
    u_char                     *name;
    size_t                      len;
    ngx_queue_t                *x, tail;
    ngx_http_location_queue_t  *lq, *lx;

    if (q == ngx_queue_last(locations)) {
        return;
    }

    lq = (ngx_http_location_queue_t *) q;

    if (lq->inclusive == NULL) {
        ngx_js_create_locations_list(locations, ngx_queue_next(q));
        return;
    }

    len  = lq->name->len;
    name = lq->name->data;

    for (x = ngx_queue_next(q);
         x != ngx_queue_sentinel(locations);
         x = ngx_queue_next(x))
    {
        lx = (ngx_http_location_queue_t *) x;

        if (len > lx->name->len
            || ngx_filename_cmp(name, lx->name->data, len) != 0)
        {
            break;
        }
    }

    q = ngx_queue_next(q);

    if (q == x) {
        ngx_js_create_locations_list(locations, x);
        return;
    }

    ngx_queue_split(locations, q, &tail);
    ngx_queue_add(&lq->list, &tail);

    if (x == ngx_queue_sentinel(locations)) {
        ngx_js_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
        return;
    }

    ngx_queue_split(&lq->list, x, &tail);
    ngx_queue_add(locations, &tail);

    ngx_js_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
    ngx_js_create_locations_list(locations, x);
}


static ngx_http_location_tree_node_t *
ngx_js_create_locations_tree(ngx_pool_t *pool, ngx_queue_t *locations,
    size_t prefix)
{
    size_t                          len;
    ngx_queue_t                    *q, tail;
    ngx_http_location_queue_t      *lq;
    ngx_http_location_tree_node_t  *node;

    q  = ngx_queue_middle(locations);
    lq = (ngx_http_location_queue_t *) q;
    len = lq->name->len - prefix;

    node = ngx_palloc(pool,
                      offsetof(ngx_http_location_tree_node_t, name) + len);
    if (node == NULL) {
        return NULL;
    }

    node->left      = NULL;
    node->right     = NULL;
    node->tree      = NULL;
    node->exact     = lq->exact;
    node->inclusive = lq->inclusive;

    node->auto_redirect = (u_char) (
        (lq->exact     && lq->exact->auto_redirect) ||
        (lq->inclusive && lq->inclusive->auto_redirect));

    node->len = (u_short) len;
    ngx_memcpy(node->name, &lq->name->data[prefix], len);

    ngx_queue_split(locations, q, &tail);

    if (ngx_queue_empty(locations)) {
        goto inclusive;
    }

    node->left = ngx_js_create_locations_tree(pool, locations, prefix);
    if (node->left == NULL) {
        return NULL;
    }

    ngx_queue_remove(q);

    if (ngx_queue_empty(&tail)) {
        goto inclusive;
    }

    node->right = ngx_js_create_locations_tree(pool, &tail, prefix);
    if (node->right == NULL) {
        return NULL;
    }

inclusive:

    if (ngx_queue_empty(&lq->list)) {
        return node;
    }

    node->tree = ngx_js_create_locations_tree(pool, &lq->list, prefix + len);
    if (node->tree == NULL) {
        return NULL;
    }

    return node;
}


/*
 * Recursively walk the static BST and add every (clcf, is_exact) pair
 * into op->prefix_locs as a non-dynamic snapshot entry.
 */
static void
ngx_js_snapshot_bst(ngx_js_server_opaque_t *op,
    ngx_http_location_tree_node_t *node)
{
    ngx_js_loc_entry_t  *e;

    if (node == NULL) {
        return;
    }

    ngx_js_snapshot_bst(op, node->left);

    if (node->exact) {
        e = ngx_array_push(&op->prefix_locs);
        if (e) {
            e->clcf     = node->exact;
            e->is_exact = 1;
            e->dynamic  = 0;
        }
    }

    if (node->inclusive) {
        e = ngx_array_push(&op->prefix_locs);
        if (e) {
            e->clcf     = node->inclusive;
            e->is_exact = 0;
            e->dynamic  = 0;
        }
    }

    ngx_js_snapshot_bst(op, node->tree);
    ngx_js_snapshot_bst(op, node->right);
}


/*
 * Rebuild the static location BST from op->prefix_locs[] and the
 * regex_locations array from op->regex_locs[].  Atomically swaps
 * root_clcf->static_locations and root_clcf->regex_locations.
 */
static ngx_int_t
ngx_js_rebuild_loc_tree(ngx_js_server_opaque_t *op, ngx_log_t *log)
{
    ngx_pool_t                    *pool;
    ngx_queue_t                    locations;
    ngx_uint_t                     i;
    ngx_js_loc_entry_t            *e;
    ngx_http_location_queue_t     *lq;
    ngx_http_location_tree_node_t *new_root;
    ngx_http_core_loc_conf_t      *root_clcf;
#if (NGX_PCRE)
    ngx_http_core_loc_conf_t     **rloc_arr;
    ngx_js_regex_entry_t          *re;
    ngx_uint_t                     ri;
#endif

    pool = ngx_create_pool(4096, log);
    if (pool == NULL) {
        return NGX_ERROR;
    }

    /* Build a location queue from prefix_locs[] */

    ngx_queue_init(&locations);

    e = op->prefix_locs.elts;

    for (i = 0; i < op->prefix_locs.nelts; i++) {

        lq = ngx_palloc(pool, sizeof(ngx_http_location_queue_t));
        if (lq == NULL) {
            ngx_destroy_pool(pool);
            return NGX_ERROR;
        }

        ngx_memzero(lq, sizeof(*lq));
        ngx_queue_init(&lq->list);

        if (e[i].is_exact) {
            lq->exact     = e[i].clcf;
            lq->inclusive = NULL;
        } else {
            lq->exact     = NULL;
            lq->inclusive = e[i].clcf;
        }

        lq->name      = &e[i].clcf->name;
        lq->file_name = (u_char *) "dynamic";
        lq->line      = 0;

        ngx_queue_insert_tail(&locations, &lq->queue);
    }

    if (ngx_queue_empty(&locations)) {
        new_root = NULL;
        goto swap;
    }

    /* Sort, join exact pairs, build list structure, build BST */

    ngx_queue_sort(&locations, ngx_js_cmp_locations);

    if (ngx_js_join_exact_locations(log, &locations) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

    ngx_js_create_locations_list(&locations, ngx_queue_head(&locations));

    new_root = ngx_js_create_locations_tree(pool, &locations, 0);
    if (new_root == NULL) {
        ngx_destroy_pool(pool);
        return NGX_ERROR;
    }

swap:

    root_clcf = op->cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    /* Swap BST root */
    root_clcf->static_locations = new_root;

#if (NGX_PCRE)
    /* Rebuild regex_locations array from op->regex_locs[] */
    if (op->regex_locs.nelts > 0) {

        rloc_arr = ngx_palloc(pool,
            (op->regex_locs.nelts + 1) * sizeof(ngx_http_core_loc_conf_t *));
        if (rloc_arr == NULL) {
            ngx_destroy_pool(pool);
            return NGX_ERROR;
        }

        re = (ngx_js_regex_entry_t *) op->regex_locs.elts;
        for (ri = 0; ri < op->regex_locs.nelts; ri++) {
            rloc_arr[ri] = re[ri].clcf;
        }
        rloc_arr[op->regex_locs.nelts] = NULL;

        root_clcf->regex_locations = rloc_arr;

    } else {
        root_clcf->regex_locations = NULL;
    }
#endif

    /* Rebuild cscf->named_locations from op->named_locs[] */
    {
        ngx_js_loc_entry_t        *ne;
        ngx_http_core_loc_conf_t **nloc_arr;
        ngx_uint_t                 ni;

        if (op->named_locs.nelts > 0) {
            nloc_arr = ngx_palloc(pool,
                (op->named_locs.nelts + 1) * sizeof(ngx_http_core_loc_conf_t *));
            if (nloc_arr == NULL) {
                ngx_destroy_pool(pool);
                return NGX_ERROR;
            }
            ne = (ngx_js_loc_entry_t *) op->named_locs.elts;
            for (ni = 0; ni < op->named_locs.nelts; ni++) {
                nloc_arr[ni] = ne[ni].clcf;
            }
            nloc_arr[op->named_locs.nelts] = NULL;
            op->cscf->named_locations = nloc_arr;
        } else {
            op->cscf->named_locations = NULL;
        }
    }

    /* Free the previous tree pool (if we own it; never free cycle->pool) */
    if (op->tree_pool) {
        ngx_destroy_pool(op->tree_pool);
    }

    op->tree_pool = pool;

    return NGX_OK;
}


/*
 * Find a prefix/exact clcf by path string in op->prefix_locs[].
 * Returns NULL if not found.
 */
static ngx_http_core_loc_conf_t *
ngx_js_find_prefix_clcf(ngx_js_server_opaque_t *op, const char *path)
{
    ngx_uint_t           i;
    ngx_js_loc_entry_t  *e;
    size_t               plen;

    plen = ngx_strlen(path);
    e    = op->prefix_locs.elts;

    for (i = 0; i < op->prefix_locs.nelts; i++) {
        if (e[i].clcf->name.len == plen
            && ngx_strncmp(e[i].clcf->name.data, path, plen) == 0)
        {
            return e[i].clcf;
        }
    }

    return NULL;
}


/*
 * srv.addLocation(pattern [, opts])
 *
 * Supported pattern prefixes (whitespace after prefix is optional):
 *   "= /path"   — exact match
 *   "^~ /path"  — preferential prefix (disables regex scan on match)
 *   "~ /regex"  — case-sensitive PCRE regex
 *   "~* /regex" — case-insensitive PCRE regex
 *   "/path"     — normal prefix
 *
 * opts:
 *   template: '/existing-path'  — copy settings from an existing location
 *   index: N                    — regex only: insert at position N in the
 *                                 regex array (0 = first checked, default
 *                                 = append at end)
 *
 * Returns the new NginxLocation object so the caller can set
 *   loc.handler, loc.root, etc. immediately.
 */
static JSValue
ngx_js_server_fn_add_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_loc_conf_t  *new_clcf, *tmpl_clcf, *srv_clcf;
    ngx_js_loc_conf_t         *jlcf;
    ngx_js_loc_entry_t        *entry;
#if (NGX_PCRE)
    ngx_js_regex_entry_t      *re_entry;
#endif
    JSValue                    opts, tmpl_val;
    const char                *pat_str, *tmpl_path;
    ngx_str_t                  name;
    u_char                    *p;
    int                        exact_match, noregex, is_named;
#if (NGX_PCRE)
    int                        is_regex, caseless;
    ngx_int_t                  regex_index;  /* insert position; -1 = append */
#endif

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->dyn_pool == NULL) {
        return JS_ThrowInternalError(ctx,
            "addLocation: dynamic pool not initialised");
    }

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "addLocation: first argument must be a pattern string");
    }

    pat_str = JS_ToCString(ctx, argv[0]);
    if (!pat_str) {
        return JS_EXCEPTION;
    }

    /* Trim leading whitespace */
    p = (u_char *) pat_str;
    while (*p == ' ') { p++; }

    exact_match = 0;
    noregex     = 0;
    is_named    = 0;
#if (NGX_PCRE)
    is_regex    = 0;
    caseless    = 0;
#endif

    /* Detect "@name" — named location */
    if (p[0] == '@') {
        is_named = 1;
        /* name keeps the '@' prefix (that is how nginx stores it) */
    }

    /* Detect "= " prefix */
    if (!is_named && p[0] == '=' && p[1] == ' ') {
        exact_match = 1;
        p += 2;
        while (*p == ' ') { p++; }

    /* Detect "^~ " prefix */
    } else if (!is_named && p[0] == '^' && p[1] == '~' && p[2] == ' ') {
        noregex = 1;
        p += 3;
        while (*p == ' ') { p++; }

#if (NGX_PCRE)
    /* Detect "~* " prefix — case-insensitive regex */
    } else if (!is_named && p[0] == '~' && p[1] == '*' && p[2] == ' ') {
        is_regex = 1;
        caseless = 1;
        p += 3;
        while (*p == ' ') { p++; }

    /* Detect "~ " prefix — case-sensitive regex */
    } else if (!is_named && p[0] == '~' && p[1] == ' ') {
        is_regex = 1;
        caseless = 0;
        p += 2;
        while (*p == ' ') { p++; }
#else
    } else if (!is_named && p[0] == '~') {
        JS_FreeCString(ctx, pat_str);
        return JS_ThrowTypeError(ctx,
            "addLocation: regex locations require PCRE support");
#endif
    }

    name.len  = ngx_strlen(p);
    name.data = ngx_pnalloc(op->dyn_pool, name.len + 1);
    if (name.data == NULL) {
        JS_FreeCString(ctx, pat_str);
        return JS_EXCEPTION;
    }

    ngx_memcpy(name.data, p, name.len);
    name.data[name.len] = '\0';

    JS_FreeCString(ctx, pat_str);

    /*
     * Duplicate detection: if a location with the same name (and same
     * is_exact / regex type) already exists, return the existing wrapper
     * rather than adding a second entry.  This makes addLocation idempotent
     * — calling it twice safely returns the same location object.
     */
    if (is_named) {
        ngx_js_loc_entry_t  *dup_e;
        ngx_uint_t           di;

        dup_e = (ngx_js_loc_entry_t *) op->named_locs.elts;
        for (di = 0; di < op->named_locs.nelts; di++) {
            if (dup_e[di].clcf->name.len == name.len
                && ngx_memcmp(dup_e[di].clcf->name.data,
                              name.data, name.len) == 0)
            {
                return ngx_js_wrap_location(ctx, dup_e[di].clcf);
            }
        }
    }
#if (NGX_PCRE)
    else if (is_regex) {
        ngx_js_regex_entry_t  *dup_re;
        ngx_uint_t             di;

        dup_re = (ngx_js_regex_entry_t *) op->regex_locs.elts;
        for (di = 0; di < op->regex_locs.nelts; di++) {
            if (dup_re[di].clcf->name.len == name.len
                && ngx_memcmp(dup_re[di].clcf->name.data,
                              name.data, name.len) == 0)
            {
                return ngx_js_wrap_location(ctx, dup_re[di].clcf);
            }
        }
    } else {
#endif
    if (!is_named) {
        ngx_js_loc_entry_t  *dup_e;
        ngx_uint_t           di;

        dup_e = (ngx_js_loc_entry_t *) op->prefix_locs.elts;
        for (di = 0; di < op->prefix_locs.nelts; di++) {
            if (dup_e[di].clcf->name.len == name.len
                && dup_e[di].is_exact == (unsigned) exact_match
                && ngx_memcmp(dup_e[di].clcf->name.data,
                              name.data, name.len) == 0)
            {
                return ngx_js_wrap_location(ctx, dup_e[di].clcf);
            }
        }
    }
#if (NGX_PCRE)
    }
#endif

    /* Look up optional template (and ordering for regex locations) */
    tmpl_clcf = NULL;
#if (NGX_PCRE)
    regex_index = -1;  /* default: append at end */
#endif

    if (argc >= 2 && JS_IsObject(argv[1])) {
        opts     = argv[1];
        tmpl_val = JS_GetPropertyStr(ctx, opts, "template");
        if (!JS_IsUndefined(tmpl_val) && !JS_IsNull(tmpl_val)) {
            tmpl_path = JS_ToCString(ctx, tmpl_val);
            if (tmpl_path) {
                tmpl_clcf = ngx_js_find_prefix_clcf(op, tmpl_path);
                JS_FreeCString(ctx, tmpl_path);
            }
        }
        JS_FreeValue(ctx, tmpl_val);
#if (NGX_PCRE)
        /*
         * index: N — absolute insert position (0 = first checked)
         * before: pattern — insert before the named regex location
         * after:  pattern — insert after  the named regex location
         * (before/after are ignored when index is also given)
         */
        {
            JSValue  idx_val    = JS_GetPropertyStr(ctx, opts, "index");
            JSValue  before_val = JS_GetPropertyStr(ctx, opts, "before");
            JSValue  after_val  = JS_GetPropertyStr(ctx, opts, "after");

            if (!JS_IsUndefined(idx_val)) {
                int32_t  iv;
                if (JS_ToInt32(ctx, &iv, idx_val) == 0) {
                    regex_index = (ngx_int_t) iv;
                }
            } else {
                /* before/after: find the named regex entry */
                JSValue      rel_val = !JS_IsUndefined(before_val)
                                       ? before_val : after_val;
                int          is_after = !JS_IsUndefined(after_val)
                                        && JS_IsUndefined(before_val);

                if (!JS_IsUndefined(rel_val)) {
                    const char           *rel_str;
                    const u_char         *rp;
                    ngx_js_regex_entry_t *re_arr;
                    ngx_uint_t            ri;
                    size_t                rlen;

                    rel_str = JS_ToCString(ctx, rel_val);
                    if (rel_str) {
                        rp = (const u_char *) rel_str;
                        while (*rp == ' ') { rp++; }
                        /* strip any modifier prefix */
                        if (rp[0]=='~' && rp[1]=='*' && rp[2]==' ') {
                            rp += 3; while (*rp == ' ') { rp++; }
                        } else if (rp[0]=='~' && rp[1]==' ') {
                            rp += 2; while (*rp == ' ') { rp++; }
                        } else if (rp[0]=='=' && rp[1]==' ') {
                            rp += 2; while (*rp == ' ') { rp++; }
                        } else if (rp[0]=='^' && rp[1]=='~' && rp[2]==' ') {
                            rp += 3; while (*rp == ' ') { rp++; }
                        }
                        rlen   = ngx_strlen(rp);
                        re_arr = (ngx_js_regex_entry_t *) op->regex_locs.elts;
                        for (ri = 0; ri < op->regex_locs.nelts; ri++) {
                            if (re_arr[ri].clcf->name.len == rlen
                                && ngx_memcmp(re_arr[ri].clcf->name.data,
                                              rp, rlen) == 0)
                            {
                                regex_index = (ngx_int_t) ri
                                              + (is_after ? 1 : 0);
                                break;
                            }
                        }
                        JS_FreeCString(ctx, rel_str);
                    }
                }
            }

            JS_FreeValue(ctx, idx_val);
            JS_FreeValue(ctx, before_val);
            JS_FreeValue(ctx, after_val);
        }
#endif
    }

    /* Server's default (implicit "/") loc_conf — used as fallback template */
    srv_clcf = op->cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    /* Allocate and initialise the new clcf */
    new_clcf = ngx_palloc(op->dyn_pool, sizeof(ngx_http_core_loc_conf_t));
    if (new_clcf == NULL) {
        return JS_EXCEPTION;
    }

    if (tmpl_clcf) {
        ngx_memcpy(new_clcf, tmpl_clcf, sizeof(ngx_http_core_loc_conf_t));
    } else {
        ngx_memcpy(new_clcf, srv_clcf, sizeof(ngx_http_core_loc_conf_t));
    }

    /* Override identity fields */
    new_clcf->name         = name;
    new_clcf->escaped_name = name;
    new_clcf->exact_match  = exact_match;
    new_clcf->noregex      = noregex;
    new_clcf->named        = is_named;
    new_clcf->noname       = 0;
#if (NGX_PCRE)
    new_clcf->regex        = NULL;  /* set below for regex locations */
#endif
    new_clcf->static_locations = NULL;
#if (NGX_PCRE)
    new_clcf->regex_locations  = NULL;
#endif
    new_clcf->handler = NULL;

#if (NGX_PCRE)
    /* Compile the regex pattern for ~ and ~* locations */
    if (is_regex) {
        ngx_regex_compile_t   rc;
        u_char                err_buf[128];
        ngx_http_regex_t     *re;

        ngx_memzero(&rc, sizeof(rc));
        rc.pattern  = name;
        rc.pool     = op->dyn_pool;
        rc.options  = caseless ? NGX_REGEX_CASELESS : 0;
        rc.err.data = err_buf;
        rc.err.len  = sizeof(err_buf) - 1;

        if (ngx_regex_compile(&rc) != NGX_OK) {
            return JS_ThrowInternalError(ctx,
                "addLocation: regex compile failed: %*s",
                (int) rc.err.len, rc.err.data);
        }

        re = ngx_palloc(op->dyn_pool, sizeof(ngx_http_regex_t));
        if (re == NULL) {
            return JS_EXCEPTION;
        }

        re->regex      = rc.regex;
        re->ncaptures  = 0;       /* no variable captures for JS routing */
        re->variables  = NULL;
        re->nvariables = 0;
        re->name       = name;

        new_clcf->regex = re;
    }
#endif

    /* Fresh loc_conf pointer array (shared module conf pointers, except JS).
     * Source priority: template's loc_conf → server ctx loc_conf.
     * Use op->cscf->ctx->loc_conf directly (not srv_clcf->loc_conf) because
     * the server root clcf's loc_conf field may be NULL after merge. */
    {
        void **base = (tmpl_clcf && tmpl_clcf->loc_conf)
                      ? tmpl_clcf->loc_conf
                      : op->cscf->ctx->loc_conf;

        new_clcf->loc_conf = ngx_palloc(op->dyn_pool,
                                        sizeof(void *) * ngx_http_max_module);
        if (new_clcf->loc_conf == NULL) {
            return JS_EXCEPTION;
        }

        if (base) {
            ngx_memcpy(new_clcf->loc_conf, base,
                       sizeof(void *) * ngx_http_max_module);
        } else {
            ngx_memzero(new_clcf->loc_conf,
                        sizeof(void *) * ngx_http_max_module);
        }

        /*
         * CRITICAL: loc_conf[core_idx] must point to new_clcf itself,
         * not to srv_clcf.  ngx_http_core_find_location uses this slot
         * to get pclcf->static_locations for nested location search.
         * If it pointed to srv_clcf, the search would loop back into the
         * top-level BST and recurse infinitely → stack overflow → SIGSEGV.
         */
        new_clcf->loc_conf[ngx_http_core_module.ctx_index] = new_clcf;
    }

    /* Fresh ngx_js_loc_conf_t — handler_idx starts at -1 (no handler) */
    jlcf = ngx_pcalloc(op->dyn_pool, sizeof(ngx_js_loc_conf_t));
    if (jlcf == NULL) {
        return JS_EXCEPTION;
    }
    jlcf->handler_idx = -1;
    new_clcf->loc_conf[ngx_js_http_module.ctx_index] = jlcf;

#if (NGX_PCRE)
    if (is_regex) {
        ngx_int_t  insert_at;
        ngx_uint_t nelts;

        /* Grow the array by one slot at the END, then memmove if needed */
        re_entry = ngx_array_push(&op->regex_locs);
        if (re_entry == NULL) {
            return JS_EXCEPTION;
        }

        nelts = op->regex_locs.nelts;   /* after push, so >= 1 */

        /* Clamp index: negative or beyond nelts-1 → append (already there) */
        if (regex_index < 0 || regex_index >= (ngx_int_t)(nelts - 1)) {
            insert_at = (ngx_int_t)(nelts - 1);  /* last slot = appended */
        } else {
            insert_at = regex_index;
        }

        if (insert_at < (ngx_int_t)(nelts - 1)) {
            /* Shift elements [insert_at .. nelts-2] forward by one */
            re_entry = (ngx_js_regex_entry_t *) op->regex_locs.elts;
            ngx_memmove(&re_entry[insert_at + 1], &re_entry[insert_at],
                        (nelts - 1 - (ngx_uint_t) insert_at)
                        * sizeof(ngx_js_regex_entry_t));
            re_entry = &re_entry[insert_at];
        }
        /* re_entry now points to the slot we own */
        re_entry->clcf     = new_clcf;
        re_entry->caseless = caseless;
        re_entry->dynamic  = 1;

        /* Rebuild (regex_locations only needs pool; BST unchanged but cheap) */
        if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
            op->regex_locs.nelts--;   /* roll back */
            return JS_EXCEPTION;
        }

    } else {
#endif
        if (is_named) {
            /* Append to named_locs[] */
            entry = ngx_array_push(&op->named_locs);
            if (entry == NULL) {
                return JS_EXCEPTION;
            }

            entry->clcf     = new_clcf;
            entry->is_exact = 0;
            entry->dynamic  = 1;

            if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
                op->named_locs.nelts--;
                return JS_EXCEPTION;
            }

        } else {
            /* Append to prefix_locs[] */
            entry = ngx_array_push(&op->prefix_locs);
            if (entry == NULL) {
                return JS_EXCEPTION;
            }

            entry->clcf     = new_clcf;
            entry->is_exact = exact_match;
            entry->dynamic  = 1;

            /* Rebuild the live location tree */
            if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
                op->prefix_locs.nelts--;  /* roll back the push */
                return JS_EXCEPTION;
            }
        }
#if (NGX_PCRE)
    }
#endif

    return ngx_js_wrap_location(ctx, new_clcf);
}


/*
 * srv.removeLocation(pattern)
 *
 * Removes the location matching pattern from the live BST / regex array.
 * Pattern syntax is the same as addLocation:
 *   "= /path"   — exact match
 *   "^~ /path"  — preferential prefix
 *   "~ /regex"  — case-sensitive regex
 *   "~* /regex" — case-insensitive regex
 *   "/path"     — normal prefix
 *
 * Regex removal matches by pattern string only (first match wins if the
 * same pattern appears more than once).
 *
 * Returns true if a location was found and removed, false otherwise.
 */
static JSValue
ngx_js_server_fn_remove_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t    *op;
    ngx_js_loc_entry_t        *e;
    const char                *pat_str;
    u_char                    *p;
    ngx_str_t                  name;
    ngx_uint_t                 i;
    int                        exact_match, is_named;
#if (NGX_PCRE)
    int                        is_regex;
    ngx_js_regex_entry_t      *re;
#endif

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "removeLocation: first argument must be a pattern string");
    }

    pat_str = JS_ToCString(ctx, argv[0]);
    if (!pat_str) {
        return JS_EXCEPTION;
    }

    /* Trim leading whitespace and parse modifier */
    p = (u_char *) pat_str;
    while (*p == ' ') { p++; }

    exact_match = 0;
    is_named    = 0;
#if (NGX_PCRE)
    is_regex    = 0;
#endif

    if (p[0] == '@') {
        is_named = 1;   /* name keeps the '@' */

    } else if (p[0] == '=' && p[1] == ' ') {
        exact_match = 1;
        p += 2;
        while (*p == ' ') { p++; }

    } else if (p[0] == '^' && p[1] == '~' && p[2] == ' ') {
        p += 3;
        while (*p == ' ') { p++; }

#if (NGX_PCRE)
    } else if (p[0] == '~' && p[1] == '*' && p[2] == ' ') {
        is_regex = 1;
        p += 3;
        while (*p == ' ') { p++; }

    } else if (p[0] == '~' && p[1] == ' ') {
        is_regex = 1;
        p += 2;
        while (*p == ' ') { p++; }
#else
    } else if (p[0] == '~') {
        JS_FreeCString(ctx, pat_str);
        return JS_ThrowTypeError(ctx,
            "removeLocation: regex locations require PCRE support");
#endif
    }

    name.len  = ngx_strlen(p);
    name.data = (u_char *) p;

    /* Named location removal */
    if (is_named) {
        e = (ngx_js_loc_entry_t *) op->named_locs.elts;

        for (i = 0; i < op->named_locs.nelts; i++) {
            if (e[i].clcf->name.len == name.len
                && ngx_memcmp(e[i].clcf->name.data, name.data, name.len) == 0)
            {
                break;
            }
        }

        JS_FreeCString(ctx, pat_str);

        if (i == op->named_locs.nelts) {
            return JS_FALSE;   /* not found */
        }

        if (i < op->named_locs.nelts - 1) {
            ngx_memmove(&e[i], &e[i + 1],
                        (op->named_locs.nelts - i - 1) * sizeof(*e));
        }
        op->named_locs.nelts--;

        if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
            op->named_locs.nelts++;
            return JS_EXCEPTION;
        }

        return JS_TRUE;
    }

#if (NGX_PCRE)
    if (is_regex) {
        /* Search regex_locs[] by pattern name string */
        re = (ngx_js_regex_entry_t *) op->regex_locs.elts;

        for (i = 0; i < op->regex_locs.nelts; i++) {
            if (re[i].clcf->name.len == name.len
                && ngx_memcmp(re[i].clcf->name.data,
                              name.data, name.len) == 0)
            {
                break;
            }
        }

        JS_FreeCString(ctx, pat_str);

        if (i == op->regex_locs.nelts) {
            return JS_FALSE;   /* not found */
        }

        /* Splice out */
        if (i < op->regex_locs.nelts - 1) {
            ngx_memmove(&re[i], &re[i + 1],
                        (op->regex_locs.nelts - i - 1)
                        * sizeof(ngx_js_regex_entry_t));
        }
        op->regex_locs.nelts--;

        if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
            op->regex_locs.nelts++;
            return JS_EXCEPTION;
        }

        return JS_TRUE;
    }
#endif

    /* Prefix / exact-match path */
    e = op->prefix_locs.elts;

    for (i = 0; i < op->prefix_locs.nelts; i++) {
        if (e[i].clcf->name.len == name.len
            && e[i].is_exact == (unsigned) exact_match
            && ngx_memcmp(e[i].clcf->name.data, name.data, name.len) == 0)
        {
            break;
        }
    }

    JS_FreeCString(ctx, pat_str);

    if (i == op->prefix_locs.nelts) {
        return JS_FALSE;   /* not found */
    }

    /* Splice the entry out of prefix_locs[] */
    if (i < op->prefix_locs.nelts - 1) {
        ngx_memmove(&e[i], &e[i + 1],
                    (op->prefix_locs.nelts - i - 1) * sizeof(*e));
    }
    op->prefix_locs.nelts--;

    /* Rebuild the live location tree */
    if (ngx_js_rebuild_loc_tree(op, op->cycle->log) != NGX_OK) {
        op->prefix_locs.nelts++;   /* roll back (entry data still intact) */
        return JS_EXCEPTION;
    }

    return JS_TRUE;
}


static void
ngx_js_server_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_server_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_server_class_id);
    if (op) {
        if (op->tree_pool) {
            ngx_destroy_pool(op->tree_pool);
            op->tree_pool = NULL;
        }
        if (op->dyn_pool) {
            ngx_destroy_pool(op->dyn_pool);
            op->dyn_pool = NULL;
        }
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_server_class = {
    "NginxServer",
    .finalizer = ngx_js_server_finalizer
};


/*
 * Magic values for ngx_js_server_get / ngx_js_server_set:
 *   0 — name                    (r/o: first server_name, or "" if none)
 *   1 — root                    (r/w: document root from implicit / location)
 *   2 — clientHeaderBufferSize  (r/o: bytes)
 *   3 — clientHeaderTimeout     (r/o: ms)
 *   4 — ignoreInvalidHeaders    (r/o: bool)
 *   5 — mergeSlashes            (r/o: bool)
 *   6 — underscoresInHeaders    (r/o: bool)
 *   7 — serverTokens            (r/o: "off" | "on" | "build")
 */
static JSValue
ngx_js_server_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf = op->cscf;

    switch (magic) {

    case 0: /* name — first server_name or "" */
        if (op->nnames == 0) {
            return JS_NewString(ctx, "");
        }

        return JS_NewStringLen(ctx, (const char *) op->names[0].data,
                               op->names[0].len);

    case 1: /* root — from the server's default (/) location config */
        clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
        return JS_NewStringLen(ctx, (const char *) clcf->root.data,
                               clcf->root.len);

    case 2: /* clientHeaderBufferSize — bytes */
        return JS_NewInt64(ctx, (int64_t) cscf->client_header_buffer_size);

    case 3: /* clientHeaderTimeout — ms */
        return JS_NewInt64(ctx, (int64_t) cscf->client_header_timeout);

    case 4: /* ignoreInvalidHeaders */
        return JS_NewBool(ctx, cscf->ignore_invalid_headers);

    case 5: /* mergeSlashes */
        return JS_NewBool(ctx, cscf->merge_slashes);

    case 6: /* underscoresInHeaders */
        return JS_NewBool(ctx, cscf->underscores_in_headers);

    case 7: /* serverTokens — read from server's default loc conf */
    {
        const char  *tok;

        clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

        switch (clcf->server_tokens) {
        case NGX_HTTP_SERVER_TOKENS_OFF:   tok = "off";   break;
        case NGX_HTTP_SERVER_TOKENS_BUILD: tok = "build"; break;
        default:                           tok = "on";    break;
        }

        return JS_NewString(ctx, tok);
    }

    case 8: /* connectionPoolSize — bytes */
        return JS_NewInt64(ctx, (int64_t) cscf->connection_pool_size);

    case 9: /* requestPoolSize — bytes */
        return JS_NewInt64(ctx, (int64_t) cscf->request_pool_size);

    }

    return JS_UNDEFINED;
}


/*
 * server.largeClientHeaderBuffers — {num: N, size: S} object.
 * Maps to large_client_header_buffers directive (ngx_bufs_t).
 */
static JSValue
ngx_js_server_get_large_client_hdr_bufs(JSContext *ctx,
    JSValueConst this_val)
{
    ngx_js_server_opaque_t    *op;
    JSValue                    obj;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "num",
                      JS_NewInt32(ctx,
                          (int32_t) op->cscf->large_client_header_buffers.num));
    JS_SetPropertyStr(ctx, obj, "size",
                      JS_NewInt64(ctx,
                          (int64_t) op->cscf->large_client_header_buffers.size));
    return obj;
}


/*
 * Recursively update any child location in the static_locations tree
 * whose root.data still points to the old server-level root.  This is
 * necessary because ngx_conf_merge_str_value copies the data pointer by
 * value; after merge, child locations that did not set their own root
 * share the same .data address as the parent.
 */
static void
ngx_js_propagate_root(ngx_http_location_tree_node_t *node,
    u_char *old_data, u_char *new_data, size_t new_len)
{
    if (node == NULL) {
        return;
    }

    ngx_js_propagate_root(node->left,  old_data, new_data, new_len);
    ngx_js_propagate_root(node->right, old_data, new_data, new_len);
    ngx_js_propagate_root(node->tree,  old_data, new_data, new_len);

    if (node->exact && node->exact->root.data == old_data) {
        node->exact->root.data    = new_data;
        node->exact->root.len     = new_len;
        node->exact->root_lengths = NULL;
    }

    if (node->inclusive && node->inclusive->root.data == old_data) {
        node->inclusive->root.data    = new_data;
        node->inclusive->root.len     = new_len;
        node->inclusive->root_lengths = NULL;
    }
}


static JSValue
ngx_js_server_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_core_loc_conf_t  *clcf;
    const char                *cstr;
    size_t                     len;
    u_char                    *old_data, *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf  = op->cscf;

    switch (magic) {
    case 1: /* root — server's default location */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(ngx_cycle->pool, len + 1);
        if (data == NULL) {
            JS_FreeCString(ctx, cstr);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(data, cstr, len + 1);
        JS_FreeCString(ctx, cstr);

        clcf     = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
        old_data = clcf->root.data;

        clcf->root.data    = data;
        clcf->root.len     = len;
        clcf->root_lengths = NULL;

        /*
         * Propagate to child locations that inherited the old root pointer
         * via ngx_conf_merge_str_value (they share the same .data address).
         */
        ngx_js_propagate_root(clcf->static_locations,
                              old_data, data, len);

#if (NGX_PCRE)
        if (clcf->regex_locations) {
            ngx_http_core_loc_conf_t  **rloc;

            for (rloc = clcf->regex_locations; *rloc; rloc++) {
                if ((*rloc)->root.data == old_data) {
                    (*rloc)->root.data    = data;
                    (*rloc)->root.len     = len;
                    (*rloc)->root_lengths = NULL;
                }
            }
        }
#endif

        return JS_UNDEFINED;

    case 2: /* clientHeaderBufferSize — bytes */
        if (JS_ToInt64(ctx, (int64_t *) &len, val) < 0) { return JS_EXCEPTION; }
        cscf->client_header_buffer_size = (size_t) len;
        return JS_UNDEFINED;

    case 3: /* clientHeaderTimeout — ms */
    {
        int64_t  n;
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        cscf->client_header_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    }

    case 4: /* ignoreInvalidHeaders */
        cscf->ignore_invalid_headers = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 5: /* mergeSlashes */
        cscf->merge_slashes = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 6: /* underscoresInHeaders */
        cscf->underscores_in_headers = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 7: /* serverTokens — "off" | "on" | "build" */
    {
        const char  *s;
        size_t       slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

        if (slen == 3 && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0) {
            clcf->server_tokens = NGX_HTTP_SERVER_TOKENS_OFF;
        } else if (slen == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
            clcf->server_tokens = NGX_HTTP_SERVER_TOKENS_ON;
        } else if (slen == 5 && ngx_strncasecmp((u_char *) s, (u_char *) "build", 5) == 0) {
            clcf->server_tokens = NGX_HTTP_SERVER_TOKENS_BUILD;
        } else {
            JS_ThrowTypeError(ctx,
                "serverTokens: expected \"off\", \"on\", or \"build\"");
            JS_FreeCString(ctx, s);
            return JS_EXCEPTION;
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }

    case 8: /* connectionPoolSize — bytes */
        if (JS_ToInt64(ctx, (int64_t *) &len, val) < 0) { return JS_EXCEPTION; }
        cscf->connection_pool_size = (size_t) len;
        return JS_UNDEFINED;

    case 9: /* requestPoolSize — bytes */
        if (JS_ToInt64(ctx, (int64_t *) &len, val) < 0) { return JS_EXCEPTION; }
        cscf->request_pool_size = (size_t) len;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * nginx.http.servers[i].names — all server_name values as a JS Array.
 */
static JSValue
ngx_js_server_get_names(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t  *op;
    JSValue                  arr;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    for (i = 0; i < op->nnames; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             JS_NewStringLen(ctx,
                                             (const char *) op->names[i].data,
                                             op->names[i].len));
    }

    return arr;
}


/*
 * server.setNames(arr) — replace the server_name list at runtime.
 *
 * arr must be an array of strings.  The strings are copied into
 * ngx_cycle->pool and stored in op->names / op->nnames, which is the
 * source the COM names getter and name getter use.
 *
 * Also rebuilds cscf->server_names (the ngx_array_t used by
 * nginx.http.rebuildVhostDispatch in Stage 5b) with the same values,
 * using a fresh array in ngx_cycle->pool.
 *
 * Note: the change is visible via the COM getters immediately but does
 * not affect request routing until Stage 5b's rebuildVhostDispatch()
 * is called.
 */
static JSValue
ngx_js_server_set_names(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_srv_conf_t  *cscf;
    JSValue                    arr;
    uint32_t                   len, i;
    ngx_str_t                 *names;
    ngx_http_server_name_t    *sn;
    const char                *s;
    size_t                     slen;
    u_char                    *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) { return JS_EXCEPTION; }

    cscf = op->cscf;

    if (argc < 1 || !JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "setNames: array argument required");
    }

    arr = argv[0];

    {
        JSValue lv = JS_GetPropertyStr(ctx, arr, "length");
        if (JS_ToUint32(ctx, &len, lv) < 0) {
            JS_FreeValue(ctx, lv);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, lv);
    }

    /* Allocate the new COM-visible names array */
    if (len > 0) {
        names = ngx_palloc(ngx_cycle->pool, len * sizeof(ngx_str_t));
        if (names == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
    } else {
        names = NULL;
    }

    /* Allocate the new cscf->server_names elts array for Stage 5b */
    if (ngx_array_init(&cscf->server_names, ngx_cycle->pool,
                       len ? len : 1,
                       sizeof(ngx_http_server_name_t)) != NGX_OK)
    {
        return JS_ThrowOutOfMemory(ctx);
    }

    for (i = 0; i < len; i++) {
        JSValue elem = JS_GetPropertyUint32(ctx, arr, i);
        if (JS_IsException(elem)) { return JS_EXCEPTION; }

        s = JS_ToCStringLen(ctx, &slen, elem);
        JS_FreeValue(ctx, elem);
        if (!s) { return JS_EXCEPTION; }

        p = ngx_pnalloc(ngx_cycle->pool, slen + 1);
        if (p == NULL) {
            JS_FreeCString(ctx, s);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memcpy(p, s, slen);
        p[slen] = '\0';
        JS_FreeCString(ctx, s);

        names[i].data = p;
        names[i].len  = slen;

        sn = ngx_array_push(&cscf->server_names);
        if (sn == NULL) { return JS_ThrowOutOfMemory(ctx); }

        ngx_memzero(sn, sizeof(ngx_http_server_name_t));
        sn->server    = cscf;
        sn->name.data = p;
        sn->name.len  = slen;
    }

    op->names  = names;
    op->nnames = len;

    return JS_UNDEFINED;
}


/*
 * nginx.http.servers[i].locations — JS Array of NginxLocation objects.
 */
static JSValue
ngx_js_server_get_locations(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_loc_conf_t  *clcf, **named;
    JSValue                    arr;
    uint32_t                   idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf = op->cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
    arr = ngx_js_build_locations(ctx, clcf);
    if (JS_IsException(arr)) {
        return arr;
    }

    /* append named locations (@name) kept in cscf->named_locations[] */
    named = op->cscf->named_locations;
    if (named) {
        JSValue  len_val = JS_GetPropertyStr(ctx, arr, "length");
        if (JS_IsException(len_val)) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }
        JS_ToUint32(ctx, &idx, len_val);
        JS_FreeValue(ctx, len_val);
        for (; *named; named++) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 ngx_js_wrap_location(ctx, *named));
        }
    }

    return arr;
}


/*
 * server.ssl — NginxSSL object if ssl_certificate is configured,
 * JS_NULL otherwise (or if nginx was built without --with-http_ssl_module).
 */
static JSValue
ngx_js_server_get_ssl(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_server_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

#if (NGX_HTTP_SSL)
    {
        ngx_http_ssl_srv_conf_t *sscf;

        sscf = op->cscf->ctx->srv_conf[ngx_http_ssl_module.ctx_index];

        if (sscf == NULL || sscf->certificates == NULL) {
            return JS_NULL;
        }

        return ngx_js_wrap_ssl(ctx, sscf);
    }
#else
    return JS_NULL;
#endif
}


static const JSCFunctionListEntry ngx_js_server_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name",                     ngx_js_server_get,                       NULL,              0),
    JS_CGETSET_MAGIC_DEF("root",                     ngx_js_server_get,                       ngx_js_server_set, 1),
    JS_CGETSET_MAGIC_DEF("names",                    ngx_js_server_get_names,                 NULL,              0),
    JS_CFUNC_DEF        ("setNames",                 1, ngx_js_server_set_names),
    JS_CGETSET_MAGIC_DEF("locations",                ngx_js_server_get_locations,             NULL,              0),
    JS_CGETSET_MAGIC_DEF("clientHeaderBufferSize",   ngx_js_server_get,                       ngx_js_server_set, 2),
    JS_CGETSET_MAGIC_DEF("clientHeaderTimeout",      ngx_js_server_get,                       ngx_js_server_set, 3),
    JS_CGETSET_MAGIC_DEF("ignoreInvalidHeaders",     ngx_js_server_get,                       ngx_js_server_set, 4),
    JS_CGETSET_MAGIC_DEF("mergeSlashes",             ngx_js_server_get,                       ngx_js_server_set, 5),
    JS_CGETSET_MAGIC_DEF("underscoresInHeaders",     ngx_js_server_get,                       ngx_js_server_set, 6),
    JS_CGETSET_MAGIC_DEF("serverTokens",             ngx_js_server_get,                       ngx_js_server_set, 7),
    JS_CGETSET_MAGIC_DEF("connectionPoolSize",       ngx_js_server_get,                       ngx_js_server_set, 8),
    JS_CGETSET_MAGIC_DEF("requestPoolSize",          ngx_js_server_get,                       ngx_js_server_set, 9),
    JS_CGETSET_DEF       ("largeClientHeaderBuffers", ngx_js_server_get_large_client_hdr_bufs, NULL),
    JS_CGETSET_DEF       ("ssl",                      ngx_js_server_get_ssl,                   NULL),
    JS_CFUNC_DEF         ("addLocation",              1, ngx_js_server_fn_add_location),
    JS_CFUNC_DEF         ("removeLocation",           1, ngx_js_server_fn_remove_location),
};


ngx_int_t
ngx_js_server_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_server_proto_funcs,
                               countof(ngx_js_server_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_server_class_id, proto);
    return NGX_OK;
}


static JSValue
ngx_js_wrap_server(JSContext *ctx, ngx_http_core_srv_conf_t *cscf,
    ngx_cycle_t *cycle)
{
    JSValue                    obj;
    ngx_js_server_opaque_t    *op;
    ngx_http_server_name_t    *sn;
    ngx_uint_t                 n;

    op = js_mallocz(ctx, sizeof(ngx_js_server_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->cscf  = cscf;
    op->cycle = cycle;

    /*
     * cscf->server_names.elts lives in cf->temp_pool, which is destroyed
     * after ngx_init_cycle() returns — before workers fork.  Copy the
     * ngx_str_t array into cycle->pool only while we are in the master
     * process (init_conf time), where cf->temp_pool is still alive.
     * Workers set nnames = 0; they only need locations, not server names.
     *
     * The individual name.data pointers are in cycle->pool already, so
     * copying the ngx_str_t structs is sufficient — no deep copy needed.
     */
    if (ngx_process != NGX_PROCESS_WORKER
        && cscf->server_names.nelts > 0)
    {
        op->nnames = cscf->server_names.nelts;

        op->names = ngx_palloc(cycle->pool,
                               op->nnames * sizeof(ngx_str_t));
        if (op->names == NULL) {
            js_free(ctx, op);
            return JS_EXCEPTION;
        }

        sn = cscf->server_names.elts;

        for (n = 0; n < op->nnames; n++) {
            op->names[n] = sn[n].name;
        }
    }

    /*
     * Initialise the dynamic location pool and snapshot all existing
     * prefix/exact locations from the static BST into op->prefix_locs[].
     * This lets ngx_js_rebuild_loc_tree reconstruct the full tree from
     * scratch when addLocation is called later.
     */
    op->dyn_pool = ngx_create_pool(4096, cycle->log);
    if (op->dyn_pool == NULL) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    if (ngx_array_init(&op->prefix_locs, op->dyn_pool, 16,
                       sizeof(ngx_js_loc_entry_t)) != NGX_OK)
    {
        ngx_destroy_pool(op->dyn_pool);
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    if (ngx_array_init(&op->regex_locs, op->dyn_pool, 4,
#if (NGX_PCRE)
                       sizeof(ngx_js_regex_entry_t)
#else
                       sizeof(void *)
#endif
                       ) != NGX_OK)
    {
        ngx_destroy_pool(op->dyn_pool);
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    if (ngx_array_init(&op->named_locs, op->dyn_pool, 4,
                       sizeof(ngx_js_loc_entry_t)) != NGX_OK)
    {
        ngx_destroy_pool(op->dyn_pool);
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    {
        ngx_http_core_loc_conf_t  *root_clcf;
#if (NGX_PCRE)
        ngx_http_core_loc_conf_t **rloc;
        ngx_js_regex_entry_t      *re;
#endif
        ngx_http_core_loc_conf_t **nloc;
        ngx_js_loc_entry_t        *ne;

        root_clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

        /* Snapshot regex locations first (preserve their order) */
#if (NGX_PCRE)
        if (root_clcf->regex_locations) {
            for (rloc = root_clcf->regex_locations; *rloc; rloc++) {
                re = ngx_array_push(&op->regex_locs);
                if (re == NULL) {
                    ngx_destroy_pool(op->dyn_pool);
                    js_free(ctx, op);
                    return JS_EXCEPTION;
                }
                re->clcf     = *rloc;
                re->caseless = 0;   /* can't determine from compiled handle */
                re->dynamic  = 0;
            }
        }
#endif

        /* Snapshot named locations */
        if (cscf->named_locations) {
            for (nloc = cscf->named_locations; *nloc; nloc++) {
                ne = ngx_array_push(&op->named_locs);
                if (ne == NULL) {
                    ngx_destroy_pool(op->dyn_pool);
                    js_free(ctx, op);
                    return JS_EXCEPTION;
                }
                ne->clcf     = *nloc;
                ne->is_exact = 0;
                ne->dynamic  = 0;
            }
        }

        /* Snapshot prefix/exact locations from the static BST */
        ngx_js_snapshot_bst(op, root_clcf->static_locations);
    }

    obj = JS_NewObjectClass(ctx, ngx_js_server_class_id);
    if (JS_IsException(obj)) {
        ngx_destroy_pool(op->dyn_pool);
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    /*
     * Build the listen[] array for this server and set it as an eager
     * property.  We identify HTTP listening sockets by comparing
     * ls->handler to ngx_http_init_connection (declared in ngx_http.h).
     * For each HTTP socket whose default_server == cscf, we add:
     *   { addr, port, ssl, http2, default }
     * The "default" flag is always true here (non-default servers sharing
     * the same address are not yet discoverable via this API).
     */
    {
        JSValue               listen_arr, entry;
        ngx_listening_t      *ls;
        ngx_uint_t            li, ai, idx;
        ngx_http_port_t      *hport;
        ngx_http_addr_conf_t *aconf;
        u_char                addr_buf[NGX_SOCKADDR_STRLEN + 1];
        ngx_str_t             sa_text;
        in_port_t             port;

        listen_arr = JS_NewArray(ctx);
        idx        = 0;
        ls         = cycle->listening.elts;

        for (li = 0; li < cycle->listening.nelts; li++) {

            /* Only HTTP sockets */
            if (ls[li].handler != ngx_http_init_connection) {
                continue;
            }

            if (ls[li].servers == NULL) {
                continue;
            }

            /*
             * At init_conf time ls->servers is ngx_http_port_t *, which
             * holds an addrs array of ngx_http_in_addr_t (IPv4) or
             * ngx_http_in6_addr_t (IPv6).  Each entry embeds an
             * ngx_http_addr_conf_t with default_server, ssl, http2, etc.
             * populated by ngx_http_add_addrs / ngx_http_add_addrs6.
             */
            hport = (ngx_http_port_t *) ls[li].servers;

            if (hport->addrs == NULL || hport->naddrs == 0) {
                continue;
            }

            /* Find the addr_conf where this server is the default */
            aconf = NULL;
            for (ai = 0; ai < hport->naddrs; ai++) {
                ngx_http_addr_conf_t *ac;
#if (NGX_HAVE_INET6)
                if (ls[li].sockaddr->sa_family == AF_INET6) {
                    ac = &((ngx_http_in6_addr_t *) hport->addrs)[ai].conf;
                } else {
#endif
                    ac = &((ngx_http_in_addr_t *) hport->addrs)[ai].conf;
#if (NGX_HAVE_INET6)
                }
#endif
                if (ac->default_server == cscf) {
                    aconf = ac;
                    break;
                }
            }

            if (aconf == NULL) {
                continue;
            }

            port = ngx_inet_get_port(ls[li].sockaddr);

            sa_text.data = addr_buf;
            sa_text.len  = ngx_sock_ntop(ls[li].sockaddr, ls[li].socklen,
                                          addr_buf, NGX_SOCKADDR_STRLEN, 0);

            entry = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, entry, "addr",
                              JS_NewStringLen(ctx,
                                              (const char *) sa_text.data,
                                              sa_text.len));
            JS_SetPropertyStr(ctx, entry, "port",
                              JS_NewInt32(ctx, (int32_t) port));
            JS_SetPropertyStr(ctx, entry, "ssl",
                              JS_NewBool(ctx, aconf->ssl));
            JS_SetPropertyStr(ctx, entry, "http2",
                              JS_NewBool(ctx, aconf->http2));
            JS_SetPropertyStr(ctx, entry, "default",
                              JS_NewBool(ctx, 1));

            JS_SetPropertyUint32(ctx, listen_arr, (uint32_t) idx++, entry);
        }

        JS_SetPropertyStr(ctx, obj, "listen", listen_arr);
    }

    return obj;
}


/* ------------------------------------------------------------------ */
/*
 * Wildcard key comparator for ngx_qsort, mirrors the static
 * ngx_http_cmp_dns_wildcards() in ngx_http.c.
 */
static int ngx_libc_cdecl
ngx_js_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first  = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


/*
 * nginx.http.rebuildVhostDispatch() — rebuild the server-name hash
 * tables for every listening address that has multiple virtual hosts.
 *
 * For each entry in ngx_js_vhost_entries (built at init_conf from
 * cmcf->ports before cf->temp_pool was destroyed), this function:
 *   1. Creates a fresh ngx_hash_keys_arrays_t from the current
 *      cscf->server_names arrays.
 *   2. Builds hash/wc_head/wc_tail into ngx_cycle->pool.
 *   3. Allocates a new ngx_http_virtual_names_t in ngx_cycle->pool
 *      and assigns the rebuilt tables into it.
 *   4. Atomically replaces addr_conf->virtual_names with the new vn.
 *
 * The replacement is safe inside a single-threaded nginx worker because
 * all request processing is serialised on the event loop.  The update
 * takes effect on the next request handled by this worker.
 *
 * Throws an Error on any internal failure (pool alloc or hash init).
 */
static JSValue
ngx_js_http_rebuild_vhost_dispatch(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_uint_t                  i, s, n;
    ngx_js_addr_entry_t        *entry;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_server_name_t     *sn;
    ngx_http_virtual_names_t   *vn;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_pool_t                 *temp_pool;
    ngx_int_t                   rc;

    for (i = 0; i < ngx_js_vhost_nentries; i++) {
        entry = &ngx_js_vhost_entries[i];

        temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, ngx_cycle->log);
        if (temp_pool == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
        ha.temp_pool = temp_pool;
        ha.pool      = ngx_cycle->pool;

        if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
            ngx_destroy_pool(temp_pool);
            return JS_ThrowOutOfMemory(ctx);
        }

        /* Add all server names from all servers sharing this address */
        for (s = 0; s < entry->nservers; s++) {
            cscf = entry->servers[s];
            sn   = cscf->server_names.elts;

            for (n = 0; n < cscf->server_names.nelts; n++) {
#if (NGX_PCRE)
                if (sn[n].regex) {
                    continue;
                }
#endif
                rc = ngx_hash_add_key(&ha, &sn[n].name, sn[n].server,
                                      NGX_HASH_WILDCARD_KEY);

                if (rc == NGX_ERROR) {
                    ngx_destroy_pool(temp_pool);
                    return JS_ThrowInternalError(ctx,
                        "rebuildVhostDispatch: ngx_hash_add_key failed");
                }

                /* NGX_BUSY means a duplicate — warn but continue */
                if (rc == NGX_BUSY) {
                    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                                  "JS rebuildVhostDispatch: duplicate "
                                  "server name \"%V\", ignored",
                                  &sn[n].name);
                }
            }
        }

        /* Allocate the new virtual_names in the cycle pool */
        vn = ngx_pcalloc(ngx_cycle->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            ngx_destroy_pool(temp_pool);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_memzero(&hash, sizeof(ngx_hash_init_t));
        hash.key         = ngx_hash_key_lc;
        hash.max_size    = ngx_js_vhost_hash_max_size;
        hash.bucket_size = ngx_js_vhost_hash_bucket_size;
        hash.name        = "server_names_hash";
        hash.pool        = ngx_cycle->pool;

        if (ha.keys.nelts) {
            hash.hash      = &vn->names.hash;
            hash.temp_pool = NULL;

            if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
                ngx_destroy_pool(temp_pool);
                return JS_ThrowInternalError(ctx,
                    "rebuildVhostDispatch: ngx_hash_init failed");
            }
        }

        if (ha.dns_wc_head.nelts) {
            ngx_qsort(ha.dns_wc_head.elts, ha.dns_wc_head.nelts,
                      sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

            hash.hash      = NULL;
            hash.temp_pool = ha.temp_pool;

            if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts,
                                       ha.dns_wc_head.nelts) != NGX_OK)
            {
                ngx_destroy_pool(temp_pool);
                return JS_ThrowInternalError(ctx,
                    "rebuildVhostDispatch: ngx_hash_wildcard_init (head) failed");
            }

            vn->names.wc_head = (ngx_hash_wildcard_t *) hash.hash;
        }

        if (ha.dns_wc_tail.nelts) {
            ngx_qsort(ha.dns_wc_tail.elts, ha.dns_wc_tail.nelts,
                      sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

            hash.hash      = NULL;
            hash.temp_pool = ha.temp_pool;

            if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                       ha.dns_wc_tail.nelts) != NGX_OK)
            {
                ngx_destroy_pool(temp_pool);
                return JS_ThrowInternalError(ctx,
                    "rebuildVhostDispatch: ngx_hash_wildcard_init (tail) failed");
            }

            vn->names.wc_tail = (ngx_hash_wildcard_t *) hash.hash;
        }

#if (NGX_PCRE)
        /* Collect regex server names */
        {
            ngx_uint_t  nregex = 0;

            for (s = 0; s < entry->nservers; s++) {
                cscf = entry->servers[s];
                sn   = cscf->server_names.elts;
                for (n = 0; n < cscf->server_names.nelts; n++) {
                    if (sn[n].regex) {
                        nregex++;
                    }
                }
            }

            if (nregex) {
                vn->nregex = nregex;
                vn->regex  = ngx_palloc(ngx_cycle->pool,
                                        nregex * sizeof(ngx_http_server_name_t));
                if (vn->regex == NULL) {
                    ngx_destroy_pool(temp_pool);
                    return JS_ThrowOutOfMemory(ctx);
                }

                nregex = 0;
                for (s = 0; s < entry->nservers; s++) {
                    cscf = entry->servers[s];
                    sn   = cscf->server_names.elts;
                    for (n = 0; n < cscf->server_names.nelts; n++) {
                        if (sn[n].regex) {
                            vn->regex[nregex++] = sn[n];
                        }
                    }
                }
            }
        }
#endif

        ngx_destroy_pool(temp_pool);

        /* Atomic pointer replacement — takes effect on next request */
        entry->addr_conf->virtual_names = vn;
    }

    return JS_UNDEFINED;
}


/*
 * nginx.http.addServer(name [, opts])
 *
 * Creates a new virtual server and registers it in every
 * ngx_js_vhost_entries[] slot so that rebuildVhostDispatch() will
 * include it in the server-name hash.
 *
 * opts.template — name of an existing server to copy configuration
 *   defaults from; defaults to the first server in nginx.http.servers[].
 *
 * Returns the new NginxServer JS wrapper.  Call
 * nginx.http.rebuildVhostDispatch() afterwards to activate routing.
 */
static JSValue
ngx_js_http_add_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char                 *name_str, *tmpl_name;
    size_t                      name_len, tmpl_name_len;
    ngx_http_conf_ctx_t        *http_ctx;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp, *tmpl_cscf, *new_cscf;
    ngx_http_core_loc_conf_t   *tmpl_root, *new_root;
    ngx_http_conf_ctx_t        *new_ctx;
    void                      **new_loc_conf, **new_srv_conf;
    ngx_http_server_name_t     *sn;
    ngx_js_addr_entry_t        *entry;
    ngx_http_core_srv_conf_t  **new_servers;
    ngx_js_server_opaque_t     *op;
    ngx_cycle_t                *cycle;
    JSValue                     srv_obj, servers_arr, s0, lv;
    uint32_t                    servers_len;
    ngx_uint_t                  i, ni;
    ngx_int_t                   tmpl_idx;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "addServer: server name (string) required");
    }

    name_str = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (name_str == NULL) {
        return JS_EXCEPTION;
    }

    /*
     * Get the cycle pointer from the first server's opaque.
     * We cannot use ngx_cycle here because during init_conf it still
     * points to the old cycle (ngx_cycle is updated in main() only after
     * ngx_init_cycle() returns).  The cycle passed to ngx_js_http_com_install
     * is stored in every server opaque's op->cycle.
     */
    servers_arr = JS_GetPropertyStr(ctx, this_val, "servers");
    lv          = JS_GetPropertyStr(ctx, servers_arr, "length");
    JS_ToUint32(ctx, &servers_len, lv);
    JS_FreeValue(ctx, lv);

    if (servers_len == 0) {
        JS_FreeValue(ctx, servers_arr);
        JS_FreeCString(ctx, name_str);
        return JS_ThrowInternalError(ctx, "addServer: no existing servers");
    }

    s0    = JS_GetPropertyUint32(ctx, servers_arr, 0);
    op    = JS_GetOpaque(s0, ngx_js_server_class_id);
    cycle = op ? op->cycle : NULL;
    JS_FreeValue(ctx, s0);

    if (cycle == NULL) {
        JS_FreeValue(ctx, servers_arr);
        JS_FreeCString(ctx, name_str);
        return JS_ThrowInternalError(ctx, "addServer: cycle unavailable");
    }

    http_ctx = (ngx_http_conf_ctx_t *)
                   cycle->conf_ctx[ngx_http_module.index];
    if (http_ctx == NULL) {
        JS_FreeValue(ctx, servers_arr);
        JS_FreeCString(ctx, name_str);
        return JS_ThrowInternalError(ctx, "addServer: no http{} block");
    }

    cmcf  = http_ctx->main_conf[ngx_http_core_module.ctx_index];
    cscfp = cmcf->servers.elts;

    /* find template server: opts.template string → search servers[]; else 0 */
    tmpl_idx = 0;

    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue tmpl_val = JS_GetPropertyStr(ctx, argv[1], "template");

        if (!JS_IsUndefined(tmpl_val) && !JS_IsNull(tmpl_val)) {
            tmpl_name = JS_ToCStringLen(ctx, &tmpl_name_len, tmpl_val);
            if (tmpl_name) {
                for (i = 0; i < servers_len; i++) {
                    JSValue s = JS_GetPropertyUint32(ctx, servers_arr, i);
                    op = JS_GetOpaque(s, ngx_js_server_class_id);
                    if (op) {
                        for (ni = 0; ni < op->nnames; ni++) {
                            if (op->names[ni].len == tmpl_name_len
                                && ngx_strncasecmp(op->names[ni].data,
                                       (u_char *) tmpl_name,
                                       tmpl_name_len) == 0)
                            {
                                tmpl_idx = (ngx_int_t) i;
                                break;
                            }
                        }
                    }
                    JS_FreeValue(ctx, s);
                    if ((ngx_uint_t) tmpl_idx == i && tmpl_idx > 0) {
                        break;
                    }
                }
                JS_FreeCString(ctx, tmpl_name);
            }
        }

        JS_FreeValue(ctx, tmpl_val);
    }

    JS_FreeValue(ctx, servers_arr);

    tmpl_cscf = cscfp[tmpl_idx];
    tmpl_root = tmpl_cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    /* allocate a new cscf as a shallow copy of the template */
    new_cscf = ngx_palloc(cycle->pool, sizeof(ngx_http_core_srv_conf_t));
    if (new_cscf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    *new_cscf = *tmpl_cscf;
    new_cscf->named_locations = NULL;  /* fresh server has no named locs */

    /* new loc_conf[] array: copy all module slots, then override core */
    new_loc_conf = ngx_palloc(cycle->pool,
                              sizeof(void *) * ngx_http_max_module);
    if (new_loc_conf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(new_loc_conf, tmpl_cscf->ctx->loc_conf,
               sizeof(void *) * ngx_http_max_module);

    /* new root clcf: copy template, clear dynamic-location-tree fields */
    new_root = ngx_palloc(cycle->pool, sizeof(ngx_http_core_loc_conf_t));
    if (new_root == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    *new_root = *tmpl_root;
    new_root->static_locations = NULL;
    new_root->regex_locations  = NULL;
    new_root->loc_conf         = new_loc_conf;
    /* self-reference required by ngx_http_core_find_location */
    new_loc_conf[ngx_http_core_module.ctx_index] = new_root;

    /* new srv_conf[] array: copy all module slots, then override core */
    new_srv_conf = ngx_palloc(cycle->pool,
                              sizeof(void *) * ngx_http_max_module);
    if (new_srv_conf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(new_srv_conf, tmpl_cscf->ctx->srv_conf,
               sizeof(void *) * ngx_http_max_module);
    new_srv_conf[ngx_http_core_module.ctx_index] = new_cscf;

    /* wire up the new conf context */
    new_ctx = ngx_palloc(cycle->pool, sizeof(ngx_http_conf_ctx_t));
    if (new_ctx == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    new_ctx->main_conf = tmpl_cscf->ctx->main_conf;
    new_ctx->srv_conf  = new_srv_conf;
    new_ctx->loc_conf  = new_loc_conf;
    new_cscf->ctx = new_ctx;

    /* server_names: fresh array with just the one new name in cycle->pool */
    if (ngx_array_init(&new_cscf->server_names, cycle->pool, 1,
                       sizeof(ngx_http_server_name_t)) != NGX_OK)
    {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }

    sn = ngx_array_push(&new_cscf->server_names);
    if (sn == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memzero(sn, sizeof(ngx_http_server_name_t));
    sn->name.len  = name_len;
    sn->name.data = ngx_pnalloc(cycle->pool, name_len);
    if (sn->name.data == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(sn->name.data, name_str, name_len);
    sn->server = new_cscf;

    JS_FreeCString(ctx, name_str);

    /* add new_cscf to every vhost dispatch entry */
    for (i = 0; i < ngx_js_vhost_nentries; i++) {
        entry = &ngx_js_vhost_entries[i];
        new_servers = ngx_palloc(cycle->pool,
                         (entry->nservers + 1)
                         * sizeof(ngx_http_core_srv_conf_t *));
        if (new_servers == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
        ngx_memcpy(new_servers, entry->servers,
                   entry->nservers * sizeof(ngx_http_core_srv_conf_t *));
        new_servers[entry->nservers] = new_cscf;
        entry->servers  = new_servers;
        entry->nservers++;
    }

    /* wrap as JS NginxServer object */
    srv_obj = ngx_js_wrap_server(ctx, new_cscf, cycle);
    if (JS_IsException(srv_obj)) {
        return srv_obj;
    }

    /* For dynamic servers added in master context op->nnames is already set
     * by ngx_js_wrap_server.  In worker context (nnames==0) we copy the name
     * into op->names so removeServer() can match by name. */
    op = JS_GetOpaque(srv_obj, ngx_js_server_class_id);
    if (op != NULL && op->nnames == 0 && sn->name.len > 0) {
        op->names = ngx_palloc(cycle->pool, sizeof(ngx_str_t));
        if (op->names != NULL) {
            op->names[0] = sn->name;
            op->nnames   = 1;
        }
    }

    /* append to nginx.http.servers[] */
    servers_arr = JS_GetPropertyStr(ctx, this_val, "servers");
    lv = JS_GetPropertyStr(ctx, servers_arr, "length");
    JS_ToUint32(ctx, &servers_len, lv);
    JS_FreeValue(ctx, lv);
    JS_SetPropertyUint32(ctx, servers_arr, servers_len,
                         JS_DupValue(ctx, srv_obj));
    JS_FreeValue(ctx, servers_arr);

    return srv_obj;
}


/*
 * nginx.http.removeServer(name)
 *
 * Removes the first server whose primary server_name equals name
 * (case-insensitive) from nginx.http.servers[] and from every
 * ngx_js_vhost_entries[] slot.
 *
 * Returns true if found and removed, false if not found.
 * Call nginx.http.rebuildVhostDispatch() afterwards to update routing.
 */
static JSValue
ngx_js_http_remove_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char                 *name_str;
    size_t                      name_len;
    JSValue                     servers_arr, lv;
    uint32_t                    servers_len, found_idx;
    ngx_js_server_opaque_t     *found_op;
    ngx_http_core_srv_conf_t   *found_cscf;
    ngx_js_addr_entry_t        *entry;
    ngx_http_core_srv_conf_t  **new_servers;
    ngx_cycle_t                *cycle;
    ngx_uint_t                  i, j, k;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "removeServer: server name (string) required");
    }

    name_str = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (name_str == NULL) {
        return JS_EXCEPTION;
    }

    /* find the server in nginx.http.servers[] by name */
    servers_arr = JS_GetPropertyStr(ctx, this_val, "servers");
    lv = JS_GetPropertyStr(ctx, servers_arr, "length");
    JS_ToUint32(ctx, &servers_len, lv);
    JS_FreeValue(ctx, lv);

    found_op   = NULL;
    found_cscf = NULL;
    found_idx  = (uint32_t) -1;

    for (i = 0; i < servers_len; i++) {
        JSValue                 s  = JS_GetPropertyUint32(ctx, servers_arr, i);
        ngx_js_server_opaque_t *op = JS_GetOpaque(s, ngx_js_server_class_id);
        ngx_uint_t              ni;

        if (op) {
            for (ni = 0; ni < op->nnames; ni++) {
                if (op->names[ni].len == name_len
                    && ngx_strncasecmp(op->names[ni].data,
                                       (u_char *) name_str, name_len) == 0)
                {
                    found_op   = op;
                    found_cscf = op->cscf;
                    found_idx  = (uint32_t) i;
                    break;
                }
            }
        }

        JS_FreeValue(ctx, s);

        if (found_op != NULL) {
            break;
        }
    }

    JS_FreeCString(ctx, name_str);

    if (found_op == NULL) {
        JS_FreeValue(ctx, servers_arr);
        return JS_FALSE;
    }

    cycle = found_op->cycle;

    /* splice found_cscf from every vhost dispatch entry */
    for (i = 0; i < ngx_js_vhost_nentries; i++) {
        entry = &ngx_js_vhost_entries[i];

        for (j = 0; j < entry->nservers; j++) {
            if (entry->servers[j] != found_cscf) {
                continue;
            }

            if (entry->nservers == 1) {
                entry->servers  = NULL;
                entry->nservers = 0;
                break;
            }

            new_servers = ngx_palloc(cycle->pool,
                             (entry->nservers - 1)
                             * sizeof(ngx_http_core_srv_conf_t *));
            if (new_servers == NULL) {
                JS_FreeValue(ctx, servers_arr);
                return JS_ThrowOutOfMemory(ctx);
            }

            for (k = 0; k < j; k++) {
                new_servers[k] = entry->servers[k];
            }
            for (k = j + 1; k < entry->nservers; k++) {
                new_servers[k - 1] = entry->servers[k];
            }

            entry->servers  = new_servers;
            entry->nservers--;
            break;
        }
    }

    /* splice the JS object from nginx.http.servers[] */
    for (i = found_idx; i + 1 < servers_len; i++) {
        JSValue next = JS_GetPropertyUint32(ctx, servers_arr, i + 1);
        JS_SetPropertyUint32(ctx, servers_arr, i, next);
    }
    JS_SetPropertyStr(ctx, servers_arr, "length",
                      JS_NewUint32(ctx, servers_len - 1));

    JS_FreeValue(ctx, servers_arr);

    return JS_TRUE;
}


/* ngx_js_http_com_install                                              */
/* ------------------------------------------------------------------ */

/*
 * Register the NginxServer and NginxLocation classes with this runtime.
 * Called by ngx_js_com_register_classes() from ngx_js_com.c.
 */
ngx_int_t
ngx_js_http_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_server_class_id,   &ngx_js_server_class)   < 0
     || JS_NewClass(rt, ngx_js_location_class_id, &ngx_js_location_class) < 0)
    {
        return NGX_ERROR;
    }

    if (ngx_js_request_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_proxy_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_ssl_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_gzip_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_headers_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_proxy_cache_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_rewrite_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_access_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_auth_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_limit_req_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_limit_conn_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_fastcgi_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_log_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

#if (NGX_HTTP_REALIP)
    if (ngx_js_realip_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    if (ngx_js_charset_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_sub_filter_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_autoindex_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_referer_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

#if (NGX_HTTP_DAV)
    if (ngx_js_dav_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    if (ngx_js_ssi_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_userid_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_addition_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_gunzip_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_slice_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_image_filter_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_xslt_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_secure_link_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_mp4_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_random_index_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_auth_request_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_gzip_static_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_memcached_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_scgi_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_uwsgi_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_js_mirror_register_class(rt) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Build nginx.http and attach it to nginx_obj.
 *
 *   nginx.http.servers[]    — Array of NginxServer
 *   nginx.http.upstreams[]  — delegated to ngx_js_upstream_com_install()
 */
ngx_int_t
ngx_js_http_com_install(JSContext *ctx, JSValue nginx_obj,
    ngx_cycle_t *cycle)
{
    JSValue                      http_obj, servers_arr;
    ngx_http_conf_ctx_t         *http_ctx;
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_uint_t                   i;

    http_ctx = (ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index];
    if (http_ctx == NULL) {
        /* http{} block was not present in nginx.conf */
        http_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);
        return NGX_OK;
    }

    cmcf = http_ctx->main_conf[ngx_http_core_module.ctx_index];

    /* ---- Build nginx.http.servers[] ---- */

    servers_arr = JS_NewArray(ctx);
    if (JS_IsException(servers_arr)) {
        return NGX_ERROR;
    }

    cscfp = cmcf->servers.elts;

    for (i = 0; i < cmcf->servers.nelts; i++) {
        JS_SetPropertyUint32(ctx, servers_arr, (uint32_t) i,
                             ngx_js_wrap_server(ctx, cscfp[i], cycle));
    }

    /* ---- Assemble nginx.http ---- */

    http_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, http_obj, "servers", servers_arr);

    /*
     * Scalar globals from ngx_http_core_main_conf_t.
     * These directives are only settable at the http{} level and have no
     * per-server or per-location counterpart, so they belong on nginx.http
     * rather than on individual server/location objects.
     */
    JS_SetPropertyStr(ctx, http_obj, "serverNamesHashMaxSize",
                      JS_NewUint32(ctx,
                          (uint32_t) cmcf->server_names_hash_max_size));
    JS_SetPropertyStr(ctx, http_obj, "serverNamesHashBucketSize",
                      JS_NewUint32(ctx,
                          (uint32_t) cmcf->server_names_hash_bucket_size));
    JS_SetPropertyStr(ctx, http_obj, "variablesHashMaxSize",
                      JS_NewUint32(ctx,
                          (uint32_t) cmcf->variables_hash_max_size));
    JS_SetPropertyStr(ctx, http_obj, "variablesHashBucketSize",
                      JS_NewUint32(ctx,
                          (uint32_t) cmcf->variables_hash_bucket_size));

    /* ---- Build nginx.http.variables{} ---- */
    /*
     * Maps every variable in cmcf->variables_hash (all registered variables,
     * including built-in ones like $uri / $args) to a plain object:
     *
     *   {index: number, writable: bool}
     *
     *   index    — position in r->variables[] at request time;
     *              (ngx_uint_t)-1 for NOHASH / prefix variables
     *   writable — true when NGX_HTTP_VAR_CHANGEABLE is set
     *
     * The hash is built by ngx_http_variables_init_vars() inside
     * ngx_http_block(), which runs before our init_conf hook, so the
     * hash is complete at this point.
     *
     * Iteration: walk each bucket's singly-linked list of ngx_hash_elt_t
     * entries.  Each entry ends with a sentinel (value == NULL).
     */
    {
        JSValue              vars_obj, meta;
        ngx_uint_t           bi;
        ngx_hash_elt_t      *elt;
        ngx_http_variable_t *v;
        u_char               name_buf[256];
        size_t               nlen;

        vars_obj = JS_NewObject(ctx);

        for (bi = 0; bi < cmcf->variables_hash.size; bi++) {

            elt = cmcf->variables_hash.buckets[bi];
            if (elt == NULL) {
                continue;
            }

            while (elt->value != NULL) {

                v = elt->value;

                nlen = v->name.len < sizeof(name_buf) - 1
                       ? v->name.len : sizeof(name_buf) - 1;
                ngx_memcpy(name_buf, v->name.data, nlen);
                name_buf[nlen] = '\0';

                meta = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, meta, "index",
                    JS_NewInt32(ctx, (int32_t) v->index));
                JS_SetPropertyStr(ctx, meta, "writable",
                    JS_NewBool(ctx,
                        (v->flags & NGX_HTTP_VAR_CHANGEABLE) != 0));

                JS_SetPropertyStr(ctx, vars_obj,
                                  (const char *) name_buf, meta);

                /* Advance to next element (variable-length, void*-aligned) */
                elt = (ngx_hash_elt_t *)
                    ngx_align_ptr(&elt->name[0] + elt->len, sizeof(void *));
            }
        }

        JS_SetPropertyStr(ctx, http_obj, "variables", vars_obj);
    }

    /* nginx.http.rebuildVhostDispatch() */
    JS_SetPropertyStr(ctx, http_obj, "rebuildVhostDispatch",
                      JS_NewCFunction(ctx,
                                      ngx_js_http_rebuild_vhost_dispatch,
                                      "rebuildVhostDispatch", 0));

    /* nginx.http.addServer() / removeServer() */
    JS_SetPropertyStr(ctx, http_obj, "addServer",
                      JS_NewCFunction(ctx,
                                      ngx_js_http_add_server,
                                      "addServer", 1));
    JS_SetPropertyStr(ctx, http_obj, "removeServer",
                      JS_NewCFunction(ctx,
                                      ngx_js_http_remove_server,
                                      "removeServer", 1));

    /* nginx.http.upstreams[] — delegated to upstream COM */
    if (ngx_js_upstream_com_install(ctx, http_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, http_obj);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);

    /*
     * Build the persistent vhost map while cmcf->ports and cf->temp_pool
     * are still valid (they are destroyed after ngx_init_cycle returns).
     */
    {
        ngx_uint_t              p, a, s, li, ai, total;
        ngx_http_conf_port_t   *ports;
        ngx_http_conf_addr_t   *addr;
        ngx_http_port_t        *hport;
        ngx_http_addr_conf_t   *ac;
        ngx_js_addr_entry_t    *entry;
        ngx_listening_t        *ls;

        ngx_js_vhost_nentries = 0;
        ngx_js_vhost_entries  = NULL;

        if (cmcf->ports != NULL) {
            /* Count addressess that need virtual host dispatch */
            total = 0;
            ports = cmcf->ports->elts;
            for (p = 0; p < cmcf->ports->nelts; p++) {
                addr = ports[p].addrs.elts;
                for (a = 0; a < ports[p].addrs.nelts; a++) {
                    if (addr[a].servers.nelts > 1) {
                        total++;
                    }
                }
            }

            if (total > 0) {
                ngx_js_vhost_entries = ngx_palloc(cycle->pool,
                                         total * sizeof(ngx_js_addr_entry_t));
                if (ngx_js_vhost_entries == NULL) {
                    JS_FreeValue(ctx, http_obj);
                    return NGX_ERROR;
                }
            }

            ngx_js_vhost_hash_max_size    = cmcf->server_names_hash_max_size;
            ngx_js_vhost_hash_bucket_size = cmcf->server_names_hash_bucket_size;

            ls = cycle->listening.elts;

            for (p = 0; p < cmcf->ports->nelts; p++) {
                addr = ports[p].addrs.elts;

                for (a = 0; a < ports[p].addrs.nelts; a++) {
                    if (addr[a].servers.nelts < 2) {
                        continue;
                    }

                    /*
                     * Find the runtime ngx_http_in_addr_t whose
                     * default_server matches this conf_addr.
                     * ngx_http_add_addrs() preserves index correspondence:
                     * hport->addrs[j].conf.default_server == addr[j].default_server
                     */
                    ac = NULL;

                    for (li = 0; li < cycle->listening.nelts; li++) {
                        if (ls[li].handler != ngx_http_init_connection) {
                            continue;
                        }
                        if (ls[li].servers == NULL) {
                            continue;
                        }

                        hport = (ngx_http_port_t *) ls[li].servers;
                        if (hport->addrs == NULL
                            || a >= hport->naddrs)
                        {
                            continue;
                        }

                        for (ai = 0; ai < hport->naddrs; ai++) {
                            ngx_http_addr_conf_t *tac;

#if (NGX_HAVE_INET6)
                            if (ls[li].sockaddr->sa_family == AF_INET6) {
                                tac = &((ngx_http_in6_addr_t *)
                                         hport->addrs)[ai].conf;
                            } else {
#endif
                                tac = &((ngx_http_in_addr_t *)
                                         hport->addrs)[ai].conf;
#if (NGX_HAVE_INET6)
                            }
#endif

                            if (tac->default_server
                                == addr[a].default_server)
                            {
                                ac = tac;
                                break;
                            }
                        }

                        if (ac != NULL) {
                            break;
                        }
                    }

                    if (ac == NULL) {
                        continue;
                    }

                    entry = &ngx_js_vhost_entries[ngx_js_vhost_nentries++];
                    entry->addr_conf = ac;
                    entry->nservers  = addr[a].servers.nelts;
                    entry->servers   = ngx_palloc(cycle->pool,
                                         entry->nservers
                                         * sizeof(ngx_http_core_srv_conf_t *));
                    if (entry->servers == NULL) {
                        JS_FreeValue(ctx, http_obj);
                        return NGX_ERROR;
                    }

                    {
                        ngx_http_core_srv_conf_t **sp = addr[a].servers.elts;
                        for (s = 0; s < entry->nservers; s++) {
                            entry->servers[s] = sp[s];
                        }
                    }
                }
            }
        }
    }

    return NGX_OK;
}
