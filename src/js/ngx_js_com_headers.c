
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 8: headers filter location configuration.
 *
 * Exposes location.headers as a NginxHeaders object:
 *
 *   expires          string   "off"|"epoch"|"max"|"access"|"modified"|
 *                             "daily"|"unset"
 *   expiresTime      number   seconds (meaningful for access/modified/daily)
 *   addHeaders       object[] [ { key, value, always }, ... ]
 *   addTrailers      object[] [ { key, value, always }, ... ]
 *   headersInherit   string   "off"|"on"|"merge"
 *   trailersInherit  string   "off"|"on"|"merge"
 *
 * For add_header/add_trailer entries, value is the literal string when the
 * directive uses no nginx variables; null when the value is dynamic.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_headers_filter_module.h"


typedef struct {
    ngx_http_headers_conf_t  *hcf;
} ngx_js_headers_opaque_t;


/* ── Dynamic-header sub-pool registry ──────────────────────────────────────
 *
 * Each GLOBAL addHeaders/addHeader/removeHeader call allocates a fresh
 * ngx_pool_t from the system allocator and writes the new array and string
 * data into it.  On the NEXT write the old pool is destroyed AFTER the new
 * array has been committed, so live memory is bounded to exactly one set of
 * dynamic headers per location at all times.
 *
 * The registry is per-worker (workers are single-threaded; no locking).
 * Workers hold COW copies of the process image after fork, so each worker's
 * registry is independent.  On reload the worker exits; the OS reclaims all
 * pools.
 */

#define NGX_JS_HDR_POOL_MAX  32

typedef struct {
    ngx_http_headers_conf_t  *hcf;
    ngx_pool_t               *pool;
} ngx_js_hdr_pool_entry_t;

static ngx_js_hdr_pool_entry_t  ngx_js_hdr_pools[NGX_JS_HDR_POOL_MAX];
static ngx_uint_t               ngx_js_hdr_pools_n;


/*
 * Prepare a write to the global (permanent) headers for hcf.
 * Creates a fresh pool, looks up and returns the previous pool (caller must
 * destroy it after committing the new array).  Returns NULL on error.
 */
static ngx_pool_t *
ngx_js_hdr_global_alloc(ngx_http_headers_conf_t *hcf,
    ngx_pool_t **old_pool_out)
{
    ngx_uint_t               i;
    ngx_pool_t               *new_pool;

    *old_pool_out = NULL;

    for (i = 0; i < ngx_js_hdr_pools_n; i++) {
        if (ngx_js_hdr_pools[i].hcf == hcf) {
            *old_pool_out = ngx_js_hdr_pools[i].pool;
            break;
        }
    }

    if (i == ngx_js_hdr_pools_n && ngx_js_hdr_pools_n >= NGX_JS_HDR_POOL_MAX) {
        return NULL;
    }

    new_pool = ngx_create_pool(512, ngx_cycle->log);
    if (new_pool == NULL) { return NULL; }

    if (i == ngx_js_hdr_pools_n) {
        ngx_js_hdr_pools[i].hcf  = hcf;
        ngx_js_hdr_pools_n++;
    }
    ngx_js_hdr_pools[i].pool = new_pool;
    return new_pool;
}


/*
 * Create a new ngx_array_t in pool, copying existing entries from
 * hcf->headers (if keep_existing).  Entries whose key matches excl/excl_len
 * are skipped.  Key and literal value strings are deep-copied into pool so
 * the array remains valid after the source pool is destroyed.
 */
static ngx_array_t *
ngx_js_hdr_cow_array(ngx_http_headers_conf_t *hcf, ngx_pool_t *pool,
    int keep_existing, const u_char *excl, size_t excl_len)
{
    ngx_array_t           *arr;
    ngx_http_header_val_t *src, *dst;
    ngx_uint_t             i;

    arr = ngx_array_create(pool, 4, sizeof(ngx_http_header_val_t));
    if (arr == NULL) { return NULL; }

    if (!keep_existing || hcf->headers == NULL) { return arr; }

    src = hcf->headers->elts;
    for (i = 0; i < hcf->headers->nelts; i++) {
        if (excl_len
            && src[i].key.len == excl_len
            && ngx_strncasecmp(src[i].key.data, (u_char *) excl, excl_len) == 0)
        {
            continue;
        }

        dst = ngx_array_push(arr);
        if (dst == NULL) { return NULL; }
        *dst = src[i];

        /* Deep-copy key so it survives destruction of the source pool. */
        dst->key.data = ngx_pnalloc(pool, src[i].key.len + 1);
        if (dst->key.data == NULL) { return NULL; }
        ngx_cpystrn(dst->key.data, src[i].key.data, src[i].key.len + 1);

        /* Deep-copy the literal value string (lengths == NULL ↔ literal). */
        if (src[i].value.lengths == NULL && src[i].value.value.len > 0) {
            dst->value.value.data =
                ngx_pnalloc(pool, src[i].value.value.len + 1);
            if (dst->value.value.data == NULL) { return NULL; }
            ngx_cpystrn(dst->value.value.data, src[i].value.value.data,
                         src[i].value.value.len + 1);
        }
        /* lengths/values/flushes: NULL for literal or bytecode in cf->pool
         * (permanent); shallow copy is safe in both cases.              */
    }
    return arr;
}


/* ── Inner write functions ─────────────────────────────────────────────────
 *
 * Each function builds a new array in pool and, on success only, assigns it
 * to hcf->headers.  On error hcf->headers is left unchanged so the caller
 * can safely destroy the pool without leaving a dangling pointer.
 */

static JSValue
ngx_js_hdr_do_set(JSContext *ctx, ngx_http_headers_conf_t *hcf,
    ngx_pool_t *pool, JSValueConst js_arr)
{
    ngx_array_t           *arr;
    ngx_http_header_val_t *hv;
    JSValue                item, kv, vv, av;
    const char            *ks, *vs;
    size_t                 klen, vlen;
    ngx_str_t              key, value;
    ngx_uint_t             always;
    int64_t                i, ilen;

    arr = ngx_js_hdr_cow_array(hcf, pool, 0, NULL, 0);
    if (arr == NULL) {
        return JS_ThrowInternalError(ctx, "addHeaders: array alloc failed");
    }

    if (JS_ToInt64(ctx, &ilen,
                   JS_GetPropertyStr(ctx, js_arr, "length")) < 0) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < ilen; i++) {
        item = JS_GetPropertyUint32(ctx, js_arr, (uint32_t) i);

        kv = JS_GetPropertyStr(ctx, item, "key");
        vv = JS_GetPropertyStr(ctx, item, "value");
        av = JS_GetPropertyStr(ctx, item, "always");

        ks     = JS_ToCStringLen(ctx, &klen, kv);
        vs     = JS_ToCStringLen(ctx, &vlen, vv);
        always = (ngx_uint_t) JS_ToBool(ctx, av);

        JS_FreeValue(ctx, kv);
        JS_FreeValue(ctx, vv);
        JS_FreeValue(ctx, av);

        if (ks == NULL || vs == NULL || klen == 0) {
            if (ks) { JS_FreeCString(ctx, ks); }
            if (vs) { JS_FreeCString(ctx, vs); }
            JS_FreeValue(ctx, item);
            return JS_ThrowTypeError(ctx,
                "addHeaders[%d]: key and value must be non-empty strings",
                (int) i);
        }

        hv = ngx_array_push(arr);
        if (hv == NULL) {
            JS_FreeCString(ctx, ks);
            JS_FreeCString(ctx, vs);
            JS_FreeValue(ctx, item);
            return JS_ThrowInternalError(ctx, "addHeaders: push failed");
        }

        key.data   = (u_char *) ks;  key.len   = klen;
        value.data = (u_char *) vs;  value.len = vlen;

        if (ngx_http_headers_add_literal(pool, &key, &value, always,
                                          hv) != NGX_OK)
        {
            JS_FreeCString(ctx, ks);
            JS_FreeCString(ctx, vs);
            JS_FreeValue(ctx, item);
            return JS_ThrowInternalError(ctx, "addHeaders: literal alloc failed");
        }

        JS_FreeCString(ctx, ks);
        JS_FreeCString(ctx, vs);
        JS_FreeValue(ctx, item);
    }

    hcf->headers = arr;   /* commit only on full success */
    return JS_UNDEFINED;
}


static JSValue
ngx_js_hdr_do_add(JSContext *ctx, ngx_http_headers_conf_t *hcf,
    ngx_pool_t *pool, ngx_str_t *key, ngx_str_t *value, ngx_uint_t always)
{
    ngx_array_t           *arr;
    ngx_http_header_val_t *hv;

    arr = ngx_js_hdr_cow_array(hcf, pool, 1, NULL, 0);
    if (arr == NULL) {
        return JS_ThrowInternalError(ctx, "addHeader: array alloc failed");
    }

    hv = ngx_array_push(arr);
    if (hv == NULL) {
        return JS_ThrowInternalError(ctx, "addHeader: push failed");
    }

    if (ngx_http_headers_add_literal(pool, key, value, always, hv) != NGX_OK) {
        return JS_ThrowInternalError(ctx, "addHeader: literal alloc failed");
    }

    hcf->headers = arr;   /* commit only on full success */
    return JS_UNDEFINED;
}


static JSValue
ngx_js_hdr_do_remove(JSContext *ctx, ngx_http_headers_conf_t *hcf,
    ngx_pool_t *pool, const u_char *excl, size_t excl_len)
{
    ngx_array_t *arr;

    arr = ngx_js_hdr_cow_array(hcf, pool, 1, excl, excl_len);
    if (arr == NULL) {
        return JS_ThrowInternalError(ctx, "removeHeader: array alloc failed");
    }

    hcf->headers = arr;   /* commit only on full success */
    return JS_UNDEFINED;
}


/* ── Shared write preamble macro ───────────────────────────────────────────
 *
 * Declares and initialises all local variables used by the three outer
 * setter/method functions.  Sets need_global and need_local; disables
 * need_local for cross-location writes (can't snapshot another location's
 * module conf from within a request for this location).
 */
#define NGX_JS_HDR_WRITE_PREAMBLE(need_global, need_local)                   \
    ngx_js_headers_opaque_t  *op;                                            \
    ngx_js_worker_t          *w;                                             \
    ngx_http_request_t       *r;                                             \
    ngx_js_req_ctx_t         *rctx;                                          \
    ngx_http_headers_conf_t  *orig_hcf;                                      \
    ngx_pool_t               *new_pool, *old_pool;                           \
    uint32_t                  wmode;                                         \
    JSValue                   ret;                                           \
                                                                             \
    op   = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);           \
    if (!op) { return JS_EXCEPTION; }                                        \
    w    = JS_GetContextOpaque(ctx);                                         \
    r    = (w != NULL) ? w->current_request : NULL;                          \
    rctx = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module)     \
                       : NULL;                                               \
    wmode       = (rctx != NULL) ? rctx->write_mode : NGX_JS_WRITE_GLOBAL;  \
    need_global = (wmode & NGX_JS_WRITE_GLOBAL) != 0;                       \
    need_local  = (wmode & NGX_JS_WRITE_LOCAL)  != 0;                       \
    if (need_local                                                           \
        && !ngx_js_is_own_conf(r, rctx,                                     \
               ngx_http_headers_filter_module.ctx_index, op->hcf))          \
    {                                                                        \
        need_local = 0;                                                      \
    }


/* ── addHeaders setter ─────────────────────────────────────────────────────
 *
 * loc.headers.addHeaders = [{key: "X-Foo", value: "bar"[, always: true]}, …]
 *
 * Replaces the entire dynamic-header list for the location.  Existing
 * config-parse add_header entries are discarded (the new array starts empty
 * and is filled from the JS argument).
 */
static JSValue
ngx_js_headers_set_add_headers(JSContext *ctx, JSValueConst this_val,
    JSValueConst val)
{
    int  need_global, need_local;
    NGX_JS_HDR_WRITE_PREAMBLE(need_global, need_local)

    if (!JS_IsArray(ctx, val)) {
        return JS_ThrowTypeError(ctx, "addHeaders: array expected");
    }

    /* LOCAL write — snapshot the per-request copy, build in r->pool. */
    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_headers_filter_module,
                sizeof(ngx_http_headers_conf_t)) != NGX_OK) {
            return JS_ThrowInternalError(ctx, "addHeaders: snapshot failed");
        }
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_hdr_do_set(ctx, op->hcf, r->pool, val);
        op->hcf  = orig_hcf;
        if (!need_global || JS_IsException(ret)) { return ret; }
        JS_FreeValue(ctx, ret);
    }

    /* GLOBAL write — build in a new sub-pool, destroy old on success. */
    if (need_global) {
        new_pool = ngx_js_hdr_global_alloc(op->hcf, &old_pool);
        if (new_pool == NULL) {
            return JS_ThrowInternalError(ctx,
                "addHeaders: global pool alloc failed (registry full at %d)",
                NGX_JS_HDR_POOL_MAX);
        }
        ret = ngx_js_hdr_do_set(ctx, op->hcf, new_pool, val);
        if (JS_IsException(ret)) {
            /* Rolled back — restore old pool in registry and free new. */
            ngx_js_hdr_global_alloc(op->hcf, &new_pool);
            if (new_pool) { ngx_destroy_pool(new_pool); }
            ngx_js_hdr_pools[ngx_js_hdr_pools_n > 0
                              ? ngx_js_hdr_pools_n - 1 : 0].pool = old_pool;
            return ret;
        }
        if (old_pool) { ngx_destroy_pool(old_pool); }
        return ret;
    }

    return JS_UNDEFINED;
}


/* ── addHeader() method ────────────────────────────────────────────────────
 *
 * loc.headers.addHeader(key, value[, always])
 *
 * Appends one header.  Existing entries (from config or previous addHeader
 * calls) are preserved (copy-on-write into the new pool).
 */
static JSValue
ngx_js_headers_fn_add(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char  *ks, *vs;
    size_t       klen, vlen;
    ngx_str_t    key, value;
    ngx_uint_t   always;
    int          need_global, need_local;

    NGX_JS_HDR_WRITE_PREAMBLE(need_global, need_local)

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "addHeader: key and value required");
    }

    ks = JS_ToCStringLen(ctx, &klen, argv[0]);
    if (ks == NULL) { return JS_EXCEPTION; }
    vs = JS_ToCStringLen(ctx, &vlen, argv[1]);
    if (vs == NULL) { JS_FreeCString(ctx, ks); return JS_EXCEPTION; }

    if (klen == 0) {
        JS_FreeCString(ctx, ks);
        JS_FreeCString(ctx, vs);
        return JS_ThrowTypeError(ctx, "addHeader: key must not be empty");
    }

    always     = (argc >= 3) ? (ngx_uint_t) JS_ToBool(ctx, argv[2]) : 0;
    key.data   = (u_char *) ks;  key.len   = klen;
    value.data = (u_char *) vs;  value.len = vlen;

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_headers_filter_module,
                sizeof(ngx_http_headers_conf_t)) != NGX_OK) {
            JS_FreeCString(ctx, ks);
            JS_FreeCString(ctx, vs);
            return JS_ThrowInternalError(ctx, "addHeader: snapshot failed");
        }
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_hdr_do_add(ctx, op->hcf, r->pool,
                                      &key, &value, always);
        op->hcf  = orig_hcf;
        if (!need_global || JS_IsException(ret)) {
            JS_FreeCString(ctx, ks);
            JS_FreeCString(ctx, vs);
            return ret;
        }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        new_pool = ngx_js_hdr_global_alloc(op->hcf, &old_pool);
        if (new_pool == NULL) {
            JS_FreeCString(ctx, ks);
            JS_FreeCString(ctx, vs);
            return JS_ThrowInternalError(ctx,
                "addHeader: global pool alloc failed (registry full at %d)",
                NGX_JS_HDR_POOL_MAX);
        }
        ret = ngx_js_hdr_do_add(ctx, op->hcf, new_pool, &key, &value, always);
        JS_FreeCString(ctx, ks);
        JS_FreeCString(ctx, vs);
        if (JS_IsException(ret)) {
            ngx_js_hdr_global_alloc(op->hcf, &new_pool);
            if (new_pool) { ngx_destroy_pool(new_pool); }
            ngx_js_hdr_pools[ngx_js_hdr_pools_n > 0
                              ? ngx_js_hdr_pools_n - 1 : 0].pool = old_pool;
            return ret;
        }
        if (old_pool) { ngx_destroy_pool(old_pool); }
        return ret;
    }

    JS_FreeCString(ctx, ks);
    JS_FreeCString(ctx, vs);
    return JS_UNDEFINED;
}


/* ── removeHeader() method ─────────────────────────────────────────────────
 *
 * loc.headers.removeHeader(key)
 *
 * Removes all entries whose key matches (case-insensitive) from the
 * location's add_header list.  No-op if no entry matches.
 */
static JSValue
ngx_js_headers_fn_remove(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char  *ks;
    size_t       klen;
    int          need_global, need_local;

    NGX_JS_HDR_WRITE_PREAMBLE(need_global, need_local)

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "removeHeader: key required");
    }

    ks = JS_ToCStringLen(ctx, &klen, argv[0]);
    if (ks == NULL) { return JS_EXCEPTION; }
    if (klen == 0) {
        JS_FreeCString(ctx, ks);
        return JS_ThrowTypeError(ctx, "removeHeader: key must not be empty");
    }

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_headers_filter_module,
                sizeof(ngx_http_headers_conf_t)) != NGX_OK) {
            JS_FreeCString(ctx, ks);
            return JS_ThrowInternalError(ctx, "removeHeader: snapshot failed");
        }
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_hdr_do_remove(ctx, op->hcf, r->pool,
                                         (u_char *) ks, klen);
        op->hcf  = orig_hcf;
        if (!need_global || JS_IsException(ret)) {
            JS_FreeCString(ctx, ks);
            return ret;
        }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        new_pool = ngx_js_hdr_global_alloc(op->hcf, &old_pool);
        if (new_pool == NULL) {
            JS_FreeCString(ctx, ks);
            return JS_ThrowInternalError(ctx,
                "removeHeader: global pool alloc failed (registry full at %d)",
                NGX_JS_HDR_POOL_MAX);
        }
        ret = ngx_js_hdr_do_remove(ctx, op->hcf, new_pool,
                                    (u_char *) ks, klen);
        JS_FreeCString(ctx, ks);
        if (JS_IsException(ret)) {
            ngx_js_hdr_global_alloc(op->hcf, &new_pool);
            if (new_pool) { ngx_destroy_pool(new_pool); }
            ngx_js_hdr_pools[ngx_js_hdr_pools_n > 0
                              ? ngx_js_hdr_pools_n - 1 : 0].pool = old_pool;
            return ret;
        }
        if (old_pool) { ngx_destroy_pool(old_pool); }
        return ret;
    }

    JS_FreeCString(ctx, ks);
    return JS_UNDEFINED;
}


static void
ngx_js_headers_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_headers_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_headers_class = {
    "NginxHeaders",
    .finalizer = ngx_js_headers_finalizer
};


static const char *
ngx_js_headers_inherit_str(ngx_uint_t v)
{
    switch (v) {
    case NGX_HTTP_HEADERS_INHERIT_ON:    return "on";
    case NGX_HTTP_HEADERS_INHERIT_MERGE: return "merge";
    default:                             return "off";
    }
}


/*
 * Magic values for ngx_js_headers_get_core:
 *   0 — expires          (string)
 *   1 — expiresTime      (number, seconds)
 *   2 — headersInherit   (string)
 *   3 — trailersInherit  (string)
 */
static JSValue
ngx_js_headers_get_core(JSContext *ctx, JSValueConst this_val, int magic)
{
    static const char *expire_modes[] = {
        "off", "epoch", "max", "access", "modified", "daily", "unset"
    };

    ngx_js_headers_opaque_t  *op;
    ngx_http_expires_t        e;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* expires */
        e = op->hcf->expires;
        if ((ngx_uint_t) e >= (sizeof(expire_modes) / sizeof(expire_modes[0])))
        {
            e = NGX_HTTP_EXPIRES_UNSET;
        }
        return JS_NewString(ctx, expire_modes[e]);

    case 1: /* expiresTime */
        return JS_NewInt64(ctx, (int64_t) op->hcf->expires_time);

    case 2: /* headersInherit */
        return JS_NewString(ctx,
                            ngx_js_headers_inherit_str(op->hcf->headers_inherit));

    case 3: /* trailersInherit */
        return JS_NewString(ctx,
                            ngx_js_headers_inherit_str(op->hcf->trailers_inherit));
    }

    return JS_UNDEFINED;
}


/*
 * Build a JS array from an ngx_array_t of ngx_http_header_val_t.
 * Each element is { key: string, value: string|null, always: bool }.
 * value is null when the directive value contains nginx variables.
 */
static JSValue
ngx_js_headers_build_array(JSContext *ctx, ngx_array_t *arr)
{
    ngx_http_header_val_t  *h;
    JSValue                 js_arr, entry;
    ngx_uint_t              i;

    js_arr = JS_NewArray(ctx);

    if (arr == NULL || arr->nelts == 0) {
        return js_arr;
    }

    h = arr->elts;

    for (i = 0; i < arr->nelts; i++) {
        entry = JS_NewObject(ctx);

        JS_SetPropertyStr(ctx, entry, "key",
                          JS_NewStringLen(ctx,
                                         (const char *) h[i].key.data,
                                         h[i].key.len));

        /* value.lengths == NULL means literal (no nginx variables) */
        if (h[i].value.lengths == NULL) {
            JS_SetPropertyStr(ctx, entry, "value",
                              JS_NewStringLen(ctx,
                                             (const char *) h[i].value.value.data,
                                             h[i].value.value.len));
        } else {
            JS_SetPropertyStr(ctx, entry, "value", JS_NULL);
        }

        JS_SetPropertyStr(ctx, entry, "always",
                          JS_NewBool(ctx, (int) h[i].always));

        JS_SetPropertyUint32(ctx, js_arr, (uint32_t) i, entry);
    }

    return js_arr;
}


static JSValue
ngx_js_headers_get_add_headers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_headers_build_array(ctx, op->hcf->headers);
}


static JSValue
ngx_js_headers_get_add_trailers(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_headers_opaque_t *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    return ngx_js_headers_build_array(ctx, op->hcf->trailers);
}


/*
 * headersInherit / trailersInherit setter.
 * Accepts "off", "on", or "merge".
 *   Magic 2 — headersInherit
 *   Magic 3 — trailersInherit
 */
static JSValue
ngx_js_headers_set_core(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_headers_opaque_t  *op;
    const char               *s;
    size_t                    slen;
    ngx_uint_t                v;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    s = JS_ToCStringLen(ctx, &slen, val);
    if (!s) { return JS_EXCEPTION; }

    if (slen == 2 && ngx_strncasecmp((u_char *) s, (u_char *) "on", 2) == 0) {
        v = NGX_HTTP_HEADERS_INHERIT_ON;
    } else if (slen == 5
               && ngx_strncasecmp((u_char *) s, (u_char *) "merge", 5) == 0)
    {
        v = NGX_HTTP_HEADERS_INHERIT_MERGE;
    } else if (slen == 3
               && ngx_strncasecmp((u_char *) s, (u_char *) "off", 3) == 0)
    {
        v = 0;
    } else {
        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
                    "inherit value must be \"off\", \"on\", or \"merge\"");
    }

    JS_FreeCString(ctx, s);

    if (magic == 2) {
        op->hcf->headers_inherit  = v;
    } else {
        op->hcf->trailers_inherit = v;
    }

    return JS_UNDEFINED;
}


/*
 * Snapshot-aware getter wrapper.
 */
static JSValue
ngx_js_headers_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_headers_opaque_t  *op;
    ngx_js_worker_t          *w;
    ngx_http_request_t       *r;
    ngx_js_req_ctx_t         *rctx;
    ngx_http_headers_conf_t  *orig_hcf;
    JSValue                   ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    w    = JS_GetContextOpaque(ctx);
    r    = (w != NULL) ? w->current_request : NULL;
    rctx = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;

    if (rctx != NULL && (rctx->read_mode & NGX_JS_WRITE_LOCAL)
        && ngx_js_is_own_conf(r, rctx,
               ngx_http_headers_filter_module.ctx_index, op->hcf))
    {
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_headers_get_core(ctx, this_val, magic);
        op->hcf  = orig_hcf;
        return ret;
    }

    return ngx_js_headers_get_core(ctx, this_val, magic);
}


/*
 * Snapshot-aware setter wrapper.
 */
static JSValue
ngx_js_headers_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_headers_opaque_t  *op;
    ngx_js_worker_t          *w;
    ngx_http_request_t       *r;
    ngx_js_req_ctx_t         *rctx;
    ngx_http_headers_conf_t  *orig_hcf;
    uint32_t                  wmode;
    int                       need_global, need_local;
    JSValue                   ret;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_headers_class_id);
    if (!op) { return JS_EXCEPTION; }

    w     = JS_GetContextOpaque(ctx);
    r     = (w != NULL) ? w->current_request : NULL;
    rctx  = (r != NULL) ? ngx_http_get_module_ctx(r, ngx_js_http_module) : NULL;
    wmode = (rctx != NULL) ? rctx->write_mode : NGX_JS_WRITE_GLOBAL;

    need_global = (wmode & NGX_JS_WRITE_GLOBAL) != 0;
    need_local  = (wmode & NGX_JS_WRITE_LOCAL)  != 0;

    if (need_local) {
        if (!ngx_js_is_own_conf(r, rctx,
                ngx_http_headers_filter_module.ctx_index, op->hcf))
        {
            need_local = 0;  /* cross-location: global only */
        }
    }

    if (need_local) {
        if (ngx_js_ensure_module_snapshot(r, &ngx_http_headers_filter_module,
                sizeof(ngx_http_headers_conf_t)) != NGX_OK) {
            return JS_ThrowInternalError(ctx, "headers snapshot alloc failed");
        }
        orig_hcf = op->hcf;
        op->hcf  = r->loc_conf[ngx_http_headers_filter_module.ctx_index];
        ret      = ngx_js_headers_set_core(ctx, this_val, val, magic);
        op->hcf  = orig_hcf;
        if (!need_global || JS_IsException(ret)) { return ret; }
        JS_FreeValue(ctx, ret);
    }

    if (need_global) {
        return ngx_js_headers_set_core(ctx, this_val, val, magic);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_headers_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("expires",         ngx_js_headers_get, NULL,                0),
    JS_CGETSET_MAGIC_DEF("expiresTime",     ngx_js_headers_get, NULL,                1),
    JS_CGETSET_MAGIC_DEF("headersInherit",  ngx_js_headers_get, ngx_js_headers_set,  2),
    JS_CGETSET_MAGIC_DEF("trailersInherit", ngx_js_headers_get, ngx_js_headers_set,  3),
    JS_CGETSET_DEF      ("addHeaders",      ngx_js_headers_get_add_headers,
                                            ngx_js_headers_set_add_headers),
    JS_CGETSET_DEF      ("addTrailers",     ngx_js_headers_get_add_trailers, NULL),
    JS_CFUNC_DEF        ("addHeader",       2, ngx_js_headers_fn_add),
    JS_CFUNC_DEF        ("removeHeader",    1, ngx_js_headers_fn_remove),
};


ngx_int_t
ngx_js_headers_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_headers_proto_funcs,
                               countof(ngx_js_headers_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_headers_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_headers(JSContext *ctx, ngx_http_headers_conf_t *hcf)
{
    JSValue                  obj;
    ngx_js_headers_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_headers_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->hcf = hcf;

    obj = JS_NewObjectClass(ctx, ngx_js_headers_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_headers_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_headers_class_id, &ngx_js_headers_class) < 0
           ? NGX_ERROR : NGX_OK;
}
