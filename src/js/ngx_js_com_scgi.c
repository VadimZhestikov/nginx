
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13v — location.scgi (NginxScgi)
 *
 * Exposes ngx_http_scgi_loc_conf_t as a JS object on location.scgi:
 *
 *   connectTimeout  number  — scgi_connect_timeout (ms)
 *   sendTimeout     number  — scgi_send_timeout (ms)
 *   readTimeout     number  — scgi_read_timeout (ms)
 *   bufferSize      number  — scgi_buffer_size (bytes)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_scgi_module.h"


typedef struct {
    ngx_http_scgi_loc_conf_t  *scf;
} ngx_js_scgi_opaque_t;


static void
ngx_js_scgi_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_scgi_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_scgi_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_scgi_class = {
    "NginxScgi",
    .finalizer = ngx_js_scgi_finalizer,
};


static JSValue
ngx_js_scgi_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_scgi_opaque_t      *op;
    ngx_http_scgi_loc_conf_t  *scf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_scgi_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    scf = op->scf;

    switch (magic) {

    case 0: /* connectTimeout */
        return JS_NewUint32(ctx, (uint32_t) scf->upstream.connect_timeout);

    case 1: /* sendTimeout */
        return JS_NewUint32(ctx, (uint32_t) scf->upstream.send_timeout);

    case 2: /* readTimeout */
        return JS_NewUint32(ctx, (uint32_t) scf->upstream.read_timeout);

    case 3: /* bufferSize */
        return JS_NewUint32(ctx, (uint32_t) scf->upstream.buffer_size);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_scgi_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("connectTimeout", ngx_js_scgi_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("sendTimeout",    ngx_js_scgi_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("readTimeout",    ngx_js_scgi_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("bufferSize",     ngx_js_scgi_get, NULL, 3),
};


ngx_int_t
ngx_js_scgi_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_scgi_class_id, &ngx_js_scgi_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_scgi_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_scgi_proto_funcs,
                               countof(ngx_js_scgi_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_scgi_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_scgi(JSContext *ctx, ngx_http_scgi_loc_conf_t *scf)
{
    JSValue               obj;
    ngx_js_scgi_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_scgi_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->scf = scf;

    obj = JS_NewObjectClass(ctx, ngx_js_scgi_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
