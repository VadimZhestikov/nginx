# C4.1 — Unit-Test Routing Rules (Without NGINX)

## What it shows

Routing logic written in pure JavaScript (`route.js`) can be unit-tested with
`qjs` — the QuickJS REPL — entirely without starting NGINX.  The same function
is then used inside the NGINX handler (`handler.js`), giving you one source of
truth that is testable both off-line and on-line.

This is the "shift-left" pattern for infrastructure code: catch routing bugs
in a fast `qjs` test run (milliseconds) before ever hitting a live NGINX
instance.

## Files

| File | Purpose |
|---|---|
| `route.js` | Pure routing function (ES module, no platform APIs) |
| `route-test.js` | 12 unit tests, runs with `qjs route-test.js` |
| `handler.js` | Same logic used inside nginx (inlined, no import needed) |
| `nginx.conf` | Server on port 8172, `/route/` endpoint |
| `test.sh` | Runs qjs tests then nginx integration checks |

## How to run

```bash
cd C4.1_Unit_test_routing_rules

# Unit tests only (no nginx)
qjs --std route-test.js

# Full test (qjs + nginx integration)
bash test.sh
```

Or manually with nginx:

```bash
../../../objs/nginx -p . -c nginx.conf

curl -H "X-Target-Uri: /api/users" -H "Accept: application/json" \
     http://localhost:8172/route/
# → {"uri":"/api/users","backend":"json_backend"}

curl "http://localhost:8172/route/?uri=%2Fadmin%2Fsettings"
# → {"uri":"/admin/settings","backend":"forbidden"}

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Route rules

| URI pattern | Headers | Backend |
|---|---|---|
| `/api/*` | `Accept: *json*` | `json_backend` |
| `/api/*` | (any) | `api_backend` |
| `/static/*` | (any) | `cdn_backend` |
| `/health` or `/health/` | (any) | `health_backend` |
| `/admin/*` | `x-internal: true` | `admin_backend` |
| `/admin/*` | (missing) | `forbidden` |
| anything else | (any) | `default_backend` |

## Demo steps

1. `qjs route-test.js` runs 12 tests in < 50 ms, no servers, no ports.
2. `bash test.sh` also starts nginx and confirms the same logic works
   as a live HTTP endpoint.
3. Modify `route.js` to add a new rule — tests immediately show whether
   it breaks existing behaviour before deployment.
