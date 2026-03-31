# JS_Pilgrim / JS DOM — Technical Documentation

This directory contains the internal technical documentation for the JS_Pilgrim
programmable-proxy layer built on top of the JS COM foundation.

Documents are ordered from the most general (design philosophy, big decisions) to
the most concrete (data-structure layouts, wire formats).  Read top-down for a
first pass; jump directly to a later section when debugging a specific subsystem.

---

## Document Index

| File | What it covers |
|---|---|
| [01_architecture.md](01_architecture.md) | What JS_Pilgrim is, the full-proxy model, the seven-layer stack |
| [02_design_decisions.md](02_design_decisions.md) | The five major architectural choices and their rationale |
| [03_request_lifecycle.md](03_request_lifecycle.md) | Request flow, phase hooks, async suspension / resume |
| [04_filter_pipeline.md](04_filter_pipeline.md) | Body and streaming filter dispatch (P5 / P15) |
| [05_com_layer.md](05_com_layer.md) | The nginx COM object tree — nginx.http, servers, locations |
| [06_workers_concurrency.md](06_workers_concurrency.md) | Fork model, per-worker runtimes, SharedWorker threading |
| [07_sab_shared_memory.md](07_sab_shared_memory.md) | SharedArrayBuffer, memfd, cross-process page sharing |
| [08_l4_pipeline.md](08_l4_pipeline.md) | Raw-TCP L4 filters, accept hooks, send filters (P4/P6/P13) |
| [09_plugin_system.md](09_plugin_system.md) | nginx.use / install / registry — hot-loadable plugins (P7/P16/P18) |
| [10_admin_shell.md](10_admin_shell.md) | Admin UI, REPL, WebSocket / JSON-RPC 2.0 (P19) |
| [11_data_structures.md](11_data_structures.md) | Key C struct layouts, field meanings, ownership rules |
| [12_protocols.md](12_protocols.md) | Wire formats: channel, broadcast, WebSocket, JSON-RPC |

---

## Codebase at a Glance

```
nginx/src/js/
  ngx_js_module.c          — master lifecycle, init_conf, fork hooks, SAB allocator
  ngx_js_com.c/h           — root nginx.* object (version, log, setTimeout, shared …)
  ngx_js_com_http.c        — nginx.http subtree, hook/filter installation
  ngx_js_http_module.c     — HTTP content handler, NginxRequest wrapper, async engine
  ngx_js_listener.c/h      — NginxHttpListener, L4 filter loops (P4/P6/P13)
  ngx_js_sw.c/h            — SharedWorker manager (pthreads, channels, SAB relay)
  ngx_js_worker.c/h        — JS Worker threads
  ngx_js_repl.c/h          — nginx.repl (eval, listen, raw fd write)
  ngx_js_com_upstream.c    — nginx.http.upstreams[], hot-reload (P14)
  ngx_js_com_*.c (×40)     — COM wrappers for every nginx module
  ngx_js.h                 — master header: all shared types and constants

nginx/t/
  js_pilgrim_p1_hooks.t … js_pilgrim_p19_admin_shell.t   — one test file per P-step

nginx/js_pilgrim_docs/
  README.adoc, p1-*.adoc … p19-*.adoc                    — user-facing step docs

nginx/js_pilgrim_apps/
  admin-shell/             — reference WebSocket admin shell app
  admin_UI_demo_v2/        — full-featured demo with per-worker REPL tabs
  zero-trust-gateway/      — JWT auth + HTML injection demo
  body-json-based-routing/ — streaming JSON routing demo
```

Total C source: ~47 000 lines across 56 files.
