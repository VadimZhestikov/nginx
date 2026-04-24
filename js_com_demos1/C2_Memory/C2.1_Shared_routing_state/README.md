# C2.1 — Shared Routing State Across Workers

## What it shows

`nginx.shared` is a cross-worker key-value store.  One worker writes a route
mapping; all other workers read it instantly.  With `worker_processes 2`, any
request that hits worker 1 to write a route is immediately visible to requests
that hit worker 2 — with zero IPC, zero file I/O.

## Why classic NGINX cannot do this

Classic NGINX location matching is static: changing it requires editing
`nginx.conf` and sending `nginx -s reload`.  The `map` directive is evaluated
per-request but the mapping is compiled at load time.  `nginx.shared` gives
you a mutable, persistent, cross-worker dictionary that any JS handler can
read or write at request time.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | 2-worker server on port 8163 |
| `handler.js` | `/admin/set-route/`, `/route/`, `/admin/list-routes/` |
| `test.sh` | Automated test: set → read → list |

## How to run

```bash
cd C2.1_Shared_routing_state
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Route before any config
curl -H "X-Path: /api/" http://localhost:8163/route/

# Set a route (from worker 1 or 2 — doesn't matter)
curl "http://localhost:8163/admin/set-route/?path=%2Fapi%2F&backend=v2"

# Read it back (visible to all workers)
curl -H "X-Path: /api/" http://localhost:8163/route/

# List all routes
curl http://localhost:8163/admin/list-routes/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. A `GET /route/` for an unknown path returns `"backend":"default"`.
2. `GET /admin/set-route/?path=/api/&backend=v2` stores the mapping in
   `nginx.shared`.
3. Subsequent `GET /route/` requests for `/api/` return `"backend":"v2"`
   regardless of which worker handles the request.
4. `/admin/list-routes/` shows all routes currently stored.
