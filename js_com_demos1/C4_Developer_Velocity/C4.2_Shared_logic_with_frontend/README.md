# C4.2 — Shared Logic with Frontend

## What it shows

A URL normalization function in `normalize.js` has **zero platform
dependencies** — no `window`, no `process`, no `ngx_*` calls.  The exact
same file:

1. Is imported by `browser-test.js` and tested with `qjs` (simulating a
   browser test runner).
2. Is inlined into `handler.js` and used inside the NGINX request handler.

This is the "isomorphic" or "universal" JavaScript pattern applied to
infrastructure: write business logic once, test it anywhere, deploy it
everywhere.

## Why classic NGINX cannot do this

Classic NGINX configuration uses a proprietary DSL.  There is no way to share
normalization/validation/routing logic between NGINX and your frontend
JavaScript.  You end up writing the same logic twice and keeping two
implementations in sync.  With JS handlers, the same `.js` file that your
browser tests import is the file NGINX evaluates.

## Files

| File | Purpose |
|---|---|
| `normalize.js` | Pure URL normalization (ES module, no platform APIs) |
| `browser-test.js` | 13 unit tests, runs with `qjs browser-test.js` |
| `handler.js` | NGINX handler (normalizeUrl inlined) |
| `nginx.conf` | Server on port 8173, `/normalize/` endpoint |
| `test.sh` | Runs qjs tests then nginx integration checks |

## How to run

```bash
cd C4.2_Shared_logic_with_frontend

# Browser-context tests (no nginx)
qjs --std browser-test.js

# Full test (qjs + nginx)
bash test.sh
```

Or manually with nginx:

```bash
../../../objs/nginx -p . -c nginx.conf

# Normalize via query parameter
curl "http://localhost:8173/normalize/?url=%2FAPI%2F%2FUsers%2F%2FProfile"
# → {"raw":"/API//Users//Profile","normalized":"/api/users/profile"}

# Normalize via header
curl -H "X-Raw-Url: /Page#Section" http://localhost:8173/normalize/
# → {"raw":"/Page#Section","normalized":"/page_section"}

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Normalization rules

1. Lowercase the entire URL.
2. Collapse consecutive `/` into one.
3. Remove trailing `/` (except for the root `/`).
4. Replace any character that is not `a-z 0-9 / - _ .` with `_`.
