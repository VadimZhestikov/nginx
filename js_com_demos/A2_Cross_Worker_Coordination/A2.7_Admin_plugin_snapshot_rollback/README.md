# A2.7 — Admin Plugin: Snapshot / Rollback

Demonstrates a self-contained admin plugin loaded via `nginx.use()` that provides
runtime feature-flag control, snapshot creation, and one-step rollback — all
propagated to every worker without a reload.

## What it shows

| Primitive | Role in this demo |
|---|---|
| `nginx.use(path, config)` | Loads the admin plugin once in master; all workers inherit it via COW fork |
| `nginx.shared` | Lock-free shared memory KV — any worker writes, all workers read instantly |
| `nginx.broadcast(fn)` | Seeds default flag values in every worker's `init_process` |
| `SharedWorker` (cfgworker.js) | Config-authority broker — fans out `apply`/`rollback` ops to all workers |
| `nginx.admin.registerHandler` | Registers named JS handlers at init-conf time; workers resolve names locally from COW-inherited registry |

Two categories of snapshot ops are supported:

- **`{shared, value}`** — writes `nginx.shared`; instantly visible to all workers, no IPC needed.
- **`{op, serverName, pattern [, handler]}`** — structural routing-tree mutations (`addLocation`, `removeLocation`); per-worker state that *requires* the SharedWorker fan-out to reach every worker.

## File layout

```
nginx.conf                  4-worker server on :8116 (reuseport)
handler.js                  Top-level js_source: loads plugin, registers handlers, installs app routes
admin-plugin/
  index.js                  Plugin entry point — nginx.admin API + REST handler
  cfgworker.js              SharedWorker config-authority (fan-out broker)
  package.json              Plugin metadata (ngxjs.main)
snapshots/                  JSON snapshot files written by the plugin at runtime
test.sh                     Self-contained test (starts nginx, runs all checks, stops nginx)
```

## Running

```bash
cd js_com_demos/A2_Cross_Worker_Coordination/A2.7_Admin_plugin_snapshot_rollback
bash test.sh
```

The test script starts nginx, runs all 15 sections, then stops nginx on exit.

## Managed shared-memory keys

| Key | Values | Effect |
|---|---|---|
| `routes.products` | `0` / `1` | Enable or disable `/api/products/` (returns 404 when `0`) |
| `routes.premium` | `0` / `1` | Enable or disable `/api/premium/` (returns 404 when `0`) |
| `canary.weight` | `0`–`100` | Percentage of `/api/` traffic routed to the v2 handler |

## REST API

All endpoints are on the `/admin/` prefix.

| Method | Path | Description |
|---|---|---|
| `GET` | `/admin/state` | Current values of all managed keys |
| `GET` | `/admin/worker` | PID of the responding worker |
| `GET` | `/admin/snapshots` | Sorted list of snapshot ids |
| `POST` | `/admin/snapshots` | Create snapshot from current state; body: `{"name":"…"}` |
| `POST` | `/admin/raw-snapshot` | Create snapshot from explicit ops; body: `{"name":"…","ops":[…]}` |
| `GET` | `/admin/snapshots/:id` | Read snapshot JSON |
| `POST` | `/admin/apply/:id` | Apply snapshot — local apply then SharedWorker fan-out |
| `POST` | `/admin/rollback` | Roll back one step (or to base if at first snapshot) |
| `POST` | `/admin/set` | Set one key; body: `{"key":"…","value":"…"}` |

App endpoints:

| Path | Live when |
|---|---|
| `/api/products/` | `routes.products == "1"` |
| `/api/premium/` | `routes.premium == "1"` |
| `/api/` | Always; `canary.weight` controls v1/v2 split |
| `/status/` | Always; returns worker PID + all flag values |
| `/dynamic/` | Present only while an `addLocation` snapshot is applied |

## Snapshot format

Snapshots are JSON files written to `snapshots/<id>.json`.  The `id` is
auto-generated as a zero-padded sequence number followed by the caller-supplied
name (`0001-products-only`, `0002-full-features`, …).

```json
{
  "id": "0001-products-only",
  "ts": 1777075073,
  "ops": [
    { "shared": "routes.products", "value": "1" },
    { "shared": "routes.premium",  "value": "0" },
    { "shared": "canary.weight",   "value": "0" }
  ]
}
```

A raw snapshot can carry structural ops alongside shared-key ops:

```json
{
  "id": "0003-add-dynamic",
  "ts": 1777075090,
  "ops": [
    { "op": "addLocation", "serverName": "localhost",
      "pattern": "/dynamic/", "handler": "dynamicHandler" }
  ]
}
```

## Cross-worker propagation

```
POST /admin/apply/:id
        │
        ▼  (worker that received the request)
   _applyOps(snap.ops)          ← immediate, local
        │
        ▼
   cfgWorker.postMessage({type:'apply', snap})
        │
        ▼  (SharedWorker thread — cfgworker.js)
   _desired = snap
   _ports.forEach(p.postMessage)  ← fan-out to ALL worker ports
        │
        ├─▶ worker 1 onmessage → _applyOps(snap.ops)
        ├─▶ worker 2 onmessage → _applyOps(snap.ops)
        └─▶ worker 3 onmessage → _applyOps(snap.ops)
```

`nginx.shared` writes in `_applyOps` are instantly visible to every worker
(lock-free shared memory), so the fan-out is redundant for scalar flag ops but
required for structural ops (`addLocation`, `removeLocation`) which mutate each
worker's private routing tree.

A worker restarted by the master catches up by sending `{type:'get'}` to the
SharedWorker in its `nginx.broadcast` callback; the SW replies with the last
`_desired` snapshot.

## Named handler registry

`nginx.admin.registerHandler(name, fn)` is called at init-conf time (master,
before fork).  All workers inherit `_handlers` via COW.  Structural-op snapshots
carry only the handler name string; each worker resolves the function locally —
no closure serialisation across worker boundaries.

```js
nginx.admin.registerHandler('dynamicHandler', function (r) {
    r.respond(200, {'Content-Type': 'application/json'},
        JSON.stringify({ resource: 'dynamic', worker: r.variable('pid') }) + '\n');
});
```

## SharedWorker API note

The nginx SharedWorker script must use the `onconnect` / per-port pattern.
Global `onmessage` in the SW thread receives raw data (not a `MessageEvent`), and
global `postMessage` does not exist.  The correct pattern:

```js
var _ports = [];

onconnect = function (e) {
    var port = e.ports[0];
    _ports.push(port);

    port.onmessage = function (ev) {   // ev.data is the raw message
        // ...
        _ports.forEach(function (p) { p.postMessage(ev.data); });  // fan-out
    };
};
```
