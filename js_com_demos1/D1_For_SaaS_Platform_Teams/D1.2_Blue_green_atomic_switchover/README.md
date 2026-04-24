# D1.2 — Blue/Green Atomic Switchover

## What this demo shows

All worker processes share a single `SharedArrayBuffer` routing flag that
controls which deployment slot (`blue` or `green`) is active. A `POST
/admin/switch/` atomically flips the flag with `Atomics.compareExchange()` —
the change is visible to every worker within microseconds, with no reload and
no in-flight request disruption.

Key mechanics:
- Pre-fork SAB allocation ensures every worker maps the same physical memory
  page — no IPC required for reads
- `Atomics.compareExchange()` is lock-free and race-free under concurrent
  switch calls
- `/app/` reads the SAB flag and subrequests to `/internal/blue/` or
  `/internal/green/` accordingly
- In production, `nginx.broadcast(fn)` can be used to run per-worker
  side-effects (cache invalidation, metric resets) on switchover

## Why it is powerful

Traditional blue/green deployment in nginx requires editing `upstream {}` or
`proxy_pass` and issuing `nginx -s reload`. That creates a several-hundred
millisecond window during config validation and worker rotation. With a SAB
flag the switchover is a single atomic CPU instruction — zero config file I/O,
zero worker restart, zero request disruption.

## Classic nginx approach

```nginx
# nginx.conf — requires reload to switch
upstream app {
    server blue-backend:8001;  # comment this out to switch to green
    # server green-backend:8002;
}
```

```bash
# Switch to green: edit conf, then:
nginx -s reload
```

## Key API

```javascript
// Pre-fork SAB: same page in every worker
var sab = new SharedArrayBuffer(4);
var arr = new Int32Array(sab);

// Atomic switch: lock-free, no race conditions
var current = Atomics.load(arr, 0);
Atomics.compareExchange(arr, 0, current, current === 0 ? 1 : 0);

// Read from any worker
var active = Atomics.load(arr, 0);  // 0=blue, 1=green
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx (2 workers, both share the SAB)
../../../objs/nginx -p . -c nginx.conf

# Initial state: blue active
curl http://127.0.0.1:8176/app/
# → blue deployment v1.0

curl http://127.0.0.1:8176/status/
# → {"active":"blue","version":"v1.0"}

# Atomic switch to green — no reload, instant
curl -X POST http://127.0.0.1:8176/admin/switch/
# → {"switched":true,"active":"green"}

curl http://127.0.0.1:8176/app/
# → green deployment v2.0

# Switch back to blue
curl -X POST http://127.0.0.1:8176/admin/switch/
curl http://127.0.0.1:8176/app/
# → blue deployment v1.0

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
