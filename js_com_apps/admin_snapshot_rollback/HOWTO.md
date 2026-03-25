# admin_snapshot_rollback — usage guide

## 1. Start nginx

```bash
# From the repo root
nginx/objs/nginx \
  -c $(pwd)/js_com_apps/admin_snapshot_rollback/nginx.conf \
  -p $(pwd)/js_com_apps/admin_snapshot_rollback
```

nginx starts with two static servers (`static1.local:8080` and
`static2.local:8080`) and an admin API on `static1.local:8080/admin/`.

---

## 2. Inspect the live state

```bash
curl -s -H "Host: static1.local" http://127.0.0.1:8080/admin/state | jq .
```

Returns the current delta relative to the baseline captured at startup.
An empty ops list means nginx is at its baseline.

```json
{ "ops": [], "peers": [] }
```

---

## 3. Peer weight management

### Change a peer weight

```bash
# Set upstream "backend" peer 0 weight to 10
curl -s -X POST -H "Host: static1.local" \
  "http://127.0.0.1:8080/admin/apply/0001-weight-shift"
```

The ops-list format accepts any path recognised by `nginx.set()`:

```json
{ "ops": [
    { "path": "http.upstreams[0].peers[0].weight", "value": 10 },
    { "path": "http.upstreams[0].peers[1].down",   "value": true }
  ]
}
```

### Capture the current state as a snapshot

```bash
curl -s -X POST \
  "http://127.0.0.1:8080/admin/snapshots?name=weight-shift" \
  -H "Host: static1.local" | jq .
# → { "id": "0001-weight-shift" }
```

---

## 4. Dynamic location handlers

Handlers must be registered by name before they can appear in snapshot ops.
Register them in your `js_source` init script:

```js
// conf/init.js
nginx.admin.registerHandler('myHandler', function (req) {
    req.return(200, 'hello from myHandler\n');
});
```

### Install a handler on an existing location

```js
// ops-list entry
{ "path": "/probe/", "handler": "myHandler" }
```

### Remove a handler (restore original)

```js
{ "path": "/probe/", "handler": null }
```

---

## 5. Structural operations

Structural ops add or remove virtual servers and locations at runtime.
All structural changes are broadcast to every worker and take effect
immediately on the next request.

### Add a server

```bash
# Create a raw snapshot that adds a server + location
curl -s -X POST \
  "http://127.0.0.1:8080/admin/apply/0002-add-srv" \
  -H "Host: static1.local"
```

Snapshot JSON:

```json
{ "id": "0002-add-srv", "ts": 0, "ops": [
    { "op": "addServer",   "name": "dynamic.local" },
    { "op": "addLocation", "serverName": "dynamic.local",
      "pattern": "/",      "handler": "dynHandler" }
  ]
}
```

After applying, test the new server:

```bash
curl -s -H "Host: dynamic.local" http://127.0.0.1:8080/
```

### Remove a server

```json
{ "op": "removeServer", "name": "dynamic.local" }
```

> **Note**: `removeServer` removes the server from the virtual-host dispatch
> table but does NOT free the underlying C memory (cycle pool allocations are
> permanent).  Only use it for servers that were dynamically added.

### Add a socket / listener

```json
{ "op": "addListener", "address": "127.0.0.1:9090",
  "serverName": "static1.local" }
```

`addListener` creates a new TCP socket, binds it, and attaches the named server
to it.  This is **irreversible** — the socket remains open for the lifetime of
the process.

---

## 6. Rollback

```bash
# Step back one snapshot
curl -s -X POST -H "Host: static1.local" \
  http://127.0.0.1:8080/admin/rollback | jq .
# → { "rolledBackTo": "0001-weight-shift" } or { "rolledBackTo": null }
```

Rollback to base (`null`) resets:
- All tracked peer scalar properties to their init-time values
- Any servers added after init (removed via `removeServer`)
- Any locations added to base servers after init (removed via `removeLocation`)

Rollback does **not** restore:
- Servers or locations that were removed from the base config (irreversible)
- Sockets / listeners (irreversible once bound)

---

## 7. Snapshot maintenance

### List snapshots

```bash
curl -s -H "Host: static1.local" http://127.0.0.1:8080/admin/snapshots | jq .
```

### View a snapshot

```bash
curl -s -H "Host: static1.local" \
  http://127.0.0.1:8080/admin/snapshots/0001-weight-shift | jq .
```

### Compact (deduplicate) a snapshot

Compaction removes shadowed ops (last-write-wins per path) and no-op ops
(setting a value back to its baseline):

```bash
curl -s -X POST \
  "http://127.com/0.1:8080/admin/snapshots/0001-weight-shift/compact" \
  -H "Host: static1.local"
```

Or via JS:

```js
nginx.admin.compactSnapshot('0001-weight-shift');
```

### Squash multiple snapshots

```js
// Merge 0001 and 0002 into a single new snapshot
nginx.admin.squash(['0001-weight-shift', '0002-add-srv'], 'combined');
```

---

## 8. Persistence across restarts

`applySnapshot(id)` stores `id` as the **pinned** snapshot.  On next startup,
add this to your init script to re-apply it automatically:

```js
// conf/init.js
var pinned = /* read from file */ std.loadFile(
    nginx.cycle.prefix + 'snapshots/.pinned');
if (pinned) {
    nginx.admin.pin(pinned.trim());
    nginx.admin.applySnapshot(pinned.trim());
}
```

Alternatively, write the pinned id to a well-known file in your own startup
hook and re-apply it on each `nginx -s reload`.

---

## 9. Creating snapshots programmatically

Use `createRawSnapshot` to inject an explicit ops-list without needing to
first apply and then capture the state:

```js
nginx.admin.createRawSnapshot('add-canary', [
    { op: 'addServer',   name: 'canary.example.com' },
    { op: 'addLocation', serverName: 'canary.example.com',
      pattern: '/',      handler: 'canaryHandler' },
    { path: 'http.upstreams[1].peers[0].weight', value: 5 },
]);
```

This is useful for generating snapshots from configuration files or external
data sources without temporarily mutating the live config.
