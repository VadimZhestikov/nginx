# C1.1 — In-Process JWT Cache (SharedArrayBuffer)

## What it shows

A `SharedArrayBuffer`-backed LRU cache that lets all NGINX worker processes
share validated JWT results without any IPC.  The first request for a token
triggers full (simulated) cryptographic verification and writes the result
into a 16-slot SAB hash table using `Atomics`.  Every subsequent request for
the same token reads from the shared cache in nanoseconds, skipping the
verification entirely.

Response header `X-Cache: HIT/MISS` shows which path was taken.

## Why classic NGINX cannot do this

Classic NGINX has no in-process scripting.  Lua modules (e.g. `lua-resty-*`)
can do per-worker caching, but workers do not share memory unless you use a
`lua_shared_dict` — which is limited to simple string/number types and
requires serialisation.  SAB gives you a typed-array view of raw shared
memory with atomic operations, exactly as in browsers.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | 2-worker server on port 8160 |
| `handler.js` | SAB cache + fake JWT issue/verify logic |
| `test.sh` | Automated test: miss → hit → stats |

## How to run

```bash
cd C1.1_In_process_JWT_cache
bash test.sh
```

Or manually:

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Get a token
TOKEN=$(curl -s 'http://localhost:8160/admin/token/?sub=alice' | grep -o '"token":"[^"]*"' | cut -d'"' -f4)

# First hit — MISS (verifies token, stores in cache)
curl -i -H "Authorization: Bearer $TOKEN" http://localhost:8160/protected/

# Second hit — HIT (reads from SAB, skips verification)
curl -i -H "Authorization: Bearer $TOKEN" http://localhost:8160/protected/

# Cache stats
curl http://localhost:8160/admin/cache-stats/

# Stop
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Notice `X-Cache: MISS` on the first request.
2. Notice `X-Cache: HIT` on every subsequent request for the same token.
3. `/admin/cache-stats/` reports how many of the 16 SAB slots are populated.
4. Because `worker_processes 2`, both workers share the same SAB — a token
   cached by worker 1 is instantly visible to worker 2 with zero IPC.
