# A2.5 — Atomics Wait/Notify Barrier

## What this demo shows

Cross-process synchronisation between an nginx **worker** (event loop thread)
and a **SharedWorker** (dedicated pthread) using `SharedArrayBuffer` +
`Atomics.wait` / `Atomics.notify`.

The worker offloads a CPU-intensive computation (Fibonacci) to the SW thread
without blocking the event loop:

1. Worker writes the input to `arr[1]`, signals via `Atomics.notify`.
2. SW thread unblocks from `Atomics.wait`, computes result, writes to `arr[2]`,
   signals back.
3. Worker polls with `nginx.setTimeout(10)` (non-blocking) until `arr[0] === 2`,
   then reads the result.

## Key insight: never call Atomics.wait in a worker

`Atomics.wait()` blocks the calling thread. In the nginx worker this would
freeze the entire event loop — all in-flight requests would stall. The correct
pattern is:

- **SharedWorker thread**: can safely call `Atomics.wait()` — it has its own
  dedicated pthread.
- **Nginx worker**: uses `Atomics.notify()` to signal the SW, then polls with
  `nginx.setTimeout()` in a non-blocking loop.

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Compute fibonacci numbers — work runs in the SharedWorker pthread
curl "http://127.0.0.1:8114/compute/?10"
# → fib(10)=55

curl "http://127.0.0.1:8114/compute/?30"
# → fib(30)=832040

# Show last result
curl http://127.0.0.1:8114/status/
# → last: fib(30)=832040

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
