
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13i — location.ssi (NginxSsi)
 *
 * Exposes ngx_http_ssi_loc_conf_t as a JS object on location.ssi:
 *
 *   enable                 boolean — ssi on/off
 *   silentErrors           boolean — ssi_silent_errors on/off
 *   ignoreRecycledBuffers  boolean — ssi_ignore_recycled_buffers on/off
 *   lastModified           boolean — ssi_last_modified on/off
 *   minFileChunk           number  — ssi_min_file_chunk (bytes)
 *   valueLen               number  — ssi_value_len (bytes)
 *
 * ngx_http_ssi_loc_conf_t is private to ngx_http_ssi_filter_module.c and
 * not exposed in its public header.  We shadow it locally — field order
 * and types must match exactly.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"


/*
 * Local shadow of ngx_http_ssi_loc_conf_t.
 * Must match the private struct in ngx_http_ssi_filter_module.c exactly.
 */
typedef struct {
    ngx_flag_t    enable;
    ngx_flag_t    silent_errors;
    ngx_flag_t    ignore_recycled_buffers;
    ngx_flag_t    last_modified;

    ngx_hash_t    types;

    size_t        min_file_chunk;
    size_t        value_len;

    ngx_array_t  *types_keys;
} ngx_js_ssi_conf_t;


typedef struct {
    ngx_js_ssi_conf_t  *slcf;
} ngx_js_ssi_opaque_t;


static void
ngx_js_ssi_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_ssi_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_ssi_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_ssi_class = {
    "NginxSsi",
    .finalizer = ngx_js_ssi_finalizer,
};


static JSValue
ngx_js_ssi_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_ssi_opaque_t  *op;
    ngx_js_ssi_conf_t    *slcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssi_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    slcf = op->slcf;

    switch (magic) {

    case 0: /* enable */
        return JS_NewBool(ctx, (int) slcf->enable);

    case 1: /* silentErrors */
        return JS_NewBool(ctx, (int) slcf->silent_errors);

    case 2: /* ignoreRecycledBuffers */
        return JS_NewBool(ctx, (int) slcf->ignore_recycled_buffers);

    case 3: /* lastModified */
        return JS_NewBool(ctx, (int) slcf->last_modified);

    case 4: /* minFileChunk */
        return JS_NewUint32(ctx, (uint32_t) slcf->min_file_chunk);

    case 5: /* valueLen */
        return JS_NewUint32(ctx, (uint32_t) slcf->value_len);

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_ssi_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable",                ngx_js_ssi_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("silentErrors",          ngx_js_ssi_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("ignoreRecycledBuffers", ngx_js_ssi_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("lastModified",          ngx_js_ssi_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("minFileChunk",          ngx_js_ssi_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("valueLen",              ngx_js_ssi_get, NULL, 5),
};


ngx_int_t
ngx_js_ssi_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_ssi_class_id, &ngx_js_ssi_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_ssi_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_ssi_proto_funcs,
                               countof(ngx_js_ssi_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_ssi_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_ssi(JSContext *ctx, void *slcf_ptr)
{
    JSValue               obj;
    ngx_js_ssi_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_ssi_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->slcf = slcf_ptr;

    obj = JS_NewObjectClass(ctx, ngx_js_ssi_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
