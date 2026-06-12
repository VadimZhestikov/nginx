# admin_snapshot_rollback

A self-contained nginx admin application built on the JS COM API that provides
live config snapshot and rollback without a process reload.

## Concept

nginx starts from a static `nginx.conf`.  At runtime operators mutate the live
config (upstream peer weights, location handlers, virtual servers, …) through
the admin API.  Each mutation can be captured as a named **snapshot** — a delta
from the base `nginx.conf` state stored as a JSON file under `snapshots/`.
Snapshots can be applied or rolled back at any time; the last applied snapshot
is remembered across restarts.

```
snapshots/
  0001-initial.json        ← oldest
  0002-add-canary.json
  0003-weight-shift.json   ← current (pinned)
```

### Snapshot format

Snapshots use an **ops-list** format.  Four op families coexist in the same
snapshot:

```json
{
  "id": "0003-weight-shift",
  "ts": 1710000000,
  "ops": [
    { "path": "http.upstreams[0].peers[0].weight", "value": 10 },
    { "prop": { "upstream": "backend", "peer": "10.0.0.1:8080",
                "property": "weight" }, "value": 10 },
    { "path": "/canary/", "handler": "canaryHandler" },
    { "op": "addServer",   "name": "dynamic.local" },
    { "op": "addLocation", "serverName": "dynamic.local",
      "pattern": "/", "handler": "dynHandler" }
  ]
}
```

### Supported op types

| Shape | Effect |
|---|---|
| `{path, value}` | `nginx.set(path, value)` — index-based path (fragile if topology changes) |
| `{prop, value}` | Named-descriptor COM scalar — resolved by name at apply time (stable) |
| `{path, handler}` | Install/clear a named JS handler on a location |
| `{op: "addServer", name}` | Add a virtual server |
| `{op: "removeServer", name}` | Remove a virtual server |
| `{op: "addLocation", serverName, pattern, handler?}` | Add a location |
| `{op: "removeLocation", serverName, pattern}` | Remove a location |
| `{op: "addListener", address, serverName?}` | Create socket, attach, bind server |

### Named-descriptor `{prop}` ops

`{prop}` ops identify COM objects by **stable names** instead of array
indices.  Use them wherever you would otherwise write a fragile index-based
path like `"http.upstreams[0].peers[0].weight"`.

Descriptor shapes:

| Shape | Resolves to |
|---|---|
| `{upstream, property}` | upstream-level scalar |
| `{upstream, peer, property}` | individual peer (`weight`, `down`, …) |
| `{server, property}` | server-level scalar |
| `{server, location, property}` | location scalar |
| `{server, location, subobject, property}` | sub-object (`proxy`, `gzip`, …) |
| string | raw `nginx.set()` path (backward compat) |

Optional `"default"` in a descriptor sets the value used on rollback-to-base.

Structural ops (`addServer`, `addLocation`) still require a `handler` name
pre-registered via `nginx.admin.registerHandler(name, fn)` in the init script.

## Admin HTTP API

| Method | Path | Action |
|---|---|---|
| GET | `/admin/state` | Current live delta + managed prop values (JSON) |
| GET | `/admin/snapshots` | List snapshot ids (JSON array) |
| POST | `/admin/snapshots` | Create snapshot → `{"id":"…"}` |
| POST | `/admin/raw-snapshot` | Create from explicit ops; body `{"name":"…","ops":[…]}` |
| GET | `/admin/snapshots/:id` | Show snapshot JSON content |
| POST | `/admin/apply/:id` | Apply snapshot → `{"applied":"…"}` |
| POST | `/admin/rollback` | Roll back one step |
| POST | `/admin/set` | Set one `nginx.shared` key; body `{"key":"…","value":"…"}` |
| POST | `/admin/compact/:id` | Compact snapshot ops in place → `{"removed":N}` |
| POST | `/admin/squash` | Merge N snapshots; body `{"ids":[…],"name":"…"}` |
| GET | `/admin/worker` | Responding worker PID |

### Query parameters

- `POST /admin/snapshots?name=<label>` — human-readable label embedded in the id

## JS API (`nginx.admin`)

```js
nginx.admin.init({props: [...]})         // declare managed COM scalar props
nginx.admin.state()                      // delta vs base + live prop values
nginx.admin.createSnapshot(name)         // persist delta + props → returns id
nginx.admin.createRawSnapshot(name, ops) // persist explicit ops-list
nginx.admin.listSnapshots()              // sorted list of ids
nginx.admin.applySnapshot(id)            // reset to base + apply
nginx.admin.rollback()                   // step back one snapshot
nginx.admin.pin(id)                      // remember for next restart
nginx.admin.registerHandler(name, fn)    // register a named JS handler
nginx.admin.compactSnapshot(id)          // deduplicate ops in place → N removed
nginx.admin.squash(ids, name)            // merge N snapshots into one
nginx.admin.compactOps(ops [, base])     // pure dedup function (last-write-wins)
nginx.admin.options.compact              // bool: auto-compact on createSnapshot
```

### Declaring managed COM scalar properties

`admin.init({props})` lets you declare COM scalar properties that
`createSnapshot()` auto-captures and `rollback()` resets to defaults.
Each descriptor uses **stable names** (not indices):

```js
nginx.admin.init({
    props: [
        { upstream: 'backend', peer: '10.0.0.1:8080',
          property: 'weight', default: 5 },
        { server: 'api', location: '/api/', subobject: 'proxy',
          property: 'connectTimeout', default: 5000 }
    ]
});
```

After `init()`, `admin.state()` returns a `"props"` sub-object with live
values alongside the existing `"ops"` and `"peers"` fields.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Base nginx configuration |
| `conf/admin.js` | Admin core: snapshot/rollback/structural ops + `{prop}` named descriptors |
| `conf/admin-api.js` | HTTP REST dispatcher (mounts on `/admin/`) |
| `snapshots/` | Persisted snapshot JSON files |
| `t/js_admin_base.t` | Base-state capture and peer sync |
| `t/js_admin_snapshot.t` | createSnapshot / applySnapshot |
| `t/js_admin_rollback.t` | rollback and pin |
| `t/js_admin_api.t` | HTTP REST endpoint coverage |
| `t/js_admin_compact.t` | compactSnapshot and squash |
| `t/js_admin_struct.t` | Structural ops: addLocation/Server, listener |
| `t/js_admin_edge.t` | Edge cases |
| `t/js_admin_prop.t` | `{prop}` named-descriptor ops; `admin.init`; new REST endpoints |

## Running

```bash
# from the repo root
nginx/objs/nginx -c $(pwd)/js_com_apps/admin_snapshot_rollback/nginx.conf \
                 -p $(pwd)/js_com_apps/admin_snapshot_rollback
```

## Running tests

```bash
TEST_NGINX_BINARY=$(pwd)/nginx/objs/nginx \
  prove js_com_apps/admin_snapshot_rollback/t/
```

## Implementation notes

### COW isolation

Location and server mutations are per-worker (copy-on-write after fork) and
propagated to all workers via `nginx.broadcast()` inside `applySnapshot`.
Socket and listener creation is irreversible once the OS-level socket is bound,
so `addListener` ops must be applied at init time (before workers fork), not via
a broadcast.

### server_names lifetime

nginx stores `cscf->server_names.elts` in `cf->temp_pool`, which is destroyed
at the end of `ngx_init_cycle()`.  `ngx_js_wrap_server()` relocates this array
into `cycle->pool` during init so that `rebuildVhostDispatch()` can safely
iterate server names in worker context.
