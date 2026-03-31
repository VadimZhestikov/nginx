# 01 — Architecture Overview

## What JS_Pilgrim Is

JS_Pilgrim is a programmable full-proxy layer embedded inside nginx.  Every
incoming TCP connection passes through a stack of independently programmable
JavaScript layers.  Existing nginx modules (proxy, static, FastCGI, …) remain
available as one of those layers — they are no longer the fixed center.

The underlying JavaScript engine is **QuickJS** (ES2020+, ~200 KB binary).  The
integration exposes nginx's internal state through a typed **COM** (Component
Object Model) object tree rooted at the global `nginx` object.

---

## The Full-Proxy Stack

```
Client TCP
  │
  ├─ L4 inbound filters (P6)        — async generators on raw bytes
  │
  ├─ Protocol detection / nginx routing
  │
  ├─ Access-phase hooks (P2)        — server / global scope
  │
  ├─ Pre-content hooks (P1)         — location scope, Koa-style middleware
  │
  ├─ Content handler (P7/P9)        — nginx phases + JS post-handler epilogue
  │
  ├─ Response-header hooks (P3)
  │
  ├─ Body / streaming filters (P5/P15)
  │
  ├─ Upstream req/res filters (P14)
  │
  └─ L4 send filters (P13)          — async generators on outbound bytes
  │
Backend TCP
```

Each layer is optional.  A location with no JS hooks behaves exactly like
stock nginx.  Layers compose: multiple hooks and filters stack via middleware
chains without mutual knowledge.

---

## Feature Steps (P-series)

The implementation was built in 19 incremental steps.  Each step adds one
coherent capability on top of the previous ones.

| Step | Capability |
|---|---|
| P1  | `location.addHook(fn)` — pre-content hooks, Koa middleware |
| P2  | `server.addHook()`, `nginx.http.addHook()` — access-phase hooks |
| P3  | `location.addResponseHook(fn)` — post-header hooks |
| P4  | `listener.on('accept', fn)` — TCP accept hooks |
| P5  | `location.addBodyFilter(asyncGenFn)` — async-generator body filters |
| P6  | `listener.addL4Filter(fn)` — raw inbound TCP byte filters |
| P7  | `nginx.use(path, config)` — hot-loadable plugin packages |
| P8  | Async hooks: `await` in hook handlers, full suspension |
| P9  | `next()` — post-handler epilogue; code after `await next()` runs after content |
| P10 | `req.ctx` — plain JS object that persists across hook/filter wrappers |
| P11 | `nginx.shared` — cross-worker key/value zone; `nginx.broadcast()` |
| P12 | L4 async generator streaming with true `await` |
| P13 | `listener.addL4SendFilter(fn)` — outbound (server→client) TCP filters |
| P14 | `location.addUpstreamFilter(fn)` — upstream request/response body filters |
| P15 | Multiple `addBodyFilter` on one location — composed filter chain |
| P16 | `nginx.use()` callable post-fork — live plugin hot-reload |
| P17 | Accept / L4 hooks on standard nginx `listen` sockets (not just JS sockets) |
| P18 | Registry-based package resolution — `vendor/pkg@semver` references |
| P19 | Admin shell: WebSocket + JSON-RPC 2.0, per-worker REPL, cross-worker relay |

---

## Axis of Extensibility

JS_Pilgrim exposes three independent dimensions of control:

### 1. Request-path hooks and filters
Synchronous or async functions attached to the nginx phase engine.  They can
inspect and modify the request at every stage without any native module changes.

### 2. Raw-socket programmability
JavaScript generators receive and produce raw `Buffer` objects for the inbound
and outbound TCP byte streams.  Protocol gatewaying, traffic shaping, and L4
multiplexing become pure JS code.

### 3. Cross-process shared state
`nginx.shared`, `SharedArrayBuffer` (via memfd), and `SharedWorker` pthreads
let worker processes share data without external stores.

---

## Source Modules Overview

| Module | File(s) | Responsibility |
|---|---|---|
| Master lifecycle | `ngx_js_module.c` | init_conf, fork hooks, SAB allocator, plugin table |
| HTTP COM tree | `ngx_js_com.c`, `ngx_js_com_http.c` | `nginx.*` global object, servers/locations/upstreams |
| HTTP content handler | `ngx_js_http_module.c` | NginxRequest, phase dispatch, filter engine |
| L4 sockets/listeners | `ngx_js_listener.c` | NginxHttpListener, accept hooks, L4 generator loops |
| SharedWorker | `ngx_js_sw.c` | pthreads, AF_UNIX channels, SAB relay via SCM_RIGHTS |
| JS Worker threads | `ngx_js_worker.c` | Worker thread loop, SW integration |
| Admin REPL | `ngx_js_repl.c` | `nginx.repl` object, eval, listen, binary fd writes |
| Module COM wrappers | `ngx_js_com_*.c` (×40) | Typed JS view of every nginx module's config struct |
