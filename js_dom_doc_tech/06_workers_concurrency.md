# 06 — Workers, Concurrency, and SharedWorkers

## nginx Process Model

nginx runs as:
```
master process
  ├── worker process 0  (handles requests via epoll)
  ├── worker process 1
  ├── …
  └── worker process N-1
```

Each worker is a separate Unix process.  There is no shared heap.  Workers
communicate only through:
- `nginx.shared` (fixed SHM zone)
- `nginx.broadcast()` (per-worker AF_UNIX socketpairs)
- `SharedWorker` pthreads (live in master, accessible from all workers)
- `SharedArrayBuffer` (via memfd, fd passed via SCM_RIGHTS)

---

## Per-Worker JS Runtime State

After fork, each worker gets the COW-inherited `JSRuntime *` and `JSContext *`
from master and attaches its own `ngx_js_worker_t`:

```c
typedef struct {
    JSRuntime              *rt;
    JSContext              *ctx;

    /* Async suspension queues */
    ngx_js_async_ctx_t    *async_pending;   /* suspended content handlers */
    ngx_js_bf_pending_t   *bf_pending;      /* async whole-body filters */
    ngx_js_sf_pending_t   *sf_pending;      /* async streaming filters */
    ngx_js_l4_pending_t   *l4_pending;      /* P12 async L4 connections */

    /* Current execution context */
    ngx_http_request_t    *current_request;

    /* SharedWorker list (dynamic SWs created post-fork) */
    ngx_js_sw_state_t     *local_sw_list;
} ngx_js_worker_t;
```

`JS_SetContextOpaque(ctx, w)` is called in `init_process` so every C function
that receives a `JSContext *` can retrieve its worker via `JS_GetContextOpaque`.

---

## JS Worker Threads (ngx_js_worker.c)

`new Worker('path/to/script.js')` from inside a request handler spawns a POSIX
thread inside the current worker process.  The thread:
1. Creates its own `JSRuntime` + `JSContext` (NOT the COW-inherited one).
2. Evaluates the specified script.
3. Runs a `poll()` message loop, processing messages from the main thread and
   from any SharedWorkers it connects to.

Communication between the main thread and the Worker thread uses an in-process
pipe pair (`to_worker.wfd` / `from_worker.rfd`).  Messages are serialised with
`JS_WriteObject`.

Worker threads also support `new SharedWorker(url)` — see below.

### Worker Thread Cleanup (GC Cycle)

A Worker thread's `onmessage` callback may capture the `SharedWorker` JS object
in its closure, forming a reference cycle:

```
sw (JS object) → sw.onmessage (callback) → upvalue → sw
```

The `ngx_js_wt_sw_t` C struct holds `on_message` (the callback), preventing
QuickJS's GC from seeing the cycle as unreachable.  At Worker thread exit:

1. For each SW entry: free `on_message`, set to `JS_UNDEFINED`, then free `js_obj`.
2. Call `JS_RunGC(rt)` to collect any surviving cycles.
3. Then `JS_FreeContext` / `JS_FreeRuntime`.

Without step 2, `JS_FreeRuntime` fires its `list_empty(&rt->gc_obj_list)` assert.

---

## SharedWorker — Design

`new SharedWorker(url)` creates a long-lived POSIX thread in the **master
process**.  The thread runs independently of the nginx event loop.  It is
accessible from any worker process and from any JS Worker thread.

### Why master process?
- A pthread in a worker process would die with the worker on SIGQUIT/crash.
- The master process is stable: it never handles requests and is only killed
  intentionally.
- All workers share the same master PID, so one SharedWorker instance is
  visible to all.

### Static vs Dynamic SharedWorkers
- **Static** (`new SharedWorker(url)` during `init_conf`): created before fork,
  channel array pre-sized to `worker_processes`.  Channels are COW-inherited by
  workers.
- **Dynamic** (`new SharedWorker(url)` from a request handler): created after
  fork via the manager thread.  Only the requesting worker gets a channel.
  See `ngx_js_sw_request_dynamic`.

---

## SharedWorker — Channel Architecture

Each SharedWorker has one AF_UNIX `SOCK_SEQPACKET` socketpair per worker:

```
[ SW thread ]  ←——— sw_fd ———→ [ channel ]  ←——— worker_fd ———→ [ worker epoll ]
```

`SOCK_SEQPACKET` provides:
- Message framing (no partial reads).
- Bidirectional (both sides can send and receive).
- Delivery guarantees within a process (no dropped messages).

In addition to the data socket, each worker has a **wake pipe** write end
(`wake_pipe[1]`) pointing at the SW thread's `wake_pipe[0]`.  After sending a
message on `worker_fd`, the worker writes one byte (the channel index) to the
wake pipe.  The SW thread polls `wake_pipe[0]` and wakes immediately, avoiding
the POLLIN unreliability on WSL2 with SOCK_SEQPACKET.

### Channel Message Format (header)
```
[type : uint32_t] [data_len : uint32_t] [n_sabs : uint32_t] [n_memfds : uint32_t]
```

Followed by:
- `data_len` bytes of QuickJS-serialised payload (`JS_WriteObject` format).
- `n_sabs × sizeof(uint64_t)`: VAs of pre-fork SharedArrayBuffers.
- `n_memfds × sizeof(ngx_js_sw_memfd_info_t)`: sender VA + size for memfd SABs.
- SCM_RIGHTS ancillary data carrying memfd file descriptors.

### Message Types
| Type | Direction | Meaning |
|---|---|---|
| `NGX_JS_SW_MSG_CONNECT` | Worker → SW | First message when a worker connects to the SW |
| `NGX_JS_SW_MSG_DATA` | Both | Normal `postMessage` payload |
| `NGX_JS_SW_MSG_TERM` | Worker → SW | Worker is shutting down |

---

## SharedWorker — Manager Thread

A single manager thread (`ngx_js_sw_manager_thread`) runs in master and handles
dynamic SW creation requests from workers:

```
Worker                     Manager
  │                           │
  │── [URL + worker_idx] ──→  │  (via sw_cmd_fds socketpair)
  │                           │  creates SW thread if new URL
  │                           │  allocates channel in SW
  │  ←── [worker_fd] ────────  │  (SCM_RIGHTS reply)
  │  ←── [wake_pipe[1]] ─────  │
```

The manager sends two file descriptors back via SCM_RIGHTS:
1. `worker_fd` — the worker's end of the AF_UNIX channel.
2. `wake_pipe[1]` — the write end of the SW's wake pipe.

The reload guard in `ngx_js_sw_manager_start`:
```c
if (sw_mgr_deferred || sw_mgr_started) {
    return NGX_OK;
}
```
prevents re-creating static variables on SIGHUP reload, which would orphan the
live manager thread and cause `pthread_join` deadlock at shutdown.

---

## Atomics.wait / Atomics.notify

The SW thread calls `JS_SetCanBlock(rt, TRUE)` so `Atomics.wait()` is permitted.
Workers call only `Atomics.notify()` (non-blocking, safe on the event loop).

For pre-fork SABs (same VA in master and workers), the Linux futex resolves to
the same physical page via the `FUTEX_WAIT` / `FUTEX_WAKE` syscalls.

For post-fork memfd SABs (different VAs, same physical pages), the futex uses
shared mode (without `FUTEX_PRIVATE_FLAG`), which keys on physical page identity.
Both VAs map to the same page → futex correctly wakes the SW.

---

## exit_process / exit_master Safety

- `exit_process` (called in each worker) frees the worker's COW-modified copy
  of the runtime.
- `exit_master` (called in the master) frees the original.
- **No double-free**: after any write-triggered COW split, master and worker hold
  distinct physical pages; freeing either is safe.
- Worker slots (`worker_slots[wi].on_message`) are explicitly freed before
  `JS_FreeContext` in `exit_process` to prevent the JSValue from appearing in
  the GC list at `JS_FreeRuntime` time.
- SW threads are joined in `exit_master` after sending a TERM message on each
  channel and writing the channel index byte to the wake pipe.
