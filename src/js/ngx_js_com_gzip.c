
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 7: gzip location configuration.
 *
 * Exposes location.gzip as a NginxGzip object (or null when the gzip
 * filter module is not compiled in, or gzip is not enabled):
 *
 *   enable       bool     gzip on/off
 *   level        number   compression level (1-9)
 *   minLength    number   gzip_min_length (bytes)
 *   buffers      object   { num: N, size: N }  gzip_buffers
 *   vary         bool     gzip_vary on/off
 *   httpVersion  string   "1.0" | "1.1"
 *   proxied      string[] active gzip_proxied flags
 *
 * The file compiles unconditionally.  When NGX_HTTP_GZIP is not
 * defined (no --with-http_gzip_static_module / filter not included)
 * ngx_js_wrap_gzip simply returns JS_NULL and the register function
 * is a no-op.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"

#ifdef NGX_HTTP_GZIP
#include "../http/modules/ngx_http_gzip_filter_module.h"
#endif


#ifdef NGX_HTTP_GZIP

typedef struct {
    ngx_http_gzip_conf_t      *gcf;   /* per-location gzip config */
    ngx_http_core_loc_conf_t  *clcf;  /* for vary/httpVersion/proxied */
} ngx_js_gzip_opaque_t;


static void
ngx_js_gzip_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_gzip_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_gzip_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_gzip_class = {
    "NginxGzip",
    .finalizer = ngx_js_gzip_finalizer
};


/*
 * Magic values for ngx_js_gzip_get:
 *   0 — enable
 *   1 — level
 *   2 — minLength
 *   3 — vary
 */
static JSValue
ngx_js_gzip_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_gzip_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return JS_NewBool(ctx,   (int) op->gcf->enable);
    case 1: return JS_NewInt64(ctx,  (int64_t) op->gcf->level);
    case 2: return JS_NewInt64(ctx,  (int64_t) op->gcf->min_length);
    case 3: return JS_NewBool(ctx,   (int) op->clcf->gzip_vary);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_gzip_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_gzip_opaque_t  *op;
    int64_t                n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_class_id);
    if (!op) { return JS_EXCEPTION; }

    switch (magic) {
    case 0: /* enable */
        op->gcf->enable = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 1: /* level */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        op->gcf->level = (ngx_int_t) n;
        return JS_UNDEFINED;
    case 2: /* minLength */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        op->gcf->min_length = (ssize_t) n;
        return JS_UNDEFINED;
    case 3: /* vary */
        op->clcf->gzip_vary = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * gzip.buffers — { num: N, size: N }
 */
static JSValue
ngx_js_gzip_get_buffers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_gzip_opaque_t  *op;
    JSValue                obj;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "num",
                      JS_NewInt64(ctx, (int64_t) op->gcf->bufs.num));
    JS_SetPropertyStr(ctx, obj, "size",
                      JS_NewInt64(ctx, (int64_t) op->gcf->bufs.size));

    return obj;
}


/*
 * gzip.httpVersion — "1.0" or "1.1"
 */
static JSValue
ngx_js_gzip_get_http_version(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_gzip_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return JS_NewString(ctx,
        op->clcf->gzip_http_version == NGX_HTTP_VERSION_10 ? "1.0" : "1.1");
}


/*
 * gzip.proxied — string[] decoded from the gzip_proxied bitmask.
 */
static JSValue
ngx_js_gzip_get_proxied(JSContext *ctx, JSValueConst this_val)
{
    static const struct {
        ngx_uint_t   flag;
        const char  *name;
    } flags[] = {
        { NGX_HTTP_GZIP_PROXIED_EXPIRED,  "expired"  },
        { NGX_HTTP_GZIP_PROXIED_NO_CACHE, "no-cache" },
        { NGX_HTTP_GZIP_PROXIED_NO_STORE, "no-store" },
        { NGX_HTTP_GZIP_PROXIED_PRIVATE,  "private"  },
        { NGX_HTTP_GZIP_PROXIED_NO_LM,    "no_last_modified" },
        { NGX_HTTP_GZIP_PROXIED_NO_ETAG,  "no_etag"  },
        { NGX_HTTP_GZIP_PROXIED_AUTH,     "auth"     },
        { NGX_HTTP_GZIP_PROXIED_ANY,      "any"      },
        { 0, NULL }
    };

    ngx_js_gzip_opaque_t  *op;
    JSValue                arr;
    ngx_uint_t             mask, i;
    uint32_t               idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_gzip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr  = JS_NewArray(ctx);
    mask = op->clcf->gzip_proxied;

    /* Special case: "off" — empty array */
    if (mask & NGX_HTTP_GZIP_PROXIED_OFF) {
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


static const JSCFunctionListEntry ngx_js_gzip_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable",       ngx_js_gzip_get, ngx_js_gzip_set, 0),
    JS_CGETSET_MAGIC_DEF("level",        ngx_js_gzip_get, ngx_js_gzip_set, 1),
    JS_CGETSET_MAGIC_DEF("minLength",    ngx_js_gzip_get, ngx_js_gzip_set, 2),
    JS_CGETSET_MAGIC_DEF("vary",         ngx_js_gzip_get, ngx_js_gzip_set, 3),
    JS_CGETSET_DEF      ("buffers",      ngx_js_gzip_get_buffers,      NULL),
    JS_CGETSET_DEF      ("httpVersion",  ngx_js_gzip_get_http_version, NULL),
    JS_CGETSET_DEF      ("proxied",      ngx_js_gzip_get_proxied,      NULL),
};


ngx_int_t
ngx_js_gzip_install_proto(JSContext *ctx)
{
#ifdef NGX_HTTP_GZIP
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_gzip_proto_funcs,
                               countof(ngx_js_gzip_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_gzip_class_id, proto);
#endif
    return NGX_OK;
}


JSValue
ngx_js_wrap_gzip(JSContext *ctx, ngx_http_gzip_conf_t *gcf,
    ngx_http_core_loc_conf_t *clcf)
{
    JSValue               obj;
    ngx_js_gzip_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_gzip_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->gcf  = gcf;
    op->clcf = clcf;

    obj = JS_NewObjectClass(ctx, ngx_js_gzip_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}

#endif /* NGX_HTTP_GZIP */


ngx_int_t
ngx_js_gzip_register_class(JSRuntime *rt)
{
#ifdef NGX_HTTP_GZIP
    return JS_NewClass(rt, ngx_js_gzip_class_id, &ngx_js_gzip_class) < 0
           ? NGX_ERROR : NGX_OK;
#else
    return NGX_OK;
#endif
}
