
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 14 — nginx.events (NginxEvents)
 *
 * Exposes the events{} block configuration as nginx.events:
 *
 *   connections       number   r/o  worker_connections
 *   use               string   r/o  event method name ("epoll", "kqueue", …)
 *   multiAccept       boolean  r/w  multi_accept on/off
 *   acceptMutex       boolean  r/w  accept_mutex on/off
 *   acceptMutexDelay  number   r/w  accept_mutex_delay (ms)
 *
 * connections is read-only because the connection pool is already
 * allocated at startup; changing ecf->connections would only make
 * the field inconsistent with cycle->connection_n.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <quickjs.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"


JSClassID  ngx_js_events_class_id;


typedef struct {
    ngx_event_conf_t  *ecf;
} ngx_js_events_opaque_t;


static void
ngx_js_events_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_events_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_events_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_events_class = {
    "NginxEvents",
    .finalizer = ngx_js_events_finalizer,
};


/*
 * Magic values:
 *   0 — connections       (r/o)
 *   1 — use               (r/o)
 *   2 — multiAccept       (r/w)
 *   3 — acceptMutex       (r/w)
 *   4 — acceptMutexDelay  (r/w)
 */
static JSValue
ngx_js_events_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_events_opaque_t  *op;
    ngx_event_conf_t        *ecf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_events_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    ecf = op->ecf;

    switch (magic) {

    case 0: /* connections — r/o */
        return JS_NewInt64(ctx, (int64_t) ecf->connections);

    case 1: /* use — event method name */
        if (ecf->name == NULL || ecf->name == (void *) NGX_CONF_UNSET) {
            return JS_NewString(ctx, "");
        }
        return JS_NewString(ctx, (const char *) ecf->name);

    case 2: /* multiAccept */
        return JS_NewBool(ctx, (int) ecf->multi_accept);

    case 3: /* acceptMutex */
        return JS_NewBool(ctx, (int) ecf->accept_mutex);

    case 4: /* acceptMutexDelay */
        return JS_NewInt64(ctx, (int64_t) ecf->accept_mutex_delay);

    } /* switch */

    return JS_UNDEFINED;
}


static JSValue
ngx_js_events_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_events_opaque_t  *op;
    ngx_event_conf_t        *ecf;
    int64_t                  n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_events_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    ecf = op->ecf;

    switch (magic) {

    case 2: /* multiAccept */
        ecf->multi_accept = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 3: /* acceptMutex */
        ecf->accept_mutex = JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 4: /* acceptMutexDelay */
        if (JS_ToInt64(ctx, &n, val) < 0) {
            return JS_EXCEPTION;
        }
        ecf->accept_mutex_delay = (ngx_msec_t) n;
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_events_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("connections",      ngx_js_events_get, NULL,               0),
    JS_CGETSET_MAGIC_DEF("use",              ngx_js_events_get, NULL,               1),
    JS_CGETSET_MAGIC_DEF("multiAccept",      ngx_js_events_get, ngx_js_events_set,  2),
    JS_CGETSET_MAGIC_DEF("acceptMutex",      ngx_js_events_get, ngx_js_events_set,  3),
    JS_CGETSET_MAGIC_DEF("acceptMutexDelay", ngx_js_events_get, ngx_js_events_set,  4),
};


ngx_int_t
ngx_js_events_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_events_class_id, &ngx_js_events_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_events_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_events_proto_funcs,
                               countof(ngx_js_events_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_events_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_events(JSContext *ctx, void *ecf_void)
{
    JSValue                  obj;
    ngx_js_events_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_events_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->ecf = (ngx_event_conf_t *) ecf_void;

    obj = JS_NewObjectClass(ctx, ngx_js_events_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
