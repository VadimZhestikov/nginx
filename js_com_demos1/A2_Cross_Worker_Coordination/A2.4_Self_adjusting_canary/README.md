# A2.4 — Self-Adjusting Canary

## What this demo shows

A JS-controlled canary deployment where the routing weight **adjusts
automatically** based on observed error rates — no external controller, no
reload required.

- Canary starts at a configurable weight (default 10% of traffic).
- Each 5xx response from the canary increments an error counter.
- When errors exceed the threshold (default 3), `canaryWeight` drops to 0%
  automatically — the canary is killed.
- Admin endpoints allow manual weight adjustment and error reset.

## Classic nginx approach

Classic nginx canary deployments are implemented with `split_clients` directives
(static percentage) or upstream weight settings. Neither can adjust
automatically at runtime based on error counts without a reload.

## Architecture

```
GET /api/
  ↓
JS handler: Math.random() < canaryWeight/100 ?
  → subrequest /backend/canary/   (tracks errors, auto-kills if >threshold)
  → subrequest /backend/stable/
```

## Key pattern

```javascript
var canaryWeight = 10;  // start at 10%
var canaryErrors = 0;
var ERROR_THRESHOLD = 3;

loc.handler = async function(r) {
    var useCanary = Math.random() * 100 < canaryWeight;
    var result = await r.subrequest(useCanary ? '/backend/canary/' : '/backend/stable/');

    if (useCanary && result.status >= 500) {
        canaryErrors++;
        if (canaryErrors >= ERROR_THRESHOLD) {
            canaryWeight = 0;  // auto-kill — no reload needed
        }
    }
    r.respond(result.status, {}, result.body);
};
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Check initial state
curl http://127.0.0.1:8111/admin/status/
# → {"canaryWeight":10,"canaryErrors":0,...}

# Send traffic — some hits canary (10%), rest goes to stable
for i in $(seq 20); do curl -s http://127.0.0.1:8111/api/; done

# Report errors from canary (simulates 5xx responses)
curl -X POST http://127.0.0.1:8111/admin/report-error/
curl -X POST http://127.0.0.1:8111/admin/report-error/
curl -X POST http://127.0.0.1:8111/admin/report-error/

# Canary auto-killed
curl http://127.0.0.1:8111/admin/status/
# → {"canaryWeight":0,"canaryLive":false,...}

# Restore for next attempt
curl -X POST "http://127.0.0.1:8111/admin/weight/?25"

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
