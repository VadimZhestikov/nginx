
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13u — location.memcached (NginxMemcached)
 *
 * Exposes ngx_http_memcached_loc_conf_t as a JS object on
 * location.memcached:
 *
 *   connectTimeout  number  — memcached_connect_timeout (ms)
 *   sendTimeout     number  — memcached_send_timeout (ms)
 *   readTimeout     number  — memcached_read_timeout (ms)
 *   bufferSize      number  — memcached_buffer_size (bytes)
 *   gzipFlag        number  — memcached_gzip_flag value (0 = not set)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_memcached_module.h"


typedef struct {
    ngx_http_memcached_loc_conf_t  *mlcf;
} ngx_js_memcached_opaque_t;


static void
ngx_js_memcached_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_memcached_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_memcached_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_memcached_class = {
    "NginxMemcached",
    .finalizer = ngx_js_memcached_finalizer,
};


static JSValue
ngx_js_memcached_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_memcached_opaque_t      *op;
    ngx_http_memcached_loc_conf_t  *mlcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_memcached_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    mlcf = op->mlcf;

    switch (magic) {

    case 0: /* connectTimeout */
        return JS_NewUint32(ctx, (uint32_t) mlcf->upstream.connect_timeout);

    case 1: /* sendTimeout */
        return JS_NewUint32(ctx, (uint32_t) mlcf->upstream.send_timeout);

    case 2: /* readTimeout */
        return JS_NewUint32(ctx, (uint32_t) mlcf->upstream.read_timeout);

    case 3: /* bufferSize */
        return JS_NewUint32(ctx, (uint32_t) mlcf->upstream.buffer_size);

    case 4: /* gzipFlag */
        return JS_NewUint32(ctx, (uint32_t) mlcf->gzip_flag);

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_memcached_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_memcached_opaque_t      *op;
    ngx_http_upstream_conf_t       *ucf;
    int64_t                         n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_memcached_class_id);
    if (!op) { return JS_EXCEPTION; }

    ucf = &op->mlcf->upstream;

    switch (magic) {
    case 0: /* connectTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->connect_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 1: /* sendTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->send_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 2: /* readTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->read_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    case 3: /* bufferSize */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        ucf->buffer_size = (size_t) n;
        return JS_UNDEFINED;
    case 4: /* gzipFlag */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        op->mlcf->gzip_flag = (ngx_uint_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_memcached_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("connectTimeout", ngx_js_memcached_get, ngx_js_memcached_set, 0),
    JS_CGETSET_MAGIC_DEF("sendTimeout",    ngx_js_memcached_get, ngx_js_memcached_set, 1),
    JS_CGETSET_MAGIC_DEF("readTimeout",    ngx_js_memcached_get, ngx_js_memcached_set, 2),
    JS_CGETSET_MAGIC_DEF("bufferSize",     ngx_js_memcached_get, ngx_js_memcached_set, 3),
    JS_CGETSET_MAGIC_DEF("gzipFlag",       ngx_js_memcached_get, ngx_js_memcached_set, 4),
};


ngx_int_t
ngx_js_memcached_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_memcached_class_id,
                       &ngx_js_memcached_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_memcached_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_memcached_proto_funcs,
                               countof(ngx_js_memcached_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_memcached_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_memcached(JSContext *ctx, ngx_http_memcached_loc_conf_t *mlcf)
{
    JSValue                     obj;
    ngx_js_memcached_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_memcached_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->mlcf = mlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_memcached_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
