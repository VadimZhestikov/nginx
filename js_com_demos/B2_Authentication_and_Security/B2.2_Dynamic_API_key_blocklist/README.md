# B2.2 — Dynamic API Key Blocklist

## What it shows

A **SharedWorker** thread maintains an in-memory `Set` of revoked API keys.
Worker request handlers check incoming `X-API-Key` headers against that set
via `postMessage`, with zero external I/O per request.

A `/admin/revoke/` endpoint adds keys to the blocklist at runtime — no nginx
reload, no shared-memory zone resize.  The change takes effect on the very
next request.

## Architecture

```
nginx worker (request handler)
    │
    │ postMessage({cmd:'check', key:'...'})
    ▼
SharedWorker thread (sw.js)
    │  blocklist = new Set([...])
    │
    └─ postMessage({allowed: true/false})
```

## Classic nginx comparison

Classic nginx validates API keys by:
1. Storing keys in a `geo` or `map` block — requires a reload to change
2. Using `auth_request` to call an external auth microservice — adds latency
3. Using `lua_shared_dict` with OpenResty — requires Lua and the OpenResty
   distribution

With a SharedWorker the blocklist lives in a plain JavaScript `Set`, updates
are applied by calling `/admin/revoke/`, and no reload or external process
is needed.

## How to run

```bash
cd B2.2_Dynamic_API_key_blocklist
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Valid key → accepted
curl -H "X-API-Key: my-valid-key-123" http://localhost:8136/api/

# Pre-revoked key → blocked immediately at startup
curl -H "X-API-Key: revoked-at-startup" http://localhost:8136/api/

# Revoke a key at runtime
curl -X POST "http://localhost:8136/admin/revoke/?key=temp-key-abc"

# Now that key is blocked — no reload needed
curl -H "X-API-Key: temp-key-abc" http://localhost:8136/api/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
