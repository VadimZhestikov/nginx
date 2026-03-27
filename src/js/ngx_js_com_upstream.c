
/*
 * Copyright (C) nginx JS contributors
 *
 * Upstream COM layer — Phase 1 + Phase 2 + Phase 3.
 *
 * Phase 1/2 — NginxPeer (config-phase, ngx_http_upstream_server_t):
 *   nginx.http.upstreams[i].name
 *
 * Phase 3 — NginxRRPeer (runtime, ngx_http_upstream_rr_peer_t):
 *   nginx.http.upstreams[i].peers[]   — live RR peers from uscf->peer.data
 *   nginx.http.upstreams[i].peers[j].address   — "host:port" string
 *   nginx.http.upstreams[i].peers[j].weight    — r/w
 *   nginx.http.upstreams[i].peers[j].maxFails  — r/w
 *   nginx.http.upstreams[i].peers[j].down      — r/w (with peers wlock)
 *   nginx.http.upstreams[i].peers[j].backup    — r/o
 *   nginx.http.upstreams[i].peers[j].conns     — r/o (runtime stat)
 *   nginx.http.upstreams[i].addPeer(addr, opts)
 *   nginx.http.upstreams[i].removePeer(addr)
 *
 * At init_conf() time peers->shpool is NULL (zone init runs later), so
 * locking macros are no-ops and allocation uses cycle->pool.  The zone
 * init callback copies all pool-allocated peers to shared memory before
 * workers fork, so JS mutations are fully propagated.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_upstream_round_robin.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* NginxPeer — Phase 1/2 config-phase peer wrapper                     */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_upstream_server_t  *srv;
} ngx_js_peer_opaque_t;


static void
ngx_js_peer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_peer_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_peer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_peer_class = {
    "NginxPeer",
    .finalizer = ngx_js_peer_finalizer
};


static JSValue
ngx_js_peer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_peer_opaque_t        *op;
    ngx_http_upstream_server_t  *srv;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    srv = op->srv;

    switch (magic) {
    case 0: return JS_NewStringLen(ctx, (const char *) srv->name.data,
                                   srv->name.len);
    case 1: return JS_NewInt32(ctx, (int32_t) srv->weight);
    case 2: return JS_NewInt32(ctx, (int32_t) srv->max_fails);
    case 3: return JS_NewBool(ctx, (int) srv->down);
    case 4: return JS_NewBool(ctx, (int) srv->backup);
    case 5: return JS_NewInt64(ctx, (int64_t) srv->fail_timeout);
    case 6: return JS_NewInt64(ctx, (int64_t) srv->max_conns);
    }

    return JS_UNDEFINED;
}



static JSValue
ngx_js_peer_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_peer_opaque_t        *op;
    ngx_http_upstream_server_t  *srv;
    int32_t                      i32;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_peer_class_id);
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

    case 5: /* failTimeout */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->fail_timeout = (time_t) i32;
        return JS_UNDEFINED;

    case 6: /* maxConns */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        srv->max_conns = (ngx_uint_t) i32;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/* Settable property names common to NginxPeer and NginxRRPeer */
static const char * const ngx_js_peer_snap_props[] = {
    "weight", "maxFails", "down", "failTimeout", "maxConns",
    NULL
};


const char * const *
ngx_js_peer_settable_props(void)
{
    return ngx_js_peer_snap_props;
}


static JSValue
ngx_js_peer_fn_snapshot(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_snapshot_opaque_t  *snap_op;
    ngx_js_snap_node_t        *node;
    JSValue                    snap_obj;

    snap_obj = ngx_js_snapshot_new(ctx);
    if (JS_IsException(snap_obj)) {
        return snap_obj;
    }

    snap_op = JS_GetOpaque(snap_obj, ngx_js_snapshot_class_id);

    node = ngx_js_snap_capture_node(ctx, this_val, ngx_js_peer_snap_props);
    if (!node) {
        JS_FreeValue(ctx, snap_obj);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_js_snapshot_append_node(snap_op, node);
    return snap_obj;
}


static const JSCFunctionListEntry ngx_js_peer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",     ngx_js_peer_get, NULL,            0),
    JS_CGETSET_MAGIC_DEF("weight",      ngx_js_peer_get, ngx_js_peer_set, 1),
    JS_CGETSET_MAGIC_DEF("maxFails",    ngx_js_peer_get, ngx_js_peer_set, 2),
    JS_CGETSET_MAGIC_DEF("down",        ngx_js_peer_get, ngx_js_peer_set, 3),
    JS_CGETSET_MAGIC_DEF("backup",      ngx_js_peer_get, NULL,            4),
    JS_CGETSET_MAGIC_DEF("failTimeout", ngx_js_peer_get, ngx_js_peer_set, 5),
    JS_CGETSET_MAGIC_DEF("maxConns",    ngx_js_peer_get, ngx_js_peer_set, 6),
    JS_CFUNC_DEF("snapshot", 0, ngx_js_peer_fn_snapshot),
};


ngx_int_t
ngx_js_peer_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_peer_proto_funcs,
                               countof(ngx_js_peer_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_peer_class_id, proto);
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* NginxRRPeer — Phase 3 runtime RR peer wrapper                       */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_upstream_rr_peers_t  *peers;   /* parent group (for locking) */
    ngx_http_upstream_rr_peer_t   *peer;
    ngx_uint_t                     backup;
} ngx_js_rr_peer_opaque_t;


static void
ngx_js_rr_peer_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_rr_peer_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_rr_peer_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_rr_peer_class = {
    "NginxRRPeer",
    .finalizer = ngx_js_rr_peer_finalizer
};


/*
 * Magic values for ngx_js_rr_peer_get / ngx_js_rr_peer_set:
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
ngx_js_rr_peer_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_rr_peer_opaque_t      *op;
    ngx_http_upstream_rr_peer_t  *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rr_peer_class_id);
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
ngx_js_rr_peer_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_rr_peer_opaque_t       *op;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *p;
    int32_t                        i32;
    ngx_uint_t                     was_down;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_rr_peer_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    peers = op->peers;
    p     = op->peer;

    switch (magic) {

    case 1: /* weight */
        if (JS_ToInt32(ctx, &i32, val)) {
            return JS_EXCEPTION;
        }
        ngx_http_upstream_rr_peers_wlock(peers);
        p->weight           = (ngx_int_t) i32;
        p->effective_weight = p->weight;
        peers->total_weight = 0;
        {
            ngx_http_upstream_rr_peer_t *q;
            for (q = peers->peer; q; q = q->next) {
                peers->total_weight += q->weight;
            }
        }
        peers->weighted = (peers->total_weight != peers->number);
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 2: /* maxFails */
        if (JS_ToInt32(ctx, &i32, val)) {
            return JS_EXCEPTION;
        }
        ngx_http_upstream_rr_peers_wlock(peers);
        p->max_fails = (ngx_uint_t) i32;
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 3: /* down */
        ngx_http_upstream_rr_peers_wlock(peers);
        was_down = p->down;
        p->down  = (ngx_uint_t) JS_ToBool(ctx, val);
        if (!was_down && p->down) {
            peers->tries--;
        } else if (was_down && !p->down) {
            peers->tries++;
        }
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 6: /* failTimeout */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_http_upstream_rr_peers_wlock(peers);
        p->fail_timeout = (time_t) i32;
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 7: /* maxConns */
        if (JS_ToInt32(ctx, &i32, val)) { return JS_EXCEPTION; }
        ngx_http_upstream_rr_peers_wlock(peers);
        p->max_conns = (ngx_uint_t) i32;
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_rr_peer_fn_snapshot(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_snapshot_opaque_t  *snap_op;
    ngx_js_snap_node_t        *node;
    JSValue                    snap_obj;

    snap_obj = ngx_js_snapshot_new(ctx);
    if (JS_IsException(snap_obj)) {
        return snap_obj;
    }

    snap_op = JS_GetOpaque(snap_obj, ngx_js_snapshot_class_id);

    node = ngx_js_snap_capture_node(ctx, this_val, ngx_js_peer_snap_props);
    if (!node) {
        JS_FreeValue(ctx, snap_obj);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_js_snapshot_append_node(snap_op, node);
    return snap_obj;
}


/*
 * upstream.snapshot() — creates a NginxSnapshot containing one node
 * per peer (all peers in the upstream's current peers[] array).
 * Works for both config-phase (NginxPeer) and runtime (NginxRRPeer).
 */
static JSValue
ngx_js_upstream_fn_snapshot(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_snapshot_opaque_t  *snap_op;
    ngx_js_snap_node_t        *node;
    JSValue                    snap_obj, peers_arr, peer_obj, len_val;
    uint32_t                   len, i;

    snap_obj = ngx_js_snapshot_new(ctx);
    if (JS_IsException(snap_obj)) {
        return snap_obj;
    }

    snap_op = JS_GetOpaque(snap_obj, ngx_js_snapshot_class_id);

    peers_arr = JS_GetPropertyStr(ctx, this_val, "peers");

    if (JS_IsException(peers_arr) || JS_IsNull(peers_arr)
        || JS_IsUndefined(peers_arr))
    {
        JS_FreeValue(ctx, peers_arr);
        return snap_obj;   /* empty snapshot — no peers to capture */
    }

    len_val = JS_GetPropertyStr(ctx, peers_arr, "length");
    if (JS_IsException(len_val)) {
        JS_FreeValue(ctx, peers_arr);
        JS_FreeValue(ctx, snap_obj);
        return JS_EXCEPTION;
    }

    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    for (i = 0; i < len; i++) {
        peer_obj = JS_GetPropertyUint32(ctx, peers_arr, i);

        if (JS_IsException(peer_obj) || JS_IsUndefined(peer_obj)
            || JS_IsNull(peer_obj))
        {
            JS_FreeValue(ctx, peer_obj);
            continue;
        }

        node = ngx_js_snap_capture_node(ctx, peer_obj,
                                        ngx_js_peer_snap_props);
        JS_FreeValue(ctx, peer_obj);

        if (!node) {
            JS_FreeValue(ctx, peers_arr);
            JS_FreeValue(ctx, snap_obj);
            return JS_ThrowOutOfMemory(ctx);
        }

        ngx_js_snapshot_append_node(snap_op, node);
    }

    JS_FreeValue(ctx, peers_arr);
    return snap_obj;
}


static const JSCFunctionListEntry ngx_js_rr_peer_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",     ngx_js_rr_peer_get, NULL,               0),
    JS_CGETSET_MAGIC_DEF("weight",      ngx_js_rr_peer_get, ngx_js_rr_peer_set, 1),
    JS_CGETSET_MAGIC_DEF("maxFails",    ngx_js_rr_peer_get, ngx_js_rr_peer_set, 2),
    JS_CGETSET_MAGIC_DEF("down",        ngx_js_rr_peer_get, ngx_js_rr_peer_set, 3),
    JS_CGETSET_MAGIC_DEF("backup",      ngx_js_rr_peer_get, NULL,               4),
    JS_CGETSET_MAGIC_DEF("conns",       ngx_js_rr_peer_get, NULL,               5),
    JS_CGETSET_MAGIC_DEF("failTimeout", ngx_js_rr_peer_get, ngx_js_rr_peer_set, 6),
    JS_CGETSET_MAGIC_DEF("maxConns",    ngx_js_rr_peer_get, ngx_js_rr_peer_set, 7),
    JS_CGETSET_MAGIC_DEF("server",      ngx_js_rr_peer_get, NULL,               8),
    JS_CGETSET_MAGIC_DEF("fails",       ngx_js_rr_peer_get, NULL,               9),
    JS_CFUNC_DEF("snapshot", 0, ngx_js_rr_peer_fn_snapshot),
};


ngx_int_t
ngx_js_rr_peer_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_rr_peer_proto_funcs,
                               countof(ngx_js_rr_peer_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_rr_peer_class_id, proto);
    return NGX_OK;
}


static JSValue
ngx_js_wrap_rr_peer(JSContext *ctx, ngx_http_upstream_rr_peers_t *peers,
    ngx_http_upstream_rr_peer_t *peer, ngx_uint_t backup)
{
    JSValue                   obj;
    ngx_js_rr_peer_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_rr_peer_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->peers  = peers;
    op->peer   = peer;
    op->backup = backup;

    obj = JS_NewObjectClass(ctx, ngx_js_rr_peer_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* NginxUpstream wrapper                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_upstream_srv_conf_t  *uscf;
} ngx_js_upstream_opaque_t;


static void
ngx_js_upstream_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_upstream_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_upstream_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_upstream_class = {
    "NginxUpstream",
    .finalizer = ngx_js_upstream_finalizer
};


static JSValue
ngx_js_upstream_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_upstream_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {
    case 0: /* name */
        return JS_NewStringLen(ctx,
                               (const char *) op->uscf->host.data,
                               op->uscf->host.len);

#if (NGX_HTTP_UPSTREAM_ZONE)
    case 1: /* zone — shm zone name, or null if not zone-backed */
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
 * nginx.http.upstreams[i].peers[] — Phase 3 live RR peers.
 *
 * Returns NginxRRPeer objects from uscf->peer.data (the round-robin
 * peers structure set up by postconfiguration).  Primary peers come
 * first, then backup peers (peers->next group).
 *
 * If peer.data is not yet set, falls back to the config-phase
 * NginxPeer objects from uscf->servers.
 */
static JSValue
ngx_js_upstream_get_peers(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_upstream_opaque_t      *op;
    ngx_http_upstream_rr_peers_t  *peers, *pg;
    ngx_http_upstream_rr_peer_t   *p;
    ngx_http_upstream_server_t    *srv;
    JSValue                        arr;
    uint32_t                       idx;
    ngx_uint_t                     i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    /* Phase 3: use live RR peers if postconfiguration has run */
    if (op->uscf->peer.data != NULL) {
        peers = (ngx_http_upstream_rr_peers_t *) op->uscf->peer.data;
        idx   = 0;

        ngx_http_upstream_rr_peers_rlock(peers);

        for (pg = peers; pg; pg = pg->next) {
            ngx_uint_t is_backup = (pg != peers);

            for (p = pg->peer; p; p = p->next) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                                     ngx_js_wrap_rr_peer(ctx, pg, p,
                                                         is_backup));
            }
        }

        ngx_http_upstream_rr_peers_unlock(peers);

        return arr;
    }

    /* Fallback: config-phase NginxPeer objects from uscf->servers */
    if (op->uscf->servers == NULL) {
        return arr;
    }

    srv = op->uscf->servers->elts;

    for (i = 0; i < op->uscf->servers->nelts; i++) {
        ngx_js_peer_opaque_t  *pop;
        JSValue                obj;

        pop = js_mallocz(ctx, sizeof(ngx_js_peer_opaque_t));
        if (!pop) {
            JS_FreeValue(ctx, arr);
            return JS_EXCEPTION;
        }

        pop->srv = &srv[i];

        obj = JS_NewObjectClass(ctx, ngx_js_peer_class_id);
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


/*
 * nginx.http.upstreams[i].addPeer(addr[, opts])
 *
 * addr  — "IP:port" string
 * opts  — optional object:
 *           weight      (default 1)
 *           maxFails    (default 1)
 *           failTimeout (default 10, in seconds)
 *           down        (default false)
 *           backup      (default false — adds to backup group if true)
 *
 * Allocates a new ngx_http_upstream_rr_peer_t from cycle->pool and
 * links it into the upstream's RR peer list.  At init_conf() time
 * peers->shpool is NULL so locking macros are no-ops; zone init will
 * copy the updated list to shared memory before workers fork.
 */
static JSValue
ngx_js_upstream_add_peer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_upstream_opaque_t      *op;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer, *tail;
    ngx_cycle_t                   *cycle;
    const char                    *addr_cstr;
    ngx_url_t                      u;
    ngx_int_t                      weight, max_fails, is_backup;
    time_t                         fail_timeout;
    ngx_uint_t                     down;
    JSValue                        opts, v;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "addPeer: address argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->uscf->peer.data == NULL) {
        return JS_ThrowTypeError(ctx,
                                 "addPeer: upstream RR data not initialised");
    }

    cycle = (ngx_cycle_t *) JS_GetContextOpaque(ctx);

    /* Parse address */
    addr_cstr = JS_ToCString(ctx, argv[0]);
    if (!addr_cstr) {
        return JS_EXCEPTION;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url.data   = (u_char *) addr_cstr;
    u.url.len    = ngx_strlen(addr_cstr);
    u.no_resolve = 1;

    if (ngx_parse_url(cycle->pool, &u) != NGX_OK || u.naddrs == 0) {
        JS_FreeCString(ctx, addr_cstr);
        return JS_ThrowTypeError(ctx, "addPeer: invalid address");
    }

    JS_FreeCString(ctx, addr_cstr);

    /* Defaults */
    weight       = 1;
    max_fails    = 1;
    fail_timeout = 10;
    down         = 0;
    is_backup    = 0;

    /* Parse opts */
    if (argc >= 2 && JS_IsObject(argv[1])) {
        opts = argv[1];

        v = JS_GetPropertyStr(ctx, opts, "weight");
        if (!JS_IsUndefined(v)) {
            int32_t w;
            if (JS_ToInt32(ctx, &w, v) == 0 && w > 0) {
                weight = w;
            }
        }
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, opts, "maxFails");
        if (!JS_IsUndefined(v)) {
            int32_t mf;
            if (JS_ToInt32(ctx, &mf, v) == 0 && mf >= 0) {
                max_fails = mf;
            }
        }
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, opts, "failTimeout");
        if (!JS_IsUndefined(v)) {
            int32_t ft;
            if (JS_ToInt32(ctx, &ft, v) == 0 && ft >= 0) {
                fail_timeout = (time_t) ft;
            }
        }
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, opts, "down");
        if (!JS_IsUndefined(v)) {
            down = (ngx_uint_t) JS_ToBool(ctx, v);
        }
        JS_FreeValue(ctx, v);

        v = JS_GetPropertyStr(ctx, opts, "backup");
        if (!JS_IsUndefined(v)) {
            is_backup = JS_ToBool(ctx, v);
        }
        JS_FreeValue(ctx, v);
    }

    peers = (ngx_http_upstream_rr_peers_t *) op->uscf->peer.data;

    /* For backup peers, use the backup group (peers->next) */
    if (is_backup) {
        if (peers->next == NULL) {
            /* Create backup group on demand */
            peers->next = ngx_pcalloc(cycle->pool,
                                      sizeof(ngx_http_upstream_rr_peers_t));
            if (peers->next == NULL) {
                return JS_ThrowOutOfMemory(ctx);
            }
            peers->next->name = peers->name;
        }
        peers = peers->next;
    }

    /* Allocate peer from pool (shpool is NULL at init_conf time) */
    peer = ngx_pcalloc(cycle->pool, sizeof(ngx_http_upstream_rr_peer_t));
    if (peer == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Allocate and copy sockaddr */
    peer->sockaddr = ngx_pcalloc(cycle->pool, u.addrs[0].socklen);
    if (peer->sockaddr == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memcpy(peer->sockaddr, u.addrs[0].sockaddr, u.addrs[0].socklen);
    peer->socklen = u.addrs[0].socklen;

    /* Name string already pool-allocated by ngx_parse_url */
    peer->name   = u.addrs[0].name;
    peer->server = u.addrs[0].name;

    peer->weight           = weight;
    peer->effective_weight = weight;
    peer->current_weight   = 0;
    peer->max_fails        = (ngx_uint_t) max_fails;
    peer->fail_timeout     = fail_timeout;
    peer->max_conns        = 0;
    peer->down             = down;

    /* Link peer at tail of the list under wlock */
    ngx_http_upstream_rr_peers_wlock(peers);

    if (peers->peer == NULL) {
        peers->peer = peer;
    } else {
        for (tail = peers->peer; tail->next; tail = tail->next) { /* empty */ }
        tail->next = peer;
    }

    peers->number++;
    peers->total_weight += (ngx_uint_t) weight;
    if (!down) {
        peers->tries++;
    }
    peers->weighted = (peers->total_weight != peers->number);
    peers->single   = (peers->number == 1);

    ngx_http_upstream_rr_peers_unlock(peers);

    return JS_UNDEFINED;
}


/*
 * nginx.http.upstreams[i].removePeer(addr)
 *
 * addr — "IP:port" string matching peer->name
 *
 * Searches primary and backup groups.  Unlinks the peer and updates
 * group counters.  For zone-backed upstreams (peers->shpool != NULL)
 * calls ngx_http_upstream_rr_peer_free_locked; for pool-allocated
 * peers the memory stays in the pool (harmless, pool is one-time).
 */
static JSValue
ngx_js_upstream_remove_peer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_upstream_opaque_t      *op;
    ngx_http_upstream_rr_peers_t  *peers, *pg;
    ngx_http_upstream_rr_peer_t   *p, **pp;
    const char                    *addr_cstr;
    ngx_str_t                      addr;
    ngx_int_t                      found;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "removePeer: address argument required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->uscf->peer.data == NULL) {
        return JS_ThrowTypeError(ctx,
                                 "removePeer: upstream RR data not initialised");
    }

    addr_cstr = JS_ToCString(ctx, argv[0]);
    if (!addr_cstr) {
        return JS_EXCEPTION;
    }

    addr.data = (u_char *) addr_cstr;
    addr.len  = ngx_strlen(addr_cstr);

    peers = (ngx_http_upstream_rr_peers_t *) op->uscf->peer.data;
    found = 0;

    for (pg = peers; pg && !found; pg = pg->next) {

        ngx_http_upstream_rr_peers_wlock(pg);

        pp = &pg->peer;

        while (*pp) {
            p = *pp;

            if (p->name.len == addr.len
                && ngx_memcmp(p->name.data, addr.data, addr.len) == 0)
            {
                /* Unlink */
                *pp = p->next;

                pg->number--;
                pg->total_weight -= (ngx_uint_t) p->weight;
                if (!p->down) {
                    pg->tries--;
                }
                if (pg->number > 0) {
                    pg->weighted = (pg->total_weight != pg->number);
                    pg->single   = (pg->number == 1);
                } else {
                    pg->weighted = 0;
                    pg->single   = 0;
                }

                /* Free peer memory if zone-backed */
                if (pg->shpool) {
                    ngx_shmtx_lock(&pg->shpool->mutex);
                    ngx_http_upstream_rr_peer_free_locked(pg, p);
                    ngx_shmtx_unlock(&pg->shpool->mutex);
                }

                found = 1;
                break;
            }

            pp = &p->next;
        }

        ngx_http_upstream_rr_peers_unlock(pg);
    }

    JS_FreeCString(ctx, addr_cstr);

    if (!found) {
        return JS_ThrowTypeError(ctx, "removePeer: peer not found");
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_upstream_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name",  ngx_js_upstream_get,       NULL, 0),
    JS_CGETSET_MAGIC_DEF("zone",  ngx_js_upstream_get,       NULL, 1),
    JS_CGETSET_MAGIC_DEF("peers", ngx_js_upstream_get_peers, NULL, 0),
    JS_CFUNC_DEF("addPeer",    1, ngx_js_upstream_add_peer),
    JS_CFUNC_DEF("removePeer", 1, ngx_js_upstream_remove_peer),
    JS_CFUNC_DEF("snapshot",   0, ngx_js_upstream_fn_snapshot),
};


ngx_int_t
ngx_js_upstream_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_upstream_proto_funcs,
                               countof(ngx_js_upstream_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_upstream_class_id, proto);
    return NGX_OK;
}


static JSValue
ngx_js_wrap_upstream(JSContext *ctx, ngx_http_upstream_srv_conf_t *uscf)
{
    JSValue                    obj;
    ngx_js_upstream_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_upstream_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->uscf = uscf;

    obj = JS_NewObjectClass(ctx, ngx_js_upstream_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_upstream_com_install                                          */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_upstream_register_classes(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_upstream_class_id, &ngx_js_upstream_class) < 0
     || JS_NewClass(rt, ngx_js_peer_class_id,     &ngx_js_peer_class)     < 0
     || JS_NewClass(rt, ngx_js_rr_peer_class_id,  &ngx_js_rr_peer_class)  < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Build nginx.http.upstreams[] and attach it to http_obj.
 * Called from ngx_js_http_com_install() in ngx_js_com_http.c.
 */
ngx_int_t
ngx_js_upstream_com_install(JSContext *ctx, JSValue http_obj,
    ngx_cycle_t *cycle)
{
    JSValue                         upstreams_arr;
    ngx_http_conf_ctx_t            *http_ctx;
    ngx_http_upstream_main_conf_t  *umcf;
    ngx_http_upstream_srv_conf_t  **uscfp;
    ngx_uint_t                      i;

    upstreams_arr = JS_NewArray(ctx);
    if (JS_IsException(upstreams_arr)) {
        return NGX_ERROR;
    }

    http_ctx = (ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index];
    if (http_ctx == NULL) {
        JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    umcf = http_ctx->main_conf[ngx_http_upstream_module.ctx_index];
    if (umcf == NULL) {
        JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);
        return NGX_OK;
    }

    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {
        /* Only named upstreams (upstream {} blocks) have NGX_HTTP_UPSTREAM_CREATE */
        if (!(uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE)) {
            continue;
        }

        JS_SetPropertyUint32(ctx, upstreams_arr, (uint32_t) i,
                             ngx_js_wrap_upstream(ctx, uscfp[i]));
    }

    JS_SetPropertyStr(ctx, http_obj, "upstreams", upstreams_arr);

    return NGX_OK;
}
