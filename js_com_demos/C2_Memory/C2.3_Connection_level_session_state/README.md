# C2.3 — Per-Request Scratchpad via r.ctx

## What it shows

`r.ctx` is a plain JavaScript object whose lifetime matches exactly one HTTP
request.  Multiple pipeline stages — pre-handler hooks, the async handler,
and response-header injection — all share the same `r.ctx` object.  When the
request ends NGINX discards it; the next request starts with a fresh `{}`.

This demo shows:

- A hook recording `startTime` and a `requestId` into `r.ctx` before the
  handler runs.
- The handler reading those values and adding `duration` and `handlerRan`.
- Response headers (`X-Request-Id`, `X-Duration`) populated from `r.ctx`.
- `r.ctx` surviving an `async`/`await` suspension (demonstrated in `/echo/`).

## Why classic NGINX cannot do this

Classic NGINX passes context between modules via C-level `ngx_http_module`
contexts — one allocated struct per module per request.  There is no dynamic
scratchpad accessible from configuration-layer scripting.  Lua's `ngx.ctx`
is the closest analogue; `r.ctx` is the JavaScript equivalent.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8165 |
| `handler.js` | Hook + handler + async handler sharing r.ctx |
| `test.sh` | Automated test: verify headers and JSON body fields |

## How to run

```bash
cd C2.3_Connection_level_session_state
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# See r.ctx fields in JSON body + response headers
curl -i http://localhost:8165/status/

# See r.ctx surviving an async/await suspension
curl -i http://localhost:8165/echo/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Each `GET /status/` increments the request counter and returns a unique
   `X-Request-Id` header and body.
2. `hookRan: true` confirms the pre-handler hook ran and wrote to `r.ctx`.
3. `duration` shows how long the handler took (typically < 1 ms).
4. `GET /echo/` shows `"phase":"after-await"` — the ctx object was not
   garbage-collected during the `await`, proving it is request-scoped.
