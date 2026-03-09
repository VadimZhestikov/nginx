
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


/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static JSValue ngx_js_wrap_location(JSContext *ctx,
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
 * Magic values for ngx_js_location_get / ngx_js_location_set:
 *   0  — path              (r/o: location name/pattern)
 *   1  — root              (r/w: document root)
 *   2  — handler           (r/w: JS content handler function)
 *   3  — internal          (r/o: bool)
 *   4  — sendfile          (r/o: bool)
 *   5  — tcpNopush         (r/o: bool)
 *   6  — tcpNodelay        (r/o: bool)
 *   7  — etag              (r/o: bool)
 *   8  — keepaliveTimeout  (r/o: ms)
 *   9  — keepaliveRequests (r/o: count)
 *   10 — clientMaxBodySize (r/o: bytes)
 *   11 — clientBodyTimeout (r/o: ms)
 *   12 — sendTimeout       (r/o: ms)
 *   13 — defaultType       (r/o: string)
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

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf = op->clcf;

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
         * Location getters are only invoked during init_conf script
         * evaluation, when the context opaque is the current cycle pointer
         * (set by ngx_js_com_init).  Use it to reach the correct main conf.
         */
        cycle = JS_GetContextOpaque(ctx);
        csmcf = ngx_http_cycle_get_module_main_conf(cycle,
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


static JSValue
ngx_js_location_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_location_opaque_t  *op;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_cycle_t               *cycle;
    const char                *cstr;
    size_t                     len;
    u_char                    *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_location_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf  = op->clcf;
    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);

    switch (magic) {
    case 1: /* root */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(cycle->pool, len + 1);
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
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_location_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("path",             ngx_js_location_get, NULL,                 0),
    JS_CGETSET_MAGIC_DEF("root",             ngx_js_location_get, ngx_js_location_set,  1),
    JS_CGETSET_MAGIC_DEF("handler",          ngx_js_location_get, ngx_js_location_set,  2),
    JS_CGETSET_MAGIC_DEF("internal",         ngx_js_location_get, NULL,                 3),
    JS_CGETSET_MAGIC_DEF("sendfile",         ngx_js_location_get, NULL,                 4),
    JS_CGETSET_MAGIC_DEF("tcpNopush",        ngx_js_location_get, NULL,                 5),
    JS_CGETSET_MAGIC_DEF("tcpNodelay",       ngx_js_location_get, NULL,                 6),
    JS_CGETSET_MAGIC_DEF("etag",             ngx_js_location_get, NULL,                 7),
    JS_CGETSET_MAGIC_DEF("keepaliveTimeout", ngx_js_location_get, NULL,                 8),
    JS_CGETSET_MAGIC_DEF("keepaliveRequests",ngx_js_location_get, NULL,                 9),
    JS_CGETSET_MAGIC_DEF("clientMaxBodySize",ngx_js_location_get, NULL,                10),
    JS_CGETSET_MAGIC_DEF("clientBodyTimeout",ngx_js_location_get, NULL,                11),
    JS_CGETSET_MAGIC_DEF("sendTimeout",      ngx_js_location_get, NULL,                12),
    JS_CGETSET_MAGIC_DEF("defaultType",      ngx_js_location_get, NULL,                13),
    JS_CGETSET_MAGIC_DEF("alias",            ngx_js_location_get, NULL,                14),
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
    JS_CGETSET_DEF       ("errorPage",       ngx_js_location_get_error_page, NULL),
};


static JSValue
ngx_js_wrap_location(JSContext *ctx, ngx_http_core_loc_conf_t *clcf)
{
    JSValue                    obj, proto;
    ngx_js_location_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_location_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->clcf = clcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_location_proto_funcs,
                               countof(ngx_js_location_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_location_class_id);
    JS_FreeValue(ctx, proto);

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
} ngx_js_server_opaque_t;


static void
ngx_js_server_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_server_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_server_class_id);
    if (op) {
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
    ngx_cycle_t               *cycle;
    const char                *cstr;
    size_t                     len;
    u_char                    *old_data, *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    cscf  = op->cscf;
    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);

    switch (magic) {
    case 1: /* root — server's default location */
        cstr = JS_ToCString(ctx, val);
        if (!cstr) {
            return JS_EXCEPTION;
        }

        len  = ngx_strlen(cstr);
        data = ngx_pnalloc(cycle->pool, len + 1);
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
 * nginx.http.servers[i].locations — JS Array of NginxLocation objects.
 */
static JSValue
ngx_js_server_get_locations(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_server_opaque_t    *op;
    ngx_http_core_loc_conf_t  *clcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_server_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    clcf = op->cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    return ngx_js_build_locations(ctx, clcf);
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
    JS_CGETSET_MAGIC_DEF("locations",                ngx_js_server_get_locations,             NULL,              0),
    JS_CGETSET_MAGIC_DEF("clientHeaderBufferSize",   ngx_js_server_get,                       NULL,              2),
    JS_CGETSET_MAGIC_DEF("clientHeaderTimeout",      ngx_js_server_get,                       NULL,              3),
    JS_CGETSET_MAGIC_DEF("ignoreInvalidHeaders",     ngx_js_server_get,                       NULL,              4),
    JS_CGETSET_MAGIC_DEF("mergeSlashes",             ngx_js_server_get,                       NULL,              5),
    JS_CGETSET_MAGIC_DEF("underscoresInHeaders",     ngx_js_server_get,                       NULL,              6),
    JS_CGETSET_MAGIC_DEF("serverTokens",             ngx_js_server_get,                       NULL,              7),
    JS_CGETSET_DEF       ("largeClientHeaderBuffers", ngx_js_server_get_large_client_hdr_bufs, NULL),
    JS_CGETSET_DEF       ("ssl",                      ngx_js_server_get_ssl,                   NULL),
};


static JSValue
ngx_js_wrap_server(JSContext *ctx, ngx_http_core_srv_conf_t *cscf,
    ngx_cycle_t *cycle)
{
    JSValue                    obj, proto;
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

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_server_proto_funcs,
                               countof(ngx_js_server_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_server_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_http_com_install                                              */
/* ------------------------------------------------------------------ */

/*
 * Register the NginxServer and NginxLocation classes with this runtime.
 * Called by ngx_js_com_register_classes() from ngx_js_com.c.
 */
static ngx_int_t
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
    JSRuntime                   *rt;
    JSValue                      http_obj, servers_arr;
    ngx_http_conf_ctx_t         *http_ctx;
    ngx_http_core_main_conf_t   *cmcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_uint_t                   i;

    rt = JS_GetRuntime(ctx);

    if (ngx_js_http_register_classes(rt) != NGX_OK) {
        return NGX_ERROR;
    }

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

    /* nginx.http.upstreams[] — delegated to upstream COM */
    if (ngx_js_upstream_com_install(ctx, http_obj, cycle) != NGX_OK) {
        JS_FreeValue(ctx, http_obj);
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);

    return NGX_OK;
}
