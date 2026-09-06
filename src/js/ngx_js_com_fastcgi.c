
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 13a: fastcgi location configuration.
 *
 * Exposes location.fastcgi as a NginxFastCGI object (null when no
 * fastcgi_pass directive is present in the location).
 *
 * NginxFastCGI properties (all read-only):
 *
 *   pass             string|null  fastcgi_pass address (literal only;
 *                                 null for dynamic / variable-based pass)
 *   index            string|null  fastcgi_index value, null if not set
 *   keepConn         boolean      fastcgi_keep_conn
 *   connectTimeout   number       fastcgi_connect_timeout (ms)
 *   sendTimeout      number       fastcgi_send_timeout (ms)
 *   readTimeout      number       fastcgi_read_timeout (ms)
 *   buffering        boolean      fastcgi_buffering
 *   requestBuffering boolean      fastcgi_request_buffering
 *   interceptErrors  boolean      fastcgi_intercept_errors
 *   params           object[]     fastcgi_param directives [{key, value}]
 *   catchStderr      string[]     fastcgi_catch_stderr patterns
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_fastcgi_module.h"


typedef struct {
    ngx_http_fastcgi_loc_conf_t  *flcf;
} ngx_js_fastcgi_opaque_t;


static void
ngx_js_fastcgi_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_fastcgi_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_fastcgi_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_fastcgi_class = {
    "NginxFastCGI",
    .finalizer = ngx_js_fastcgi_finalizer
};


/*
 * params getter: [{key, value}] from params_source.
 *
 * The handler stores ngx_http_upstream_param_t (key, value, skip_empty),
 * not plain ngx_keyval_t — use the correct type for element sizing.
 */
static JSValue
ngx_js_fastcgi_get_params(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_fastcgi_opaque_t       *op;
    ngx_http_upstream_param_t     *p;
    JSValue                        arr, obj;
    ngx_uint_t                     i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_fastcgi_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->flcf->params_source == NULL) {
        return arr;
    }

    p = op->flcf->params_source->elts;

    for (i = 0; i < op->flcf->params_source->nelts; i++) {
        obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "key",
            JS_NewStringLen(ctx, (const char *) p[i].key.data,
                            p[i].key.len));
        JS_SetPropertyStr(ctx, obj, "value",
            JS_NewStringLen(ctx, (const char *) p[i].value.data,
                            p[i].value.len));
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


/*
 * catchStderr getter: string[] from catch_stderr (ngx_str_t[]).
 */
static JSValue
ngx_js_fastcgi_get_catch_stderr(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_fastcgi_opaque_t  *op;
    ngx_str_t                *s;
    JSValue                   arr;
    ngx_uint_t                i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_fastcgi_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->flcf->catch_stderr == NULL) {
        return arr;
    }

    s = op->flcf->catch_stderr->elts;

    for (i = 0; i < op->flcf->catch_stderr->nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
            JS_NewStringLen(ctx, (const char *) s[i].data, s[i].len));
    }

    return arr;
}


/*
 * Magic values for ngx_js_fastcgi_get:
 *   0 — pass
 *   1 — index
 *   2 — keepConn
 *   3 — connectTimeout
 *   4 — sendTimeout
 *   5 — readTimeout
 *   6 — buffering
 *   7 — requestBuffering
 *   8 — interceptErrors
 */
static JSValue
ngx_js_fastcgi_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_fastcgi_opaque_t      *op;
    ngx_http_fastcgi_loc_conf_t  *flcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_fastcgi_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    flcf = op->flcf;

    switch (magic) {
    case 0: /* pass */
    {
        ngx_http_upstream_srv_conf_t  *usc;
        u_char                         buf[NGX_SOCKADDR_STRLEN];
        u_char                        *p;

        if (flcf->fastcgi_lengths != NULL || flcf->upstream.upstream == NULL) {
            return JS_NULL;
        }

        usc = flcf->upstream.upstream;

        if (!usc->no_port && usc->port) {
            p = ngx_snprintf(buf, sizeof(buf), "%V:%d",
                             &usc->host, (int) usc->port);
            return JS_NewStringLen(ctx, (const char *) buf, p - buf);
        }

        return JS_NewStringLen(ctx, (const char *) usc->host.data,
                               usc->host.len);
    }

    case 1: /* index */
        if (flcf->index.len == 0) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx, (const char *) flcf->index.data,
                               flcf->index.len);

    case 2: return JS_NewBool(ctx, flcf->keep_conn);
    case 3: return JS_NewUint32(ctx, (uint32_t) flcf->upstream.connect_timeout);
    case 4: return JS_NewUint32(ctx, (uint32_t) flcf->upstream.send_timeout);
    case 5: return JS_NewUint32(ctx, (uint32_t) flcf->upstream.read_timeout);
    case 6: return JS_NewBool(ctx, flcf->upstream.buffering);
    case 7: return JS_NewBool(ctx, flcf->upstream.request_buffering);
    case 8: return JS_NewBool(ctx, flcf->upstream.intercept_errors);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_fastcgi_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_fastcgi_opaque_t       *op;
    ngx_http_upstream_conf_t      *ucf;
    int64_t                        n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_fastcgi_class_id);
    if (!op) { return JS_EXCEPTION; }

    ucf = &op->flcf->upstream;

    switch (magic) {
    case 1: /* index — string stored in pool */
    {
        const char  *s;
        size_t       slen;
        u_char      *p;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        p = ngx_pnalloc(ngx_js_conf_cycle()->pool, slen + 1);
        if (!p) { JS_FreeCString(ctx, s); return JS_EXCEPTION; }
        ngx_memcpy(p, s, slen);
        p[slen] = '\0';
        JS_FreeCString(ctx, s);
        op->flcf->index.data = p;
        op->flcf->index.len  = slen;
        return JS_UNDEFINED;
    }
    case 2: /* keepConn */
        op->flcf->keep_conn = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 3: /* connectTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->connect_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 4: /* sendTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->send_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 5: /* readTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->read_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 6: /* buffering */
        ucf->buffering = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 7: /* requestBuffering */
        ucf->request_buffering = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    case 8: /* interceptErrors */
        ucf->intercept_errors = JS_ToBool(ctx, val);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_fastcgi_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("pass",             ngx_js_fastcgi_get, NULL,              0),
    JS_CGETSET_MAGIC_DEF("index",            ngx_js_fastcgi_get, ngx_js_fastcgi_set, 1),
    JS_CGETSET_MAGIC_DEF("keepConn",         ngx_js_fastcgi_get, ngx_js_fastcgi_set, 2),
    JS_CGETSET_MAGIC_DEF("connectTimeout",   ngx_js_fastcgi_get, ngx_js_fastcgi_set, 3),
    JS_CGETSET_MAGIC_DEF("sendTimeout",      ngx_js_fastcgi_get, ngx_js_fastcgi_set, 4),
    JS_CGETSET_MAGIC_DEF("readTimeout",      ngx_js_fastcgi_get, ngx_js_fastcgi_set, 5),
    JS_CGETSET_MAGIC_DEF("buffering",        ngx_js_fastcgi_get, ngx_js_fastcgi_set, 6),
    JS_CGETSET_MAGIC_DEF("requestBuffering", ngx_js_fastcgi_get, ngx_js_fastcgi_set, 7),
    JS_CGETSET_MAGIC_DEF("interceptErrors",  ngx_js_fastcgi_get, ngx_js_fastcgi_set, 8),
    JS_CGETSET_DEF       ("params",          ngx_js_fastcgi_get_params,       NULL),
    JS_CGETSET_DEF       ("catchStderr",     ngx_js_fastcgi_get_catch_stderr, NULL),
};


ngx_int_t
ngx_js_fastcgi_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_fastcgi_proto_funcs,
                               countof(ngx_js_fastcgi_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_fastcgi_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_fastcgi(JSContext *ctx, ngx_http_fastcgi_loc_conf_t *flcf)
{
    JSValue                   obj;
    ngx_js_fastcgi_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_fastcgi_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->flcf = flcf;

    obj = JS_NewObjectClass(ctx, ngx_js_fastcgi_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_fastcgi_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_fastcgi_class_id, &ngx_js_fastcgi_class) < 0
           ? NGX_ERROR : NGX_OK;
}
