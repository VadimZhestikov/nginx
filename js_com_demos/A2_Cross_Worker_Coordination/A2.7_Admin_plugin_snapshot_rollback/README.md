# A2.7 — Admin Plugin: Snapshot / Rollback

Demonstrates a self-contained admin plugin loaded via `nginx.use()` that provides
runtime feature-flag control, COM scalar property management, snapshot creation,
one-step rollback, multi-snapshot squash, and cross-worker propagation — all
without a reload.

## What it shows

| Primitive | Role in this demo |
|---|---|
| `nginx.use(path, config)` | Loads the admin plugin once in master; all workers inherit it via COW fork |
| `nginx.shared` | Lock-free shared memory KV — any worker writes, all workers read instantly |
| `nginx.broadcast(fn)` | Seeds default values in every worker's `init_process` |
| `SharedWorker` (cfgworker.js) | Config-authority broker — fans out `apply`/`rollback` ops to all workers |
| `nginx.admin.registerHandler` | Registers named JS handlers at init-conf time; workers resolve names locally from COW-inherited registry |
| `loc.headers.addHeader/removeHeader` | Runtime `add_header` mutation (via `{prop}` snapshot ops) |

Three categories of snapshot ops are supported:

- **`{shared, value}`** — writes `nginx.shared`; instantly visible to all workers, no IPC needed.
- **`{prop, value}`** — COM mutation using a *stable named descriptor* (upstream name + peer address, not array index); applied via `_resolveTarget` at runtime so snapshots survive peer reordering or `addLocation` calls.  Supports scalar and **array-valued** properties (e.g. `loc.headers.addHeaders`); `compactOps` uses `_valEqual` (JSON.stringify) for correct identity removal on arrays.
- **`{op, serverName, pattern [, handler]}`** — structural routing-tree mutations (`addLocation`, `removeLocation`); per-worker state that *requires* the SharedWorker fan-out.

## File layout

```
nginx.conf                  4-worker server on :8116 (reuseport); demo_backend upstream
handler.js                  Top-level js_source: loads plugin with keys + props config
admin-plugin/
  index.js                  Plugin entry point — nginx.admin API + REST handler
  cfgworker.js              SharedWorker config-authority (fan-out broker)
  package.json              Plugin metadata (ngxjs.main)
snapshots/                  JSON snapshot files written by the plugin at runtime
test.sh                     Self-contained test (starts nginx, runs 20 sections, stops nginx)
```

## Running

```bash
cd js_com_demos/A2_Cross_Worker_Coordination/A2.7_Admin_plugin_snapshot_rollback
bash test.sh   # 21 sections, 370 checks
```

## Plugin configuration

```js
nginx.use('./admin-plugin', {
    // nginx.shared keys managed by this plugin (seeded at startup)
    keys: {
        'routes.products': '0',
        'routes.premium':  '0',
        'canary.weight':   '0'
    },
    // COM scalar and array-valued properties captured by createSnapshot() and
    // restored by rollback().  Descriptors use stable names (not array indices).
    // Use subobject: to navigate one level deeper before accessing the property.
    props: [
        { upstream: 'demo_backend', peer: '127.0.0.1:8091', property: 'weight', default: 5 },
        { upstream: 'demo_backend', peer: '127.0.0.1:8092', property: 'weight', default: 3 },
        // Array-valued props (addHeaders): identity removal uses _valEqual (JSON.stringify)
        { server: 'localhost', location: '/api/products/', subobject: 'headers',
          property: 'addHeaders', default: [] },
        { server: 'localhost', location: '/api/premium/', subobject: 'headers',
          property: 'addHeaders', default: [] }
    ]
});
```

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
| `GET` | `/admin/state` | Current values of all managed keys + live prop values |
| `GET` | `/admin/worker` | PID of the responding worker |
| `GET` | `/admin/snapshots` | Sorted list of snapshot ids |
| `POST` | `/admin/snapshots` | Create snapshot from current state; body: `{"name":"…"}` |
| `POST` | `/admin/raw-snapshot` | Create snapshot from explicit ops; body: `{"name":"…","ops":[…]}` |
| `GET` | `/admin/snapshots/:id` | Read snapshot JSON |
| `POST` | `/admin/apply/:id` | Apply snapshot — local apply then SharedWorker fan-out |
| `POST` | `/admin/rollback` | Roll back one step (or to base if at first snapshot) |
| `POST` | `/admin/set` | Set one shared key; body: `{"key":"…","value":"…"}` |
| `POST` | `/admin/compact/:id` | Compact a snapshot's ops in place; returns `{"removed":N}` |
| `POST` | `/admin/squash` | Merge N snapshots into one; body: `{"ids":[…],"name":"…"}` |

App endpoints:

| Path | Live when |
|---|---|
| `/api/products/` | `routes.products == "1"` |
| `/api/premium/` | `routes.premium == "1"` |
| `/api/` | Always; `canary.weight` controls v1/v2 split |
| `/status/` | Always; returns worker PID + all flag values |
| `/dynamic/` | Present only while an `addLocation` snapshot is applied |

## Snapshot format

Snapshots are JSON files in `snapshots/<id>.json`.  The `id` is auto-generated
as a zero-padded sequence number plus the caller-supplied name.

**Shared-key ops:**
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

**Named-descriptor COM scalar ops (stable across topology changes):**
```json
{
  "id": "0005-rebalance",
  "ts": 1777075200,
  "ops": [
    { "prop": { "upstream": "demo_backend",
                "peer":     "127.0.0.1:8091",
                "property": "weight" }, "value": 2 },
    { "prop": { "upstream": "demo_backend",
                "peer":     "127.0.0.1:8092",
                "property": "weight" }, "value": 8 }
  ]
}
```

The `{prop}` descriptor resolves to the target COM object **at apply time** by
name (upstream name, peer address, server name, location path).  An index-based
raw string path (`"http.upstreams[0].peers[0].weight"`) is also accepted for
backward compatibility, but breaks if peers are reordered.

**Array-valued `{prop}` op (response headers):**
```json
{
  "id": "0006-add-demo-header",
  "ts": 1777075300,
  "ops": [
    { "shared": "routes.products", "value": "1" },
    { "prop": { "server":    "localhost",
                "location":  "/api/products/",
                "subobject": "headers",
                "property":  "addHeaders" },
      "value": [{ "key": "X-Demo", "value": "v1", "always": false }] }
  ]
}
```

A snapshot that sets `addHeaders = []` (the default) is elided by
`compactSnapshot` because `_valEqual` uses `JSON.stringify` to compare arrays,
treating any two empty arrays as identical.

**Structural ops (mixed with other op types):**
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

## squash and compactSnapshot

**`squash(ids, name)` / `POST /admin/squash`** — merge N incremental snapshots
into a single one that encodes their net effect.  Last-write-wins per key;
values that are already at the base default are elided.

```bash
# Typical workflow: squash a week of daily snapshots into one
curl -X POST http://127.0.0.1:8116/admin/squash \
     -H 'Content-Type: application/json' \
     -d '{"ids":["0001-mon","0002-tue","0003-wed","0004-thu","0005-fri"],"name":"week-01"}'
# → {"id":"0006-week-01"}
```

**`compactSnapshot(id)` / `POST /admin/compact/:id`** — rewrite a single
snapshot in place, removing:
- duplicate ops for the same key (last-write-wins)
- ops that restore the value to the configured default (no-op)

```bash
curl -X POST http://127.0.0.1:8116/admin/compact/0001-products-only
# → {"id":"0001-products-only","removed":2}
```

**`admin.options.compact`** — when set to `true`, `createSnapshot` auto-compacts
before writing.  Set via JS: `nginx.admin.options.compact = true`.

## Cross-worker propagation

```
POST /admin/apply/:id
        │
        ▼  (worker that received the request)
   _applyOps(_baseOps())        ← reset to base (clears all managed state)
   _applyOps(snap.ops)          ← apply desired state
        │
        ▼
   cfgWorker.postMessage({type:'apply', snap})
        │
        ▼  (SharedWorker thread — cfgworker.js)
   _desired = snap
   _ports.forEach(p.postMessage)  ← fan-out to ALL other worker ports
        │
        ├─▶ worker 1 onmessage → _applyOps(_baseOps()); _applyOps(snap.ops)
        ├─▶ worker 2 onmessage → _applyOps(_baseOps()); _applyOps(snap.ops)
        └─▶ worker 3 onmessage → _applyOps(_baseOps()); _applyOps(snap.ops)
```

Each apply **resets all managed state to base first**, then applies the
snapshot ops.  This means a snapshot represents a *desired state*, not a
diff — applying snapshot B after A clears any props set by A that are absent
from B (e.g. `addHeaders` goes back to `[]` on rollback to an older snapshot).

`nginx.shared` writes are instantly visible to all workers (shared memory).
Structural ops (`addLocation`, `removeLocation`) and `{prop}` COM writes
require the fan-out to mutate each worker's private routing tree or local conf.

A worker restarted by the master catches up by sending `{type:'get'}` to the
SharedWorker in its `nginx.broadcast` callback; the SW replies with the last
`_desired` snapshot so the new worker applies the full current state.

## Named handler registry

`nginx.admin.registerHandler(name, fn)` is called at init-conf time (master,
before fork).  All workers inherit `_handlers` via COW.  Structural-op snapshots
carry only the handler name string; each worker resolves the function locally.

```js
nginx.admin.registerHandler('dynamicHandler', function (r) {
    r.respond(200, {'Content-Type': 'application/json'},
        JSON.stringify({ resource: 'dynamic', worker: r.variable('pid') }) + '\n');
});
```

## SharedWorker API note

The nginx SharedWorker script must use the `onconnect` / per-port pattern.
Global `onmessage` in the SW thread receives raw data, and global `postMessage`
does not exist:

```js
var _ports = [];

onconnect = function (e) {
    var port = e.ports[0];
    _ports.push(port);

    port.onmessage = function (ev) {
        // ...
        _ports.forEach(function (p) { p.postMessage(ev.data); });
    };
};
```
