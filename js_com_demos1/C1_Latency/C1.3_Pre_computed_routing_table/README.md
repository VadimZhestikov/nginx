# C1.3 — Pre-Computed Routing Table (SharedWorker)

## What it shows

A SharedWorker thread loads `routes.json` into a JavaScript `Map` once at
startup.  Every worker process request handler sends a `{cmd:'route'}` message
and gets an O(1) Map lookup result back — no file I/O, no JSON parsing, no
per-request work beyond a single message round-trip.

The routing table lives in one place (the SharedWorker), shared across all
NGINX worker processes without duplicating data.

## Why classic NGINX cannot do this

Classic NGINX routing (`location` blocks, `map` directives) is static —
routes must be defined at config-load time and require a reload to change.
The JS SharedWorker can hot-reload `routes.json` at any time by sending an
`{cmd:'init'}` message without restarting NGINX.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | 2-worker server on port 8162 |
| `handler.js` | Creates SharedWorker, handles `/route/` and `/admin/routes/` |
| `sw.js` | SharedWorker: loads routes.json, answers lookup queries |
| `routes.json` | Route prefix → backend mapping |
| `test.sh` | Automated test: verify all 5 prefixes + fallback |

## How to run

```bash
cd C1.3_Pre_computed_routing_table
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Look up a route by target URI
curl -H "X-Target-Uri: /api/v2/users" http://localhost:8162/route/

# List all loaded routes
curl http://localhost:8162/admin/routes/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Start nginx — the SharedWorker loads `routes.json` immediately.
2. Send requests with `X-Target-Uri` header set to various paths.
3. Each response includes `{"target_uri":..., "backend":...}`.
4. Unknown prefixes return `"backend":"default_backend"`.
5. To add a route: edit `routes.json` and POST `{cmd:'init'}` to the
   SharedWorker (add an `/admin/reload/` endpoint for production use).
