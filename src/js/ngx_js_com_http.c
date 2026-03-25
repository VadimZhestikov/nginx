
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
#include <math.h>
#include <ngx_http.h>
#include <ngx_http_proxy_module.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "ngx_js_listener.h"

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

/*
 * Saved at ngx_js_http_com_install time so that addServer() can obtain
 * the correct cycle pointer even when nginx.http.servers[] is empty.
 * (ngx_cycle is still pointing at the old cycle during init_conf.)
 */
static ngx_cycle_t                *ngx_js_http_cycle;

/*
 * Synthetic template server conf built when nginx.conf has an http{} block
 * but no server{} blocks inside.  Used as the copy-source by addServer()
 * when nginx.http.servers[] is empty.
 */
static ngx_http_core_srv_conf_t   *ngx_js_default_cscf;


/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_server_opaque_t is defined later in the file (server section).
 * Forward-declare only the tag so ngx_js_location_opaque_t can hold
 * a back-pointer to it.
 */
typedef struct ngx_js_server_opaque_s ngx_js_server_opaque_t;

JSValue ngx_js_wrap_location(JSContext *ctx,
    ngx_http_core_loc_conf_t *clcf);
static JSValue ngx_js_wrap_location_ex(JSContext *ctx,
    ngx_http_core_loc_conf_t *clcf, ngx_js_server_opaque_t *srv_op);

static void ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx,
    ngx_js_server_opaque_t *srv_op);

static JSValue ngx_js_build_locations(JSContext *ctx,
    ngx_http_core_loc_conf_t *root_clcf, ngx_js_server_opaque_t *srv_op);

static JSValue ngx_js_location_fn_add_location(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_js_location_fn_remove_location(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue ngx_js_location_fn_clone(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue ngx_js_location_fn_clear_handler(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue ngx_js_http_fn_match(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue ngx_js_server_fn_find_location(JSContext *ctx,
    JSValueConst this_val, int argc, JSValueConst *argv);

JSValue ngx_js_wrap_server(JSContext *ctx,
    ngx_http_core_srv_conf_t *cscf, ngx_cycle_t *cycle);

/* ------------------------------------------------------------------ */
/* NginxLocation wrapper                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_core_loc_conf_t  *clcf;
    uint32_t                   write_mode;  /* NGX_JS_WRITE_GLOBAL/LOCAL/BOTH */
    uint32_t                   read_mode;   /* NGX_JS_WRITE_GLOBAL or LOCAL   */
    /*
     * Back-pointer to the owning server's opaque.  Set by wrap_location_ex
     * when the location is created in a server context (init_conf or
     * addLocation).  NULL for locations wrapped from r.location (read-only).
     * Enables loc.addLocation() / loc.removeLocation() on nested paths.
     */
    ngx_js_server_opaque_t    *srv_op;
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
 *   82 — hasHandler        (r/o: bool, true when a JS content handler is set)
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

    case 82: /* hasHandler — true when a JS content handler is installed */
    {
        ngx_js_loc_conf_t  *jlcf;

        jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];
        return JS_NewBool(ctx, jlcf && jlcf->handler_idx >= 0);
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

        /* Save original clcf->handler on the very first JS assignment so
         * that clearHandler() can restore it later.                        */
        if (jlcf->handler_idx < 0) {
            jlcf->original_handler = (ngx_js_http_handler_pt) clcf->handler;
        }

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


/* ------------------------------------------------------------------ */
/* Filter list management                                               */
/* ------------------------------------------------------------------ */

/*
 * Deep-copy src into a new array allocated in pool.
 * Entries are plain structs (fn_idx is just a uint32) — no refcount needed.
 */
static ngx_array_t *
ngx_js_copy_filter_list(ngx_pool_t *pool, ngx_array_t *src)
{
    ngx_array_t           *dst;
    ngx_js_filter_entry_t *se, *de;
    ngx_uint_t             i;

    dst = ngx_array_create(pool, src->nelts ? src->nelts : 4,
                           sizeof(ngx_js_filter_entry_t));
    if (dst == NULL) {
        return NULL;
    }

    se = src->elts;
    for (i = 0; i < src->nelts; i++) {
        de = ngx_array_push(dst);
        if (de == NULL) {
            return NULL;
        }
        *de = se[i];
    }

    return dst;
}


/*
 * Return the global __ngx_filters__ array, creating it if absent.
 * Caller must JS_FreeValue the returned value.
 */
static JSValue
ngx_js_get_filter_registry(JSContext *ctx)
{
    JSValue  global, registry;

    global   = JS_GetGlobalObject(ctx);
    registry = JS_GetPropertyStr(ctx, global, "__ngx_filters__");

    if (!JS_IsArray(ctx, registry)) {
        JS_FreeValue(ctx, registry);
        registry = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_filters__",
                          JS_DupValue(ctx, registry));
    }

    JS_FreeValue(ctx, global);
    return registry;
}


/* Append fn to __ngx_filters__, return its index. */
static uint32_t
ngx_js_filter_register_fn(JSContext *ctx, JSValue fn)
{
    JSValue   registry, len_val;
    uint32_t  idx;

    registry = ngx_js_get_filter_registry(ctx);
    len_val  = JS_GetPropertyStr(ctx, registry, "length");
    JS_ToUint32(ctx, &idx, len_val);
    JS_FreeValue(ctx, len_val);
    JS_SetPropertyUint32(ctx, registry, idx, JS_DupValue(ctx, fn));
    JS_FreeValue(ctx, registry);
    return idx;
}


/* Retrieve fn at fn_idx from __ngx_filters__. Caller must JS_FreeValue. */
static JSValue
ngx_js_filter_get_fn(JSContext *ctx, uint32_t fn_idx)
{
    JSValue  registry, fn;

    registry = ngx_js_get_filter_registry(ctx);
    fn       = JS_GetPropertyUint32(ctx, registry, fn_idx);
    JS_FreeValue(ctx, registry);
    return fn;
}


/* Set __ngx_filters__[fn_idx] = null (releases the GC root for that slot). */
static void
ngx_js_filter_unregister_fn(JSContext *ctx, uint32_t fn_idx)
{
    JSValue  registry;

    registry = ngx_js_get_filter_registry(ctx);
    JS_SetPropertyUint32(ctx, registry, fn_idx, JS_NULL);
    JS_FreeValue(ctx, registry);
}


/*
 * Ensure *listp is owned by this conf (copy-on-first-write).
 * Creates a new empty array if *listp is NULL.
 * Sets *own = 1 on success.
 * pool must be the location's own pool (jlcf->pool) to avoid use-after-free.
 */
static ngx_int_t
ngx_js_filter_ensure_own(ngx_array_t **listp, ngx_uint_t *own,
    ngx_pool_t *pool)
{
    ngx_array_t  *arr;

    if (*own) {
        /* already ours — create if still NULL */
        if (*listp == NULL) {
            arr = ngx_array_create(pool, 4, sizeof(ngx_js_filter_entry_t));
            if (arr == NULL) {
                return NGX_ERROR;
            }
            *listp = arr;
        }
        return NGX_OK;
    }

    /* COW: copy parent's entries into a new array */
    if (*listp == NULL) {
        arr = ngx_array_create(pool, 4, sizeof(ngx_js_filter_entry_t));
    } else {
        arr = ngx_js_copy_filter_list(pool, *listp);
    }

    if (arr == NULL) {
        return NGX_ERROR;
    }

    *listp = arr;
    *own   = 1;
    return NGX_OK;
}


/*
 * Find an entry in list matching ref (string name or function reference).
 * Returns 0-based index or -1 if not found.
 */
static ngx_int_t
ngx_js_filter_find(JSContext *ctx, ngx_array_t *list, JSValueConst ref)
{
    ngx_js_filter_entry_t  *elts;
    ngx_uint_t              i;
    const char             *name;
    size_t                  nlen;
    JSValue                 fn;
    int                     eq;

    if (list == NULL || list->nelts == 0) {
        return -1;
    }

    elts = list->elts;

    if (JS_IsString(ref)) {
        name = JS_ToCStringLen(ctx, &nlen, ref);
        if (!name) {
            return -1;
        }
        for (i = 0; i < list->nelts; i++) {
            if (elts[i].name.len == nlen
                && ngx_strncmp(elts[i].name.data, (u_char *) name, nlen) == 0)
            {
                JS_FreeCString(ctx, name);
                return (ngx_int_t) i;
            }
        }
        JS_FreeCString(ctx, name);
        return -1;
    }

    if (JS_IsFunction(ctx, ref)) {
        for (i = 0; i < list->nelts; i++) {
            fn = ngx_js_filter_get_fn(ctx, elts[i].fn_idx);
            eq = JS_StrictEq(ctx, fn, ref);
            JS_FreeValue(ctx, fn);
            if (eq) {
                return (ngx_int_t) i;
            }
        }
        return -1;
    }

    return -1;
}


/*
 * Insert entry at pos, shifting later entries right.
 * pos must be in [0, arr->nelts] (inclusive — append when pos == nelts).
 */
static ngx_int_t
ngx_js_filter_insert_at(ngx_array_t *arr, ngx_uint_t pos,
    ngx_js_filter_entry_t *entry)
{
    ngx_js_filter_entry_t  *elts, *slot;

    slot = ngx_array_push(arr);
    if (slot == NULL) {
        return NGX_ERROR;
    }

    elts = arr->elts;

    if (pos < arr->nelts - 1) {
        ngx_memmove(elts + pos + 1, elts + pos,
                    (arr->nelts - 1 - pos) * sizeof(ngx_js_filter_entry_t));
    }

    elts[pos] = *entry;
    return NGX_OK;
}


/* Remove entry at pos, shifting later entries left; unregisters fn. */
static void
ngx_js_filter_remove_at(JSContext *ctx, ngx_array_t *arr, ngx_uint_t pos)
{
    ngx_js_filter_entry_t  *elts;

    elts = arr->elts;
    ngx_js_filter_unregister_fn(ctx, elts[pos].fn_idx);

    if (pos < arr->nelts - 1) {
        ngx_memmove(elts + pos, elts + pos + 1,
                    (arr->nelts - 1 - pos) * sizeof(ngx_js_filter_entry_t));
    }

    arr->nelts--;
}


/*
 * Build a plain JS object { name, priority, fn } for one entry.
 * Returns JS_EXCEPTION on failure.
 */
static JSValue
ngx_js_filter_entry_to_obj(JSContext *ctx, ngx_js_filter_entry_t *e)
{
    JSValue  obj, fn, name_val;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return obj;
    }

    fn = ngx_js_filter_get_fn(ctx, e->fn_idx);

    if (e->name.len > 0) {
        name_val = JS_NewStringLen(ctx, (const char *) e->name.data, e->name.len);
    } else {
        name_val = JS_NULL;
    }

    JS_SetPropertyStr(ctx, obj, "name",     name_val);
    JS_SetPropertyStr(ctx, obj, "priority", JS_NewInt32(ctx, e->priority));
    JS_SetPropertyStr(ctx, obj, "fn",       fn);

    return obj;
}


/*
 * Options parsed from the optional second argument of addHeaderFilter /
 * addBodyFilter.
 */
typedef struct {
    ngx_str_t  name;        /* empty if not provided */
    ngx_int_t  priority;    /* NGX_JS_FILTER_PRIORITY_DEFAULT if not provided */
    JSValue    before_ref;  /* JS_UNDEFINED if not provided */
    JSValue    after_ref;   /* JS_UNDEFINED if not provided */
    ngx_int_t  insert_idx;  /* -1 if not provided */
} ngx_js_add_filter_opts_t;


static ngx_int_t
ngx_js_parse_add_filter_opts(JSContext *ctx, JSValueConst opts_val,
    ngx_js_add_filter_opts_t *opts, ngx_pool_t *pool)
{
    JSValue     v;
    const char *s;
    size_t      slen;
    int32_t     i32;

    opts->name.len   = 0;
    opts->name.data  = NULL;
    opts->priority   = NGX_JS_FILTER_PRIORITY_DEFAULT;
    opts->before_ref = JS_UNDEFINED;
    opts->after_ref  = JS_UNDEFINED;
    opts->insert_idx = -1;

    if (JS_IsUndefined(opts_val) || JS_IsNull(opts_val)) {
        return NGX_OK;
    }

    if (!JS_IsObject(opts_val)) {
        JS_ThrowTypeError(ctx, "addFilter: opts must be an object");
        return NGX_ERROR;
    }

    /* name */
    v = JS_GetPropertyStr(ctx, opts_val, "name");
    if (JS_IsException(v)) {
        return NGX_ERROR;
    }
    if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
        s = JS_ToCStringLen(ctx, &slen, v);
        if (!s) {
            JS_FreeValue(ctx, v);
            return NGX_ERROR;
        }
        opts->name.data = ngx_pnalloc(pool, slen);
        if (opts->name.data == NULL) {
            JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, v);
            return NGX_ERROR;
        }
        ngx_memcpy(opts->name.data, s, slen);
        opts->name.len = slen;
        JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, v);

    /* priority */
    v = JS_GetPropertyStr(ctx, opts_val, "priority");
    if (JS_IsException(v)) {
        return NGX_ERROR;
    }
    if (!JS_IsUndefined(v)) {
        if (JS_ToInt32(ctx, &i32, v)) {
            JS_FreeValue(ctx, v);
            return NGX_ERROR;
        }
        opts->priority = (ngx_int_t) i32;
    }
    JS_FreeValue(ctx, v);

    /* index */
    v = JS_GetPropertyStr(ctx, opts_val, "index");
    if (JS_IsException(v)) {
        return NGX_ERROR;
    }
    if (!JS_IsUndefined(v)) {
        if (JS_ToInt32(ctx, &i32, v)) {
            JS_FreeValue(ctx, v);
            return NGX_ERROR;
        }
        opts->insert_idx = (ngx_int_t) i32;
    }
    JS_FreeValue(ctx, v);

    /* before */
    v = JS_GetPropertyStr(ctx, opts_val, "before");
    if (JS_IsException(v)) {
        return NGX_ERROR;
    }
    if (!JS_IsUndefined(v)) {
        opts->before_ref = v;  /* caller frees */
    } else {
        JS_FreeValue(ctx, v);
    }

    /* after */
    v = JS_GetPropertyStr(ctx, opts_val, "after");
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, opts->before_ref);
        opts->before_ref = JS_UNDEFINED;
        return NGX_ERROR;
    }
    if (!JS_IsUndefined(v)) {
        opts->after_ref = v;  /* caller frees */
    } else {
        JS_FreeValue(ctx, v);
    }

    return NGX_OK;
}


/*
 * Parse the mode string that is the first argument of addBodyFilter.
 * Returns NGX_OK and sets *mode_out on success; throws TypeError and
 * returns NGX_ERROR on bad input.
 */
static ngx_int_t
ngx_js_parse_body_filter_mode(JSContext *ctx, JSValueConst mode_val,
    ngx_uint_t *mode_out)
{
    const char  *s;

    if (!JS_IsString(mode_val)) {
        JS_ThrowTypeError(ctx,
                          "addBodyFilter: first argument must be a mode string "
                          "('wholeBodySync', 'wholeBodyAsync', "
                          "'streamingSync', 'streamingAsync')");
        return NGX_ERROR;
    }

    s = JS_ToCString(ctx, mode_val);
    if (!s) {
        return NGX_ERROR;
    }

    if (strcmp(s, "wholeBodySync") == 0) {
        *mode_out = NGX_JS_FILTER_WB_SYNC;

    } else if (strcmp(s, "wholeBodyAsync") == 0) {
        *mode_out = NGX_JS_FILTER_WB_ASYNC;

    } else if (strcmp(s, "streamingSync") == 0) {
        *mode_out = NGX_JS_FILTER_STREAM_SYNC;

    } else if (strcmp(s, "streamingAsync") == 0) {
        *mode_out = NGX_JS_FILTER_STREAM_ASYNC;

    } else {
        JS_ThrowTypeError(ctx,
                          "addBodyFilter: unknown mode '%s'; expected "
                          "'wholeBodySync', 'wholeBodyAsync', "
                          "'streamingSync', or 'streamingAsync'", s);
        JS_FreeCString(ctx, s);
        return NGX_ERROR;
    }

    JS_FreeCString(ctx, s);
    return NGX_OK;
}


/*
 * Core add logic shared by addHeaderFilter and addBodyFilter.
 * is_body: 0 = header list, 1 = body list.
 * mode: NGX_JS_FILTER_* — used only when is_body == 1.
 */
/*
 * If the filter array *listp is currently being iterated by the dispatch loop
 * (w->dispatching_*_arr matches), COW it before any mutation so the loop's
 * pre-captured elts pointer stays valid.  Called from add_impl and remove_impl.
 * After this call, *listp and *own are updated; ensure_own is then a no-op.
 */
static ngx_int_t
ngx_js_filter_predispatch_cow(JSContext *ctx, ngx_array_t **listp,
    ngx_uint_t *own, ngx_pool_t *pool, int is_body)
{
    ngx_js_worker_t  *w;
    ngx_array_t      *dispatching, *arr;

    w = JS_GetContextOpaque(ctx);
    if (w == NULL) {
        return NGX_OK;
    }

    dispatching = is_body ? w->dispatching_body_arr : w->dispatching_hdr_arr;

    if (dispatching == NULL || dispatching != *listp) {
        return NGX_OK;
    }

    /* The array is being iterated — give jlcf a fresh copy */
    arr = (*listp != NULL)
        ? ngx_js_copy_filter_list(pool, *listp)
        : ngx_array_create(pool, 4, sizeof(ngx_js_filter_entry_t));

    if (arr == NULL) {
        return NGX_ERROR;
    }

    *listp = arr;
    *own   = 1;
    return NGX_OK;
}


static JSValue
ngx_js_filter_add_impl(JSContext *ctx, ngx_http_core_loc_conf_t *clcf,
    int is_body, ngx_uint_t mode, JSValue fn, ngx_js_add_filter_opts_t *opts)
{
    ngx_js_loc_conf_t     *jlcf;
    ngx_array_t          **listp;
    ngx_uint_t            *own;
    ngx_js_filter_entry_t  entry;
    ngx_uint_t             pos, i;

    jlcf  = clcf->loc_conf[ngx_js_http_module.ctx_index];
    listp = is_body ? &jlcf->body_filters   : &jlcf->header_filters;
    own   = is_body ? &jlcf->own_body_filters : &jlcf->own_header_filters;

    if (ngx_js_filter_predispatch_cow(ctx, listp, own, jlcf->pool, is_body)
        != NGX_OK)
    {
        return JS_ThrowOutOfMemory(ctx);
    }

    if (ngx_js_filter_ensure_own(listp, own, jlcf->pool) != NGX_OK) {
        return JS_ThrowOutOfMemory(ctx);
    }

    entry.fn_idx   = ngx_js_filter_register_fn(ctx, fn);
    entry.name     = opts->name;
    entry.priority = opts->priority;
    entry.mode     = is_body ? mode : NGX_JS_FILTER_WB_SYNC;

    /* Determine insertion position (precedence: index > before/after > priority) */
    if (opts->insert_idx >= 0) {
        pos = (ngx_uint_t) opts->insert_idx;
        if (pos > (*listp)->nelts) {
            pos = (*listp)->nelts;
        }

    } else if (!JS_IsUndefined(opts->before_ref)) {
        ngx_int_t found = ngx_js_filter_find(ctx, *listp, opts->before_ref);
        pos = (found >= 0) ? (ngx_uint_t) found : (*listp)->nelts;

    } else if (!JS_IsUndefined(opts->after_ref)) {
        ngx_int_t found = ngx_js_filter_find(ctx, *listp, opts->after_ref);
        pos = (found >= 0) ? (ngx_uint_t) found + 1 : (*listp)->nelts;

    } else {
        /* priority-based insertion: find first entry with higher priority */
        pos = (*listp)->nelts;  /* default: append */
        ngx_js_filter_entry_t *elts = (*listp)->elts;
        for (i = 0; i < (*listp)->nelts; i++) {
            if (elts[i].priority > opts->priority) {
                pos = i;
                break;
            }
        }
    }

    if (ngx_js_filter_insert_at(*listp, pos, &entry) != NGX_OK) {
        ngx_js_filter_unregister_fn(ctx, entry.fn_idx);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Update the whole-body flag so the body filter can select Mode A vs B. */
    if (is_body
        && (mode == NGX_JS_FILTER_WB_SYNC || mode == NGX_JS_FILTER_WB_ASYNC))
    {
        jlcf->body_filter_has_wb = 1;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_location_fn_add_header_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    ngx_js_add_filter_opts_t   opts;
    JSValue                    ret;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "addHeaderFilter: first argument must be a function");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    {
        ngx_js_loc_conf_t *jlcf = op->clcf->loc_conf[ngx_js_http_module.ctx_index];
        if (ngx_js_parse_add_filter_opts(ctx,
                                         argc > 1 ? argv[1] : JS_UNDEFINED,
                                         &opts, jlcf->pool) != NGX_OK)
        {
            return JS_EXCEPTION;
        }
    }

    ret = ngx_js_filter_add_impl(ctx, op->clcf, 0, NGX_JS_FILTER_WB_SYNC,
                                 argv[0], &opts);
    JS_FreeValue(ctx, opts.before_ref);
    JS_FreeValue(ctx, opts.after_ref);
    return ret;
}


/*
 * addBodyFilter(mode, fn[, opts])
 *
 * mode (required string):
 *   'wholeBodySync'   — fn(req, body) → string
 *   'wholeBodyAsync'  — async fn(req, body) → Promise<string>
 *   'streamingSync'   — fn(req, chunk, flags) → void; uses req.sendBuffer()
 *   'streamingAsync'  — async fn(req, chunk, flags) → void; uses req.sendBuffer()
 *
 * opts (optional object): same {name, priority, before, after, index} as before.
 */
static JSValue
ngx_js_location_fn_add_body_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;
    ngx_js_add_filter_opts_t   opts;
    ngx_uint_t                 mode;
    JSValue                    ret;

    /* argv[0] = mode string, argv[1] = fn, argv[2] = opts */
    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "addBodyFilter(mode, fn[, opts]): "
                                 "two arguments required");
    }

    if (ngx_js_parse_body_filter_mode(ctx, argv[0], &mode) != NGX_OK) {
        return JS_EXCEPTION;
    }

    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
                                 "addBodyFilter: second argument must be a function");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    {
        ngx_js_loc_conf_t *jlcf = op->clcf->loc_conf[ngx_js_http_module.ctx_index];
        if (ngx_js_parse_add_filter_opts(ctx,
                                         argc > 2 ? argv[2] : JS_UNDEFINED,
                                         &opts, jlcf->pool) != NGX_OK)
        {
            return JS_EXCEPTION;
        }
    }

    ret = ngx_js_filter_add_impl(ctx, op->clcf, 1, mode, argv[1], &opts);
    JS_FreeValue(ctx, opts.before_ref);
    JS_FreeValue(ctx, opts.after_ref);
    return ret;
}


/*
 * Core remove logic shared by removeHeaderFilter and removeBodyFilter.
 * ref is a string name or function reference.
 */
static JSValue
ngx_js_filter_remove_impl(JSContext *ctx, ngx_http_core_loc_conf_t *clcf,
    int is_body, JSValueConst ref)
{
    ngx_js_loc_conf_t  *jlcf;
    ngx_array_t       **listp;
    ngx_uint_t         *own;
    ngx_int_t           idx;

    jlcf  = clcf->loc_conf[ngx_js_http_module.ctx_index];
    listp = is_body ? &jlcf->body_filters   : &jlcf->header_filters;
    own   = is_body ? &jlcf->own_body_filters : &jlcf->own_header_filters;

    idx = ngx_js_filter_find(ctx, *listp, ref);
    if (idx < 0) {
        return JS_UNDEFINED;  /* no-op */
    }

    if (ngx_js_filter_predispatch_cow(ctx, listp, own, jlcf->pool, is_body)
        != NGX_OK)
    {
        return JS_ThrowOutOfMemory(ctx);
    }

    if (ngx_js_filter_ensure_own(listp, own, jlcf->pool) != NGX_OK) {
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Re-find after potential COW copy */
    idx = ngx_js_filter_find(ctx, *listp, ref);
    if (idx >= 0) {
        ngx_js_filter_remove_at(ctx, *listp, (ngx_uint_t) idx);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_location_fn_remove_header_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "removeHeaderFilter: argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_filter_remove_impl(ctx, op->clcf, 0, argv[0]);
}


static JSValue
ngx_js_location_fn_remove_body_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "removeBodyFilter: argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_filter_remove_impl(ctx, op->clcf, 1, argv[0]);
}


/*
 * getHeaderFilter(ref) / getBodyFilter(ref) — return entry object or null.
 */
static JSValue
ngx_js_filter_get_impl(JSContext *ctx, ngx_http_core_loc_conf_t *clcf,
    int is_body, JSValueConst ref)
{
    ngx_js_loc_conf_t     *jlcf;
    ngx_array_t           *list;
    ngx_int_t              idx;

    jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];
    list = is_body ? jlcf->body_filters : jlcf->header_filters;

    idx = ngx_js_filter_find(ctx, list, ref);
    if (idx < 0) {
        return JS_NULL;
    }

    return ngx_js_filter_entry_to_obj(ctx,
                                      &((ngx_js_filter_entry_t *) list->elts)[idx]);
}


static JSValue
ngx_js_location_fn_get_header_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "getHeaderFilter: argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_filter_get_impl(ctx, op->clcf, 0, argv[0]);
}


static JSValue
ngx_js_location_fn_get_body_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *op;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "getBodyFilter: argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_filter_get_impl(ctx, op->clcf, 1, argv[0]);
}


/*
 * headerFilters / bodyFilters getter — returns a snapshot JS array of
 * { name, priority, fn } objects.  magic: 0 = header, 1 = body.
 */
static JSValue
ngx_js_location_get_filter_list(JSContext *ctx, JSValueConst this_val,
    int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_js_loc_conf_t         *jlcf;
    ngx_array_t               *list;
    ngx_js_filter_entry_t     *elts;
    JSValue                    arr, obj;
    ngx_uint_t                 i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    jlcf = op->clcf->loc_conf[ngx_js_http_module.ctx_index];
    list = magic ? jlcf->body_filters : jlcf->header_filters;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    if (list == NULL) {
        return arr;
    }

    elts = list->elts;
    for (i = 0; i < list->nelts; i++) {
        obj = ngx_js_filter_entry_to_obj(ctx, &elts[i]);
        if (JS_IsException(obj)) {
            JS_FreeValue(ctx, arr);
            return obj;
        }
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }

    return arr;
}


/* ------------------------------------------------------------------ */
/* Filter dispatch (called from ngx_js_http_module.c filter hooks)      */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_header_filters_run(JSContext *ctx, JSRuntime *rt,
    ngx_http_request_t *r, ngx_js_loc_conf_t *jlcf)
{
    ngx_js_filter_entry_t  *elts;
    ngx_js_worker_t        *w;
    JSValue                 req_obj, fn, result;
    JSContext              *job_ctx;
    ngx_uint_t              i, nelts;

    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        ngx_js_log_exception(ctx, r->connection->log);
        return NGX_ERROR;
    }

    /*
     * Pre-capture nelts and elts, then mark the array as "dispatching".
     * If a filter callback calls addHeaderFilter / removeHeaderFilter, the
     * mutator detects the flag and COWs jlcf->header_filters to a new array
     * before touching it, leaving the array pointed to by elts unchanged.
     * Removed filters have their fn_idx unregistered; JS_IsFunction returns
     * false for those entries so they are skipped.
     */
    w                      = JS_GetContextOpaque(ctx);
    w->dispatching_hdr_arr = jlcf->header_filters;
    nelts                  = jlcf->header_filters->nelts;
    elts                   = jlcf->header_filters->elts;

    for (i = 0; i < nelts; i++) {
        fn = ngx_js_filter_get_fn(ctx, elts[i].fn_idx);

        if (!JS_IsFunction(ctx, fn)) {
            /* fn_idx was unregistered (filter removed during dispatch) */
            JS_FreeValue(ctx, fn);
            continue;
        }

        result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &req_obj);
        JS_FreeValue(ctx, fn);

        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }

        if (JS_IsException(result)) {
            ngx_js_log_exception(ctx, r->connection->log);
        }

        JS_FreeValue(ctx, result);
    }

    w->dispatching_hdr_arr = NULL;

    JS_FreeValue(ctx, req_obj);
    return NGX_OK;
}


/*
 * Run all JS body filters for jlcf's location (whole-body mode).
 *
 * Each filter is called as  fn(r, bodyString) → new_body_string | undefined.
 * If a filter returns a string, it replaces the current body for subsequent
 * filters.  Non-string returns (undefined, null, ...) are ignored.
 * Exceptions are logged and the filter is skipped; the body is unchanged.
 *
 * out_body is always written (copy of body if no filter modifies it).
 */
/* Forward declaration — defined after ngx_js_body_filters_run below. */
static void  ngx_js_flatten_stream_out(ngx_http_request_t *r,
    ngx_js_req_ctx_t *rctx, u_char **data_out, size_t *len_out);

/* Materialise a JSValue string into r->pool and write to *dst. */
static ngx_int_t
ngx_js_body_val_to_str(JSContext *ctx, ngx_http_request_t *r,
    JSValue body_val, ngx_str_t *dst)
{
    const char  *str;
    size_t       slen;
    u_char      *p;

    str = JS_ToCStringLen(ctx, &slen, body_val);
    if (str == NULL) {
        ngx_js_log_exception(ctx, r->connection->log);
        return NGX_ERROR;
    }

    if (slen > 0) {
        p = ngx_pnalloc(r->pool, slen);
        if (p == NULL) {
            JS_FreeCString(ctx, str);
            return NGX_ERROR;
        }
        ngx_memcpy(p, str, slen);
        dst->data = p;
        dst->len  = slen;
    } else {
        dst->data = (u_char *) "";
        dst->len  = 0;
    }

    JS_FreeCString(ctx, str);
    return NGX_OK;
}


ngx_int_t
ngx_js_body_filters_run(JSContext *ctx, JSRuntime *rt,
    ngx_http_request_t *r, ngx_js_loc_conf_t *jlcf,
    ngx_str_t *body, ngx_str_t *out_body, ngx_uint_t start_idx)
{
    ngx_js_filter_entry_t  *elts;
    ngx_js_worker_t        *w;
    ngx_js_req_ctx_t       *rctx;
    ngx_js_bf_pending_t    *bf_p;
    JSValue                 req_obj, fn, body_val, result, flags_obj, args[3];
    JSContext              *job_ctx;
    ngx_uint_t              i, nelts, nargs;
    int                     state;
    u_char                 *flat_data;
    size_t                  flat_len;

    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        ngx_js_log_exception(ctx, r->connection->log);
        *out_body = *body;
        return NGX_ERROR;
    }

    body_val  = JS_NewStringLen(ctx, (const char *) body->data, body->len);
    flags_obj = JS_UNDEFINED;  /* lazily created for streaming filters */

    w                       = JS_GetContextOpaque(ctx);
    w->dispatching_body_arr = jlcf->body_filters;
    nelts                   = jlcf->body_filters->nelts;
    elts                    = jlcf->body_filters->elts;

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);

    for (i = start_idx; i < nelts; i++) {

        fn = ngx_js_filter_get_fn(ctx, elts[i].fn_idx);
        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            continue;
        }

        if (rctx != NULL) {
            rctx->active_filter_mode = elts[i].mode;
        }

        /*
         * Streaming filters in the WB loop receive the whole accumulated
         * body as a single chunk with flags.last = true.  STREAM_SYNC
         * emits via req.sendBuffer(); STREAM_ASYNC returns a Promise
         * whose resolved string becomes the next body.
         */
        if (elts[i].mode == NGX_JS_FILTER_STREAM_SYNC
            || elts[i].mode == NGX_JS_FILTER_STREAM_ASYNC)
        {
            if (JS_IsUndefined(flags_obj)) {
                flags_obj = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, flags_obj, "last", JS_TRUE);
            }

            if (rctx != NULL) {
                rctx->stream_out      = NULL;
                rctx->stream_out_last = &rctx->stream_out;
            }

            args[0] = req_obj;
            args[1] = body_val;
            args[2] = flags_obj;
            nargs   = 3;

        } else {
            args[0] = req_obj;
            args[1] = body_val;
            nargs   = 2;
        }

        result = JS_Call(ctx, fn, JS_UNDEFINED, nargs, args);
        JS_FreeValue(ctx, fn);

        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }

        if (rctx != NULL) {
            rctx->active_filter_mode = NGX_JS_FILTER_WB_SYNC;
        }

        if (JS_IsException(result)) {
            ngx_js_log_exception(ctx, r->connection->log);
            JS_FreeValue(ctx, result);
            if (elts[i].mode == NGX_JS_FILTER_STREAM_SYNC) {
                /* exception in streaming filter: treat as drop */
                JS_FreeValue(ctx, body_val);
                body_val = JS_NewStringLen(ctx, "", 0);
                if (rctx != NULL) {
                    rctx->stream_out = NULL;
                }
            }
            continue;
        }

        /* STREAM_SYNC: flatten stream_out into new body_val */
        if (elts[i].mode == NGX_JS_FILTER_STREAM_SYNC) {
            JS_FreeValue(ctx, result);  /* return value is ignored */
            JS_FreeValue(ctx, body_val);

            if (rctx != NULL && rctx->stream_out != NULL) {
                ngx_js_flatten_stream_out(r, rctx, &flat_data, &flat_len);
                body_val = JS_NewStringLen(ctx, (const char *) flat_data,
                                           flat_len);
            } else {
                /* no sendBuffer call: drop (emit empty body downstream) */
                body_val = JS_NewStringLen(ctx, "", 0);
            }
            continue;
        }

        /*
         * WB_ASYNC and STREAM_ASYNC: handle Promise state.
         * Both use the same bf_pending mechanism; STREAM_ASYNC resolves
         * like WB_ASYNC — the Promise resolved string becomes the new body.
         */
        if (elts[i].mode == NGX_JS_FILTER_WB_ASYNC
            || elts[i].mode == NGX_JS_FILTER_STREAM_ASYNC)
        {
            state = (int) JS_PromiseState(ctx, result);

            if (state == -1) {
                /* not a Promise — treat as pass-through or string replace */
                if (JS_IsString(result)) {
                    JS_FreeValue(ctx, body_val);
                    body_val = result;
                } else {
                    JS_FreeValue(ctx, result);
                }
                continue;
            }

            switch ((JSPromiseStateEnum) state) {

            case JS_PROMISE_FULFILLED: {
                JSValue res = JS_PromiseResult(ctx, result);
                if (JS_IsString(res)) {
                    JS_FreeValue(ctx, body_val);
                    body_val = res;
                } else {
                    JS_FreeValue(ctx, res);
                    /* non-string: pass-through (keep body_val) */
                }
                JS_FreeValue(ctx, result);
                continue;
            }

            case JS_PROMISE_REJECTED: {
                JSValue reason = JS_PromiseResult(ctx, result);
                JSValue str_v  = JS_ToString(ctx, reason);
                const char *cs = JS_ToCString(ctx, str_v);
                if (cs) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "js: async body filter rejected: %s", cs);
                    JS_FreeCString(ctx, cs);
                }
                JS_FreeValue(ctx, str_v);
                JS_FreeValue(ctx, reason);
                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, body_val);
                if (!JS_IsUndefined(flags_obj)) {
                    JS_FreeValue(ctx, flags_obj);
                }
                JS_FreeValue(ctx, req_obj);
                w->dispatching_body_arr = NULL;
                *out_body = *body;
                return NGX_ERROR;
            }

            case JS_PROMISE_PENDING:
                /*
                 * Materialise the current body (before this async filter ran)
                 * into out_body so the caller can stash it in rctx->wb_body.
                 * When the promise resolves, bf_async_check updates wb_body
                 * with the resolved value and resumes from i+1.
                 */
                if (ngx_js_body_val_to_str(ctx, r, body_val, out_body)
                    != NGX_OK)
                {
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, body_val);
                    if (!JS_IsUndefined(flags_obj)) {
                        JS_FreeValue(ctx, flags_obj);
                    }
                    JS_FreeValue(ctx, req_obj);
                    w->dispatching_body_arr = NULL;
                    return NGX_ERROR;
                }

                bf_p = ngx_pcalloc(r->pool, sizeof(ngx_js_bf_pending_t));
                if (bf_p == NULL) {
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, body_val);
                    if (!JS_IsUndefined(flags_obj)) {
                        JS_FreeValue(ctx, flags_obj);
                    }
                    JS_FreeValue(ctx, req_obj);
                    w->dispatching_body_arr = NULL;
                    return NGX_ERROR;
                }

                bf_p->promise    = JS_DupValue(ctx, result);
                bf_p->resume_idx = i + 1;
                bf_p->w          = w;
                bf_p->r          = r;
                bf_p->next       = w->bf_pending;
                w->bf_pending    = bf_p;

                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, body_val);
                if (!JS_IsUndefined(flags_obj)) {
                    JS_FreeValue(ctx, flags_obj);
                }
                JS_FreeValue(ctx, req_obj);
                w->dispatching_body_arr = NULL;
                return NGX_AGAIN;
            }
        }

        /* WB_SYNC: string → replace body, else pass-through */
        if (JS_IsString(result)) {
            JS_FreeValue(ctx, body_val);
            body_val = result;
        } else {
            JS_FreeValue(ctx, result);
        }
    }

    if (!JS_IsUndefined(flags_obj)) {
        JS_FreeValue(ctx, flags_obj);
    }
    w->dispatching_body_arr = NULL;

    /* Materialise final body_val into r->pool */
    if (ngx_js_body_val_to_str(ctx, r, body_val, out_body) != NGX_OK) {
        JS_FreeValue(ctx, body_val);
        JS_FreeValue(ctx, req_obj);
        *out_body = *body;
        return NGX_ERROR;
    }

    JS_FreeValue(ctx, body_val);
    JS_FreeValue(ctx, req_obj);
    return NGX_OK;
}


/*
 * Helper: flatten rctx->stream_out chain into a contiguous pool buffer.
 * Returns a pointer to the buffer in *data_out and the length in *len_out.
 * If stream_out is NULL or empty, sets *len_out = 0 and *data_out = "".
 */
static void
ngx_js_flatten_stream_out(ngx_http_request_t *r, ngx_js_req_ctx_t *rctx,
    u_char **data_out, size_t *len_out)
{
    ngx_chain_t  *cl;
    ngx_buf_t    *b;
    size_t        total;
    u_char       *p;

    total = 0;
    for (cl = rctx->stream_out; cl; cl = cl->next) {
        b = cl->buf;
        if (ngx_buf_in_memory(b)) {
            total += (size_t)(b->last - b->pos);
        }
    }

    if (total == 0) {
        *data_out = (u_char *) "";
        *len_out  = 0;
        return;
    }

    p = ngx_pnalloc(r->pool, total);
    if (p == NULL) {
        *data_out = (u_char *) "";
        *len_out  = 0;
        return;
    }

    *data_out = p;
    *len_out  = total;

    for (cl = rctx->stream_out; cl; cl = cl->next) {
        b = cl->buf;
        if (ngx_buf_in_memory(b)) {
            p = ngx_copy(p, b->pos, (size_t)(b->last - b->pos));
        }
    }
}


/*
 * Streaming filter chain: each filter receives the PREVIOUS filter's output
 * as its input (composing semantics).  STREAM_SYNC filters emit via
 * req.sendBuffer(); STREAM_ASYNC filters return a Promise whose resolved
 * string becomes the next cur_data.  start_idx allows resuming after an
 * async suspension.  Returns NGX_OK, NGX_AGAIN (w->sf_pending set), or
 * NGX_ERROR.
 */
ngx_int_t
ngx_js_streaming_filters_run(JSContext *ctx, JSRuntime *rt,
    ngx_http_request_t *r, ngx_js_loc_conf_t *jlcf,
    u_char *chunk_data, size_t chunk_len, ngx_uint_t is_last,
    ngx_uint_t start_idx)
{
    ngx_js_filter_entry_t  *elts;
    ngx_js_worker_t        *w;
    ngx_js_req_ctx_t       *rctx;
    ngx_js_sf_pending_t    *sf_p;
    JSValue                 req_obj, fn, chunk_val, flags_obj, result, args[3];
    JSContext              *job_ctx;
    ngx_uint_t              i, nelts;
    u_char                 *cur_data;
    size_t                  cur_len;
    int                     state;

    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        ngx_js_log_exception(ctx, r->connection->log);
        return NGX_ERROR;
    }

    flags_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, flags_obj, "last",
                      is_last ? JS_TRUE : JS_FALSE);

    w                       = JS_GetContextOpaque(ctx);
    w->dispatching_body_arr = jlcf->body_filters;
    nelts                   = jlcf->body_filters->nelts;
    elts                    = jlcf->body_filters->elts;

    rctx     = ngx_http_get_module_ctx(r, ngx_js_http_module);
    cur_data = chunk_data;
    cur_len  = chunk_len;

    for (i = start_idx; i < nelts; i++) {
        if (elts[i].mode != NGX_JS_FILTER_STREAM_SYNC
            && elts[i].mode != NGX_JS_FILTER_STREAM_ASYNC)
        {
            continue;
        }

        fn = ngx_js_filter_get_fn(ctx, elts[i].fn_idx);
        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            continue;
        }

        /*
         * Reset stream_out before each filter so each filter's sendBuffer
         * calls accumulate fresh output.  After the call we flatten
         * stream_out into cur_data for the next filter's input.
         * After the last filter stream_out is left intact for the caller.
         */
        if (rctx != NULL) {
            rctx->stream_out      = NULL;
            rctx->stream_out_last = &rctx->stream_out;
            rctx->active_filter_mode = elts[i].mode;
        }

        chunk_val = JS_NewStringLen(ctx, (const char *) cur_data, cur_len);

        args[0] = req_obj;
        args[1] = chunk_val;
        args[2] = flags_obj;

        result = JS_Call(ctx, fn, JS_UNDEFINED, 3, args);
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, chunk_val);

        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }

        if (rctx != NULL) {
            rctx->active_filter_mode = NGX_JS_FILTER_WB_SYNC;
        }

        if (JS_IsException(result)) {
            ngx_js_log_exception(ctx, r->connection->log);
            JS_FreeValue(ctx, result);
            /* On exception, treat as drop (cur_data = "") */
            cur_data = (u_char *) "";
            cur_len  = 0;
            if (rctx != NULL) {
                rctx->stream_out = NULL;
            }
            continue;
        }

        /* STREAM_ASYNC: expect a Promise from the return value */
        if (elts[i].mode == NGX_JS_FILTER_STREAM_ASYNC) {
            state = (int) JS_PromiseState(ctx, result);

            if (state == -1) {
                /* not a Promise — pass-through: keep cur_data unchanged */
                JS_FreeValue(ctx, result);
                continue;
            }

            switch ((JSPromiseStateEnum) state) {

            case JS_PROMISE_FULFILLED: {
                JSValue     res = JS_PromiseResult(ctx, result);
                const char *s;
                size_t      slen;
                u_char     *p;

                JS_FreeValue(ctx, result);

                if (JS_IsString(res)) {
                    s = JS_ToCStringLen(ctx, &slen, res);
                    if (s) {
                        if (slen > 0) {
                            p = ngx_pnalloc(r->pool, slen);
                            if (p) {
                                ngx_memcpy(p, s, slen);
                                cur_data = p;
                                cur_len  = slen;
                            } else {
                                cur_data = (u_char *) "";
                                cur_len  = 0;
                            }
                        } else {
                            cur_data = (u_char *) "";
                            cur_len  = 0;
                        }
                        JS_FreeCString(ctx, s);
                    }
                }
                /* Non-string: pass-through — keep cur_data unchanged */

                JS_FreeValue(ctx, res);
                /*
                 * stream_out stays NULL; any subsequent SYNC filter will reset
                 * it and populate it via sendBuffer.  Post-loop: if this was
                 * the last active filter, stream_out is built from cur_data.
                 */
                continue;
            }

            case JS_PROMISE_REJECTED: {
                JSValue reason = JS_PromiseResult(ctx, result);
                JSValue str_v  = JS_ToString(ctx, reason);
                const char *cs = JS_ToCString(ctx, str_v);
                if (cs) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "js: async streaming filter rejected: %s",
                                  cs);
                    JS_FreeCString(ctx, cs);
                }
                JS_FreeValue(ctx, str_v);
                JS_FreeValue(ctx, reason);
                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, flags_obj);
                JS_FreeValue(ctx, req_obj);
                w->dispatching_body_arr = NULL;
                return NGX_ERROR;
            }

            case JS_PROMISE_PENDING:
                /* Suspend: save cur_data (= input to this async filter) */
                sf_p = ngx_pcalloc(r->pool, sizeof(ngx_js_sf_pending_t));
                if (sf_p == NULL) {
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, flags_obj);
                    JS_FreeValue(ctx, req_obj);
                    w->dispatching_body_arr = NULL;
                    return NGX_ERROR;
                }

                sf_p->promise    = JS_DupValue(ctx, result);
                sf_p->resume_idx = i + 1;
                sf_p->w          = w;
                sf_p->r          = r;
                sf_p->cur_data   = cur_data;
                sf_p->cur_len    = cur_len;
                sf_p->is_last    = is_last;
                sf_p->next       = w->sf_pending;
                w->sf_pending    = sf_p;

                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, flags_obj);
                JS_FreeValue(ctx, req_obj);
                w->dispatching_body_arr = NULL;
                return NGX_AGAIN;
            }
        }

        /* STREAM_SYNC: flatten stream_out into cur_data for next filter */
        if (rctx != NULL) {
            ngx_js_flatten_stream_out(r, rctx, &cur_data, &cur_len);
        } else {
            cur_data = (u_char *) "";
            cur_len  = 0;
        }
        JS_FreeValue(ctx, result);
    }

    JS_FreeValue(ctx, flags_obj);
    JS_FreeValue(ctx, req_obj);
    w->dispatching_body_arr = NULL;

    /*
     * If the last active filter was STREAM_ASYNC FULFILLED (or a non-Promise
     * async return / pass-through "not a Promise"), stream_out is NULL but
     * cur_data may hold content that still needs to be emitted.  Build a
     * chain link from cur_data so the caller can pass it downstream.
     */
    if (rctx != NULL && rctx->stream_out == NULL && cur_len > 0) {
        ngx_buf_t    *b;
        ngx_chain_t  *link;

        b = ngx_calloc_buf(r->pool);
        if (b != NULL) {
            b->pos    = cur_data;
            b->last   = cur_data + cur_len;
            b->memory = 1;

            link = ngx_alloc_chain_link(r->pool);
            if (link != NULL) {
                link->buf  = b;
                link->next = NULL;
                rctx->stream_out      = link;
                rctx->stream_out_last = &link->next;
            }
        }
    }

    return NGX_OK;
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
    JS_CGETSET_MAGIC_DEF("hasHandler",              ngx_js_location_get, NULL,                82),
    JS_CGETSET_DEF       ("errorPage",             ngx_js_location_get_error_page,
                                                   ngx_js_location_set_error_page),

    /* Per-request snapshot read/write control */
    JS_CFUNC_DEF("setWriteMode",   1, ngx_js_location_fn_set_write_mode),
    JS_CFUNC_DEF("setReadMode",    1, ngx_js_location_fn_set_read_mode),
    JS_CFUNC_DEF("setProperty",    2, ngx_js_location_fn_set_property),
    JS_CFUNC_DEF("getProperty",    1, ngx_js_location_fn_get_property),

    /* Handler management */
    JS_CFUNC_DEF("clearHandler",   0, ngx_js_location_fn_clear_handler),

    /* Nested-location management (requires srv_op; not on r.location) */
    JS_CFUNC_DEF("addLocation",    1, ngx_js_location_fn_add_location),
    JS_CFUNC_DEF("removeLocation", 1, ngx_js_location_fn_remove_location),
    JS_CFUNC_DEF("clone",          1, ngx_js_location_fn_clone),

    /* Filter list management */
    JS_CGETSET_MAGIC_DEF("headerFilters", ngx_js_location_get_filter_list,
                          NULL, 0),
    JS_CGETSET_MAGIC_DEF("bodyFilters",   ngx_js_location_get_filter_list,
                          NULL, 1),
    JS_CFUNC_DEF("addHeaderFilter",    1, ngx_js_location_fn_add_header_filter),
    JS_CFUNC_DEF("addBodyFilter",      2, ngx_js_location_fn_add_body_filter),
    JS_CFUNC_DEF("removeHeaderFilter", 1, ngx_js_location_fn_remove_header_filter),
    JS_CFUNC_DEF("removeBodyFilter",   1, ngx_js_location_fn_remove_body_filter),
    JS_CFUNC_DEF("getHeaderFilter",    1, ngx_js_location_fn_get_header_filter),
    JS_CFUNC_DEF("getBodyFilter",      1, ngx_js_location_fn_get_body_filter),
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
    return ngx_js_wrap_location_ex(ctx, clcf, NULL);
}


/*
 * Like ngx_js_wrap_location but also stores the owning server opaque,
 * enabling loc.addLocation() / loc.removeLocation() on the result.
 */
static JSValue
ngx_js_wrap_location_ex(JSContext *ctx, ngx_http_core_loc_conf_t *clcf,
    ngx_js_server_opaque_t *srv_op)
{
    JSValue                    obj;
    ngx_js_location_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_location_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->clcf       = clcf;
    op->write_mode = NGX_JS_WRITE_GLOBAL;
    op->read_mode  = NGX_JS_WRITE_GLOBAL;
    op->srv_op     = srv_op;

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
 * srv_op is the owning server opaque (may be NULL for plain wraps).
 */
static void
ngx_js_collect_locations(JSContext *ctx, JSValue arr,
    ngx_http_location_tree_node_t *node, uint32_t *idx,
    ngx_js_server_opaque_t *srv_op)
{
    if (node == NULL) {
        return;
    }

    /* left subtree */
    ngx_js_collect_locations(ctx, arr, node->left, idx, srv_op);

    /* this node's exact match (= prefix) */
    if (node->exact) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location_ex(ctx, node->exact, srv_op));
    }

    /* this node's inclusive (prefix) match */
    if (node->inclusive) {
        JS_SetPropertyUint32(ctx, arr, (*idx)++,
                             ngx_js_wrap_location_ex(ctx, node->inclusive, srv_op));
    }

    /* child subtree (shared prefix children) */
    ngx_js_collect_locations(ctx, arr, node->tree, idx, srv_op);

    /* right subtree */
    ngx_js_collect_locations(ctx, arr, node->right, idx, srv_op);
}


/*
 * Build a JS Array of NginxLocation objects for a server's default
 * root location config (which holds the static_locations tree).
 * srv_op is stored in every returned NginxLocation for loc.addLocation().
 */
static JSValue
ngx_js_build_locations(JSContext *ctx, ngx_http_core_loc_conf_t *root_clcf,
    ngx_js_server_opaque_t *srv_op)
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
                                 ngx_js_wrap_location_ex(ctx, *rloc, srv_op));
        }
    }
#endif

    ngx_js_collect_locations(ctx, arr, root_clcf->static_locations, &idx,
                             srv_op);

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


struct ngx_js_server_opaque_s {
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
};


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
 * Count the nesting depth of a location path.
 *
 *   /api           → 1
 *   /api/v1        → 2
 *   /api/v1/detail → 3
 *
 * Named (@name) and regex patterns don't start with '/' and return 1
 * (they are always server-level).
 */
static ngx_uint_t
ngx_js_path_depth(const ngx_str_t *name)
{
    ngx_uint_t  depth;
    size_t      i;

    if (name->len == 0) {
        return 0;
    }

    depth = 1;
    for (i = 1; i < name->len; i++) {
        if (name->data[i] == '/') {
            depth++;
        }
    }

    return depth;
}


/*
 * Parse the "depth" option from a JS opts object.
 * Returns NGX_MAX_UINT32_VALUE when depth is absent, Infinity, or NaN
 * (treated as "unlimited").
 */
static ngx_uint_t
ngx_js_parse_depth_opt(JSContext *ctx, JSValueConst opts)
{
    JSValue     depth_val;
    double      depth_d;
    ngx_uint_t  depth;

    if (JS_IsUndefined(opts) || !JS_IsObject(opts)) {
        return NGX_MAX_UINT32_VALUE;
    }

    depth_val = JS_GetPropertyStr(ctx, opts, "depth");

    if (JS_IsUndefined(depth_val) || JS_IsNull(depth_val)) {
        JS_FreeValue(ctx, depth_val);
        return NGX_MAX_UINT32_VALUE;
    }

    if (JS_ToFloat64(ctx, &depth_d, depth_val) != 0) {
        JS_FreeValue(ctx, depth_val);
        return NGX_MAX_UINT32_VALUE;
    }

    JS_FreeValue(ctx, depth_val);

    if (isnan(depth_d) || isinf(depth_d) || depth_d < 0) {
        return NGX_MAX_UINT32_VALUE;
    }

    depth = (ngx_uint_t) depth_d;

    return depth;
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
 * Filter op->prefix_locs[], op->regex_locs[], and op->named_locs[] in-place,
 * keeping only entries whose nesting depth does not exceed depth_limit.
 *
 *   depth_limit = 0                 → remove everything (empty server)
 *   depth_limit = 1                 → keep only top-level paths (/foo)
 *   depth_limit = NGX_MAX_UINT32_VALUE → keep all (no-op)
 */
static void
ngx_js_depth_filter_locs(ngx_js_server_opaque_t *op, ngx_uint_t depth_limit)
{
    ngx_js_loc_entry_t    *pe;
    ngx_uint_t             i, j, d;

    if (depth_limit == NGX_MAX_UINT32_VALUE) {
        return;  /* unlimited — nothing to do */
    }

    /* Filter prefix_locs[] */
    pe = op->prefix_locs.elts;
    j  = 0;
    for (i = 0; i < op->prefix_locs.nelts; i++) {
        d = ngx_js_path_depth(&pe[i].clcf->name);
        if (d <= depth_limit) {
            pe[j++] = pe[i];
        }
    }
    op->prefix_locs.nelts = j;

#if (NGX_PCRE)
    /* regex_locs are always depth 1 — remove all if depth_limit == 0 */
    if (depth_limit == 0) {
        op->regex_locs.nelts = 0;
    }
#endif

    /* named_locs are always depth 1 */
    if (depth_limit == 0) {
        op->named_locs.nelts = 0;
    }
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
ngx_js_do_add_location(JSContext *ctx, ngx_js_server_opaque_t *op,
    int argc, JSValueConst *argv)
{
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
                return ngx_js_wrap_location_ex(ctx, dup_e[di].clcf, op);
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
                return ngx_js_wrap_location_ex(ctx, dup_re[di].clcf, op);
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
                return ngx_js_wrap_location_ex(ctx, dup_e[di].clcf, op);
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

    return ngx_js_wrap_location_ex(ctx, new_clcf, op);
}


/*
 * srv.addLocation(pattern [, opts]) — public JS method on NginxServer.
 */
static JSValue
ngx_js_server_fn_add_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_do_add_location(ctx, op, argc, argv);
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
ngx_js_do_remove_location(JSContext *ctx, ngx_js_server_opaque_t *op,
    int argc, JSValueConst *argv)
{
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


/*
 * srv.removeLocation(pattern) — public JS method on NginxServer.
 */
static JSValue
ngx_js_server_fn_remove_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_do_remove_location(ctx, op, argc, argv);
}


/*
 * loc.addLocation(pattern [, opts])
 *
 * Delegates to the server-level addLocation logic.  The new location is
 * added to the server's flat prefix_locs[] snapshot; the BST builder
 * automatically groups it under the parent via shared-prefix nesting.
 *
 * Requires the location to have been obtained from server.locations[]
 * or a prior srv.addLocation() call.  Not available on r.location.
 */
static JSValue
ngx_js_location_fn_add_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *loc_op;

    loc_op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!loc_op) {
        return JS_EXCEPTION;
    }

    if (loc_op->srv_op == NULL) {
        return JS_ThrowTypeError(ctx,
            "addLocation: not available on r.location (read-only context)");
    }

    return ngx_js_do_add_location(ctx, loc_op->srv_op, argc, argv);
}


/*
 * loc.clearHandler()
 *
 * Removes the JS content handler from this location, restoring clcf->handler
 * to the value it had before the first location.handler = fn assignment.
 * If no JS handler was ever set this is a no-op.
 *
 * Typical use: rollback — when a snapshot that set a handler is replaced by
 * one that does not mention this location's handler.
 */
static JSValue
ngx_js_location_fn_clear_handler(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *loc_op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_js_loc_conf_t         *jlcf;

    loc_op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!loc_op) {
        return JS_EXCEPTION;
    }

    clcf = loc_op->clcf;
    jlcf = clcf->loc_conf[ngx_js_http_module.ctx_index];

    if (jlcf->handler_idx < 0) {
        return JS_UNDEFINED;   /* no JS handler — nothing to do */
    }

    clcf->handler     = (ngx_http_handler_pt) jlcf->original_handler;  /* restore */
    jlcf->handler_idx = -1;

    return JS_UNDEFINED;
}


/*
 * loc.removeLocation(pattern)
 *
 * Delegates to the server-level removeLocation logic.
 * Same constraints as loc.addLocation().
 */
static JSValue
ngx_js_location_fn_remove_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t  *loc_op;

    loc_op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!loc_op) {
        return JS_EXCEPTION;
    }

    if (loc_op->srv_op == NULL) {
        return JS_ThrowTypeError(ctx,
            "removeLocation: not available on r.location (read-only context)");
    }

    return ngx_js_do_remove_location(ctx, loc_op->srv_op, argc, argv);
}


/*
 * loc.clone(newPattern [, {depth: N}])
 *
 * Creates a new prefix location at newPattern that is a shallow copy of
 * this location (same handler, same config values).
 *
 * If depth > 0, child locations from the same server whose paths start
 * with this location's path are also copied with the source prefix
 * replaced by newPattern.  E.g., cloning /api → /v2 with depth:1
 * additionally creates /v2/v1 (copy of /api/v1), /v2/v2 (copy of /api/v2).
 *
 * depth:0 (default when omitted) = only the location itself, no children.
 * depth:N = copy children up to N levels of relative nesting.
 * depth:Infinity = copy all descendants.
 *
 * Returns the new NginxLocation object.
 */
static JSValue
ngx_js_location_fn_clone(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_location_opaque_t   *loc_op;
    ngx_js_server_opaque_t     *srv_op;
    ngx_http_core_loc_conf_t   *src_clcf, *new_clcf, *child_clcf;
    ngx_js_loc_entry_t         *src_entries, *entry;
    const char                 *pat_str;
    u_char                     *p, *new_child_data;
    ngx_str_t                   new_name;
    ngx_uint_t                  i, n_snap, depth_limit, rel_depth;
    size_t                      suffix_len, new_child_len;

    loc_op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!loc_op) {
        return JS_EXCEPTION;
    }

    if (loc_op->srv_op == NULL) {
        return JS_ThrowTypeError(ctx,
            "clone: not available on r.location (read-only context)");
    }

    srv_op   = loc_op->srv_op;
    src_clcf = loc_op->clcf;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "clone: first argument must be a pattern string");
    }

    pat_str = JS_ToCString(ctx, argv[0]);
    if (!pat_str) {
        return JS_EXCEPTION;
    }

    /* Trim leading whitespace; only plain prefix patterns supported */
    p = (u_char *) pat_str;
    while (*p == ' ') { p++; }

    if (p[0] == '=' || p[0] == '^' || p[0] == '~' || p[0] == '@') {
        JS_FreeCString(ctx, pat_str);
        return JS_ThrowTypeError(ctx,
            "clone: only plain prefix patterns supported");
    }

    new_name.len  = ngx_strlen(p);
    new_name.data = ngx_pnalloc(srv_op->dyn_pool, new_name.len + 1);
    if (new_name.data == NULL) {
        JS_FreeCString(ctx, pat_str);
        return JS_EXCEPTION;
    }
    ngx_memcpy(new_name.data, p, new_name.len);
    new_name.data[new_name.len] = '\0';
    JS_FreeCString(ctx, pat_str);

    /* depth:0 is the default (no children) */
    depth_limit = ngx_js_parse_depth_opt(ctx,
                                         argc >= 2 ? argv[1] : JS_UNDEFINED);
    if (depth_limit == NGX_MAX_UINT32_VALUE && argc < 2) {
        depth_limit = 0;  /* omitted → depth:0 (location only, no children) */
    }

    /* Idempotency: return existing if newPattern already in prefix_locs[] */
    {
        ngx_js_loc_entry_t  *dup_e = srv_op->prefix_locs.elts;
        ngx_uint_t           di;

        for (di = 0; di < srv_op->prefix_locs.nelts; di++) {
            if (dup_e[di].clcf->name.len == new_name.len
                && !dup_e[di].is_exact
                && ngx_memcmp(dup_e[di].clcf->name.data,
                              new_name.data, new_name.len) == 0)
            {
                return ngx_js_wrap_location_ex(ctx, dup_e[di].clcf, srv_op);
            }
        }
    }

    /* Create the new location as a shallow copy of source */
    new_clcf = ngx_palloc(srv_op->dyn_pool, sizeof(ngx_http_core_loc_conf_t));
    if (new_clcf == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    *new_clcf        = *src_clcf;
    new_clcf->name   = new_name;
    new_clcf->noname = 0;
    new_clcf->named  = 0;
    new_clcf->exact_match = 0;
    new_clcf->noregex     = 0;

    entry = ngx_array_push(&srv_op->prefix_locs);
    if (entry == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    entry->clcf     = new_clcf;
    entry->is_exact = 0;
    entry->dynamic  = 1;

    /*
     * Copy children: iterate the snapshot taken before we pushed the new
     * entry (n_snap) so we don't process our own entry.  For each source
     * child whose path starts with src_clcf->name and whose relative
     * depth ≤ depth_limit, create a repathied copy.
     */
    if (depth_limit > 0 && src_clcf->name.len > 0) {

        n_snap     = srv_op->prefix_locs.nelts - 1; /* entries before push */
        src_entries = srv_op->prefix_locs.elts;

        for (i = 0; i < n_snap; i++) {
            ngx_http_core_loc_conf_t  *ce;
            ngx_str_t                  rel;

            ce = src_entries[i].clcf;

            /* Must be strictly longer than source and share the prefix */
            if (ce->name.len <= src_clcf->name.len) {
                continue;
            }
            if (ngx_memcmp(ce->name.data,
                           src_clcf->name.data, src_clcf->name.len) != 0)
            {
                continue;
            }
            if (ce->name.data[src_clcf->name.len] != '/') {
                continue;
            }

            /* Relative depth: count slashes in the suffix part */
            rel.data = ce->name.data + src_clcf->name.len;
            rel.len  = ce->name.len  - src_clcf->name.len;
            rel_depth = ngx_js_path_depth(&rel);

            if (rel_depth > depth_limit) {
                continue;
            }

            /* Build repathied name: newPattern + suffix */
            suffix_len     = ce->name.len - src_clcf->name.len;
            new_child_len  = new_name.len + suffix_len;
            new_child_data = ngx_pnalloc(srv_op->dyn_pool,
                                         new_child_len + 1);
            if (new_child_data == NULL) {
                return JS_ThrowOutOfMemory(ctx);
            }
            ngx_memcpy(new_child_data, new_name.data, new_name.len);
            ngx_memcpy(new_child_data + new_name.len,
                       ce->name.data + src_clcf->name.len, suffix_len);
            new_child_data[new_child_len] = '\0';

            child_clcf = ngx_palloc(srv_op->dyn_pool,
                                    sizeof(ngx_http_core_loc_conf_t));
            if (child_clcf == NULL) {
                return JS_ThrowOutOfMemory(ctx);
            }
            *child_clcf           = *ce;
            child_clcf->name.data = new_child_data;
            child_clcf->name.len  = new_child_len;
            child_clcf->noname    = 0;

            entry = ngx_array_push(&srv_op->prefix_locs);
            if (entry == NULL) {
                return JS_ThrowOutOfMemory(ctx);
            }
            entry->clcf     = child_clcf;
            entry->is_exact = src_entries[i].is_exact;
            entry->dynamic  = 1;
        }
    }

    /* Rebuild the live BST with the new location(s) */
    if (ngx_js_rebuild_loc_tree(srv_op, srv_op->cycle->log) != NGX_OK) {
        return JS_EXCEPTION;
    }

    return ngx_js_wrap_location_ex(ctx, new_clcf, srv_op);
}


static void
ngx_js_server_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_server_opaque_t   *op;
    ngx_http_core_loc_conf_t *root_clcf;

    op = JS_GetOpaque(val, ngx_js_server_class_id);
    if (op) {
        if (op->tree_pool) {
            /*
             * tree_pool owns the BST nodes (static_locations),
             * regex_locations array, and named_locations array.
             * NULL these pointers out before destroying the pool so
             * that no stale pointer remains in any clcf after we free
             * the memory — e.g. the request-phase location matcher or
             * another finalizer running during JS_FreeRuntime teardown.
             */
            if (op->cscf && op->cscf->ctx) {
                root_clcf = op->cscf->ctx->loc_conf[
                                ngx_http_core_module.ctx_index];
                if (root_clcf) {
                    root_clcf->static_locations = NULL;
#if (NGX_PCRE)
                    root_clcf->regex_locations  = NULL;
#endif
                }
            }
            if (op->cscf) {
                op->cscf->named_locations = NULL;
            }

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


/* ------------------------------------------------------------------ */
/* Public accessor — Stage 52 Phase C                                  */
/* Returns the cscf* from a NginxServer JS object, NULL on failure.    */
/* If cycle_out is non-NULL, *cycle_out is set to op->cycle.           */
/* ------------------------------------------------------------------ */

ngx_http_core_srv_conf_t *
ngx_js_server_get_cscf(JSValueConst srv, ngx_cycle_t **cycle_out)
{
    ngx_js_server_opaque_t  *op;

    op = JS_GetOpaque(srv, ngx_js_server_class_id);
    if (op == NULL) {
        return NULL;
    }

    if (cycle_out != NULL) {
        *cycle_out = op->cycle;
    }

    return op->cscf;
}


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
    arr = ngx_js_build_locations(ctx, clcf, op);
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
                                 ngx_js_wrap_location_ex(ctx, *named, op));
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


/*
 * srv.clone(newName)
 *
 * Creates an independent deep copy of this server under a new server_name.
 *
 * Unlike nginx.http.addServer({template}) which starts with empty locations,
 * clone() copies the full current location tree — prefix, regex, and named
 * locations (both static and dynamic) — so the new server inherits all
 * existing handlers and configuration.
 *
 * The copy is structural: each location in the clone initially points to
 * the same ngx_http_core_loc_conf_t as the source (same handler, same
 * config values).  addLocation() on the clone creates new, independent
 * entries; modifying shared locations via .handler = fn affects both
 * servers.
 *
 * The clone is added to every ngx_js_vhost_entries[] slot.  Call
 * nginx.http.rebuildVhostDispatch() afterwards to activate routing.
 *
 * Returns the new NginxServer JS wrapper.
 */
static JSValue
ngx_js_server_fn_clone(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t     *src_op, *new_op;
    ngx_http_core_srv_conf_t   *src_cscf, *new_cscf;
    ngx_http_core_loc_conf_t   *src_root, *new_root;
    ngx_http_conf_ctx_t        *new_ctx;
    void                      **new_loc_conf, **new_srv_conf;
    ngx_http_server_name_t     *sn;
    ngx_js_addr_entry_t        *entry;
    ngx_http_core_srv_conf_t  **new_servers;
    ngx_cycle_t                *cycle;
    JSValue                     srv_obj, global, nginx_obj, http_obj;
    JSValue                     servers_arr, lv;
    const char                 *name_str;
    size_t                      name_len;
    uint32_t                    servers_len;
    ngx_uint_t                  i;

    src_op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!src_op) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "clone: new server name required");
    }

    name_str = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (name_str == NULL) {
        return JS_EXCEPTION;
    }

    src_cscf = src_op->cscf;
    src_root = src_cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
    cycle    = src_op->cycle;

    /* allocate new cscf as a shallow copy */
    new_cscf = ngx_palloc(cycle->pool, sizeof(ngx_http_core_srv_conf_t));
    if (new_cscf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    *new_cscf = *src_cscf;

    /* new loc_conf[]: copy all module slots */
    new_loc_conf = ngx_palloc(cycle->pool,
                              sizeof(void *) * ngx_http_max_module);
    if (new_loc_conf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(new_loc_conf, src_cscf->ctx->loc_conf,
               sizeof(void *) * ngx_http_max_module);

    /*
     * new root clcf: copy source root INCLUDING its current location-tree
     * pointers (static_locations, regex_locations).  ngx_js_wrap_server
     * will snapshot these into new_op->prefix_locs / regex_locs /
     * named_locs, and the subsequent ngx_js_rebuild_loc_tree call will
     * replace them with an independent fresh BST.
     */
    new_root = ngx_palloc(cycle->pool, sizeof(ngx_http_core_loc_conf_t));
    if (new_root == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    *new_root = *src_root;
    new_root->loc_conf = new_loc_conf;
    new_loc_conf[ngx_http_core_module.ctx_index] = new_root;

    /* new srv_conf[]: copy all module slots, override core slot */
    new_srv_conf = ngx_palloc(cycle->pool,
                              sizeof(void *) * ngx_http_max_module);
    if (new_srv_conf == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(new_srv_conf, src_cscf->ctx->srv_conf,
               sizeof(void *) * ngx_http_max_module);
    new_srv_conf[ngx_http_core_module.ctx_index] = new_cscf;

    /* wire up the new conf context */
    new_ctx = ngx_palloc(cycle->pool, sizeof(ngx_http_conf_ctx_t));
    if (new_ctx == NULL) {
        JS_FreeCString(ctx, name_str);
        return JS_ThrowOutOfMemory(ctx);
    }
    new_ctx->main_conf = src_cscf->ctx->main_conf;
    new_ctx->srv_conf  = new_srv_conf;
    new_ctx->loc_conf  = new_loc_conf;
    new_cscf->ctx = new_ctx;

    /* server_names: fresh array with just the new name */
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

    /*
     * Wrap: ngx_js_wrap_server snapshots new_root->static_locations
     * (= source's current BST), new_root->regex_locations, and
     * new_cscf->named_locations into the new opaque's snapshot arrays.
     */
    srv_obj = ngx_js_wrap_server(ctx, new_cscf, cycle);
    if (JS_IsException(srv_obj)) {
        return srv_obj;
    }

    /*
     * Rebuild the location tree so the clone has its own independent BST,
     * regex array, and named_locations pointer — not sharing with source.
     * Then apply optional depth filter before the final rebuild.
     */
    new_op = JS_GetOpaque(srv_obj, ngx_js_server_class_id);
    if (new_op != NULL) {
        ngx_uint_t  depth_limit;

        /* Parse {depth: N} from second argument (if given) */
        depth_limit = ngx_js_parse_depth_opt(ctx,
                                             argc >= 2 ? argv[1] : JS_UNDEFINED);

        /* Filter snapshot arrays to the requested depth */
        ngx_js_depth_filter_locs(new_op, depth_limit);

        if (ngx_js_rebuild_loc_tree(new_op, cycle->log) != NGX_OK) {
            JS_FreeValue(ctx, srv_obj);
            return JS_ThrowInternalError(ctx, "clone: rebuild_loc_tree failed");
        }
    }

    /* ensure nnames is populated for removeServer() in worker context */
    if (new_op != NULL && new_op->nnames == 0 && sn->name.len > 0) {
        new_op->names = ngx_palloc(cycle->pool, sizeof(ngx_str_t));
        if (new_op->names != NULL) {
            new_op->names[0] = sn->name;
            new_op->nnames   = 1;
        }
    }

    /* add new_cscf to every vhost dispatch entry */
    for (i = 0; i < ngx_js_vhost_nentries; i++) {
        entry = &ngx_js_vhost_entries[i];
        new_servers = ngx_palloc(cycle->pool,
                         (entry->nservers + 1)
                         * sizeof(ngx_http_core_srv_conf_t *));
        if (new_servers == NULL) {
            JS_FreeValue(ctx, srv_obj);
            return JS_ThrowOutOfMemory(ctx);
        }
        ngx_memcpy(new_servers, entry->servers,
                   entry->nservers * sizeof(ngx_http_core_srv_conf_t *));
        new_servers[entry->nservers] = new_cscf;
        entry->servers  = new_servers;
        entry->nservers++;
    }

    /* append to nginx.http.servers[] */
    global     = JS_GetGlobalObject(ctx);
    nginx_obj  = JS_GetPropertyStr(ctx, global, "nginx");
    http_obj   = JS_GetPropertyStr(ctx, nginx_obj, "http");
    servers_arr = JS_GetPropertyStr(ctx, http_obj, "servers");
    lv = JS_GetPropertyStr(ctx, servers_arr, "length");
    JS_ToUint32(ctx, &servers_len, lv);
    JS_FreeValue(ctx, lv);
    JS_SetPropertyUint32(ctx, servers_arr, servers_len,
                         JS_DupValue(ctx, srv_obj));
    JS_FreeValue(ctx, servers_arr);
    JS_FreeValue(ctx, http_obj);
    JS_FreeValue(ctx, nginx_obj);
    JS_FreeValue(ctx, global);

    return srv_obj;
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
    JS_CFUNC_DEF         ("clone",                    1, ngx_js_server_fn_clone),
    JS_CFUNC_DEF         ("findLocation",             1, ngx_js_server_fn_find_location),
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


JSValue
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
     *
     * We also relocate cscf->server_names.elts into cycle->pool so that
     * rebuildVhostDispatch() can safely iterate the full server_names
     * array (including sn->regex and sn->server fields) in worker context,
     * long after cf->temp_pool has been destroyed.
     */
    if (ngx_process != NGX_PROCESS_WORKER
        && cscf->server_names.nelts > 0)
    {
        ngx_http_server_name_t  *sn_copy;

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

        /* Relocate the elts array itself so it survives cf->temp_pool
         * destruction.  The pointer fields inside each entry (regex,
         * server, name.data) already live in cycle->pool.             */
        sn_copy = ngx_palloc(cycle->pool,
                             op->nnames * sizeof(ngx_http_server_name_t));
        if (sn_copy == NULL) {
            js_free(ctx, op);
            return JS_EXCEPTION;
        }
        ngx_memcpy(sn_copy, sn,
                   op->nnames * sizeof(ngx_http_server_name_t));
        cscf->server_names.elts = sn_copy;
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
 * ngx_js_build_default_server_conf
 *
 * Builds a synthetic ngx_http_core_srv_conf_t suitable for use as the
 * copy-source template in ngx_js_http_add_server() when nginx.http.servers[]
 * is empty (i.e., nginx.conf has an http{} block but no server{} blocks).
 *
 * Each HTTP module's create_srv_conf / create_loc_conf callback is invoked
 * with a minimal fake ngx_conf_t (pool = cycle->pool) so every slot in the
 * srv_conf[] / loc_conf[] arrays is properly initialised (NGX_CONF_UNSET
 * values rather than raw zeroes, matching what nginx sets during a normal
 * config parse).  The ngx_http_core_module server-level defaults are then
 * applied inline, mirroring ngx_http_core_merge_srv_conf().
 */
static ngx_http_core_srv_conf_t *
ngx_js_build_default_server_conf(ngx_cycle_t *cycle,
    ngx_http_conf_ctx_t *http_ctx)
{
    char                        *rv;
    ngx_conf_t                   fake_cf;
    ngx_conf_file_t              fake_file;
    ngx_http_conf_ctx_t          fake_ctx;
    ngx_module_t               **mods;
    ngx_http_module_t           *hmod;
    ngx_http_core_srv_conf_t    *cscf;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_conf_ctx_t         *new_ctx;
    void                       **srv_conf, **loc_conf, *mconf;
    ngx_uint_t                   i;
    static u_char                fname[] = "[js-synthesized]";

    /*
     * Minimal fake ngx_conf_t.  Module create_* functions use cf->pool and
     * cf->temp_pool for allocations, cf->log / cf->cycle for diagnostics,
     * and cf->conf_file only to record file_name / line for error messages.
     * Module merge_* functions additionally use cf->ctx to locate the
     * current http/srv/loc context chain.
     */
    ngx_memzero(&fake_cf,   sizeof(ngx_conf_t));
    ngx_memzero(&fake_file, sizeof(ngx_conf_file_t));
    ngx_memzero(&fake_ctx,  sizeof(ngx_http_conf_ctx_t));
    fake_file.file.name.data = fname;
    fake_file.file.name.len  = sizeof(fname) - 1;
    fake_cf.pool             = cycle->pool;
    fake_cf.temp_pool        = cycle->pool;
    fake_cf.log              = cycle->log;
    fake_cf.cycle            = cycle;
    fake_cf.conf_file        = &fake_file;
    fake_cf.ctx              = &fake_ctx;
    fake_cf.module_type      = NGX_HTTP_MODULE;
    fake_cf.cmd_type         = NGX_HTTP_SRV_CONF;

    /* fake_ctx starts as a copy of http_ctx (the http{}-level parent) */
    fake_ctx = *http_ctx;

    srv_conf = ngx_pcalloc(cycle->pool, sizeof(void *) * ngx_http_max_module);
    loc_conf = ngx_pcalloc(cycle->pool, sizeof(void *) * ngx_http_max_module);
    if (srv_conf == NULL || loc_conf == NULL) {
        return NULL;
    }

    mods = cycle->modules;

    /* Phase 1: call create_srv_conf / create_loc_conf for every http module */
    for (i = 0; mods[i]; i++) {
        if (mods[i]->type != NGX_HTTP_MODULE) {
            continue;
        }

        hmod = mods[i]->ctx;

        if (hmod->create_srv_conf) {
            mconf = hmod->create_srv_conf(&fake_cf);
            if (mconf == NULL) {
                return NULL;
            }
            srv_conf[mods[i]->ctx_index] = mconf;
        }

        if (hmod->create_loc_conf) {
            mconf = hmod->create_loc_conf(&fake_cf);
            if (mconf == NULL) {
                return NULL;
            }
            loc_conf[mods[i]->ctx_index] = mconf;
        }
    }

    cscf = srv_conf[ngx_http_core_module.ctx_index];
    clcf = loc_conf[ngx_http_core_module.ctx_index];

    if (cscf == NULL || clcf == NULL) {
        return NULL;
    }

    /*
     * Phase 2: merge against the http{}-level parent confs.
     * This mirrors what ngx_http_merge_servers() does for every server{}
     * block: call merge_srv_conf(parent=http_ctx->srv_conf[i], child=new)
     * and merge_loc_conf(parent=http_ctx->loc_conf[i], child=new).
     * After merging, all module-specific fields (logs, error_log, timeouts,
     * etc.) inherit the http{}-level defaults, exactly as a server{} block
     * with no overrides would.
     */
    fake_ctx.srv_conf = srv_conf;
    fake_ctx.loc_conf = loc_conf;

    for (i = 0; mods[i]; i++) {
        if (mods[i]->type != NGX_HTTP_MODULE) {
            continue;
        }

        hmod = mods[i]->ctx;

        if (hmod->merge_srv_conf) {
            rv = hmod->merge_srv_conf(&fake_cf,
                                      http_ctx->srv_conf[mods[i]->ctx_index],
                                      srv_conf[mods[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return NULL;
            }
        }

        if (hmod->merge_loc_conf) {
            rv = hmod->merge_loc_conf(&fake_cf,
                                      http_ctx->loc_conf[mods[i]->ctx_index],
                                      loc_conf[mods[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return NULL;
            }
        }
    }

    /* Wire up the server conf context */
    new_ctx = ngx_palloc(cycle->pool, sizeof(ngx_http_conf_ctx_t));
    if (new_ctx == NULL) {
        return NULL;
    }
    new_ctx->main_conf = http_ctx->main_conf;
    new_ctx->srv_conf  = srv_conf;
    new_ctx->loc_conf  = loc_conf;
    cscf->ctx          = new_ctx;

    /* self-reference required by ngx_http_core_find_location */
    clcf->loc_conf = loc_conf;

    return cscf;
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

    if (servers_len > 0) {
        s0    = JS_GetPropertyUint32(ctx, servers_arr, 0);
        op    = JS_GetOpaque(s0, ngx_js_server_class_id);
        cycle = op ? op->cycle : NULL;
        JS_FreeValue(ctx, s0);

        if (cycle == NULL) {
            JS_FreeValue(ctx, servers_arr);
            JS_FreeCString(ctx, name_str);
            return JS_ThrowInternalError(ctx, "addServer: cycle unavailable");
        }

    } else {
        /*
         * No existing servers — use the cycle saved when the http module
         * was installed.  ngx_cycle cannot be used here because during
         * init_conf it still points to the old cycle.
         */
        cycle = ngx_js_http_cycle ? ngx_js_http_cycle
                                  : (ngx_cycle_t *) ngx_cycle;
        if (cycle == NULL) {
            JS_FreeValue(ctx, servers_arr);
            JS_FreeCString(ctx, name_str);
            return JS_ThrowInternalError(ctx, "addServer: cycle unavailable");
        }
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

    if (cmcf->servers.nelts == 0) {
        /*
         * No static server{} blocks — use the pre-built default template.
         * ngx_js_default_cscf is constructed in ngx_js_http_com_install()
         * when it detects an empty servers list.
         */
        tmpl_cscf = ngx_js_default_cscf;
        if (tmpl_cscf == NULL) {
            JS_FreeCString(ctx, name_str);
            return JS_ThrowInternalError(ctx,
                "addServer: no server template available"
                " (http{} has no server{} blocks and default conf"
                " could not be built)");
        }

    } else {
        tmpl_cscf = cscfp[tmpl_idx];
    }

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


/*
 * srv.findLocation(pattern)
 *
 * Returns the NginxLocation whose pattern exactly equals the given string,
 * or null if no such location exists on this server.
 *
 * Pattern syntax is identical to addLocation():
 *   "/path"        — plain prefix
 *   "= /path"      — exact match
 *   "^~ /path"     — preferential prefix
 *   "~ /regex"     — case-sensitive regex
 *   "~* /regex"    — case-insensitive regex
 *   "@name"        — named location
 */
static JSValue
ngx_js_server_fn_find_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_server_opaque_t    *op;
    const char                *pat_str;
    u_char                    *p;
    ngx_str_t                  name;
    int                        exact_match, noregex, is_named;
#if (NGX_PCRE)
    int                        is_regex;
#endif
    ngx_js_loc_entry_t        *pe;
    ngx_uint_t                 i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "findLocation: pattern string required");
    }

    pat_str = JS_ToCString(ctx, argv[0]);
    if (pat_str == NULL) {
        return JS_EXCEPTION;
    }

    /* Parse modifier prefix — same logic as ngx_js_do_add_location */
    p = (u_char *) pat_str;
    while (*p == ' ') { p++; }

    exact_match = 0;
    noregex     = 0;
    is_named    = 0;
#if (NGX_PCRE)
    is_regex    = 0;
#endif

    if (p[0] == '@') {
        is_named = 1;

    } else if (p[0] == '=' && p[1] == ' ') {
        exact_match = 1;
        p += 2;
        while (*p == ' ') { p++; }

    } else if (p[0] == '^' && p[1] == '~' && p[2] == ' ') {
        noregex = 1;
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
#endif
    }

    name.data = p;
    name.len  = ngx_strlen(p);

    JS_FreeCString(ctx, pat_str);

#if (NGX_PCRE)
    /* Scan regex_locs[] */
    if (is_regex) {
        ngx_js_regex_entry_t  *re = op->regex_locs.elts;
        for (i = 0; i < op->regex_locs.nelts; i++) {
            ngx_http_core_loc_conf_t  *clcf = re[i].clcf;
            if (clcf->name.len == name.len
                && ngx_memcmp(clcf->name.data, name.data, name.len) == 0)
            {
                return ngx_js_wrap_location_ex(ctx, clcf, op);
            }
        }
        return JS_NULL;
    }
#endif

    /* Scan named_locs[] */
    if (is_named) {
        pe = op->named_locs.elts;
        for (i = 0; i < op->named_locs.nelts; i++) {
            ngx_http_core_loc_conf_t  *clcf = pe[i].clcf;
            if (clcf->name.len == name.len
                && ngx_memcmp(clcf->name.data, name.data, name.len) == 0)
            {
                return ngx_js_wrap_location_ex(ctx, clcf, op);
            }
        }
        return JS_NULL;
    }

    /* Scan prefix_locs[] (exact, noregex, and plain prefix) */
    pe = op->prefix_locs.elts;
    for (i = 0; i < op->prefix_locs.nelts; i++) {
        ngx_http_core_loc_conf_t  *clcf = pe[i].clcf;

        if ((int) clcf->exact_match != exact_match) {
            continue;
        }
        if ((int) clcf->noregex != noregex) {
            continue;
        }
        if (clcf->name.len != name.len) {
            continue;
        }
        if (ngx_memcmp(clcf->name.data, name.data, name.len) != 0) {
            continue;
        }

        return ngx_js_wrap_location_ex(ctx, clcf, op);
    }

    return JS_NULL;
}


/*
 * nginx.http.match(uri [, serverName])
 *
 * Simulates nginx location-matching for the given URI on the named server
 * (default: first server) and returns the matching NginxLocation object,
 * or null if no location matches.
 *
 * Matching order mirrors nginx:
 *   1. Exact match (= /path)
 *   2. Longest preferential-prefix (^~ /path) — skips regex if found
 *   3. Ordered regex locations (~ / ~*)
 *   4. Longest plain-prefix match
 *   5. null
 */
static JSValue
ngx_js_http_fn_match(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char              *uri_cstr, *srv_cstr;
    size_t                   uri_len, srv_len;
    ngx_str_t                uri_str;
    JSValue                  servers_arr, lv, srv_obj;
    uint32_t                 servers_len, i;
    ngx_js_server_opaque_t  *srv_op;
    ngx_js_loc_entry_t      *pe;
    ngx_http_core_loc_conf_t *exact_clcf, *best_prefix_clcf, *best_noregex_clcf;
    size_t                   best_prefix_len, best_noregex_len;
    int                      skip_regex;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "match: uri (string) required");
    }

    uri_cstr = JS_ToCStringLen(ctx, &uri_len, argv[0]);
    if (uri_cstr == NULL) {
        return JS_EXCEPTION;
    }

    srv_cstr = NULL;
    srv_len  = 0;
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        srv_cstr = JS_ToCStringLen(ctx, &srv_len, argv[1]);
        if (srv_cstr == NULL) {
            JS_FreeCString(ctx, uri_cstr);
            return JS_EXCEPTION;
        }
    }

    uri_str.data = (u_char *) uri_cstr;
    uri_str.len  = uri_len;

    /* Find the target server */
    servers_arr = JS_GetPropertyStr(ctx, this_val, "servers");
    lv = JS_GetPropertyStr(ctx, servers_arr, "length");
    JS_ToUint32(ctx, &servers_len, lv);
    JS_FreeValue(ctx, lv);

    srv_op  = NULL;
    srv_obj = JS_UNDEFINED;

    for (i = 0; i < servers_len; i++) {
        JSValue                 s  = JS_GetPropertyUint32(ctx, servers_arr, i);
        ngx_js_server_opaque_t *op = JS_GetOpaque(s, ngx_js_server_class_id);

        if (op == NULL) {
            JS_FreeValue(ctx, s);
            continue;
        }

        if (srv_cstr == NULL) {
            /* no server name given — use first server */
            srv_op  = op;
            srv_obj = s;
            break;
        }

        ngx_uint_t ni;
        for (ni = 0; ni < op->nnames; ni++) {
            if (op->names[ni].len == srv_len
                && ngx_strncasecmp(op->names[ni].data,
                                   (u_char *) srv_cstr, srv_len) == 0)
            {
                srv_op  = op;
                srv_obj = s;
                break;
            }
        }

        if (srv_op != NULL) {
            break;
        }

        JS_FreeValue(ctx, s);
    }

    JS_FreeValue(ctx, servers_arr);

    if (srv_cstr != NULL) {
        JS_FreeCString(ctx, srv_cstr);
    }

    if (srv_op == NULL) {
        JS_FreeCString(ctx, uri_cstr);
        return JS_NULL;
    }

    /* Phase 1: scan prefix_locs[] for exact / best-prefix / best-noregex */
    exact_clcf       = NULL;
    best_prefix_clcf = NULL;
    best_noregex_clcf = NULL;
    best_prefix_len  = 0;
    best_noregex_len = 0;
    skip_regex       = 0;

    pe = srv_op->prefix_locs.elts;

    for (i = 0; i < (uint32_t) srv_op->prefix_locs.nelts; i++) {
        ngx_http_core_loc_conf_t  *clcf = pe[i].clcf;
        ngx_str_t                 *name = &clcf->name;

        if (clcf->named) {
            continue;
        }

        if (clcf->exact_match) {
            if (name->len == uri_len
                && ngx_memcmp(name->data, uri_cstr, uri_len) == 0)
            {
                exact_clcf = clcf;
                break;
            }
            continue;
        }

        /* prefix match: uri must start with name */
        if (uri_len < name->len) {
            continue;
        }
        if (ngx_memcmp(name->data, uri_cstr, name->len) != 0) {
            continue;
        }

        if (clcf->noregex) {
            if (name->len > best_noregex_len) {
                best_noregex_len  = name->len;
                best_noregex_clcf = clcf;
            }
        } else {
            if (name->len > best_prefix_len) {
                best_prefix_len  = name->len;
                best_prefix_clcf = clcf;
            }
        }
    }

    /* Step 1: exact match wins immediately */
    if (exact_clcf != NULL) {
        JS_FreeValue(ctx, srv_obj);
        JS_FreeCString(ctx, uri_cstr);
        return ngx_js_wrap_location_ex(ctx, exact_clcf, srv_op);
    }

    /* Step 2: preferential-prefix (^~) beats regex if it's longer than
     *         any plain prefix found so far */
    if (best_noregex_clcf != NULL
        && best_noregex_len >= best_prefix_len)
    {
        skip_regex = 1;
    }

    if (skip_regex) {
        JS_FreeValue(ctx, srv_obj);
        JS_FreeCString(ctx, uri_cstr);
        return ngx_js_wrap_location_ex(ctx, best_noregex_clcf, srv_op);
    }

#if (NGX_PCRE)
    /* Step 3: first matching regex location */
    {
        ngx_js_regex_entry_t  *re = srv_op->regex_locs.elts;
        ngx_uint_t             nre = srv_op->regex_locs.nelts;
        ngx_uint_t             j;
        ngx_int_t              n;

        for (j = 0; j < nre; j++) {
            int                        captures[3];
            ngx_http_core_loc_conf_t  *clcf = re[j].clcf;
            if (clcf->regex == NULL) {
                continue;
            }
            n = ngx_regex_exec(clcf->regex->regex, &uri_str, captures, 3);
            if (n >= 0) {
                JS_FreeValue(ctx, srv_obj);
                JS_FreeCString(ctx, uri_cstr);
                return ngx_js_wrap_location_ex(ctx, clcf, srv_op);
            }
        }
    }
#endif

    JS_FreeValue(ctx, srv_obj);
    JS_FreeCString(ctx, uri_cstr);

    /* Step 4: longest plain prefix (may be NULL → return null) */
    if (best_prefix_clcf != NULL) {
        return ngx_js_wrap_location_ex(ctx, best_prefix_clcf, srv_op);
    }

    /* Step 5: also return the best noregex if that's all we have */
    if (best_noregex_clcf != NULL) {
        return ngx_js_wrap_location_ex(ctx, best_noregex_clcf, srv_op);
    }

    return JS_NULL;
}


/* ================================================================== */
/* F1 — nginx.cycle.sockets[] / nginx.http.sockets[] HTTP entries     */
/* ================================================================== */

void
ngx_js_http_socket_entries(JSContext *ctx, JSValue arr,
    ngx_cycle_t *cycle, ngx_uint_t *idx)
{
    ngx_uint_t                 li, ai, ji, vi;
    ngx_listening_t           *ls;
    ngx_http_port_t           *hport;
    ngx_http_addr_conf_t      *ac;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_http_server_name_t    *sn;
    JSValue                    entry, names_arr, name_map, srv_obj, fn;
    ngx_js_http_listener_state_t *st;
    uint32_t                   js_handle;
    ngx_uint_t                 nnames;
    u_char                    *lc_key;
    /* deduplication of cscf pointers across addrs in static sockets */
    ngx_http_core_srv_conf_t  *seen_cscf[64];
    ngx_uint_t                 n_seen;
    ngx_uint_t                 already_seen;

    if (cycle->listening.nelts == 0) {
        return;
    }

    ls = cycle->listening.elts;

    for (li = 0; li < cycle->listening.nelts; li++) {

        if (ls[li].handler != ngx_http_init_connection) {
            continue;
        }

        entry     = JS_NewObject(ctx);
        names_arr = JS_NewArray(ctx);
        name_map  = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, entry, "address",
                          JS_NewStringLen(ctx,
                              (const char *) ls[li].addr_text.data,
                              ls[li].addr_text.len));
        JS_SetPropertyStr(ctx, entry, "fd",
                          JS_NewInt32(ctx, (int32_t) ls[li].fd));
        JS_SetPropertyStr(ctx, entry, "type",
                          JS_NewString(ctx,
                              ls[li].type == SOCK_DGRAM ? "udp" : "tcp"));
        JS_SetPropertyStr(ctx, entry, "open",
                          JS_NewBool(ctx, (int) ls[li].open));
        JS_SetPropertyStr(ctx, entry, "reuseport",
                          JS_NewBool(ctx, (int) ls[li].reuseport));
        JS_SetPropertyStr(ctx, entry, "wildcard",
                          JS_NewBool(ctx, (int) ls[li].wildcard));
        JS_SetPropertyStr(ctx, entry, "protocol",
                          JS_NewString(ctx, "http"));

        /* detect JS-created socket */
        js_handle = NGX_JS_SOCKET_REG_MAX;
        for (ji = 0; ji < NGX_JS_SOCKET_REG_MAX; ji++) {
            if (ngx_js_socket_reg[ji] != NULL
                && ngx_js_socket_reg[ji]->fd == ls[li].fd)
            {
                js_handle = (uint32_t) ji;
                break;
            }
        }
        JS_SetPropertyStr(ctx, entry, "jsCreated",
                          JS_NewBool(ctx,
                              (int) (js_handle < NGX_JS_SOCKET_REG_MAX)));
        JS_SetPropertyStr(ctx, entry, "jsHandle",
                          JS_NewInt32(ctx,
                              js_handle < NGX_JS_SOCKET_REG_MAX
                                  ? (int32_t) js_handle : -1));

        nnames  = 0;
        n_seen  = 0;

        if (js_handle < NGX_JS_SOCKET_REG_MAX) {
            /* JS-created: look up HTTP listener registry */
            st = NULL;
            for (ji = 0; ji < NGX_JS_LISTENER_REG_MAX; ji++) {
                if (ngx_js_listener_reg[ji] != NULL
                    && ngx_js_listener_reg[ji]->socket_handle == js_handle)
                {
                    st = ngx_js_listener_reg[ji];
                    break;
                }
            }

            if (st != NULL && st->default_server != NULL) {
                /* default server names */
                cscf = st->default_server;
                sn   = cscf->server_names.elts;
                for (ji = 0; ji < cscf->server_names.nelts; ji++) {
                    JS_SetPropertyUint32(ctx, names_arr, nnames++,
                        JS_NewStringLen(ctx,
                            (const char *) sn[ji].name.data,
                            sn[ji].name.len));
                    lc_key = js_malloc(ctx, sn[ji].name.len + 1);
                    if (lc_key) {
                        ngx_strlow(lc_key, sn[ji].name.data, sn[ji].name.len);
                        lc_key[sn[ji].name.len] = '\0';
                        srv_obj = ngx_js_wrap_server(ctx, cscf, cycle);
                        JS_SetPropertyStr(ctx, name_map,
                                          (const char *) lc_key, srv_obj);
                        js_free(ctx, lc_key);
                    }
                }

                /* virtual server names */
                for (vi = 0; vi < st->nvservers; vi++) {
                    cscf = st->vservers[vi];
                    sn   = cscf->server_names.elts;
                    for (ji = 0; ji < cscf->server_names.nelts; ji++) {
                        JS_SetPropertyUint32(ctx, names_arr, nnames++,
                            JS_NewStringLen(ctx,
                                (const char *) sn[ji].name.data,
                                sn[ji].name.len));
                        lc_key = js_malloc(ctx, sn[ji].name.len + 1);
                        if (lc_key) {
                            ngx_strlow(lc_key, sn[ji].name.data,
                                       sn[ji].name.len);
                            lc_key[sn[ji].name.len] = '\0';
                            srv_obj = ngx_js_wrap_server(ctx, cscf, cycle);
                            JS_SetPropertyStr(ctx, name_map,
                                              (const char *) lc_key, srv_obj);
                            js_free(ctx, lc_key);
                        }
                    }
                }
            }

        } else if (ls[li].servers != NULL) {
            /* static socket: walk hport->addrs[], dedup by cscf */
            hport = (ngx_http_port_t *) ls[li].servers;

            for (ai = 0; ai < hport->naddrs; ai++) {
#if (NGX_HAVE_INET6)
                if (ls[li].sockaddr->sa_family == AF_INET6) {
                    ac = &((ngx_http_in6_addr_t *) hport->addrs)[ai].conf;
                } else {
#endif
                    ac = &((ngx_http_in_addr_t *) hport->addrs)[ai].conf;
#if (NGX_HAVE_INET6)
                }
#endif

                cscf = ac->default_server;
                if (cscf == NULL) {
                    continue;
                }

                already_seen = 0;
                for (vi = 0; vi < n_seen; vi++) {
                    if (seen_cscf[vi] == cscf) {
                        already_seen = 1;
                        break;
                    }
                }
                if (already_seen) {
                    continue;
                }
                if (n_seen < 64) {
                    seen_cscf[n_seen++] = cscf;
                }

                sn = cscf->server_names.elts;
                for (ji = 0; ji < cscf->server_names.nelts; ji++) {
                    JS_SetPropertyUint32(ctx, names_arr, nnames++,
                        JS_NewStringLen(ctx,
                            (const char *) sn[ji].name.data,
                            sn[ji].name.len));
                    lc_key = js_malloc(ctx, sn[ji].name.len + 1);
                    if (lc_key) {
                        ngx_strlow(lc_key, sn[ji].name.data, sn[ji].name.len);
                        lc_key[sn[ji].name.len] = '\0';
                        srv_obj = ngx_js_wrap_server(ctx, cscf, cycle);
                        JS_SetPropertyStr(ctx, name_map,
                                          (const char *) lc_key, srv_obj);
                        js_free(ctx, lc_key);
                    }
                }
            }
        }

        JS_SetPropertyStr(ctx, entry, "serverNames", names_arr);

        fn = JS_NewCFunctionData(ctx, ngx_js_socket_server_by_name_fn,
                                 1, 0, 1, &name_map);
        JS_FreeValue(ctx, name_map);
        JS_SetPropertyStr(ctx, entry, "serverByName", fn);

        JS_SetPropertyUint32(ctx, arr, (*idx)++, entry);
    }
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
    "pass",
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
 * ngx_js_settable_props(ctx, obj) — return a JS array of settable property
 * name strings for a known COM object.  Returns an empty array for objects
 * whose class is not in the settable table.
 *
 * Handles: NginxLocation, NginxProxy, NginxGzip, NginxHeaders, NginxRewrite,
 *          NginxPeer, NginxRrPeer.
 */
JSValue
ngx_js_settable_props(JSContext *ctx, JSValueConst obj)
{
    JSClassID             cid;
    const char * const   *names;
    JSValue               arr;
    uint32_t              i;

    cid   = JS_GetClassID(obj);
    names = NULL;

    if (cid == ngx_js_location_class_id) {
        names = ngx_js_loc_snap_props;

    } else if (cid == ngx_js_proxy_class_id) {
        names = ngx_js_proxy_snap_props;

    } else if (cid == ngx_js_gzip_class_id) {
        names = ngx_js_gzip_snap_props;

    } else if (cid == ngx_js_headers_class_id) {
        names = ngx_js_headers_snap_props;

    } else if (cid == ngx_js_rewrite_class_id) {
        names = ngx_js_rewrite_snap_props;

    } else if (cid == ngx_js_peer_class_id
               || cid == ngx_js_rr_peer_class_id) {
        names = ngx_js_peer_settable_props();
    }

    arr = JS_NewArray(ctx);

    if (JS_IsException(arr) || names == NULL) {
        return arr;
    }

    for (i = 0; names[i] != NULL; i++) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, names[i]));
    }

    return arr;
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

    /* Save cycle for addServer() when nginx.http.servers[] is empty */
    ngx_js_http_cycle = cycle;

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

    /*
     * When nginx.conf has an http{} block but no server{} blocks, build a
     * synthetic default server conf so that addServer() has something to
     * copy defaults from (connection pool sizes, header timeouts, etc.).
     */
    if (cmcf->servers.nelts == 0) {
        ngx_js_default_cscf =
            ngx_js_build_default_server_conf(cycle, http_ctx);
        if (ngx_js_default_cscf == NULL) {
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "js: failed to build default server conf;"
                          " addServer() will fail when servers[] is empty");
        }
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

    /* nginx.http.match(uri [, serverName]) */
    JS_SetPropertyStr(ctx, http_obj, "match",
                      JS_NewCFunction(ctx, ngx_js_http_fn_match,
                                      "match", 1));

    /* nginx.http.upstreams[] — delegated to upstream COM */
    if (ngx_js_upstream_com_install(ctx, http_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, http_obj);
        return NGX_ERROR;
    }

    /* nginx.http.attach(sock) — Stage 52 Phase B */
    if (ngx_js_listener_install(ctx, http_obj) != NGX_OK) {
        JS_FreeValue(ctx, http_obj);
        return NGX_ERROR;
    }

    /* nginx.http.sockets[] — F1 */
    {
        JSValue     socks_arr;
        ngx_uint_t  sidx = 0;

        socks_arr = JS_NewArray(ctx);
        if (!JS_IsException(socks_arr)) {
            ngx_js_http_socket_entries(ctx, socks_arr, cycle, &sidx);
            JS_SetPropertyStr(ctx, http_obj, "sockets", socks_arr);
        }
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
