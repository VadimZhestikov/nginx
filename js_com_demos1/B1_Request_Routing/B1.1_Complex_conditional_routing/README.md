# B1.1 — Complex Conditional Routing

## What it shows

Routes a single incoming request to different internal backends based on **three
simultaneous conditions** evaluated in JavaScript:

| Condition | Checked via |
|---|---|
| Tenant identity | `X-Tenant` request header |
| URI prefix | `r.uri.indexOf('/api/')` |
| Accepted content type | `Accept` request header |

Decision matrix:

| X-Tenant | URI | Accept | Backend |
|---|---|---|---|
| `acme` | starts `/api/` | contains `json` | acme JSON backend |
| `acme` | anything | anything | acme HTML backend |
| (anything else) | — | — | default backend |

## Classic nginx comparison

Equivalent logic in classic nginx requires three nested `map {}` blocks (one per
condition) plus `if ($combined_var = "...")` guards inside each location — the
`if` directive is famously error-prone in nginx and cannot perform complex
boolean combinations.  In JavaScript the same logic is a six-line `if/else`.

## How to run

```bash
cd B1.1_Complex_conditional_routing
bash test.sh
```

The script starts nginx on port 8130, runs three curl requests, and stops nginx.

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Test 1: acme tenant with JSON API request → acme JSON backend
curl -H "X-Tenant: acme" -H "Accept: application/json" \
    http://localhost:8130/api/route/

# Test 2: acme tenant without JSON accept → acme HTML backend
curl -H "X-Tenant: acme" -H "Accept: text/html" \
    http://localhost:8130/route/

# Test 3: unknown tenant → default backend
curl http://localhost:8130/route/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
