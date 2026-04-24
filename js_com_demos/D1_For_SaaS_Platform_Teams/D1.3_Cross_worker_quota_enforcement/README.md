# D1.3 — Cross-Worker Quota Enforcement

## What this demo shows

A lock-free token-bucket rate limiter shared across all nginx worker processes
via a `SharedArrayBuffer`. Every `/api/` request atomically decrements the
token counter using `Atomics.add()`; when the counter reaches zero all further
requests receive `429 Too Many Requests` — regardless of which worker handles
them. `GET /admin/refill/` resets the bucket to its configured maximum.

Key mechanics:
- Pre-fork SAB allocation means every worker shares the same physical memory
- `Atomics.add(arr, 0, -1)` returns the OLD value — if `> 0` the token was
  consumed; if `<= 0` the decrement is reversed and the request is rejected
- No mutex, no spin-lock, no external Redis/memcached dependency
- `worker_processes 2` shows that the quota is global, not per-worker

## Why it is powerful

Traditional nginx rate limiting (`limit_req_zone`) is per-worker by default —
a 10 req/s limit with 4 workers effectively allows 40 req/s. Synchronizing
across workers requires `zone` directives backed by shared memory, which work
only for simple fixed-rate limiting. With a SAB + Atomics the entire quota
logic is fully programmable: token buckets, sliding windows, tenant-specific
limits, burst allowances — all in plain JavaScript, all truly cross-worker.

## Classic nginx approach

```nginx
# Per-worker limit (not truly global without extra modules)
limit_req_zone $binary_remote_addr zone=api:10m rate=10r/s;
server {
    location /api/ { limit_req zone=api burst=5; }
}
```

## Key API

```javascript
// Lock-free token consume
var oldVal = Atomics.add(arr, 0, -1);
if (oldVal <= 0) {
    Atomics.add(arr, 0, 1);   // restore — prevent negative drift
    r.respond(429, {}, 'Quota exhausted\n');
    return;
}

// Refill
Atomics.store(arr, 0, QUOTA_MAX);
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx (2 workers sharing the SAB)
../../../objs/nginx -p . -c nginx.conf

# Check initial state
curl http://127.0.0.1:8179/status/
# → {"quota_max":10,"tokens_remaining":10,...}

# Burn through all 10 tokens
for i in $(seq 1 10); do curl -s http://127.0.0.1:8179/api/; done
# → OK — request #1, tokens remaining: 9
# → OK — request #2, tokens remaining: 8
# ...
# → OK — request #10, tokens remaining: 0

# 11th request is rejected
curl -i http://127.0.0.1:8179/api/
# → HTTP/1.1 429 Too Many Requests

# Refill and try again
curl http://127.0.0.1:8179/admin/refill/
curl http://127.0.0.1:8179/api/
# → OK — request #11, tokens remaining: 9

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
