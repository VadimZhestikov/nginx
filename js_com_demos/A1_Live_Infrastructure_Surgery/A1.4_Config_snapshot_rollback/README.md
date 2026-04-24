# A1.4 — Config Snapshot & Rollback

## What this demo shows

`loc.snapshot()` captures the complete state of a location (handler, root,
proxy settings, gzip settings, etc.) into an opaque object. `snap.restore()`
atomically reinstates the saved state. Together they enable:

- **Safe experimentation**: snapshot before a change, roll back if something goes wrong
- **Canary deployments**: snapshot stable handler, apply new handler, monitor, rollback
- **Automated circuit breakers**: if error rate spikes, restore() without a reload

## Classic nginx approach

There is no equivalent. Config changes require editing files and reloading nginx.
Rolling back means reverting the file edit and reloading again — two operations
with a gap between them during which the bad config is live.

## Key API

```javascript
// At config-phase or inside a handler:
var snap = loc.snapshot();          // capture full location state

loc.handler = newHandler;           // mutate — affects all future requests
// ... run experiment ...

snap.restore();                     // revert atomically — no reload needed
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Starts at v1-stable
curl http://127.0.0.1:8106/data/
# → v1-stable

# Save checkpoint
curl -X POST http://127.0.0.1:8106/admin/snapshot/
# → snapshot saved: v1-stable

# Deploy experimental version
curl -X POST "http://127.0.0.1:8106/admin/change/?v2-experimental"
curl http://127.0.0.1:8106/data/
# → v2-experimental

# Something went wrong — roll back instantly
curl -X POST http://127.0.0.1:8106/admin/rollback/
curl http://127.0.0.1:8106/data/
# → v1-stable    ← restored without any reload

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
