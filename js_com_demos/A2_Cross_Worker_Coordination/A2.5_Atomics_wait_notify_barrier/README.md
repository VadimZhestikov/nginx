# A2.5 — SharedWorker compute offload (channel signal + SAB data plane)

## What this demo shows

How an nginx **worker** (event-loop thread) offloads a CPU-intensive
computation (Fibonacci) to a **SharedWorker** (dedicated pthread in the master)
**without blocking the event loop** — using each shared primitive for what it
is actually good at:

- **`SharedArrayBuffer` — the data plane.** Input and result live in a pre-fork
  SAB (the same physical page in every process), so the payload is shared by
  reference, never copied across the process boundary.
- **The SharedWorker channel (`postMessage`/`onmessage`) — the signal plane.**
  The channel is an AF_UNIX socket the nginx event loop already watches via
  epoll, so the SW's reply wakes the worker immediately and reliably.

Flow of one `/compute/?n` request:

1. Worker writes `n` into `arr[0]` and `postMessage(sab)` to the SW.
2. SW reads `arr[0]`, computes, writes the result to `arr[1]`, and replies
   `'done'` on the channel.
3. The worker's `onmessage` fires (epoll-driven, no polling) and reads `arr[1]`.

## Key insight: signal over the channel, not Atomics, across processes

`Atomics.wait()` blocks the calling thread, so it must **never** run in the
nginx event-loop thread — it would freeze every in-flight request. A dedicated
thread (the SW) *can* call it. But for **cross-process** signalling it is the
wrong tool entirely:

- The SharedWorker runs in the **master**; nginx workers are **forked
  children**. QuickJS implements `Atomics.wait`/`notify` with a **per-process**
  waiter list (a pthread mutex + condition variables), **not** OS futexes. So a
  worker's `Atomics.notify` can never wake the SW thread parked in the master —
  **cross-process `notify` is inert.**
- An earlier version of this demo papered over that by having the SW
  **busy-poll** a SAB flag on a 10 ms `Atomics.wait` timeout. That made `notify`
  decorative and, worse, let the SW thread be **CPU-starved for seconds** under
  load (the scheduler had no signal that work was waiting), so first requests
  occasionally stalled multiple seconds and were rescued only by retries.

The fix is structural: **use the channel as the doorbell.** Delivering a message
makes the SW thread *runnable* — which is exactly the case the scheduler
prioritises — so there is no busy-poll, no retry, and no startup stall. The SAB
stays for the *data* (and shines when that data is large); the channel carries
the *signal*. `Atomics.load`/`store` are still used for well-defined access to
the shared slots — but there is no `Atomics.wait`/`notify` anywhere.

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

# Stop nginx (the SW is event-driven, so it shuts down cleanly)
../../../objs/nginx -p . -c nginx.conf -s stop
```
