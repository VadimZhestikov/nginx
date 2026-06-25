# A2.9 — Local worker pool + Atomics barrier + cross-worker aggregate

## What this demo shows

The **positive counterpart to A2.5**: where `Atomics.wait`/`notify` genuinely
belongs, and how to combine all four shared-concurrency primitives correctly in
one request path.

Each nginx worker process runs a **pool of local JS `Worker`s** that share a
`SharedArrayBuffer`. A request fans a parallel computation across the pool; the
workers synchronise with a real **`Atomics` barrier**; then the pool's worker 0
folds the result into a **global aggregate held in a `SharedWorker`** (shared
across all nginx worker processes).

```
                nginx worker process (P)                        master (M)
  ┌──────────────────────────────────────────────┐        ┌──────────────────┐
  │ event loop ──postMessage(job)──▶ Worker 0 ─┐  │        │  SharedWorker     │
  │                                  Worker 1  │  │        │  globalTotal,     │
  │   SAB (partials + barrier) ◀────  Worker 2  │  │        │  globalCount      │
  │                                  Worker 3 ◀┘  │        └──────────────────┘
  │   Atomics barrier (intra-process)            │             ▲
  │   Worker 0 ──────── channel (postMessage) ───┼─────────────┘
  └──────────────────────────────────────────────┘   (one channel slot per nginx worker)
```

`GET /task/?n=N` computes `sum(0 .. N-1)` in parallel:

1. The event loop `postMessage`s the job to all pool workers.
2. Each worker sums its slice into the SAB, then meets the others at a
   sense-reversing **`Atomics` barrier**.
3. After the barrier, worker 0 reduces the partials → local sum, sends it to the
   **SharedWorker** over the channel, and reports `{local, global, count}` back.
4. The event loop responds.

`GET /global/` reads the SharedWorker aggregate (routed through worker 0).

## The rule this demo embodies

| Primitive | Role | Scope |
|---|---|---|
| `Atomics.wait`/`notify` | the pool barrier | **same process only** |
| channel (`postMessage`) | job dispatch + worker↔SharedWorker | crosses process boundary |
| `SharedArrayBuffer` | the shared data plane | either |
| `SharedWorker` | global state across nginx workers | lives in the master |

**`Atomics.wait`/`notify` works here because the pool is intra-process.** All
pool workers are pthreads inside the *same* nginx worker process, so an
`Atomics.notify` reliably wakes a parked worker — the case A2.5 never had (its
SharedWorker lived in the master, so cross-process notify was inert and it
degenerated to a starvable busy-poll). Validated: 30/30 cold starts with **0
stalls** under heavy CPU oversubscription.

Two design constraints the demo respects:

- **Dispatch over the channel, not Atomics.** Workers are woken for a job by
  `postMessage` and *return* after each job, so they stay parked in their channel
  loop and remain terminable. A worker stuck in an infinite `Atomics.wait` could
  not be `pthread_join`ed on shutdown — it would hang `nginx -s stop`.
- **One SharedWorker connection per nginx worker.** A static SharedWorker has
  exactly one channel slot per nginx worker process (`nchannels =
  worker_processes`). So within a process only one thread may hold the link —
  here worker 0. The event loop never talks to the SharedWorker directly; reads
  (`/global/`) are routed through worker 0.

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx (2 worker processes -> the SharedWorker aggregates across both)
../../../objs/nginx -p . -c nginx.conf

# Parallel sum of 0..N-1, folded into the global total
curl "http://127.0.0.1:8234/task/?100"
# → {"n":100,"local_sum":4950,"global_total":4950,"global_count":1}

curl "http://127.0.0.1:8234/task/?1000"
# → {"n":1000,"local_sum":499500,"global_total":504450,"global_count":2}

# Read the cross-worker aggregate
curl "http://127.0.0.1:8234/global/"
# → {"globalTotal":504450,"globalCount":2}

# Stop nginx (pool workers + SharedWorker are event-driven, so it stops cleanly)
../../../objs/nginx -p . -c nginx.conf -s stop
```
