# admin_snapshot_rollback — architecture notes

## Component map

```
nginx.conf                ← static base configuration
conf/admin.js             ← admin core (snapshot/rollback engine)
conf/admin-api.js         ← HTTP REST dispatcher (mounts on /admin/)
snapshots/*.json          ← persisted snapshot files
```

`admin.js` is loaded as a `js_source` file.  It runs at `init_conf` time in
the master process, captures the baseline state, and exposes `nginx.admin` on
the global object.  `admin-api.js` (also a `js_source`) installs HTTP handlers
on `/admin/` locations at init time.

---

## Baseline capture

At init_conf time `admin.js` calls `_captureBaseState()` which:

1. Walks every upstream peer and records each `nginx.settable()` property as a
   `{path, value}` pair into `_base`.
2. Snapshots `_baseServerNames` (all current server names) and
   `_baseLocations` (per-server location patterns).

These baselines are frozen — they do not change on reload.  `_applySnapshot`
uses them to determine which servers and locations were added dynamically and
must be torn down during a reset.

---

## Snapshot format

```
{
  "id":  "0003-weight-shift",
  "ts":  1710000000,            ← Unix timestamp
  "ops": [ …op objects… ]      ← ordered list of mutations
}
```

Each op is one of:

| Shape | Effect |
|---|---|
| `{path, value}` | `nginx.set(path, value)` — index-based (fragile if topology changes) |
| `{prop, value}` | Named-descriptor COM scalar — resolved by `_resolveTarget(desc)` at apply time using `upstream.name`, `peer.address`, `server.name`, `location.path` instead of fragile indices |
| `{path, handler}` | Install / clear a JS handler on a location |
| `{op:"addServer", name}` | `nginx.http.addServer(name)` |
| `{op:"removeServer", name}` | `nginx.http.removeServer(name)` |
| `{op:"addLocation", serverName, pattern, handler?}` | `srv.addLocation(pat)` |
| `{op:"removeLocation", serverName, pattern}` | `srv.removeLocation(pat)` |
| `{op:"addListener", address, serverName?}` | `nginx.createSocket` + `attach` |

### `{prop}` named descriptor

`_resolveTarget(desc)` walks the COM tree at apply time:
- `{upstream, peer?, property}` — finds upstream by name, peer by `p.address`
- `{server, location?, subobject?, property}` — finds server by `s.name` / `s.names[]`, location by `l.path`
- string — falls back to `nginx.set(path, value)` (backward compat)

`_readProp(desc)` is the read counterpart used by `createSnapshot` to capture
live values of declared managed props.

### Managed props (`admin.init`)

`admin.init({props: [...]})` populates `_managedProps`.  Each descriptor may
include an optional `"default"` value used by `_applyOps` when rolling back
past the first snapshot.  `createSnapshot` appends `{prop, value}` ops for
each managed prop.  `admin.state()` includes a `"props"` sub-object.

---

## Apply / rollback flow

```
applySnapshot(id)
  └─ nginx.broadcast(fn)     ← runs fn in EVERY worker
       └─ _applySnapshot(snap)
            1. _applyOps(_base)           reset peer scalars
            2. structural reset:
               a. collect extra servers → removedRefs (keeps JS wrappers alive)
               b. nginx.http.removeServer(name)  for each extra server
               c. nginx.http.rebuildVhostDispatch()
               d. removedRefs = null   → finalizers run AFTER vhost rebuild
               e. removeLocation(pat)  for each extra location on base servers
            3. _applyOps(snap.ops)     apply the snapshot's mutations
```

`nginx.broadcast(fn)` in master context (init_conf) queues `fn` for each
worker's first event-loop tick.  In worker context it calls `fn` immediately
and synchronously on the current event-loop iteration.

---

## COW isolation

Workers are forked from the master after `init_conf`.  Every worker gets a
copy-on-write view of the master's JS heap (QuickJS runtime + context).

Because mutations are applied inside `nginx.broadcast()`, each worker applies
the same ops independently to its own COW copy:

- Peer scalar writes (`nginx.set`) modify COW'd C structs in place.
- `addLocation` / `removeLocation` rebuild the location BST in a per-server
  pool (`op->tree_pool`) that is private to each worker after the first write.
- `addServer` / `removeServer` modify `ngx_js_vhost_entries[]` arrays (also
  COW'd) and the `nginx.http.servers` JS array.

The net effect is that all workers end up with identical state after each
`broadcast` completes.

---

## server_names lifetime

nginx stores `cscf->server_names.elts` in `cf->temp_pool`, which is destroyed
at the end of `ngx_init_cycle()` — BEFORE workers fork.  The JS module patches
this at `ngx_js_wrap_server()` time (still inside `ngx_init_cycle`): the full
`ngx_http_server_name_t` array is copied into `cycle->pool` and
`cscf->server_names.elts` is updated to point at the copy.

This makes it safe for `rebuildVhostDispatch()` to iterate
`cscf->server_names` in worker context long after init.

---

## Dynamic server lifecycle

```
addServer('dynamic.local')
  → allocates new_cscf, new_root_clcf, new_ctx  in cycle->pool
  → initialises new_cscf->server_names           in cycle->pool
  → appends new_cscf to ngx_js_vhost_entries[i].servers
  → appends JS wrapper to nginx.http.servers[]
  → returns JS wrapper (ref count = 2: servers[] + return value)

addLocation('/', …)  on the new server
  → allocates new_clcf, jlcf                     in op->dyn_pool
  → appends to op->prefix_locs
  → calls ngx_js_rebuild_loc_tree():
      creates op->tree_pool, builds BST, sets root_clcf->static_locations

removeServer('dynamic.local')
  → splices cscf from ngx_js_vhost_entries[i].servers
  → splices JS wrapper from nginx.http.servers[] (ref count → 1)
  → JS wrapper still alive (held by removedRefs)

rebuildVhostDispatch()
  → rebuilds virtual_names hash without dynamic.local

removedRefs = null
  → JS wrapper ref count → 0
  → ngx_js_server_finalizer() runs:
      NULLs root_clcf->static_locations (and regex_/named_locations)
      destroys op->tree_pool
      destroys op->dyn_pool
      frees op
```

The `removedRefs` guard in `_applySnapshot` ensures the finalizer does not
run until AFTER `rebuildVhostDispatch()` has updated the hash, so no live
request can be dispatched to the server whose pools are being freed.

---

## Location tree internals

Each server wrapper holds an `ngx_js_server_opaque_t` with:

| Field          | Pool      | Contents                                           |
|----------------|-----------|----------------------------------------------------|
| `prefix_locs`  | `dyn_pool`| Flat snapshot of all prefix/exact locations        |
| `regex_locs`   | `dyn_pool`| Ordered list of regex locations                    |
| `named_locs`   | `dyn_pool`| Named (`@foo`) locations                           |
| `tree_pool`    | own pool  | BST nodes for `root_clcf->static_locations`        |
| `dyn_pool`     | own pool  | `new_clcf` / `jlcf` structs for dynamic locations |

`addLocation` appends to `prefix_locs` / `regex_locs` / `named_locs` and
calls `ngx_js_rebuild_loc_tree()`.  `removeLocation` splices the entry and
rebuilds.

`ngx_js_rebuild_loc_tree()`:
1. Creates a fresh `tree_pool`.
2. Builds a new BST from `op->prefix_locs`.
3. Sets `root_clcf->static_locations = new_root` atomically.
4. Destroys the OLD `tree_pool`.
5. Sets `op->tree_pool = new_pool`.

Step 4 happens AFTER step 3, so no request sees a partially-rebuilt tree.

---

## Handler registry

Handlers are registered by name before being referenced in snapshot ops:

```js
nginx.admin.registerHandler('myHandler', function (req) { … });
```

Stored in the module-level `_handlers` object.  At `_applyOps` time, handler
names are resolved from this map.  A handler name that is not registered is
logged and skipped (no exception).

`handler: null` clears the handler (restores the location's original handler,
if any, via `loc.clearHandler()`).

---

## Compaction algorithm

`compactOps(ops [, baseOps])`:

1. Walk the ops list in REVERSE order.
2. Keep the LAST occurrence of each `(op-type, key)` — earlier duplicates are
   shadowed (last-write-wins).  Keys:
   - `{path}` ops: `'V:<path>'`
   - `{prop}` ops: `'P:<upstream>:<peer>:<server>:<location>:<subobject>:<property>'`
   - `{path, handler}` ops: `'H:<path>'`
   - structural `{op}` ops: `'S:<op>:<name>:<serverName>:<pattern>'`
3. If `baseOps` is provided, remove any `{path}` or `{prop}` op whose value
   equals the baseline value (identity removal).

`squash(ids, name)` concatenates the ops of multiple snapshots and compacts
the result into a single new snapshot.

---

## Test coverage

| Test file | What it covers |
|---|---|
| `js_admin_base.t` | Baseline capture; peer weight delta detection |
| `js_admin_snapshot.t` | `createSnapshot`, `applySnapshot`, snapshot JSON format |
| `js_admin_rollback.t` | `rollback`, `pin`, multi-step rollback chains |
| `js_admin_api.t` | HTTP REST endpoints; error responses |
| `js_admin_compact.t` | `compactSnapshot`, `squash`; ops deduplication |
| `js_admin_edge.t` | Edge cases |
| `js_admin_struct.t` | `addLocation`, `removeLocation`, `addServer`, `removeServer`, `addListener`; structural rollback |
| `js_admin_prop.t` | `{prop}` named-descriptor create/apply; `admin.init` auto-capture; `compactOps` dedup for `{prop}`; rollback to defaults; `compact/:id`, `squash`, `worker` REST endpoints |
