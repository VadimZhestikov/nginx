
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13h — location.dav (NginxDav)
 *
 * Exposes ngx_http_dav_loc_conf_t as a JS object on location.dav:
 *
 *   methods[]          Array of strings — enabled DAV methods
 *                      ("PUT", "DELETE", "MKCOL", "COPY", "MOVE")
 *   access             number  — dav_access octal permission bits
 *   minDeleteDepth     number  — min_delete_depth
 *   createFullPutPath  boolean — create_full_put_path on/off
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_dav_module.h"


#if (NGX_HTTP_DAV)


typedef struct {
    ngx_http_dav_loc_conf_t  *dlcf;
} ngx_js_dav_opaque_t;


static void
ngx_js_dav_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_dav_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_dav_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_dav_class = {
    "NginxDav",
    .finalizer = ngx_js_dav_finalizer,
};


static JSValue
ngx_js_dav_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_dav_opaque_t      *op;
    ngx_http_dav_loc_conf_t  *dlcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_dav_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    dlcf = op->dlcf;

    switch (magic) {

    case 0: /* methods — array of enabled method name strings */
    {
        JSValue     arr;
        ngx_uint_t  n, i, mask;

        static const struct { ngx_uint_t bit; const char *name; } methods[] = {
            { NGX_HTTP_PUT,    "PUT"    },
            { NGX_HTTP_DELETE, "DELETE" },
            { NGX_HTTP_MKCOL,  "MKCOL" },
            { NGX_HTTP_COPY,   "COPY"   },
            { NGX_HTTP_MOVE,   "MOVE"   },
        };

        arr  = JS_NewArray(ctx);
        n    = 0;
        mask = dlcf->methods;

        if (!(mask & NGX_HTTP_DAV_OFF)) {
            for (i = 0; i < countof(methods); i++) {
                if (mask & methods[i].bit) {
                    JS_SetPropertyUint32(ctx, arr, (uint32_t) n++,
                        JS_NewString(ctx, methods[i].name));
                }
            }
        }

        return arr;
    }

    case 1: /* access */
        return JS_NewUint32(ctx, (uint32_t) dlcf->access);

    case 2: /* minDeleteDepth */
        return JS_NewUint32(ctx, (uint32_t) dlcf->min_delete_depth);

    case 3: /* createFullPutPath */
        return JS_NewBool(ctx, (int) dlcf->create_full_put_path);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_dav_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("methods",           ngx_js_dav_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("access",            ngx_js_dav_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("minDeleteDepth",    ngx_js_dav_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("createFullPutPath", ngx_js_dav_get, NULL, 3),
};


ngx_int_t
ngx_js_dav_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_dav_class_id,
                       &ngx_js_dav_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_dav_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_dav_proto_funcs,
                               countof(ngx_js_dav_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_dav_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_dav(JSContext *ctx, ngx_http_dav_loc_conf_t *dlcf)
{
    JSValue              obj;
    ngx_js_dav_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_dav_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->dlcf = dlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_dav_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


#endif /* NGX_HTTP_DAV */
