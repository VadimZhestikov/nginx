
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 13b: access log location configuration.
 *
 * Exposes location.log as a NginxLog object:
 *
 *   off    boolean    true when "access_log off" is set
 *   logs   object[]   one entry per access_log directive:
 *            path     string | null   literal file path, or null for
 *                                     syslog targets and variable-based paths
 *            format   string          log format name (e.g. "combined")
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_log_module.h"


typedef struct {
    ngx_http_log_loc_conf_t  *llcf;
} ngx_js_log_opaque_t;


static void
ngx_js_log_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_log_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_log_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_log_class = {
    "NginxLog",
    .finalizer = ngx_js_log_finalizer
};


/*
 * logs getter: array of {path, format} objects, one per access_log directive.
 * path is null for syslog targets and variable-based paths.
 */
static JSValue
ngx_js_log_get_logs(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_log_opaque_t  *op;
    ngx_http_log_t       *log;
    JSValue               arr, obj;
    ngx_uint_t            i;
    ngx_str_t            *path, *fmt;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_log_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->llcf->logs == NULL) {
        return arr;
    }

    log = op->llcf->logs->elts;

    for (i = 0; i < op->llcf->logs->nelts; i++) {
        obj = JS_NewObject(ctx);

        /* path: literal file path, null for syslog or dynamic */
        if (log[i].file != NULL && log[i].script == NULL) {
            path = &log[i].file->name;
            JS_SetPropertyStr(ctx, obj, "path",
                JS_NewStringLen(ctx, (const char *) path->data, path->len));
        } else {
            JS_SetPropertyStr(ctx, obj, "path", JS_NULL);
        }

        /* format name */
        if (log[i].format != NULL) {
            fmt = &log[i].format->name;
            JS_SetPropertyStr(ctx, obj, "format",
                JS_NewStringLen(ctx, (const char *) fmt->data, fmt->len));
        } else {
            JS_SetPropertyStr(ctx, obj, "format", JS_NULL);
        }

        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


/*
 * Magic values for ngx_js_log_get:
 *   0 — off
 */
static JSValue
ngx_js_log_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_log_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_log_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: return JS_NewBool(ctx, op->llcf->off);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_log_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("off",  ngx_js_log_get,      NULL, 0),
    JS_CGETSET_DEF       ("logs", ngx_js_log_get_logs, NULL),
};


JSValue
ngx_js_wrap_log(JSContext *ctx, ngx_http_log_loc_conf_t *llcf)
{
    JSValue               obj, proto;
    ngx_js_log_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_log_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->llcf = llcf;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_log_proto_funcs,
                               countof(ngx_js_log_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_log_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_log_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_log_class_id, &ngx_js_log_class) < 0
           ? NGX_ERROR : NGX_OK;
}
