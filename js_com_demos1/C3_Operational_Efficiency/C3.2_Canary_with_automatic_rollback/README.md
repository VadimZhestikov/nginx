# C3.2 — Canary Deployment with Automatic Rollback

## What it shows

A SharedWorker monitors 5xx error rates across all worker processes using a
sliding window of the last 20 responses.  When the canary backend error rate
exceeds 40%, the SharedWorker automatically switches mode to `'rollback'` and
all subsequent requests are served exclusively by the stable backend — with
no human intervention and no NGINX reload.

Architecture:
- `/internal/stable/` — always 200 (simulated healthy backend)
- `/internal/canary/` — always 500 (simulated buggy canary)
- `/canary/` — routes 50/50 in canary mode, reports status back to SW
- SharedWorker (`sw.js`) — tracks error rates, triggers rollback

## Why classic NGINX cannot do this

Classic NGINX upstream health checks (`max_fails`, `fail_timeout`) operate at
the TCP/HTTP connection level — they detect failed connections, not semantic
5xx errors from application logic.  They also react to the same fixed thresholds
for all upstreams.  The JS SharedWorker approach gives you full control: custom
error classification, sliding window logic, configurable per-route thresholds,
and instant rollback without waiting for a health-check interval.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8167; internal backends + admin endpoints |
| `handler.js` | Routing logic; records status to SW |
| `sw.js` | SharedWorker: sliding window monitor, triggers rollback |
| `test.sh` | Automated test: enable canary → generate errors → verify rollback |

## How to run

```bash
cd C3.2_Canary_with_automatic_rollback
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Check initial status
curl http://localhost:8167/admin/canary-status/

# Enable canary (50% traffic to buggy backend)
curl http://localhost:8167/admin/enable-canary/

# Send requests — watch rollback happen automatically
for i in $(seq 1 20); do curl -s http://localhost:8167/canary/; done

# Rollback should be active now
curl http://localhost:8167/admin/canary-status/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Start in `stable` mode — all requests go to stable backend.
2. Enable canary with `/admin/enable-canary/`.
3. Send 20 requests — roughly half hit the buggy canary (always 500).
4. After ~9 errors in the window (>40%), SW sets mode to `rollback`.
5. All subsequent requests go to stable even without any admin action.
6. Error rate and window state visible at `/admin/canary-status/`.
