# 05 — nginx COM Object Tree

## Overview

The global `nginx` object exposed to JS code is the root of a typed COM
(Component Object Model) tree.  Every nginx configuration struct has a JS
counterpart that exposes its fields as readable (and, for mutation, writable)
properties.

The tree mirrors nginx's internal `ngx_cycle_t` hierarchy:

```
nginx
  .version          — "1.29.7"
  .cpuCount         — worker_processes directive value
  .log(msg)         — writes to error.log at NOTICE level
  .setTimeout(ms)   — returns a Promise that resolves after ms milliseconds
  .broadcast(msg)   — sends a plain JS value to all workers
  .shared           — cross-worker key/value zone (P11)
  .plugins          — loaded plugin registry (P19)

  .http
    .servers[]      — array of NginxServer objects
      .name         — first server_name
      .names[]      — all server_name values
      .locations[]  — array of NginxLocation objects
        .path       — location match string
        .handler    — content handler function (assignable)
        .addHook(fn)
        .addResponseHook(fn)
        .addBodyFilter(fn)
        .addUpstreamFilter(fn)
        .addUpstreamRequestFilter(fn)
    .upstreams[]    — array of NginxUpstream objects
      .name
      .peers[]      — array of NginxPeer
        .host, .port, .weight, .down, .maxFails
```

---

## Class IDs (ngx_js_com.h)

Each COM class is registered as a QuickJS class with a stable `JSClassID`.
Class IDs are declared as `extern JSClassID ngx_js_*_class_id` in `ngx_js_com.h`
and defined in the respective `.c` files.

Key class IDs:

| Class | Variable | Owner file |
|---|---|---|
| `NginxServer` | `ngx_js_srv_class_id` | `ngx_js_com_http.c` |
| `NginxLocation` | `ngx_js_loc_class_id` | `ngx_js_com_http.c` |
| `NginxRequest` | `ngx_js_req_class_id` | `ngx_js_http_module.c` |
| `NginxUpstream` | `ngx_js_ups_class_id` | `ngx_js_com_upstream.c` |
| `NginxShared` | `ngx_js_shared_class_id` | `ngx_js_com.c` |
| `SharedWorker` | `ngx_js_sw_class_id` | `ngx_js_sw.c` |
| `Worker` | `ngx_js_wt_class_id` | `ngx_js_worker.c` |

---

## nginx.http — NginxHttp Object

Implemented in `ngx_js_com_http.c` (~329 KB, the largest file).

### nginx.http.servers[]

Each `NginxServer` wraps an `ngx_http_core_srv_conf_t *`.  JS accesses the
struct's `server_name` array to build the `names[]` array.  The opaque pointer
on the JS object is the C srv_conf pointer.

`NginxServer.locations` lazily builds an array of `NginxLocation` objects by
walking the `ngx_http_core_loc_conf_t` location tree.

### NginxLocation

Wraps `ngx_http_core_loc_conf_t *` and `ngx_js_loc_conf_t *`.

`ngx_js_loc_conf_t` (in `ngx_js.h`) extends the standard nginx per-location
config with JS-specific fields:

```c
typedef struct {
    ngx_http_core_loc_conf_t  *core_clcf;   /* parent nginx location */
    ngx_array_t                hook_fns;     /* JSValue[8]: P1 hooks */
    ngx_array_t                response_hook_fns; /* P3 hooks */
    ngx_array_t                filter_fns;   /* P5/P15 body filters */
    ngx_array_t                upstream_filter_fns;   /* P14 */
    ngx_array_t                upstream_request_filter_fns;
    JSValue                    handler;      /* loc.handler assignment */
} ngx_js_loc_conf_t;
```

### Mutation and Config Snapshots

When a JS hook mutates a location config (e.g., `loc.proxy.pass = "..."`) the
change must be isolated to the current worker.  A **snapshot** mechanism in
`ngx_js_req_ctx_t` tracks which module config structs have been snapshotted:

```c
typedef struct {
    void            *original;     /* pointer to the original struct */
    void            *copy;         /* per-worker copy */
    ngx_module_t    *module;       /* which module owns the struct */
} ngx_js_module_snap_t;
```

`write_mode` in `ngx_js_req_ctx_t` controls whether mutations go to the global
struct (`NGX_JS_WRITE_GLOBAL`) or to a per-request snapshot
(`NGX_JS_WRITE_LOCAL`).  The snapshot is discarded at request end.

---

## nginx.http.upstreams[] — Hot Reload (P14)

`NginxUpstream` wraps `ngx_http_upstream_srv_conf_t *`.

Peers in a round-robin upstream can be modified at runtime:

```javascript
var ups = nginx.http.upstreams.find(u => u.name === 'backend');
ups.peers[0].weight = 10;
ups.peers[1].down = true;
```

Changes take effect immediately for that worker.  For cluster-wide changes, the
hook must run in all workers via `nginx.broadcast()` + a `nginx.on('message', fn)`
receiver.

`ngx_js_com_upstream.c` implements the snapshot-and-rollback pattern:
`ups.snapshot()` takes a point-in-time copy; `ups.rollback()` reverts.

---

## nginx.shared — Cross-Worker Key/Value (P11)

### Storage Layout

A fixed-size nginx shared-memory zone (`ngx_shm_zone_t`) is allocated in master
and mapped at the same VA in all workers.  Layout:

```
[ngx_js_shared_hdr_t]
[ngx_js_shared_entry_t × NGX_JS_SHARED_MAX]  (256 entries)
```

```c
typedef struct {
    ngx_atomic_t  lock;         /* spinlock */
    uint32_t      count;        /* live entries */
} ngx_js_shared_hdr_t;

typedef struct {
    u_char  used;
    char    key[128];
    char    val[512];
} ngx_js_shared_entry_t;
```

Total zone size: ~163 KB.

### JS API

```javascript
nginx.shared.set('counter', '0');
nginx.shared.get('counter');          // → '0'
nginx.shared.incr('counter', 1);      // atomic increment; returns new value string
nginx.shared.delete('counter');
nginx.shared.keys();                  // → ['counter']
```

All operations hold the spinlock while executing.  Long-running JS code should
not call `nginx.shared` in a tight loop: spinlocks are not yield-safe.

---

## Module COM Wrappers (ngx_js_com_*.c × 40)

Each nginx built-in module that is compiled in has a corresponding
`ngx_js_com_<module>.c` file.  These files expose the module's per-location,
per-server, and per-http config structs as COM objects.

Examples:

| File | Exposes |
|---|---|
| `ngx_js_com_proxy.c` | `loc.proxy.pass`, `loc.proxy.readTimeout`, … |
| `ngx_js_com_ssl.c` | `srv.ssl.cert`, `srv.ssl.key`, … |
| `ngx_js_com_headers.c` | `loc.headers.set()`, `loc.headers.add()`, … |
| `ngx_js_com_gzip.c` | `loc.gzip.enabled`, `loc.gzip.minLength`, … |
| `ngx_js_com_limit_req.c` | `loc.limitReq.zone`, `loc.limitReq.burst`, … |

The pattern is uniform:
1. Define a `JSClassDef` with a finalizer that does nothing (the C struct is pool-owned).
2. Implement getter/setter `JS_CGETSET_DEF` for each field.
3. Register the class and attach instances to the parent COM node in `ngx_js_com_http_init`.

Because nginx linker guards (`NGX_HTTP_PROXY_MODULE_GUARD`, etc.) surround
every reference to non-universal symbols, the wrappers compile cleanly regardless
of which nginx modules are enabled.

---

## nginx.setTimeout (Async Timer)

```javascript
let result = await nginx.setTimeout(500);  // resolves after 500 ms
```

Implementation in `ngx_js_com.c`:
1. Allocates `ngx_js_timer_t` from the pool.
2. Registers an `ngx_event_t` timer with `ngx_add_timer`.
3. Returns a `JSValue` Promise.
4. The timer callback resolves the Promise and calls `ngx_js_async_check(w)`.

This is the mechanism used by hooks to implement debouncing, retries, and
health-check polling.

---

## nginx.broadcast (Cross-Worker Events)

```javascript
// In worker 0:
nginx.broadcast({type: 'reload-config', data: payload});

// In all workers (including 0):
nginx.on('message', function(msg) {
    if (msg.type === 'reload-config') { /* … */ }
});
```

Implementation:
- Master holds one AF_UNIX socketpair per worker.
- `nginx.broadcast` serialises the message with `JS_WriteObject` and sends it
  to all per-worker read fds.
- Each worker's epoll watches its read fd.  On data-ready, it calls
  `ngx_js_bcast_handler`, deserialises, and fires all `nginx.on('message', …)`
  callbacks.
