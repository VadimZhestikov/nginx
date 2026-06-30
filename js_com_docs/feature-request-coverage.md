# Customer feature requests → js_com / pilgrim coverage

This document maps recurring customer requests for NGINX features that current
NGINX does **not** provide against what the **js_com / pilgrim** programmable
layer covers today.

> **Source & confidentiality.** The requests below were extracted from an
> internal feature-request deck and have been **anonymized**: all customer
> names and infrastructure details are deliberately omitted. Only the generic
> capability asks are retained.

The coverage assessment is an engineering mapping to existing js_com APIs and
demos — not a product commitment.

## Legend

| Mark | Meaning |
|---|---|
| ✅ | Covered — an API and/or runnable demo exists |
| 🟡 | Partial — achievable in JS today, or partially addressed |
| 🔵 | Planned — a design exists, not yet built |
| ❌ | Out of scope — a core-NGINX concern the JS layer does not address |

## Coverage at a glance

```
Coverage of 17 customer requests
  ✅ Covered        ████████████████████████  4  (24%)
  🟡 Partial        ████████████████████████████████████████████████  8  (47%)
  🔵 Planned        ██████  1  (6%)
  ❌ Out of scope   ████████████████████████  4  (24%)

  Addressable by js_com (✅+🟡+🔵):  13 / 17  (76%)
  Core-NGINX, out of scope (❌):       4 / 17  (24%)
```

## Coverage

| Feature requested (anonymized) | Coverage | How / where in js_com / pilgrim |
|---|---|---|
| **Atomic config updates** — one momentary change applied across all workers, no reload | ✅ | Live COM mutation (`addServer` / `addLocation` / `setCertificate` …) + `nginx.broadcast` fan-out applies atomically to all workers with no reload. Demos **A2.6** (runtime route broadcast), **A2.7** (snapshot / rollback); see `js-reconfig-guide-all-workers.adoc`. *Within one instance: full; cross-machine needs an external coordinator.* Note: the request was framed as conflicting with disk-based dynamic reload — pilgrim drives it from JS, not disk. |
| **HSM / re-read keys without a full reload** (via a control API) | ✅ | `server.ssl.setCertificate(cert, key)` hot-swaps cert + key live, zero connection drop, no reload. Demo **A1.1**. |
| **Exact cipher actually used** (not the configured one) | ✅ | A JS handler reads the negotiated `$ssl_cipher` (and related SSL variables) per request and can log or act on it. |
| **Subrequest logging** (JS-initiated) | ✅ | JS owns its subrequests, so it can log each with full detail (timing, target, result). |
| **JA3 / JA4 TLS fingerprinting** | 🟡 | Demo **D4.3** computes a TLS fingerprint in JS — bounded by which ClientHello fields NGINX exposes to JS (full JA4 may need additional handshake data). |
| **Better timing observability** — where are the waits / delays; clarity on `header_time` / `connect_time` / `upstream_time` | 🟡 | JS request handlers capture per-request timing breakdowns and emit custom structured logs / metrics. Demo **B5.3** (per-request metrics). Surfaces clearer values without changing NGINX's built-in variables. |
| **Exact reason for "no live upstream" / why a peer failed** | 🟡 | The COM tree exposes `nginx.http.upstreams[].peers[]` state; a JS handler can inspect peer health and log the precise reason. |
| **Exact reasons for TLS errors** | 🟡 | JS can read exposed SSL variables and log richer error context — limited by what NGINX surfaces to JS. |
| **Stream-level logs for HTTP/2 & HTTP/3** | 🟡 | JS handlers (including the stream module) can emit custom logs; coverage depends on the JS hooks available in the h2 / h3 stream path. |
| **Standardized / certified / documented JS libraries** | 🟡 | pilgrim runs full **QuickJS (ES2020+)** with `std` / `os` modules — a real, predictable JS standard library vs njs's subset. "Certified / supported" is a product / process concern, not a technical gap. |
| **Trace the actual config path a request followed** (locations + subrequests via njs / `auth_request` / `error_page`) | 🟡 | Demo **A3.3** (config dry-run, `nginx.http.match(uri)`) simulates which location matches; JS handlers can log the live path. A full cross-subrequest trace would need additional instrumentation. |
| **Dynamic config updates from disk** (re-read config at runtime — *also requested, and flagged as conflicting with the atomic-update ask above*) | 🟡 | pilgrim applies live config changes from **JS / COM**, not by re-reading files from disk; a `SharedWorker` can watch a file and push changes via `nginx.broadcast`. This also sidesteps the conflict the request itself notes — JS-driven *atomic* mutation avoids the disk-reload-vs-"one momentary change" race. |
| **Database-backed config variable storage** | 🔵 | The **db-connect** design: a SharedWorker DB gateway + per-worker cache exposing external KV / SQL values to config and handlers. Achievable on this platform; not yet built. |
| **Remove the kill switch** | ❌ | Licensing / build concern — not a JS-extensibility feature. |
| **Spread cache loader / manager across more than one process** | ❌ | NGINX core process-model change; outside the JS layer's scope. |
| **SVCB / Service Binding DNS (RFC 9460)** | ❌ | Resolver / core feature; not addressed by the JS layer. |
| **HTTP/2 & gRPC proxying performance** | ❌ | Core data-plane throughput; JS adds overhead rather than improving NGINX's proxying. |

## Summary

Of 17 distinct asks:

- **4 directly covered (✅)** — atomic reload-free config changes, live cert/key
  swap, negotiated-cipher access, subrequest logging.
- **8 partial / achievable in JS (🟡)** — mostly observability (timing, upstream
  and TLS error detail, stream logs), TLS fingerprinting, JS-library maturity,
  disk-driven dynamic config updates,
  and config-path tracing.
- **1 planned (🔵)** — DB-backed config variables (db-connect).
- **4 out of scope (❌)** — kill switch, multi-process cache loader, SVCB DNS,
  and HTTP/2 / gRPC performance — all core-NGINX, not JS-layer concerns.

The cluster pilgrim is **strongest** on is precisely the recurring enterprise
ask: **atomic, reload-free configuration changes propagated to all workers**,
plus **live certificate rotation**. The biggest gap that already has a design is
**database-backed config variables** (db-connect).

## See also

- [`../js_com_demos/README.md`](../js_com_demos/README.md) — runnable demos referenced above (A1.1, A2.6, A2.7, A3.3, B5.3, D4.3).
- [`js-dom-manual.adoc`](js-dom-manual.adoc) — main developer manual (COM tree, dynamic reconfiguration, Workers/SharedWorkers).
- [`js-reconfig-guide-all-workers.adoc`](js-reconfig-guide-all-workers.adoc) — atomic cross-worker config changes (the flagship coverage item).
