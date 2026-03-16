
/*
 * Copyright (C) nginx JS contributors
 *
 * Stream upstream COM layer — Stage 53.
 *
 * NginxStreamPeer     — config-phase peer (ngx_stream_upstream_server_t)
 * NginxStreamRRPeer   — runtime RR peer   (ngx_stream_upstream_rr_peer_t)
 * NginxStreamUpstream — wraps ngx_stream_upstream_srv_conf_t
 *
 * nginx.stream.upstreams[i].name
 * nginx.stream.upstreams[i].zone
 * nginx.stream.upstreams[i].peers[]
 *   .address / .weight / .maxFails / .failTimeout / .maxConns / .down
 *   .backup / .conns / .server / .fails  (RR only)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include <ngx_stream_upstream_round_robin.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* Class ID definitions                                                 */
/* ------------------------------------------------------------------ */

JSClassID  ngx_js_stream_upstream_class_id;
JSClassID  ngx_js_stream_peer_class_id;
JSClassID  ngx_js_stream_rr_peer_class_id;


/* ------------------------------------------------------------------ */
/* NginxStreamPeer — config-phase peer (ngx_stream_upstream_server_t)  */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_stream_upstream_server_t  *srv;
} ngx_js_stream_peer_opaque_t;


static void
ngx_js_stream_peer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_peer_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_peer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_peer_class = {
    "NginxStreamPeer",
    .finalizer = ngx_js_stream_peer_finalizer,
};


/*
 * Magic values for ngx_js_stream_peer_get:
 *   0 — address      (r/o)
 *   1 — weight       (r/w)
 *   2 — maxFails     (r/w)
 *   3 — down         (r/w)
 *   4 — backup       (r/o)
 *   5 — failTimeout  (r/w, seconds)
 *   6 — maxConns     (r/w)
 */
static JSValue
ngx_js_stream_peer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_peer_opaque_t   *op;
    ngx_stream_upstream_server_t  *srv;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    srv = op->srv;

    switch (magic) {
    case 0: return JS_NewStringLen(ctx, (const char *) srv->name.data,
                                   srv->name.len);
    case 1: return JS_NewInt32(ctx, (int32_t) srv->weight);
    case 2: return JS_NewInt32(ctx, (int32_t) srv->max_fails);
    case 3: return JS_NewBool(ctx,  (int) srv->down);
    case 4: return JS_NewBool(ctx,  (int) srv->backup);
    case 5: return JS_NewInt64(ctx, (int64_t) srv->fail_timeout);
    case 6: return JS_NewInt64(ctx, (int64_t) srv->max_conns);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_stream_peer_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_stream_peer_opaque_t   *op;
    ngx_stream_upstream_server_t  *srv;
    int32_t                        i32;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    srv = op->srv;

    switch (magic) {
    case 1:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->weight = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 2:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->max_fails = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 3:
        srv->down = (ngx_uint_t) JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 5:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->fail_timeout = (time_t) i32;
        return JS_UNDEFINED;

    case 6:
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->max_conns = (ngx_uint_t) i32;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_peer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",     ngx_js_stream_peer_get, NULL,                    0),
    JS_CGETSET_MAGIC_DEF("weight",      ngx_js_stream_peer_get, ngx_js_stream_peer_set,  1),
    JS_CGETSET_MAGIC_DEF("maxFails",    ngx_js_stream_peer_get, ngx_js_stream_peer_set,  2),
    JS_CGETSET_MAGIC_DEF("down",        ngx_js_stream_peer_get, ngx_js_stream_peer_set,  3),
    JS_CGETSET_MAGIC_DEF("backup",      ngx_js_stream_peer_get, NULL,                    4),
    JS_CGETSET_MAGIC_DEF("failTimeout", ngx_js_stream_peer_get, ngx_js_stream_peer_set,  5),
    JS_CGETSET_MAGIC_DEF("maxConns",    ngx_js_stream_peer_get, ngx_js_stream_peer_set,  6),
};


/* ------------------------------------------------------------------ */
/* NginxStreamRRPeer — runtime RR peer (ngx_stream_upstream_rr_peer_t) */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_stream_upstream_rr_peers_t  *peers;   /* parent group (for locking) */
    ngx_stream_upstream_rr_peer_t   *peer;
    ngx_uint_t                       backup;
} ngx_js_stream_rr_peer_opaque_t;


static void
ngx_js_stream_rr_peer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_rr_peer_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_rr_peer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_rr_peer_class = {
    "NginxStreamRRPeer",
    .finalizer = ngx_js_stream_rr_peer_finalizer,
};


/*
 * Magic values for ngx_js_stream_rr_peer_get:
 *   0 — address      (r/o)
 *   1 — weight       (r/w, with wlock)
 *   2 — maxFails     (r/w, with wlock)
 *   3 — down         (r/w, with wlock; updates peers->tries)
 *   4 — backup       (r/o)
 *   5 — conns        (r/o, runtime stat)
 *   6 — failTimeout  (r/w, seconds, with wlock)
 *   7 — maxConns     (r/w, with wlock)
 *   8 — server       (r/o, configured server address string)
 *   9 — fails        (r/o, runtime fail counter)
 */
static JSValue
ngx_js_stream_rr_peer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_rr_peer_opaque_t  *op;
    ngx_stream_upstream_rr_peer_t   *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_rr_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    p = op->peer;

    switch (magic) {
    case 0: return JS_NewStringLen(ctx, (const char *) p->name.data,
                                   p->name.len);
    case 1: return JS_NewInt32(ctx,  (int32_t) p->weight);
    case 2: return JS_NewInt32(ctx,  (int32_t) p->max_fails);
    case 3: return JS_NewBool(ctx,   (int) p->down);
    case 4: return JS_NewBool(ctx,   (int) op->backup);
    case 5: return JS_NewInt32(ctx,  (int32_t) p->conns);
    case 6: return JS_NewInt64(ctx,  (int64_t) p->fail_timeout);
    case 7: return JS_NewInt64(ctx,  (int64_t) p->max_conns);
    case 8: return JS_NewStringLen(ctx, (const char *) p->server.data,
                                   p->server.len);
    case 9: return JS_NewInt64(ctx,  (int64_t) p->fails);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_stream_rr_peer_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_stream_rr_peer_opaque_t  *op;
    ngx_stream_upstream_rr_peers_t  *peers;
    ngx_stream_upstream_rr_peer_t   *p;
    int32_t                          i32;
    ngx_uint_t                       was_down;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_rr_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    peers = op->peers;
    p     = op->peer;

    switch (magic) {

    case 1: /* weight */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_stream_upstream_rr_peers_wlock(peers);
        p->weight           = (ngx_int_t) i32;
        p->effective_weight = p->weight;
        peers->total_weight = 0;
        {
            ngx_stream_upstream_rr_peer_t *q;
            for (q = peers->peer; q; q = q->next) {
                peers->total_weight += (ngx_uint_t) q->weight;
            }
        }
        peers->weighted = (peers->total_weight != peers->number);
        ngx_stream_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 2: /* maxFails */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_stream_upstream_rr_peers_wlock(peers);
        p->max_fails = (ngx_uint_t) i32;
        ngx_stream_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 3: /* down */
        ngx_stream_upstream_rr_peers_wlock(peers);
        was_down = p->down;
        p->down  = (ngx_uint_t) JS_ToBool(ctx, val);
        if (!was_down && p->down) {
            peers->tries--;
        } else if (was_down && !p->down) {
            peers->tries++;
        }
        ngx_stream_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 6: /* failTimeout */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_stream_upstream_rr_peers_wlock(peers);
        p->fail_timeout = (time_t) i32;
        ngx_stream_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 7: /* maxConns */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_stream_upstream_rr_peers_wlock(peers);
        p->max_conns = (ngx_uint_t) i32;
        ngx_stream_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_rr_peer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",     ngx_js_stream_rr_peer_get, NULL,                       0),
    JS_CGETSET_MAGIC_DEF("weight",      ngx_js_stream_rr_peer_get, ngx_js_stream_rr_peer_set,  1),
    JS_CGETSET_MAGIC_DEF("maxFails",    ngx_js_stream_rr_peer_get, ngx_js_stream_rr_peer_set,  2),
    JS_CGETSET_MAGIC_DEF("down",        ngx_js_stream_rr_peer_get, ngx_js_stream_rr_peer_set,  3),
    JS_CGETSET_MAGIC_DEF("backup",      ngx_js_stream_rr_peer_get, NULL,                       4),
    JS_CGETSET_MAGIC_DEF("conns",       ngx_js_stream_rr_peer_get, NULL,                       5),
    JS_CGETSET_MAGIC_DEF("failTimeout", ngx_js_stream_rr_peer_get, ngx_js_stream_rr_peer_set,  6),
    JS_CGETSET_MAGIC_DEF("maxConns",    ngx_js_stream_rr_peer_get, ngx_js_stream_rr_peer_set,  7),
    JS_CGETSET_MAGIC_DEF("server",      ngx_js_stream_rr_peer_get, NULL,                       8),
    JS_CGETSET_MAGIC_DEF("fails",       ngx_js_stream_rr_peer_get, NULL,                       9),
};


static JSValue
ngx_js_wrap_stream_rr_peer(JSContext *ctx,
    ngx_stream_upstream_rr_peers_t *peers,
    ngx_stream_upstream_rr_peer_t *peer, ngx_uint_t backup)
{
    JSValue                          obj;
    ngx_js_stream_rr_peer_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_rr_peer_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->peers  = peers;
    op->peer   = peer;
    op->backup = backup;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_rr_peer_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* NginxStreamUpstream wrapper                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_stream_upstream_srv_conf_t  *uscf;
} ngx_js_stream_upstream_opaque_t;


static void
ngx_js_stream_upstream_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_upstream_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_upstream_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_upstream_class = {
    "NginxStreamUpstream",
    .finalizer = ngx_js_stream_upstream_finalizer,
};


static JSValue
ngx_js_stream_upstream_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_upstream_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* name */
        return JS_NewStringLen(ctx,
                               (const char *) op->uscf->host.data,
                               op->uscf->host.len);

#if (NGX_STREAM_UPSTREAM_ZONE)
    case 1: /* zone */
        if (op->uscf->shm_zone == NULL) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx,
                               (const char *) op->uscf->shm_zone->shm.name.data,
                               op->uscf->shm_zone->shm.name.len);
#else
    case 1:
        return JS_NULL;
#endif
    }

    return JS_UNDEFINED;
}


/*
 * nginx.stream.upstreams[i].peers[]
 *
 * Returns NginxStreamRRPeer objects when postconfiguration has set up
 * uscf->peer.data; falls back to config-phase NginxStreamPeer objects.
 */
static JSValue
ngx_js_stream_upstream_get_peers(JSContext *ctx, JSValueConst this_val,
    int magic)
{
    ngx_js_stream_upstream_opaque_t  *op;
    ngx_stream_upstream_rr_peers_t   *peers, *pg;
    ngx_stream_upstream_rr_peer_t    *p;
    ngx_stream_upstream_server_t     *srv;
    JSValue                           arr;
    uint32_t                          idx;
    ngx_uint_t                        i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    /* Use live RR peers if postconfiguration has run */
    if (op->uscf->peer.data != NULL) {
        peers = (ngx_stream_upstream_rr_peers_t *) op->uscf->peer.data;
        idx   = 0;

        ngx_stream_upstream_rr_peers_rlock(peers);

        for (pg = peers; pg; pg = pg->next) {
            ngx_uint_t  is_backup = (pg != peers);

            for (p = pg->peer; p; p = p->next) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                                     ngx_js_wrap_stream_rr_peer(ctx, pg, p,
                                                                 is_backup));
            }
        }

        ngx_stream_upstream_rr_peers_unlock(peers);

        return arr;
    }

    /* Fallback: config-phase NginxStreamPeer objects */
    if (op->uscf->servers == NULL) {
        return arr;
    }

    srv = op->uscf->servers->elts;

    for (i = 0; i < op->uscf->servers->nelts; i++) {
        ngx_js_stream_peer_opaque_t  *pop;
        JSValue                       obj;

        pop = js_mallocz(ctx, sizeof(ngx_js_stream_peer_opaque_t));
        if (!pop) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }

        pop->srv = &srv[i];

        obj = JS_NewObjectClass(ctx, ngx_js_stream_peer_class_id);
        if (JS_IsException(obj)) {
            js_free(ctx, pop);
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }

        JS_SetOpaque(obj, pop);
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, obj);
    }

    return arr;
}


static const JSCFunctionListEntry  ngx_js_stream_upstream_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name",  ngx_js_stream_upstream_get,       NULL, 0),
    JS_CGETSET_MAGIC_DEF("zone",  ngx_js_stream_upstream_get,       NULL, 1),
    JS_CGETSET_MAGIC_DEF("peers", ngx_js_stream_upstream_get_peers, NULL, 0),
};


static JSValue
ngx_js_wrap_stream_upstream(JSContext *ctx,
    ngx_stream_upstream_srv_conf_t *uscf)
{
    JSValue                           obj;
    ngx_js_stream_upstream_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_upstream_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->uscf = uscf;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_upstream_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* Register / install                                                   */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_stream_upstream_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_stream_upstream_class_id,
                    &ngx_js_stream_upstream_class) < 0
     || JS_NewClass(rt, ngx_js_stream_peer_class_id,
                    &ngx_js_stream_peer_class) < 0
     || JS_NewClass(rt, ngx_js_stream_rr_peer_class_id,
                    &ngx_js_stream_rr_peer_class) < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_js_stream_upstream_install_protos(JSContext *ctx)
{
    JSValue  proto;

    /* NginxStreamPeer */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) { return NGX_ERROR; }
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_peer_proto_funcs,
                               countof(ngx_js_stream_peer_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_peer_class_id, proto);

    /* NginxStreamRRPeer */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) { return NGX_ERROR; }
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_rr_peer_proto_funcs,
                               countof(ngx_js_stream_rr_peer_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_rr_peer_class_id, proto);

    /* NginxStreamUpstream */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) { return NGX_ERROR; }
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_upstream_proto_funcs,
                               countof(ngx_js_stream_upstream_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_upstream_class_id, proto);

    return NGX_OK;
}


/*
 * Build nginx.stream.upstreams[] and attach it to stream_obj.
 * Called from ngx_js_stream_install() in ngx_js_stream_listener.c.
 */
ngx_int_t
ngx_js_stream_upstream_com_install(JSContext *ctx, JSValue stream_obj,
    ngx_cycle_t *cycle)
{
    JSValue                          upstreams_arr;
    ngx_stream_upstream_main_conf_t *umcf;
    ngx_stream_upstream_srv_conf_t **uscfp;
    ngx_uint_t                       i;

    if (ngx_js_stream_upstream_install_protos(ctx) != NGX_OK) {
        return NGX_ERROR;
    }

    upstreams_arr = JS_NewArray(ctx);
    if (JS_IsException(upstreams_arr)) {
        return NGX_ERROR;
    }

    if (cycle->conf_ctx == NULL) {
        JS_SetPropertyStr(ctx, stream_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    umcf = ngx_stream_cycle_get_module_main_conf(cycle,
                                                 ngx_stream_upstream_module);
    if (umcf == NULL) {
        JS_SetPropertyStr(ctx, stream_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {
        /* Only named upstreams (upstream {} blocks) */
        if (!(uscfp[i]->flags & NGX_STREAM_UPSTREAM_CREATE)) {
            continue;
        }

        JS_SetPropertyUint32(ctx, upstreams_arr, (uint32_t) i,
                             ngx_js_wrap_stream_upstream(ctx, uscfp[i]));
    }

    JS_SetPropertyStr(ctx, stream_obj, "upstreams", upstreams_arr);
    return NGX_OK;
}
