# E2 — DDoS: hot-patch limit_req while under attack

## Scenario

Your public API is under a DDoS attack. Traffic is flooding in faster than the
current `limit_req` zone allows, and the burst queue is absorbing too many
requests before the rate limit kicks in. You need to tighten the throttle
immediately — drop the rate, shrink the burst, enable nodelay so excess requests
are rejected instantly instead of queued — without touching nginx.conf or
issuing a reload (which would briefly suspend rate-limiting enforcement).

## What this demo shows

`location.limitReq.limits[i]` gives live read-write access to the `limit_req`
parameters configured for that location. Changes take effect for the next
request processed by the worker — no reload, no double-process window, no gap
in enforcement.

## Key API

```javascript
var lr  = location.limitReq;      // NginxLimitReq for the location
var lim = lr.limits[0];           // first limit_req zone entry

// Tighten under DDoS
lim.rate    = 2;     // r/s (was 20)  — writes under shm zone mutex
lim.burst   = 2;     // (was 10)
lim.nodelay = true;  // reject excess immediately, don't queue
lr.dryRun   = false; // switch from observation to enforcement

// Restore after attack subsides
lim.rate    = 20;
lim.burst   = 10;
lim.nodelay = false;
```

## Architecture

```
port 8202 (public API, Host: api.example.com)     ← protected by limit_req
port 8203 (ops plane, Host: ops.internal)          ← no rate limit
```

The ops plane is on a separate port so the control endpoints themselves are
never subject to the very rate limit they manage.

## limit_req parameters exposed

| Property | Type | Notes |
|----------|------|-------|
| `lim.zone` | string | Zone name (read-only) |
| `lim.rate` | number | Requests/second; stored ×1000 internally, written under shm mutex |
| `lim.burst` | number | Max queue depth |
| `lim.nodelay` | boolean | `true` = reject excess immediately |
| `lim.delay` | number | Threshold at which delay starts (0 when nodelay) |
| `lr.dryRun` | boolean | Observe only vs. enforce |
| `lr.logLevel` | string | `"info"` / `"notice"` / `"warn"` / `"error"` |
| `lr.statusCode` | number | HTTP status returned on rejection (default 503) |

## Control-plane API

| Method | Path | Action |
|--------|------|--------|
| `POST` | `/ddos/tighten` | Drop rate to 2 r/s, burst 2, nodelay on |
| `POST` | `/ddos/relax` | Restore normal parameters (20 r/s, burst 10) |
| `GET`  | `/ddos/status` | Print current parameters as JSON |

## How to run

```bash
bash test.sh
```

## Manual walkthrough

```bash
# Start nginx
../../objs/nginx -p . -c nginx.conf

# Normal mode: rate=20, burst=10
curl http://127.0.0.1:8203/ddos/status
# → { "rate": 20, "burst": 10, "nodelay": false, ... }

# API is responsive
curl http://127.0.0.1:8202/api
# → api-ok

# --- DDoS attack starts ---
curl -X POST http://127.0.0.1:8203/ddos/tighten
# → mitigation activated

# Parameters changed instantly in the live process
curl http://127.0.0.1:8203/ddos/status
# → { "rate": 2, "burst": 2, "nodelay": true, "dryRun": false, ... }

# --- Attack subsides, restore normal operation ---
curl -X POST http://127.0.0.1:8203/ddos/relax
# → normal mode restored

curl http://127.0.0.1:8203/ddos/status
# → { "rate": 20, "burst": 10, "nodelay": false, ... }

# Stop nginx
../../objs/nginx -p . -c nginx.conf -s stop
```
