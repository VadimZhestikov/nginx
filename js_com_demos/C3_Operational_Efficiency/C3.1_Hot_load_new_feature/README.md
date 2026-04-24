# C3.1 — Hot-Load New Feature Plugin

## What it shows

`nginx.use(path, config)` loads a JavaScript file as a plugin at runtime.
The file must define a global `install(nginx, config)` function; `nginx.use`
evaluates the file and calls `install` immediately.  The plugin re-wires
location handlers, giving you **zero-downtime feature swaps** without
`nginx -s reload`.

Flow:
1. At startup, `handler.js` loads `plugin_v1/` → `/api/` responds with v1.
2. `GET /admin/load/?version=v2` evaluates `plugin_v2/index.js` in the same runtime
   → `/api/` immediately switches to v2.
3. `GET /admin/load/?version=v1` rolls back.

## Why classic NGINX cannot do this

Classic NGINX requires a config-file edit + `nginx -s reload` to change any
handler behaviour.  A reload spawns new workers, drains old ones, and has
~100 ms of elevated latency.  Plugin hot-loading changes behaviour
instantaneously, in the current worker process, with zero restarts.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8166; `/api/`, `/admin/load/`, `/admin/version/` |
| `handler.js` | Loads v1 at startup, exposes hot-swap endpoint |
| `plugin_v1/index.js` | Feature v1: basic response |
| `plugin_v2/index.js` | Feature v2: improved response + extra headers |
| `test.sh` | Automated test: v1 → swap to v2 → verify → rollback |

## How to run

```bash
cd C3.1_Hot_load_new_feature
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

curl http://localhost:8166/api/               # v1 response
curl http://localhost:8166/admin/version/     # {"version":"v1"}

curl "http://localhost:8166/admin/load/?version=v2"   # hot-swap
curl -i http://localhost:8166/api/            # v2 response + X-Feature-Flags

curl "http://localhost:8166/admin/load/?version=v1"   # rollback
curl http://localhost:8166/api/               # v1 again

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Start nginx → v1 is active.
2. Hit `/admin/load/?version=v2` — no reload, no downtime.
3. Confirm `/api/` returns `v2 response` and the `X-Feature-Flags` header.
4. Roll back to v1 with `/admin/load/?version=v1`.
5. The plugin file can come from a local path, a shared volume, or (in
   production) be fetched from a config store before calling `nginx.use()`.
