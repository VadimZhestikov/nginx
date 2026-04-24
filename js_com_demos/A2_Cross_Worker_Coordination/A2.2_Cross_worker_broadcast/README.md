# A2.2 — Cross-Worker Broadcast via nginx.shared

## What this demo shows

`nginx.shared` is a fixed shared-memory key-value store accessible from every
worker process. Writes from one worker are immediately visible to all others —
no IPC, no external store, no locks visible to JS.

This enables a "live config push" pattern:

1. An admin endpoint writes to `nginx.shared`.
2. Every subsequent request — regardless of which worker handles it — reads the
   updated value from `nginx.shared`.

## Classic nginx approach

Classic nginx has no mechanism to push a config value change to running workers
without a reload. The `lua-nginx-module` offers `ngx.shared.DICT` for a similar
pattern but it is not available in pure JavaScript.

## Key API

```javascript
// Write (any worker)
nginx.shared.set('feature.theme', 'dark');

// Read (any worker — immediately sees the latest value)
var theme = nginx.shared.get('feature.theme');

// Delete
nginx.shared.delete('feature.theme');

// List all keys
var keys = nginx.shared.keys();
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx with 2 workers
../../../objs/nginx -p . -c nginx.conf

# See initial config — seeded at startup
curl http://127.0.0.1:8109/config/

# Push a config update from the admin endpoint
curl -X POST --data "upstream.url=http://backend-v2.internal/" \
     http://127.0.0.1:8109/admin/update/

# All subsequent requests see the new value regardless of which worker answers
curl http://127.0.0.1:8109/config/
curl http://127.0.0.1:8109/config/
curl http://127.0.0.1:8109/config/

# Remove a key
curl -X POST "http://127.0.0.1:8109/admin/delete/?max.rps"

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
