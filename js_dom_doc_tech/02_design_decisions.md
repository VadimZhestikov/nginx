# 02 — Major Design Decisions

Five architectural choices shape the entire implementation.  Each one closes off
a set of alternatives; this document explains what was chosen, what was rejected,
and why.

---

## Decision 1: COW Fork — One Runtime, N Workers

### What
The JS runtime (`JSRuntime` + `JSContext`) is created **once** in the master
process during `init_conf`, fully initialized by evaluating all `js_source`
scripts, and then **inherited by worker processes via copy-on-write fork**.

Workers do not create new runtimes, do not re-evaluate scripts, and do not import
modules again.  After fork each worker calls `JS_SetContextOpaque(ctx, w)` to
attach its own `ngx_js_worker_t` pointer to the shared context.

### Why
- **Zero startup cost** per worker: no JS parse/compile/link after fork.
- **Identical initial state**: every worker starts from the same evaluated
  snapshot, including all registered hooks and filters.
- **COW pages are shared until written**: read-only bytecode and string tables are
  never duplicated.

### What was rejected
- **Per-worker runtimes with serialised init**: requires re-evaluating every
  `js_source` script in every worker.  For large plugin sets this is O(N×scripts)
  cost at startup and at every reload.
- **Shared runtime with a global lock**: QuickJS is not thread-safe.  A global
  mutex would serialize all request processing.
- **V8 isolates or worker threads per request**: far too much memory and startup
  latency for short-lived HTTP requests.

### Consequences
- Any JS value created during `init_conf` is readable (COW) by all workers.
- Mutation of shared state is process-local after fork (each worker gets its own
  COW copy).  This is intentional: `nginx.shared` and `SharedWorker` are the
  sanctioned cross-worker channels.
- `exit_process` calls `JS_FreeRuntime` on the COW copy; `exit_master` frees the
  original.  No double-free because each process holds a distinct physical mapping
  after at least one write.

---

## Decision 2: Async Generator Streams for L4

### What
Raw TCP byte streams (L4 inbound and outbound) are exposed as **async
generators**.  A filter is an async generator function:

```javascript
async function* myFilter(chunks, conn) {
    for await (const chunk of chunks) {
        yield transform(chunk);
    }
}
```

`chunks` is an upstream async iterable; the filter yields transformed chunks
downstream.  Multiple filters compose naturally by wrapping iterables.

### Why
- **Composability**: generator wrapping is the standard JS composition pattern
  for streams.  No bespoke "filter chain" object is needed.
- **Back-pressure**: `yield` naturally suspends until the downstream consumer is
  ready.  No explicit flow-control API required.
- **Cancellation**: returning or throwing from the generator closes the upstream
  iterable via `return()` / `throw()` on the iterator protocol.
- **Integration with async/await**: the entire HTTP hook system also uses Promises,
  so generator-based L4 fits the same mental model.

### What was rejected
- **Node.js-style Readable/Writable streams**: complex API with many event types,
  error propagation is non-obvious, back-pressure requires manual `cork()/uncork()`.
- **Synchronous callback chains**: no natural back-pressure, callback hell for
  stateful transforms.
- **WASM-compiled WASI streams**: too much overhead, no benefit over native QuickJS
  generators.

### Consequences
- L4 filter state lives in the generator's local variables between `yield` calls.
- The nginx C event loop drives the generator by calling `next()` when new data
  arrives on the socket.
- A generator that `return`s prematurely causes the connection to be closed.

---

## Decision 3: Koa-Style Middleware for HTTP Hooks

### What
Pre-content hooks (`location.addHook`) and access hooks (`server.addHook`) use
a **Koa-style "next" middleware pattern**:

```javascript
location.addHook(async function(req, next) {
    // pre-handler code
    await next();
    // post-handler code (runs after content handler returns)
});
```

Multiple hooks on the same location/server form a chain.  Each calls `next()` to
pass control to the inner layer.  A hook that calls `req.respond(...)` without
calling `next()` short-circuits the chain.

### Why
- **Familiar**: every Node.js/Express/Koa developer knows this pattern.
- **Pre + post in one function**: the same hook can inspect headers before the
  handler and modify the response after, using plain sequential code.
- **Composability without coupling**: hooks do not know about each other.
- **Cancellation is first-class**: `req.respond()` without `next()` is a clean
  "I handled it, stop here" signal.

### What was rejected
- **Separate before/after callbacks**: `addBeforeHook` + `addAfterHook` doubles the
  API surface and makes it hard to share state between pre and post phases.
- **Express-style `(req, res, next)`**: the `res` object is redundant (nginx does
  not have a separate response object at request time); `req.respond()` subsumes it.
- **Event emitters**: too low-level, no built-in ordering or cancellation.

### Consequences
- Hooks are internally represented as an IIFE-built chain; the last `next()` calls
  into the nginx content handler.
- If a hook never calls `next()` AND never calls `req.respond()`, the request hangs
  (intentional for long-polling, admin sockets, etc.).
- The `req.responded` C flag prevents double-response if both the hook and the
  content handler try to write.

---

## Decision 4: req.ctx for Cross-Wrapper State

### What
`req.ctx` is a **plain JS object** allocated once per request and attached to the
nginx request pool.  It persists across all hook and filter invocations for that
request, including across async suspension/resume boundaries.

```javascript
location.addHook(async function(req, next) {
    req.ctx.startTime = Date.now();
    await next();
    console.log('duration', Date.now() - req.ctx.startTime);
});
```

### Why
- **No per-hook closure needed** to share state: hooks written independently can
  still communicate via `req.ctx` without knowing about each other.
- **Safe lifetime**: the `JSValue` for `ctx` is rooted in `ngx_js_req_ctx_t`,
  which is cleaned up by an nginx pool cleanup handler.  It cannot be
  garbage-collected while the request is alive.
- **Zero allocation cost for simple cases**: if no hook uses `req.ctx`, no object
  is allocated.

### What was rejected
- **WeakMap keyed on the request wrapper**: requires the request wrapper to be a
  stable identity across calls, which it is not (a new JS wrapper is allocated for
  each hook invocation to avoid lifetime issues).
- **Global map keyed on request pointer**: requires manual GC, race-prone.
- **Attaching properties directly to the request wrapper**: the request wrapper is
  short-lived and property writes would not persist across wrapper re-allocations.

### Consequences
- `req.ctx` is initialised lazily on first access (`JS_NewObject` on first read).
- The `ngx_js_req_ctx_t` struct lives in the nginx request pool; the JSValue is
  freed in a pool cleanup callback, never by GC directly.
- Post-handler epilogue (code after `await next()`) accesses the same `req.ctx`
  as the pre-handler code.

---

## Decision 5: memfd for Post-Fork SharedArrayBuffer

### What
`SharedArrayBuffer` created **after fork** (in worker processes or in SW threads)
uses `memfd_create(2)` + `MAP_SHARED` instead of `MAP_SHARED|MAP_ANONYMOUS`.  The
memfd file descriptor is passed between processes via `SCM_RIGHTS` (Unix socket
ancillary data).  The recipient mmaps the fd at a new virtual address in its own
process.

`SharedArrayBuffer` created **before fork** (during `init_conf`) uses
`MAP_SHARED|MAP_ANONYMOUS`.  These pages appear at the same virtual address in
every worker after fork, so no fd passing is needed.

### Why
- **`MAP_SHARED|MAP_ANONYMOUS` cannot be inherited across non-parent/child forks**:
  there is no fd to re-open.  Any process that was not a fork child cannot map
  the same physical pages.
- **memfd is anonymous but has an fd**: `memfd_create()` returns a file descriptor
  that keeps the backing pages alive in the kernel even if all mappings are closed.
  Sending the fd via `SCM_RIGHTS` lets any process mmap the same pages.
- **SCM_RIGHTS is AF_UNIX ancillary data**: fits naturally into the existing
  SharedWorker channel socketpairs, which are already AF_UNIX SOCK_SEQPACKET.
- **Pre-fork SABs need no fd**: they are at identical VAs in all workers because
  of fork COW.  Sending a VA integer is enough.

### What was rejected
- **POSIX shared memory (`shm_open`)**: requires a global name, name collision
  risk, manual cleanup on crash.
- **`memfd` + file-descriptor table passed at init**: limits the set of processes
  that can share to those that were alive at init time.
- **Separate IPC mechanism (pipes, sockets, message queues)**: high complexity for
  what is fundamentally a memory-mapping problem.

### Consequences
- The `ngx_js_sab_fd_table` (64 entries, mutex) tracks `{ptr, fd, size, local_refs}`
  for every memfd-backed SAB in the current process.
- `local_refs` is decremented in `ngx_js_sab_free` (called by QuickJS GC).  When
  it reaches 0, the mmap and fd are closed.
- Pre-fork SABs use an atomic `ref_count` field in the mmap header instead of the
  fd table, because all processes share the same physical pages and the counter
  must be visible to all of them.
- The serialisation format for messages containing SABs embeds the sender's virtual
  address; `channel_recv` patches it to the receiver's VA after mmap.
