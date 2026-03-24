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

Snapshots use an **ops-list** format.  Each op is either a property write, a
named handler install, or a structural change:

```json
{
  "id": "0003-weight-shift",
  "ts": 1710000000,
  "ops": [
    { "path": "http.upstreams[0].peers[0].weight", "value": 10 },
    { "path": "http.upstreams[0].peers[1].weight", "value":  1 },
    { "path": "/canary/", "handler": "canaryHandler" },
    { "op": "addServer",   "name": "dynamic.local" },
    { "op": "addLocation", "serverName": "dynamic.local",
      "pattern": "/", "handler": "dynHandler" }
  ]
}
```

### Supported op types

| `op` field     | Effect                                                  |
|----------------|---------------------------------------------------------|
| *(absent)*     | `nginx.set(path, value)` — set a peer scalar property   |
| *(absent)*     | `loc.handler = fn` — install/clear a named JS handler   |
| `addServer`    | Add a virtual server (name required)                    |
| `removeServer` | Remove a virtual server by name                         |
| `addLocation`  | Add a location to a server (serverName + pattern)       |
| `removeLocation` | Remove a location from a server                       |
| `addListener`  | Create a socket, attach it, and optionally bind a server|

Structural ops (`addServer`, `addLocation`) require a `handler` name that was
pre-registered with `nginx.admin.registerHandler(name, fn)` in the init script.

## Admin HTTP API

| Method | Path                  | Action                                  |
|--------|-----------------------|-----------------------------------------|
| GET    | /admin/state          | Current live config delta (JSON)        |
| GET    | /admin/snapshots      | List snapshot ids (JSON array)          |
| POST   | /admin/snapshots      | Create snapshot → `{"id":"0001-…"}`     |
| GET    | /admin/snapshots/:id  | Show snapshot JSON content              |
| POST   | /admin/apply/:id      | Apply snapshot → `{"applied":"0001-…"}` |
| POST   | /admin/rollback       | Roll back to previous snapshot          |

### Query parameters

- `POST /admin/snapshots?name=<label>` — human-readable label embedded in the id

## JS API (`nginx.admin`)

```js
nginx.admin.state()                      // current delta vs base
nginx.admin.createSnapshot(name)         // persist delta → returns id
nginx.admin.createRawSnapshot(name, ops) // persist explicit ops-list
nginx.admin.listSnapshots()              // sorted list of ids
nginx.admin.applySnapshot(id)            // reset to base + apply
nginx.admin.rollback()                   // step back one snapshot
nginx.admin.pin(id)                      // remember for next restart
nginx.admin.registerHandler(name, fn)    // register a named JS handler
nginx.admin.compactSnapshot(id)          // deduplicate ops in place
nginx.admin.squash(ids, name)            // merge snapshots into one
nginx.admin.options.compact              // auto-compact on createSnapshot
```

## Files

| File                  | Purpose                                        |
|-----------------------|------------------------------------------------|
| `nginx.conf`          | Base nginx configuration                       |
| `conf/admin.js`       | Admin core: snapshot/rollback/structural ops   |
| `conf/admin-api.js`   | HTTP REST dispatcher (mounts on `/admin/`)     |
| `snapshots/`          | Persisted snapshot JSON files                  |
| `t/js_admin_base.t`   | Base-state capture and peer sync tests         |
| `t/js_admin_snapshot.t` | createSnapshot / applySnapshot tests         |
| `t/js_admin_rollback.t` | rollback and pin tests                       |
| `t/js_admin_api.t`    | HTTP REST endpoint tests                       |
| `t/js_admin_compact.t`| compactSnapshot and squash tests               |
| `t/js_admin_struct.t` | Structural ops: addLocation/Server, listener   |

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
