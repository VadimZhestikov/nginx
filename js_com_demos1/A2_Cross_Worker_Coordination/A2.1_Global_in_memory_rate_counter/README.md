# A2.1 — Global In-Memory Rate Counter

## What this demo shows

A `SharedArrayBuffer` allocated before `fork()` has the **same virtual address**
in every worker process. `Atomics.add()` increments the counter atomically
without a mutex or external store. With `worker_processes 2`, both workers share
the same physical memory page — `/count/` always reflects the true aggregate total.

## Classic nginx approach

Classic nginx has no shared in-process memory for JS logic. Counters must live
in an external store (Redis, memcached) at the cost of network round-trips on
every request. The `lua-nginx-module` offers `ngx.shared.DICT` (C-level shared
memory with spinlocks) but it is not composable with JavaScript logic.

## Key pattern

```javascript
// Config phase (before fork) — same VA in all workers
var sab = new SharedArrayBuffer(8);
var arr = new Int32Array(sab);

loc.handler = function(r) {
    var n = Atomics.add(arr, 0, 1) + 1;   // atomic increment, returns old value
    r.respond(200, {}, 'hit #' + n + '\n');
};
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx with 2 workers
../../../objs/nginx -p . -c nginx.conf

# Initial count
curl http://127.0.0.1:8108/count/
# → total=0 last-worker=-1

# Send 10 requests — they may land on either worker
for i in $(seq 10); do curl -s http://127.0.0.1:8108/hit/; done

# All 10 counted, regardless of which worker handled each request
curl http://127.0.0.1:8108/count/
# → total=10 last-worker=0  (or 1)

# Reset
curl -X POST http://127.0.0.1:8108/reset/
curl http://127.0.0.1:8108/count/
# → total=0 last-worker=-1

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
