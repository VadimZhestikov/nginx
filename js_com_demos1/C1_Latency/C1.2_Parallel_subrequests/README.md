# C1.2 — Fan-out / Fan-in with Subrequests

## What it shows

`/merged/` issues three sequential internal subrequests to `/internal/alpha/`,
`/internal/beta/`, and `/internal/gamma/`, parses each JSON response, and
returns a single combined JSON object to the client in one round-trip.

This is the "API gateway aggregation" pattern — multiple upstream micro-services
merged server-side so the browser makes only one network call.

> **Note:** `Promise.all()` over subrequests is not yet supported; the demo
> uses sequential `await r.subrequest(...)` calls.  The pattern is the same;
> parallel execution will be enabled in a future release.

## Why classic NGINX cannot do this

Classic NGINX can proxy to one upstream per location.  Composing multiple
upstream responses requires either `ngx_http_addition_module` (append-only,
no JSON merging), SSI (server-side includes, text-only), or an external
service.  The JS subrequest API lets you fan out, parse, transform, and
re-merge responses entirely inside NGINX.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8161; internal locations for each micro-service |
| `handler.js` | Internal handlers + async fan-out aggregator |
| `test.sh` | Automated test: verify all 3 services appear in merged output |

## How to run

```bash
cd C1.2_Parallel_subrequests
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf
curl -s http://localhost:8161/merged/ | python3 -m json.tool
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. `GET /merged/` returns a single JSON object with `sources` array containing
   all three service responses plus a `total_value` aggregation.
2. The internal locations (`/internal/alpha/` etc.) are marked `internal;` so
   they cannot be reached directly from outside — they are private to NGINX.
3. Replace the stub handlers with real `proxy_pass` directives to upstream
   micro-services for a production aggregation gateway.
