# 03 — Request Lifecycle

## Overview

An HTTP request goes through four distinct phases in JS_Pilgrim:

```
1. Access phase   — P2 hooks (server / global scope)
2. Pre-content    — P1 hooks (location scope, Koa middleware)
3. Content        — nginx content handler (or inline req.respond())
4. Post-response  — P3 hooks (response headers), P5/P15 body filters
```

The JS engine drives this via Promises.  Each phase may suspend the nginx
event loop while awaiting external I/O, timers, or SharedWorker replies.

---

## NginxRequest Wrapper

`NginxRequest` is the JS object a handler or hook receives as `req`.  It is a
thin QuickJS wrapper around the C `ngx_http_request_t *`.

Key properties:
- `req.method`, `req.uri`, `req.args` — nginx strings, lazily converted to JS
- `req.headers` — Proxy object over nginx input headers
- `req.respond(status, headers, body)` — schedules the response; sets the
  `rctx->responded` flag
- `req.ctx` — per-request plain JS object (see Decision 4 in `02_design_decisions.md`)
- `req.hijack()` — takes over the connection socket for websocket / long-poll

The wrapper object is **short-lived**: a new JS object may be created for each
hook invocation to avoid lifetime issues where a stored `req` reference outlives
the nginx request pool.  `req.ctx` bridges state across those boundaries.

---

## Phase 1: Pre-Content Hooks (P1)

### Installation
```javascript
var loc = nginx.http.servers[0].locations.find(l => l.path === '/api/');
loc.addHook(async function(req, next) { ... });
```

`addHook` appends the function to `loc->hook_fns[]` (up to 8 hooks per location).
At the end of `init_conf`, or when the last hook is added via hot-reload (P16),
the hook array is compiled into a chain IIFE:

```javascript
// Synthesised chain (conceptual):
async function __chain__(req) {
    await hook0(req, async function next() {
        await hook1(req, async function next() {
            // ... content handler slot
        });
    });
}
```

### Execution (C side)
1. `ngx_js_content_handler` is set as the nginx content handler for the location.
2. On each request it calls `ngx_js_run_hook_chain(r, w)`:
   - Creates `ngx_js_async_ctx_t`, increments `r->main->count`.
   - Calls the chain function with `JS_Call`.
   - If the call returns a pending Promise, suspends: stores `async_ctx` in
     `w->async_pending`, returns `NGX_DONE`.
   - If the call resolves synchronously, calls `ngx_js_async_check` immediately.

### Suspension and Resume
When a hook calls `await somePromise`:
- QuickJS returns `JS_EXCEPTION` or a pending Promise to the C caller.
- The C caller detects "pending" and returns `NGX_DONE` from the nginx handler.
- `r->main->count` prevents nginx from finalising the request.
- When the Promise resolves (timer, IO completion, SharedWorker reply):
  - `ngx_js_async_check(w)` scans `w->async_pending`.
  - Calls `ngx_http_finalize_request(r, NGX_DONE)` to resume nginx processing.

### Short-circuit (cancellation)
A hook that calls `req.respond(200, {}, "done")` without calling `next()`:
- Sets `rctx->responded = 1` in C.
- The chain's inner `next()` function checks `responded` and does nothing.
- The outer hook's `await next()` resolves immediately to `undefined`.
- The chain finishes; the queued response is sent.

---

## Phase 2: Access-Phase Hooks (P2)

`server.addHook(fn)` and `nginx.http.addHook(fn)` install hooks at the nginx
`NGX_HTTP_ACCESS_PHASE`.  They run before content, before P1 hooks.

Semantics are identical to P1 (Koa middleware, `next()`, async suspension) but
the hook runs once per server or globally regardless of which location matched.

Access hooks are useful for:
- Authentication/authorisation (reject before reaching the content handler).
- Request logging before any processing.
- Global rate limiting or circuit-breaking.

P2 hooks use a separate `is_p2_hook` flag in `ngx_js_async_ctx_t` so the resume
path can call the correct nginx finalisation function.

---

## Phase 3: Response-Header Hooks (P3)

`location.addResponseHook(fn)` installs a handler at `NGX_HTTP_HEADER_FILTER_PHASE`.

The hook receives `req` with response header fields (`req.responseHeaders`, etc.)
already populated by the upstream or content handler.  It may modify headers
before they are sent to the client.

Response hooks do **not** support `await` in the current implementation: header
filters in nginx must be synchronous (the header chain is not suspendable).  Async
variants are a planned extension.

---

## Post-Handler Epilogue (P9)

The `next()` call in a P1/P2 hook suspends the hook until the content handler
(and all inner hooks) complete.  Code written after `await next()` therefore runs
**after** the content handler has returned but **before** the response is sent:

```javascript
loc.addHook(async function(req, next) {
    req.ctx.t0 = Date.now();
    await next();
    // Here: content handler has run, response headers are set but not sent.
    req.setResponseHeader('X-Duration', String(Date.now() - req.ctx.t0));
});
```

This is implemented by the chain synthesiser: the outermost `next()` passed to
`hook0` wraps the content handler call and resolves only after it returns.  The
epilogue code (after `await next()`) then runs as normal Promise continuation.

---

## Async Suspension in Detail

`ngx_js_async_ctx_t` (one per suspended request):

```c
typedef struct {
    ngx_http_request_t  *r;
    JSValue              req_obj;     /* DupValue — keeps wrapper alive */
    JSValue              promise;     /* outer chain Promise */
    ngx_js_async_ctx_t  *next;        /* intrusive linked list in w->async_pending */
    unsigned             is_hook:1;
    unsigned             is_p2_hook:1;
} ngx_js_async_ctx_t;
```

`w->async_pending` is a singly-linked list.  `ngx_js_async_check(w)` is called:
- After every `ngx_event_t` callback (timer, socket read/write).
- After any `channel_recv` in the SharedWorker dispatch path.
- After a JS Worker delivers a message to the main thread.

The check function iterates the list and calls `JS_ExecutePendingJob(rt, &ctx)`
until no microtasks remain.  It then inspects each `async_ctx->promise`: if
resolved, `ngx_http_finalize_request(r, NGX_DONE)` is called to resume nginx.

---

## Request Wrapper Lifetime

The `NginxRequest` JS object is allocated from the nginx request pool via a pool
cleanup callback.  Key lifetime rules:

1. The C `ngx_http_request_t` is valid for the entire request life.
2. The JS wrapper is valid only within the hook/handler call that received it.
3. `req.ctx` (a JSValue stored in `ngx_js_req_ctx_t`) is valid for the entire
   request life — it is rooted in the request pool.
4. Storing `req` (not `req.ctx`) in a closure and accessing it after the handler
   returns is undefined behaviour: the wrapper may have been freed.

---

## ngx_js_http_module.c — Central Dispatch

This 265 KB file is the heart of the JS HTTP layer.  Key functions:

| Function | Purpose |
|---|---|
| `ngx_js_content_handler` | Nginx content-phase entry point |
| `ngx_js_run_hook_chain` | Builds and calls the synthesised middleware chain |
| `ngx_js_async_check` | Drains microtasks, resumes suspended requests |
| `ngx_js_run_body_filter` | Dispatches to the appropriate filter mode (P5) |
| `ngx_js_run_chain` | Sends an `ngx_chain_t` to the downstream filter chain |
| `ngx_js_request_respond` | Implements `req.respond()` — sets headers, queues body |
| `ngx_js_req_ctx_cleanup` | Pool cleanup handler that frees `ctx_obj` JSValue |
