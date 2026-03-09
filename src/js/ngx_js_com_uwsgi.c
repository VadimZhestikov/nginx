
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13v — location.uwsgi (NginxUwsgi)
 *
 * Exposes ngx_http_uwsgi_loc_conf_t as a JS object on location.uwsgi:
 *
 *   connectTimeout  number  — uwsgi_connect_timeout (ms)
 *   sendTimeout     number  — uwsgi_send_timeout (ms)
 *   readTimeout     number  — uwsgi_read_timeout (ms)
 *   bufferSize      number  — uwsgi_buffer_size (bytes)
 *   modifier1       number  — uwsgi_modifier1 (protocol byte, default 0)
 *   modifier2       number  — uwsgi_modifier2 (protocol byte, default 0)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_uwsgi_module.h"


typedef struct {
    ngx_http_uwsgi_loc_conf_t  *ucf;
} ngx_js_uwsgi_opaque_t;


static void
ngx_js_uwsgi_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_uwsgi_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_uwsgi_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_uwsgi_class = {
    "NginxUwsgi",
    .finalizer = ngx_js_uwsgi_finalizer,
};


static JSValue
ngx_js_uwsgi_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_uwsgi_opaque_t      *op;
    ngx_http_uwsgi_loc_conf_t  *ucf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_uwsgi_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    ucf = op->ucf;

    switch (magic) {

    case 0: /* connectTimeout */
        return JS_NewUint32(ctx, (uint32_t) ucf->upstream.connect_timeout);

    case 1: /* sendTimeout */
        return JS_NewUint32(ctx, (uint32_t) ucf->upstream.send_timeout);

    case 2: /* readTimeout */
        return JS_NewUint32(ctx, (uint32_t) ucf->upstream.read_timeout);

    case 3: /* bufferSize */
        return JS_NewUint32(ctx, (uint32_t) ucf->upstream.buffer_size);

    case 4: /* modifier1 */
        return JS_NewUint32(ctx, (uint32_t) ucf->modifier1);

    case 5: /* modifier2 */
        return JS_NewUint32(ctx, (uint32_t) ucf->modifier2);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_uwsgi_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("connectTimeout", ngx_js_uwsgi_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("sendTimeout",    ngx_js_uwsgi_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("readTimeout",    ngx_js_uwsgi_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("bufferSize",     ngx_js_uwsgi_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("modifier1",      ngx_js_uwsgi_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("modifier2",      ngx_js_uwsgi_get, NULL, 5),
};


ngx_int_t
ngx_js_uwsgi_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_uwsgi_class_id, &ngx_js_uwsgi_class) < 0
           ? NGX_ERROR : NGX_OK;
}


JSValue
ngx_js_wrap_uwsgi(JSContext *ctx, ngx_http_uwsgi_loc_conf_t *ucf)
{
    JSValue                obj, proto;
    ngx_js_uwsgi_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_uwsgi_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->ucf = ucf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_uwsgi_proto_funcs,
                               countof(ngx_js_uwsgi_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_uwsgi_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
