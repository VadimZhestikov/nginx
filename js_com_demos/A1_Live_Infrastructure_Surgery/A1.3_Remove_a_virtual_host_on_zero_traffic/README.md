# A1.3 — Remove a Virtual Host on Zero Traffic

## What this demo shows

Safe tenant decommissioning in three steps — all at runtime:

1. Mark the tenant vhost for removal (stop accepting new traffic via a gate).
2. Wait until the in-flight request counter reaches zero.
3. Call `nginx.http.removeServer(name)` + `rebuildVhostDispatch()`.

This demo tracks per-vhost in-flight request counts in JS variables (works
for `worker_processes 1`). In a multi-worker setup use a `SharedArrayBuffer`
with `Atomics.add` for cross-worker coordination.

## Classic nginx approach

Classic nginx has no mechanism to remove a server at runtime without editing
`nginx.conf` and issuing `nginx -s reload`. If a tenant is decommissioned but
reload hasn't happened yet, their server keeps accepting traffic.

## Key API

```javascript
// Remove an existing server (returns true on success, false if not found)
var removed = nginx.http.removeServer('tenant-gone.example.com');
if (removed) {
    nginx.http.rebuildVhostDispatch();
}
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# No dynamic servers yet
curl http://127.0.0.1:8105/count/
# → 0

# Create two tenants at runtime
curl -X POST "http://127.0.0.1:8105/create/?acme.local"
curl -X POST "http://127.0.0.1:8105/create/?beta.local"

curl http://127.0.0.1:8105/count/
# → 2

# Both tenants serve traffic
curl -H "Host: acme.local" http://127.0.0.1:8105/
# → tenant: acme.local

# Remove acme — zero traffic (sync handler, so count is already 0)
curl -X POST "http://127.0.0.1:8105/remove/?acme.local"
# → removed: acme.local

# acme is gone, beta still serves
curl -H "Host: acme.local" http://127.0.0.1:8105/
# → 404
curl -H "Host: beta.local" http://127.0.0.1:8105/
# → tenant: beta.local

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
