
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
#include <math.h>
#include "ngx_js_com.h"

/* Like JS_CGETSET_MAGIC_DEF but with JS_PROP_ENUMERABLE so that
 * for...in and Object.keys() can discover these prototype getters. */
#define NGX_JS_CGETSET_MAGIC_ENUM(name, fgetter, fsetter, magic)           \
    { name, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE,                     \
      JS_DEF_CGETSET_MAGIC, magic,                                         \
      .u = { .getset = { .get = { .getter_magic = fgetter },               \
                         .set = { .setter_magic = fsetter } } } }


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



/*
 * Peer numbers are bounds-checked exactly the way nginx bounds-checks the
 * `server` directive that sets them, because these setters write the SAME
 * fields.  Without it a negative slipped straight through: NginxPeer writes
 * srv->weight, an ngx_uint_t, so `peers[0].weight = -1` stored ~1.8e19, and
 * that value is then summed into peers->total_weight and decides which backend
 * every request goes to.  JS_ToInt32 alone is not validation -- it is a cast.
 */
static int
ngx_js_peer_num(JSContext *ctx, JSValueConst val, int32_t min,
    const char *name, int32_t *out)
{
    int64_t  n64;
    char     label[64];

    /* One implementation of the check, in ngx_js_com.c.  This is the adapter
     * that keeps the int32 out-param the peer setters use; it is deliberately
     * not a second copy of the logic, because a second copy is how the HTTP
     * and stream peer setters came to differ in the first place. */
    ngx_snprintf((u_char *) label, sizeof(label) - 1, "peer.%s%Z", name);

    if (ngx_js_com_num_range(ctx, val, min, 2147483647, label, &n64) < 0) {
        return -1;
    }

    *out = (int32_t) n64;
    return 0;
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
        if (ngx_js_peer_num(ctx, val, 1, "weight", &i32)) { return JS_EXCEPTION; }
        srv->weight = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 2:
        if (ngx_js_peer_num(ctx, val, 0, "maxFails", &i32)) { return JS_EXCEPTION; }
        srv->max_fails = (ngx_uint_t) i32;
        return JS_UNDEFINED;

    case 3:
        srv->down = (ngx_uint_t) JS_ToBool(ctx, val);
        return JS_UNDEFINED;

    case 5: /* failTimeout */
        if (ngx_js_peer_num(ctx, val, 0, "failTimeout", &i32)) {
            return JS_EXCEPTION;
        }
        srv->fail_timeout = (time_t) i32;
        return JS_UNDEFINED;

    case 6: /* maxConns */
        if (ngx_js_peer_num(ctx, val, 0, "maxConns", &i32)) { return JS_EXCEPTION; }
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
    NGX_JS_CGETSET_MAGIC_ENUM("address",     ngx_js_peer_get, NULL,            0),
    NGX_JS_CGETSET_MAGIC_ENUM("weight",      ngx_js_peer_get, ngx_js_peer_set, 1),
    NGX_JS_CGETSET_MAGIC_ENUM("maxFails",    ngx_js_peer_get, ngx_js_peer_set, 2),
    NGX_JS_CGETSET_MAGIC_ENUM("down",        ngx_js_peer_get, ngx_js_peer_set, 3),
    NGX_JS_CGETSET_MAGIC_ENUM("backup",      ngx_js_peer_get, NULL,            4),
    NGX_JS_CGETSET_MAGIC_ENUM("failTimeout", ngx_js_peer_get, ngx_js_peer_set, 5),
    NGX_JS_CGETSET_MAGIC_ENUM("maxConns",    ngx_js_peer_get, ngx_js_peer_set, 6),
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


/*
 * ngx_js_rr_peer_is_zoned(obj) — 1 if the runtime RR peer wrapped by obj
 * belongs to a zone-backed (shared-memory) upstream.  Zone-backed peers live
 * in shared memory and their scalar setters propagate to every worker under
 * the rr_peers lock; non-zoned peers are worker-local (shpool == NULL, lock is
 * a no-op).  Lets describe() report propagation: zoned-shared vs worker-local.
 */
ngx_int_t
ngx_js_rr_peer_is_zoned(JSValueConst obj)
{
    ngx_js_rr_peer_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_rr_peer_class_id);
    if (op == NULL || op->peers == NULL) {
        return 0;
    }

    return op->peers->shpool != NULL;
}


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
        if (ngx_js_peer_num(ctx, val, 1, "weight", &i32)) {
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
        if (ngx_js_peer_num(ctx, val, 0, "maxFails", &i32)) {
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
        if (ngx_js_peer_num(ctx, val, 0, "failTimeout", &i32)) {
            return JS_EXCEPTION;
        }
        ngx_http_upstream_rr_peers_wlock(peers);
        p->fail_timeout = (time_t) i32;
        ngx_http_upstream_rr_peers_unlock(peers);
        return JS_UNDEFINED;

    case 7: /* maxConns */
        if (ngx_js_peer_num(ctx, val, 0, "maxConns", &i32)) {
            return JS_EXCEPTION;
        }
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
    NGX_JS_CGETSET_MAGIC_ENUM("address",     ngx_js_rr_peer_get, NULL,               0),
    NGX_JS_CGETSET_MAGIC_ENUM("weight",      ngx_js_rr_peer_get, ngx_js_rr_peer_set, 1),
    NGX_JS_CGETSET_MAGIC_ENUM("maxFails",    ngx_js_rr_peer_get, ngx_js_rr_peer_set, 2),
    NGX_JS_CGETSET_MAGIC_ENUM("down",        ngx_js_rr_peer_get, ngx_js_rr_peer_set, 3),
    NGX_JS_CGETSET_MAGIC_ENUM("backup",      ngx_js_rr_peer_get, NULL,               4),
    NGX_JS_CGETSET_MAGIC_ENUM("conns",       ngx_js_rr_peer_get, NULL,               5),
    NGX_JS_CGETSET_MAGIC_ENUM("failTimeout", ngx_js_rr_peer_get, ngx_js_rr_peer_set, 6),
    NGX_JS_CGETSET_MAGIC_ENUM("maxConns",    ngx_js_rr_peer_get, ngx_js_rr_peer_set, 7),
    NGX_JS_CGETSET_MAGIC_ENUM("server",      ngx_js_rr_peer_get, NULL,               8),
    NGX_JS_CGETSET_MAGIC_ENUM("fails",       ngx_js_rr_peer_get, NULL,               9),
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


/* ================================================================== */
/* Per-request peer selection — a custom balancer (iRules LB::select). */
/* upstream.onSelectPeer(fn): fn(peers, connCtx) -> peer index (or -1  */
/* to fall back to round-robin). Wraps the RR peer.init/get/free so the */
/* upstream's health / retry / accounting are preserved; JS only        */
/* influences WHICH peer is chosen. The selection fn is rooted in the   */
/* global __ngx_lb_hooks__ array (GC root); a small table maps each     */
/* balanced upstream to its saved RR init + registry slot.              */
/* ================================================================== */

typedef struct {
    ngx_http_upstream_srv_conf_t   *uscf;
    uint32_t                        slot;        /* index into __ngx_lb_hooks__ */
} ngx_js_lb_t;

typedef struct {
    ngx_http_upstream_rr_peer_data_t  *rrp;   /* wrapped RR per-request data */
    ngx_event_get_peer_pt              get;   /* saved RR get  */
    ngx_event_free_peer_pt             free;  /* saved RR free */
    ngx_http_request_t                *r;
    uint32_t                           slot;
} ngx_js_lb_peer_t;

static ngx_js_lb_t  ngx_js_lbs[64];
static ngx_uint_t   ngx_js_nlbs = 0;


static JSValue
ngx_js_lb_registry(JSContext *ctx)
{
    JSValue  global, reg;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_lb_hooks__");
    if (JS_IsUndefined(reg)) {
        JS_FreeValue(ctx, reg);
        reg = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_lb_hooks__", JS_DupValue(ctx, reg));
    }
    JS_FreeValue(ctx, global);
    return reg;
}


static ngx_js_lb_t *
ngx_js_lb_find(ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_uint_t  i;

    for (i = 0; i < ngx_js_nlbs; i++) {
        if (ngx_js_lbs[i].uscf == uscf) {
            return &ngx_js_lbs[i];
        }
    }
    return NULL;
}


/* Ask JS which peer to use; returns the index or -1 (fall back to RR). */
static ngx_int_t
ngx_js_lb_choose(ngx_js_lb_peer_t *lp)
{
    ngx_js_conf_t                 *jcf;
    JSContext                     *ctx;
    JSRuntime                     *rt;
    JSValue                        reg, fn, arr, args[2], ret, o;
    ngx_http_upstream_rr_peers_t  *peers;
    ngx_http_upstream_rr_peer_t   *peer;
    uint32_t                       i;
    int32_t                        idx;

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return -1;
    }
    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);

    reg = ngx_js_lb_registry(ctx);
    fn  = JS_GetPropertyUint32(ctx, reg, lp->slot);
    JS_FreeValue(ctx, reg);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return -1;
    }

    /* build a lightweight snapshot array of the primary peers under rlock,
     * then release the lock BEFORE calling into JS */
    peers = lp->rrp->peers;
    arr   = JS_NewArray(ctx);
    i     = 0;

    ngx_http_upstream_rr_peers_rlock(peers);
    for (peer = peers->peer; peer; peer = peer->next) {
        o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "name",
            JS_NewStringLen(ctx, (char *) peer->name.data, peer->name.len));
        JS_SetPropertyStr(ctx, o, "down",   JS_NewBool(ctx, (int) peer->down));
        JS_SetPropertyStr(ctx, o, "conns",  JS_NewInt64(ctx, (int64_t) peer->conns));
        JS_SetPropertyStr(ctx, o, "weight", JS_NewInt32(ctx, (int32_t) peer->weight));
        JS_SetPropertyUint32(ctx, arr, i++, o);
    }
    ngx_http_upstream_rr_peers_unlock(peers);

    args[0] = arr;
    args[1] = (lp->r && lp->r->connection)
              ? ngx_js_connection_ctx_obj(ctx, lp->r->connection) : JS_UNDEFINED;

    /*
     * Only a real, finite, in-range NUMBER is taken as a peer index.  Anything
     * else falls back to round-robin, which is what the contract already says
     * -1 does.
     *
     * JS_ToInt32() answers 0 without an error for undefined, for NaN, for {}
     * and for "nonsense", so a selection function that fell off the end without
     * returning — the easiest mistake to make in a callback whose whole job is
     * to return something — silently sent EVERY request to peer 0.  Measured
     * on three backends: `return;` gave B1,B1,B1,B1,B1,B1 where round-robin
     * gives B1,B2,B3.  Two thirds of the pool idle and one backend carrying
     * everything, with nothing logged.
     *
     * This is stricter than the ToNumber policy used by the config setters
     * (ngx_js_com_num_range), deliberately: those parse operator input where
     * coercion is conventional, while this is a hot-path callback whose author
     * meant to return an index, and where a safe documented fallback exists.
     * The range test also keeps the cast defined — converting an out-of-range
     * double to int32_t is undefined behaviour.
     */
    idx = -1;
    ret = JS_Call(ctx, fn, JS_UNDEFINED, 2, (JSValueConst *) args);
    if (JS_IsException(ret)) {
        ngx_js_log_exception(ctx, ngx_cycle->log);

    } else if (JS_IsNumber(ret)) {
        double  d;

        if (JS_ToFloat64(ctx, &d, ret) < 0) {
            JS_FreeValue(ctx, JS_GetException(ctx));   /* do not leave it set */

        } else if (!isnan(d) && !isinf(d) && d >= 0 && d <= 2147483647) {
            idx = (int32_t) d;
        }
    }

    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, arr);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, fn);
    while (JS_ExecutePendingJob(rt, NULL) > 0) { /* drain */ }

    return idx;
}


static ngx_int_t
ngx_js_lb_get(ngx_peer_connection_t *pc, void *data)
{
    ngx_js_lb_peer_t              *lp = data;
    ngx_http_upstream_rr_peers_t  *peers = lp->rrp->peers;
    ngx_http_upstream_rr_peer_t   *peer;
    ngx_int_t                      idx, i;

    idx = ngx_js_lb_choose(lp);

    if (idx >= 0) {
        ngx_http_upstream_rr_peers_wlock(peers);

        peer = peers->peer;
        for (i = 0; i < idx && peer; i++) {
            peer = peer->next;
        }

        if (peer && !peer->down
            && (peer->max_conns == 0 || peer->conns < peer->max_conns))
        {
            pc->sockaddr = peer->sockaddr;
            pc->socklen  = peer->socklen;
            pc->name     = &peer->name;
            peer->conns++;
            lp->rrp->current = peer;

            ngx_http_upstream_rr_peers_unlock(peers);

            pc->cached     = 0;
            pc->connection = NULL;
            return NGX_OK;
        }

        ngx_http_upstream_rr_peers_unlock(peers);
    }

    /* -1, out of range, or peer unavailable -> round-robin */
    return lp->get(pc, lp->rrp);
}


static void
ngx_js_lb_free(ngx_peer_connection_t *pc, void *data, ngx_uint_t state)
{
    ngx_js_lb_peer_t  *lp = data;

    lp->free(pc, lp->rrp, state);
}


static ngx_int_t
ngx_js_lb_init(ngx_http_request_t *r, ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_js_lb_t       *lb;
    ngx_js_lb_peer_t  *lp;

    lb = ngx_js_lb_find(uscf);
    if (lb == NULL) {
        return NGX_ERROR;
    }

    /* Set up the standard round-robin per-request state directly. (We can't
     * reuse the upstream's saved peer.init: pilgrim wraps it for the dynamic
     * RR-peer COM, and that wrapper's per-request struct is not RR-layout
     * compatible.) This makes r->upstream->peer.{data,get,free} the genuine RR
     * ones, with rrp->peers == uscf->peer.data (the group the COM reads). */
    if (ngx_http_upstream_init_round_robin_peer(r, uscf) != NGX_OK) {
        return NGX_ERROR;
    }

    lp = ngx_palloc(r->pool, sizeof(ngx_js_lb_peer_t));
    if (lp == NULL) {
        return NGX_ERROR;
    }

    lp->rrp  = r->upstream->peer.data;
    lp->get  = r->upstream->peer.get;
    lp->free = r->upstream->peer.free;
    lp->r    = r;
    lp->slot = lb->slot;

    r->upstream->peer.data = lp;
    r->upstream->peer.get  = ngx_js_lb_get;
    r->upstream->peer.free = ngx_js_lb_free;

    return NGX_OK;
}


static JSValue
ngx_js_upstream_on_select_peer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_upstream_opaque_t      *op;
    ngx_http_upstream_srv_conf_t  *uscf;
    ngx_js_lb_t                   *lb;
    JSValue                        reg, lenv;
    int64_t                        len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_upstream_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "onSelectPeer(fn): expected a function");
    }

    uscf = op->uscf;
    if (uscf->peer.data == NULL || uscf->peer.init == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "onSelectPeer: upstream not initialised");
    }

    reg = ngx_js_lb_registry(ctx);

    lb = ngx_js_lb_find(uscf);
    if (lb == NULL) {
        if (ngx_js_nlbs >= 64) {
            JS_FreeValue(ctx, reg);
            return JS_ThrowInternalError(ctx,
                                    "onSelectPeer: too many balanced upstreams");
        }

        lb            = &ngx_js_lbs[ngx_js_nlbs++];
        lb->uscf      = uscf;

        len  = 0;
        lenv = JS_GetPropertyStr(ctx, reg, "length");
        JS_ToInt64(ctx, &len, lenv);
        JS_FreeValue(ctx, lenv);
        lb->slot = (uint32_t) len;

        uscf->peer.init = ngx_js_lb_init;      /* install the wrapper */
    }

    /* one selection fn per upstream (set/replace) */
    JS_SetPropertyUint32(ctx, reg, lb->slot, JS_DupValue(ctx, argv[0]));
    JS_FreeValue(ctx, reg);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_upstream_proto_funcs[] = {
    NGX_JS_CGETSET_MAGIC_ENUM("name",  ngx_js_upstream_get,       NULL, 0),
    NGX_JS_CGETSET_MAGIC_ENUM("zone",  ngx_js_upstream_get,       NULL, 1),
    NGX_JS_CGETSET_MAGIC_ENUM("peers", ngx_js_upstream_get_peers, NULL, 0),
    JS_CFUNC_DEF("addPeer",      1, ngx_js_upstream_add_peer),
    JS_CFUNC_DEF("removePeer",   1, ngx_js_upstream_remove_peer),
    JS_CFUNC_DEF("onSelectPeer", 1, ngx_js_upstream_on_select_peer),
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
