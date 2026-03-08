
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 4: proxy_pass / ngx_http_proxy_loc_conf_t.
 *
 * Exposes location.proxy as a NginxProxy object with read-only properties:
 *   pass                string | null | "dynamic"
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
 * Magic values for ngx_js_proxy_get:
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
ngx_js_proxy_get(JSContext *ctx, JSValueConst this_val, int magic)
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


static const JSCFunctionListEntry ngx_js_proxy_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("pass",                ngx_js_proxy_get, NULL,  0),
    JS_CGETSET_MAGIC_DEF("httpVersion",         ngx_js_proxy_get, NULL,  1),
    JS_CGETSET_MAGIC_DEF("connectTimeout",      ngx_js_proxy_get, NULL,  2),
    JS_CGETSET_MAGIC_DEF("sendTimeout",         ngx_js_proxy_get, NULL,  3),
    JS_CGETSET_MAGIC_DEF("readTimeout",         ngx_js_proxy_get, NULL,  4),
    JS_CGETSET_MAGIC_DEF("buffering",           ngx_js_proxy_get, NULL,  5),
    JS_CGETSET_MAGIC_DEF("requestBuffering",    ngx_js_proxy_get, NULL,  6),
    JS_CGETSET_MAGIC_DEF("interceptErrors",     ngx_js_proxy_get, NULL,  7),
    JS_CGETSET_MAGIC_DEF("bufferSize",          ngx_js_proxy_get, NULL,  8),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTries",   ngx_js_proxy_get, NULL,  9),
    JS_CGETSET_MAGIC_DEF("nextUpstreamTimeout", ngx_js_proxy_get, NULL, 10),
    JS_CGETSET_DEF       ("buffers",            ngx_js_proxy_get_buffers,       NULL),
    JS_CGETSET_DEF       ("nextUpstream",       ngx_js_proxy_get_next_upstream, NULL),
    JS_CGETSET_DEF       ("setHeader",          ngx_js_proxy_get_set_header,    NULL),
};


JSValue
ngx_js_wrap_proxy(JSContext *ctx, ngx_http_proxy_loc_conf_t *plcf)
{
    JSValue                obj, proto;
    ngx_js_proxy_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_proxy_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->plcf = plcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_proxy_proto_funcs,
                               countof(ngx_js_proxy_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_proxy_class_id);
    JS_FreeValue(ctx, proto);

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
