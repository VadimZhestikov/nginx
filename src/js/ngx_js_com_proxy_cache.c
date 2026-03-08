
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 9: proxy cache location configuration.
 *
 * Exposes location.proxy.cache as a NginxProxyCache object (or null
 * when no proxy_cache directive is set for the location):
 *
 *   zone              string    shared-memory zone name
 *   minUses           number    proxy_cache_min_uses
 *   methods           string[]  cacheable HTTP methods
 *   lock              bool      proxy_cache_lock
 *   lockTimeout       number    proxy_cache_lock_timeout (ms)
 *   lockAge           number    proxy_cache_lock_age (ms)
 *   revalidate        bool      proxy_cache_revalidate
 *   convertHead       bool      proxy_cache_convert_head
 *   backgroundUpdate  bool      proxy_cache_background_update
 *   valid             object[]  [{status, seconds}] from proxy_cache_valid
 *   useStale          string[]  proxy_cache_use_stale flags
 *
 * The entire file is wrapped in #if (NGX_HTTP_CACHE).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


#if (NGX_HTTP_CACHE)

#include <ngx_http_cache.h>


typedef struct {
    ngx_http_upstream_conf_t  *uconf;
} ngx_js_proxy_cache_opaque_t;


static void
ngx_js_proxy_cache_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_proxy_cache_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_proxy_cache_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_proxy_cache_class = {
    "NginxProxyCache",
    .finalizer = ngx_js_proxy_cache_finalizer
};


/*
 * Magic values for ngx_js_proxy_cache_get:
 *   0 — minUses
 *   1 — lock
 *   2 — lockTimeout       (ms)
 *   3 — lockAge           (ms)
 *   4 — revalidate
 *   5 — convertHead
 *   6 — backgroundUpdate
 */
static JSValue
ngx_js_proxy_cache_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_proxy_cache_opaque_t  *op;
    ngx_http_upstream_conf_t     *u;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_cache_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    u = op->uconf;

    switch (magic) {
    case 0: return JS_NewInt64(ctx,  (int64_t) u->cache_min_uses);
    case 1: return JS_NewBool(ctx,   (int) u->cache_lock);
    case 2: return JS_NewInt64(ctx,  (int64_t) u->cache_lock_timeout);
    case 3: return JS_NewInt64(ctx,  (int64_t) u->cache_lock_age);
    case 4: return JS_NewBool(ctx,   (int) u->cache_revalidate);
    case 5: return JS_NewBool(ctx,   (int) u->cache_convert_head);
    case 6: return JS_NewBool(ctx,   (int) u->cache_background_update);
    }

    return JS_UNDEFINED;
}


/*
 * cache.zone — the shared-memory cache zone name string.
 */
static JSValue
ngx_js_proxy_cache_get_zone(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_proxy_cache_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_cache_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->uconf->cache_zone == NULL) {
        return JS_NULL;
    }

    return JS_NewStringLen(ctx,
                           (const char *) op->uconf->cache_zone->shm.name.data,
                           op->uconf->cache_zone->shm.name.len);
}


/*
 * cache.methods — string[] of cacheable HTTP method names.
 */
static JSValue
ngx_js_proxy_cache_get_methods(JSContext *ctx, JSValueConst this_val)
{
    static const struct {
        ngx_uint_t   flag;
        const char  *name;
    } methods[] = {
        { NGX_HTTP_GET,  "GET"  },
        { NGX_HTTP_HEAD, "HEAD" },
        { NGX_HTTP_POST, "POST" },
        { 0, NULL }
    };

    ngx_js_proxy_cache_opaque_t  *op;
    JSValue                       arr;
    ngx_uint_t                    mask, i;
    uint32_t                      idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_cache_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr  = JS_NewArray(ctx);
    mask = op->uconf->cache_methods;
    idx  = 0;

    for (i = 0; methods[i].name != NULL; i++) {
        if (mask & methods[i].flag) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, methods[i].name));
        }
    }

    return arr;
}


/*
 * cache.valid — [{status: N, seconds: N}, ...] from proxy_cache_valid.
 */
static JSValue
ngx_js_proxy_cache_get_valid(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_proxy_cache_opaque_t  *op;
    JSValue                       arr, entry;
    ngx_http_cache_valid_t       *cv;
    ngx_uint_t                    i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_cache_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->uconf->cache_valid == NULL || op->uconf->cache_valid->nelts == 0) {
        return arr;
    }

    cv = op->uconf->cache_valid->elts;

    for (i = 0; i < op->uconf->cache_valid->nelts; i++) {
        entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "status",
                          JS_NewInt64(ctx, (int64_t) cv[i].status));
        JS_SetPropertyStr(ctx, entry, "seconds",
                          JS_NewInt64(ctx, (int64_t) cv[i].valid));
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, entry);
    }

    return arr;
}


/*
 * cache.useStale — string[] decoded from the cache_use_stale bitmask.
 * Uses the same NGX_HTTP_UPSTREAM_FT_* flags as next_upstream.
 */
static JSValue
ngx_js_proxy_cache_get_use_stale(JSContext *ctx, JSValueConst this_val)
{
    static const struct {
        ngx_uint_t   flag;
        const char  *name;
    } stale_flags[] = {
        { NGX_HTTP_UPSTREAM_FT_ERROR,           "error"          },
        { NGX_HTTP_UPSTREAM_FT_TIMEOUT,         "timeout"        },
        { NGX_HTTP_UPSTREAM_FT_INVALID_HEADER,  "invalid_header" },
        { NGX_HTTP_UPSTREAM_FT_HTTP_500,        "http_500"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_502,        "http_502"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_503,        "http_503"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_504,        "http_504"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_403,        "http_403"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_404,        "http_404"       },
        { NGX_HTTP_UPSTREAM_FT_HTTP_429,        "http_429"       },
        { NGX_HTTP_UPSTREAM_FT_UPDATING,        "updating"       },
        { 0, NULL }
    };

    ngx_js_proxy_cache_opaque_t  *op;
    JSValue                       arr;
    ngx_uint_t                    mask, i;
    uint32_t                      idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_proxy_cache_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr  = JS_NewArray(ctx);
    mask = op->uconf->cache_use_stale;

    /* "off" flag means no stale serving — return empty array */
    if (mask & NGX_HTTP_UPSTREAM_FT_OFF) {
        return arr;
    }

    idx = 0;
    for (i = 0; stale_flags[i].name != NULL; i++) {
        if (mask & stale_flags[i].flag) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, stale_flags[i].name));
        }
    }

    return arr;
}


static const JSCFunctionListEntry ngx_js_proxy_cache_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("minUses",           ngx_js_proxy_cache_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("lock",              ngx_js_proxy_cache_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("lockTimeout",       ngx_js_proxy_cache_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("lockAge",           ngx_js_proxy_cache_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("revalidate",        ngx_js_proxy_cache_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("convertHead",       ngx_js_proxy_cache_get, NULL, 5),
    JS_CGETSET_MAGIC_DEF("backgroundUpdate",  ngx_js_proxy_cache_get, NULL, 6),
    JS_CGETSET_DEF      ("zone",              ngx_js_proxy_cache_get_zone,       NULL),
    JS_CGETSET_DEF      ("methods",           ngx_js_proxy_cache_get_methods,    NULL),
    JS_CGETSET_DEF      ("valid",             ngx_js_proxy_cache_get_valid,      NULL),
    JS_CGETSET_DEF      ("useStale",          ngx_js_proxy_cache_get_use_stale,  NULL),
};


JSValue
ngx_js_wrap_proxy_cache(JSContext *ctx, ngx_http_upstream_conf_t *uconf)
{
    JSValue                       obj, proto;
    ngx_js_proxy_cache_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_proxy_cache_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->uconf = uconf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_proxy_cache_proto_funcs,
                               countof(ngx_js_proxy_cache_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_proxy_cache_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}

#endif /* NGX_HTTP_CACHE */


ngx_int_t
ngx_js_proxy_cache_register_class(JSRuntime *rt)
{
#if (NGX_HTTP_CACHE)
    return JS_NewClass(rt, ngx_js_proxy_cache_class_id,
                       &ngx_js_proxy_cache_class) < 0
           ? NGX_ERROR : NGX_OK;
#else
    return NGX_OK;
#endif
}
