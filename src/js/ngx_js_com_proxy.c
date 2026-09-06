
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 4: proxy_pass / ngx_http_proxy_loc_conf_t.
 *
 * Exposes location.proxy as a NginxProxy object with properties:
 *   pass                string | null | "dynamic"  (r/w: set to upstream name)
 *   httpVersion         "1.0" | "1.1"
 *   connectTimeout      number (ms)
 *   sendTimeout         number (ms)
 *   readTimeout         number (ms)
 *   buffering           bool
 *   requestBuffering    bool
 *   interceptErrors     bool
 *   bufferSize          number (bytes)
 *   buffers             {num, size}
 *   nextUpstream        string[]
 *   nextUpstreamTries   number
 *   nextUpstreamTimeout number (ms)
 *   setHeader           [{key, value}]
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_proxy_module.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"

/* Forward declaration from ngx_js_com_proxy_cache.c */
#if (NGX_HTTP_CACHE)
JSValue  ngx_js_wrap_proxy_cache(JSContext *ctx,
    ngx_http_upstream_conf_t *uconf);
#endif


typedef struct {
    ngx_http_proxy_loc_conf_t  *plcf;
} ngx_js_proxy_opaque_t;


static void
ngx_js_proxy_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_proxy_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_proxy_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_proxy_class = {
    "NginxProxy",
    .finalizer = ngx_js_proxy_finalizer
};


/*
 * Magic values for ngx_js_proxy_get_core:
 *   0  — pass
 *   1  — httpVersion
 *   2  — connectTimeout
 *   3  — sendTimeout
 *   4  — readTimeout
 *   5  — buffering
 *   6  — requestBuffering
 *   7  — interceptErrors
 *   8  — bufferSize
 *   9  — nextUpstreamTries
 *   10 — nextUpstreamTimeout
 */
static JSValue
ngx_js_proxy_get_core(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_proxy_opaque_t      *op;
    ngx_http_proxy_loc_conf_t  *plcf;
    ngx_http_upstream_conf_t   *ucf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    plcf = op->plcf;
    ucf  = &plcf->upstream;

    switch (magic) {

    case 0: /* pass */
        if (plcf->url.len > 0) {
            return JS_NewStringLen(ctx, (const char *) plcf->url.data,
                                   plcf->url.len);
        }
        if (plcf->proxy_lengths != NULL) {
            return JS_NewString(ctx, "dynamic");
        }
        return JS_NULL;

    case 1: /* httpVersion */
        return JS_NewString(ctx,
                            plcf->http_version == NGX_HTTP_VERSION_10
                            ? "1.0" : "1.1");

    case 2:  return JS_NewInt64(ctx, (int64_t) ucf->connect_timeout);
    case 3:  return JS_NewInt64(ctx, (int64_t) ucf->send_timeout);
    case 4:  return JS_NewInt64(ctx, (int64_t) ucf->read_timeout);
    case 5:  return JS_NewBool(ctx, ucf->buffering);
    case 6:  return JS_NewBool(ctx, ucf->request_buffering);
    case 7:  return JS_NewBool(ctx, ucf->intercept_errors);
    case 8:  return JS_NewInt64(ctx, (int64_t) ucf->buffer_size);
    case 9:  return JS_NewInt64(ctx, (int64_t) ucf->next_upstream_tries);
    case 10: return JS_NewInt64(ctx, (int64_t) ucf->next_upstream_timeout);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_proxy_set_core(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_proxy_opaque_t          *op;
    ngx_http_upstream_conf_t       *ucf;
    int64_t                         n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) { return JS_EXCEPTION; }

    ucf = &op->plcf->upstream;

    switch (magic) {
    case 0: /* pass — upstream name (e.g. "backend" or "http://backend") */
    {
        const char                     *cstr;
        size_t                          slen;
        const char                     *name;
        size_t                          nlen;
        u_char                         *data;
        ngx_http_conf_ctx_t            *http_ctx;
        ngx_http_upstream_main_conf_t  *umcf;
        ngx_http_upstream_srv_conf_t  **uscfp;
        ngx_uint_t                      i;

        cstr = JS_ToCStringLen(ctx, &slen, val);
        if (!cstr) { return JS_EXCEPTION; }

        /* Strip optional scheme prefix to get the bare upstream name */
        name = cstr;
        nlen = slen;
        if (nlen > 7 && ngx_strncmp(name, "http://", 7) == 0) {
            name += 7; nlen -= 7;
        } else if (nlen > 8 && ngx_strncmp(name, "https://", 8) == 0) {
            name += 8; nlen -= 8;
        }
        /* Strip trailing slash */
        while (nlen > 0 && name[nlen - 1] == '/') { nlen--; }

        http_ctx = (ngx_http_conf_ctx_t *) ngx_cycle->conf_ctx[ngx_http_module.index];
        umcf     = http_ctx->main_conf[ngx_http_upstream_module.ctx_index];
        uscfp    = umcf->upstreams.elts;

        for (i = 0; i < umcf->upstreams.nelts; i++) {
            if (!(uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE)) { continue; }
            if (uscfp[i]->host.len == nlen
                && ngx_strncasecmp(uscfp[i]->host.data,
                                   (u_char *) name, nlen) == 0)
            {
                data = ngx_pnalloc(ngx_js_conf_cycle()->pool, slen + 1);
                if (data == NULL) {
                    JS_FreeCString(ctx, cstr);
                    return JS_ThrowOutOfMemory(ctx);
                }
                ngx_memcpy(data, cstr, slen + 1);
                JS_FreeCString(ctx, cstr);

                op->plcf->upstream.upstream = uscfp[i];
                op->plcf->url.data          = data;
                op->plcf->url.len           = slen;
                return JS_UNDEFINED;
            }
        }

        JS_ThrowTypeError(ctx, "proxy.pass: upstream not found");
        JS_FreeCString(ctx, cstr);
        return JS_EXCEPTION;
    }

    case 1: /* httpVersion — "1.0" or "1.1" */
    {
        const char  *s;
        size_t       slen;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        if (slen == 3 && ngx_strncmp(s, "1.0", 3) == 0) {
            op->plcf->http_version = NGX_HTTP_VERSION_10;
        } else if (slen == 3 && ngx_strncmp(s, "1.1", 3) == 0) {
            op->plcf->http_version = NGX_HTTP_VERSION_11;
        } else {
            JS_FreeCString(ctx, s);
            return JS_ThrowTypeError(ctx,
                        "httpVersion must be \"1.0\" or \"1.1\"");
        }

        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;
    }
    case 2:  /* connectTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->connect_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 3:  /* sendTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->send_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 4:  /* readTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->read_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 5:  /* buffering */
        ucf->buffering = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 6:  /* requestBuffering */
        ucf->request_buffering = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 7:  /* interceptErrors */
        ucf->intercept_errors = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 8:  /* bufferSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->buffer_size = (size_t) n;
        return JS_UNDEFINED;
    case 9:  /* nextUpstreamTries */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->next_upstream_tries = (ngx_uint_t) n;
        return JS_UNDEFINED;
    case 10: /* nextUpstreamTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->next_upstream_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * Snapshot-aware getter wrapper: if read_mode=LOCAL and the opaque conf
 * belongs to the current request's location, temporarily redirect op->plcf
 * to the snapshotted copy before delegating to ngx_js_proxy_get_core.
 */
static JSValue
ngx_js_proxy_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_proxy_opaque_t     *op;
    ngx_js_worker_t           *w;
    ngx_http_request_t        *r;
    ngx_js_req_ctx_t          *rctx;
    ngx_http_proxy_loc_conf_t *orig_plcf;
    JSValue                    ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) { return JS_EXCEPTION; }

    w    = JS_GetContextOpaque(ctx);
    r    = (w != NULL) ? w->current_request : NULL;
    rctx = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;

    if (rctx != NULL && (rctx->read_mode & NGX_JS_WRITE_LOCAL)
        && ngx_js_is_own_conf(r, rctx, ngx_http_proxy_module.ctx_index,
                              op->plcf))
    {
        orig_plcf = op->plcf;
        op->plcf  = r->loc_conf[ngx_http_proxy_module.ctx_index];
        ret       = ngx_js_proxy_get_core(ctx, this_val, magic);
        op->plcf  = orig_plcf;
        return ret;
    }

    return ngx_js_proxy_get_core(ctx, this_val, magic);
}


/*
 * Snapshot-aware setter wrapper: writes to the local snapshot and/or the
 * global conf depending on rctx->write_mode.
 */
static JSValue
ngx_js_proxy_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_proxy_opaque_t     *op;
    ngx_js_worker_t           *w;
    ngx_http_request_t        *r;
    ngx_js_req_ctx_t          *rctx;
    ngx_http_proxy_loc_conf_t *orig_plcf;
    uint32_t                   wmode;
    int                        need_global, need_local;
    JSValue                    ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) { return JS_EXCEPTION; }

    w     = JS_GetContextOpaque(ctx);
    r     = (w != NULL) ? w->current_request : NULL;
    rctx  = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;
    wmode = (rctx != NULL) ? rctx->write_mode : NGX_JS_WRITE_GLOBAL;

    need_global = (wmode & NGX_JS_WRITE_GLOBAL) != 0;
    need_local  = (wmode & NGX_JS_WRITE_LOCAL)  != 0;

    if (need_local) {
        if (!ngx_js_is_own_conf(r, rctx,
                ngx_http_proxy_module.ctx_index, op->plcf))
        {
            need_local = 0;  /* cross-location: global only */
        }
    }

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_proxy_module,
                sizeof(ngx_http_proxy_loc_conf_t)) != NGX_OK) {
            return JS_ThrowInternalError(ctx, "proxy snapshot alloc failed");
        }
        orig_plcf = op->plcf;
        op->plcf  = r->loc_conf[ngx_http_proxy_module.ctx_index];
        ret       = ngx_js_proxy_set_core(ctx, this_val, val, magic);
        op->plcf  = orig_plcf;
        if (!need_global || JS_IsException(ret)) { return ret; }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        return ngx_js_proxy_set_core(ctx, this_val, val, magic);
    }

    return JS_UNDEFINED;
}


/*
 * proxy.buffers — {num: N, size: S}.
 * Maps to proxy_buffers directive (ngx_bufs_t).
 */
static JSValue
ngx_js_proxy_get_buffers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_proxy_opaque_t  *op;
    JSValue                 obj;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "num",
                      JS_NewInt32(ctx,
                          (int32_t) op->plcf->upstream.bufs.num));
    JS_SetPropertyStr(ctx, obj, "size",
                      JS_NewInt64(ctx,
                          (int64_t) op->plcf->upstream.bufs.size));
    return obj;
}


/*
 * proxy.nextUpstream — array of condition name strings decoded from the
 * next_upstream bitmask (proxy_next_upstream directive).
 */
static JSValue
ngx_js_proxy_get_next_upstream(JSContext *ctx, JSValueConst this_val)
{
    static const struct {
        ngx_uint_t   flag;
        const char  *name;
    } flags[] = {
        { NGX_HTTP_UPSTREAM_FT_ERROR,          "error"          },
        { NGX_HTTP_UPSTREAM_FT_TIMEOUT,        "timeout"        },
        { NGX_HTTP_UPSTREAM_FT_INVALID_HEADER, "invalid_header" },
        { NGX_HTTP_UPSTREAM_FT_HTTP_500,       "http_500"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_502,       "http_502"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_503,       "http_503"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_504,       "http_504"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_403,       "http_403"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_404,       "http_404"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_429,       "http_429"       },
        { NGX_HTTP_UPSTREAM_FT_NON_IDEMPOTENT, "non_idempotent" },
        { 0, NULL }
    };

    ngx_js_proxy_opaque_t  *op;
    JSValue                 arr;
    ngx_uint_t              mask;
    uint32_t                idx;
    ngx_uint_t              i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr  = JS_NewArray(ctx);
    mask = op->plcf->upstream.next_upstream;

    if (mask & NGX_HTTP_UPSTREAM_FT_OFF) {
        JS_SetPropertyUint32(ctx, arr, 0, JS_NewString(ctx, "off"));
        return arr;
    }

    idx = 0;

    for (i = 0; flags[i].name != NULL; i++) {
        if (mask & flags[i].flag) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, flags[i].name));
        }
    }

    return arr;
}


/*
 * proxy.setHeader — [{key, value}] from proxy_set_header directives.
 * Maps to headers_source (ngx_array_t * of ngx_keyval_t).
 */
static JSValue
ngx_js_proxy_get_set_header(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_proxy_opaque_t  *op;
    JSValue                 arr, obj;
    ngx_keyval_t           *kv;
    ngx_uint_t              i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->plcf->headers_source == NULL) {
        return arr;
    }

    kv = op->plcf->headers_source->elts;

    for (i = 0; i < op->plcf->headers_source->nelts; i++) {
        obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "key",
                          JS_NewStringLen(ctx,
                                          (const char *) kv[i].key.data,
                                          kv[i].key.len));
        JS_SetPropertyStr(ctx, obj, "value",
                          JS_NewStringLen(ctx,
                                          (const char *) kv[i].value.data,
                                          kv[i].value.len));
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


/*
 * proxy.cache — NginxProxyCache wrapping the upstream cache conf,
 * or null when no proxy_cache directive is configured.
 */
static JSValue
ngx_js_proxy_get_cache(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_proxy_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

#if (NGX_HTTP_CACHE)
    /* cache is active when a cache zone is assigned */
    if (op->plcf->upstream.cache_zone == NULL) {
        return JS_NULL;
    }

    return ngx_js_wrap_proxy_cache(ctx, &op->plcf->upstream);
#else
    return JS_NULL;
#endif
}


static const JSCFunctionListEntry ngx_js_proxy_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("pass",                ngx_js_proxy_get, ngx_js_proxy_set,  0),
    JS_CGETSET_MAGIC_DEF("httpVersion",         ngx_js_proxy_get, ngx_js_proxy_set,  1),
    JS_CGETSET_MAGIC_DEF("connectTimeout",      ngx_js_proxy_get, ngx_js_proxy_set,  2),
    JS_CGETSET_MAGIC_DEF("sendTimeout",         ngx_js_proxy_get, ngx_js_proxy_set,  3),
    JS_CGETSET_MAGIC_DEF("readTimeout",         ngx_js_proxy_get, ngx_js_proxy_set,  4),
    JS_CGETSET_MAGIC_DEF("buffering",           ngx_js_proxy_get, ngx_js_proxy_set,  5),
    JS_CGETSET_MAGIC_DEF("requestBuffering",    ngx_js_proxy_get, ngx_js_proxy_set,  6),
    JS_CGETSET_MAGIC_DEF("interceptErrors",     ngx_js_proxy_get, ngx_js_proxy_set,  7),
    JS_CGETSET_MAGIC_DEF("bufferSize",          ngx_js_proxy_get, ngx_js_proxy_set,  8),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTries",   ngx_js_proxy_get, ngx_js_proxy_set,  9),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTimeout", ngx_js_proxy_get, ngx_js_proxy_set, 10),
    JS_CGETSET_DEF       ("buffers",            ngx_js_proxy_get_buffers,       NULL),
    JS_CGETSET_DEF       ("nextUpstream",       ngx_js_proxy_get_next_upstream, NULL),
    JS_CGETSET_DEF       ("setHeader",          ngx_js_proxy_get_set_header,    NULL),
    JS_CGETSET_DEF       ("cache",              ngx_js_proxy_get_cache,         NULL),
};


ngx_int_t
ngx_js_proxy_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_proxy_proto_funcs,
                               countof(ngx_js_proxy_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_proxy_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_proxy(JSContext *ctx, ngx_http_proxy_loc_conf_t *plcf)
{
    JSValue                obj;
    ngx_js_proxy_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_proxy_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->plcf = plcf;

    obj = JS_NewObjectClass(ctx, ngx_js_proxy_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_proxy_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_proxy_class_id, &ngx_js_proxy_class) < 0
           ? NGX_ERROR : NGX_OK;
}
