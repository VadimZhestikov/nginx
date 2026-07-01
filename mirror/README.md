# mirror

**Converge nginx and BIG-IP.** A project (F5) to reflect each product's strengths
into the other, along three threads:

1. **API parity** — bring BIG-IP's battle-tested TCL/iRules L4-and-upper traffic
   API to nginx (and missing nginx APIs to BIG-IP).
2. **Substrate interchange** — investigate running nginx on BIG-IP's lower layers
   (TMM), and BIG-IP on OS sockets.
3. **Unified programmability** — use **js_com / pilgrim** as the single
   programmability layer for both engines.

This directory is the working prototype for the **event/command model** — the
heart of threads 1 + 3.

## Decisions

- **Compatibility stance = hybrid.** Build **(B) now**: a clean, *iRules-inspired*
  JS event/command model on pilgrim COM — not bound to TCL. Add **(A) later**: a
  TCL/iRules → unified-JS transpiler for the existing iRules install base.
- **First layer = HTTP-first, spine-complete.** Validate the model on pilgrim's
  already-working HTTP hooks, but build the full **connection-lifecycle spine**
  (accept → request → response → close) with per-connection flow-local state —
  because the real risk is the *connection-centric (iRules) vs request-centric
  (nginx)* mismatch, not the HTTP command surface.

## What phase 1 is

A pure JS layer over pilgrim hooks (no C changes):

| Unified event | pilgrim binding | phase 1 |
|---|---|---|
| `onClientAccept` | `server.on('accept', fn)` | ✅ wired |
| `onRequestHeaders` | `location.addHook(fn)` | ✅ wired |
| `onResponseHeaders` | `location.addResponseHook(fn)` | ✅ wired |
| `onClientClose` | `conn.onClose(fn)` | ✅ wired (phase 2) |
| L4 / TLS / LB events | — | phase 3 |

- **`lib/mirror.js`** — the framework. Canonical event lattice (full stack,
  HTTP+accept wired), a **capability table** (`CAPS`) gating which commands are
  valid per event (= pilgrim's `describe()`/safety-classes, indexed by event),
  per-connection **flow-local** store, global **`table`** store, and
  `mirror.attach(server, location, handlers)`.
- **`example/`** — one iRule translated by hand as the acceptance test
  (`app.js`), plus `nginx.conf` + `test.sh`.

### Run

```bash
cd example && bash test.sh    # 6/6 pass
```

The test proves: accept→request→response **linkage**, per-connection flow-local
**persisting across keepalive requests**, per-request routing, a global `table`
counter, and the **per-event capability gate** firing.

## Phase-1 findings → phase-2 status (thread-1 nginx gaps)

Building the spine surfaced concrete pilgrim gaps. Phase 2 **closed the first
two in the C module** (the "carefully add missing API to nginx" work):

1. **Per-connection state handle / request↔connection linkage — DONE.** Added a
   first-class per-connection context in the module (`ngx_js_listener.c`): it
   lives on the nginx connection pool, persists across every keepalive request,
   and is freed at close. Exposed as **`conn.ctx`** (accept hook) and
   **`r.connCtx`** (request/response) — the same object. This *replaces* phase
   1's 4-tuple approximation; mirror flow-local is now the real thing.
2. **Connection-close event — DONE.** Added **`conn.onClose(fn)`**; it fires when
   the connection pool is destroyed, with the per-connection ctx as its argument,
   and the module frees the ctx after. This backs `onClientClose`.
3. **Per-request LB/upstream selection** (`onSelectUpstream` / iRules
   `LB::select`) and **thin TLS ClientHello events** — still open, deferred to
   phase 3.

Phase-2 acceptance test (`example/test.sh`, 9/9): the per-connection **serial is
identical across keepalive requests** (proving one shared per-connection object)
and **differs across connections**, and **`onClientClose` fires** with the
accept-time flow-local still readable.

New pilgrim API added in phase 2:
- `NginxConnection.ctx` — persistent per-connection object (accept hook).
- `NginxConnection.onClose(fn)` — connection-close callback.
- `request.connCtx` — the same per-connection object, from request/response.

## Phase 3 (next)

- Wire L4 / TLS / LB events; close the per-request LB-selection and ClientHello
  gaps (more thread-1 API in the module).
- State: `table` → `nginx.shared` / SharedWorker (multi-worker) and the
  db-connect project (external); `session`/persistence → a COM persistence API.
- Then decision (A): the TCL/iRules → mirror transpiler.

> All commits for this project are prefixed `mirror:`.
