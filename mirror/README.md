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
| `onClientClose` | — (no pilgrim hook) | ❌ gap |
| L4 / TLS / LB events | — | phase 2 |

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

## Phase-1 findings (→ thread-1 nginx gaps)

Building the spine on the current pilgrim API surfaced concrete gaps to close in
phase 2 ("carefully add missing API to nginx"):

1. **No per-connection state handle / no request↔connection linkage.** The accept
   hook's `NginxConnection` (`remoteAddr`/`remotePort`/`reject`) can't carry state
   to the HTTP request handlers. Phase 1 **approximates** flow-local by keying a
   per-worker map on the client 4-tuple (`addr:port`); correct because accept +
   all of a connection's requests run in one worker, but it needs the real thing.
2. **No connection-close / log event** (`onClientClose`). Without it, flow-local
   entries can't be evicted precisely (phase 1 size-caps the map as a stopgap).
3. **No per-request LB/upstream selection** (`onSelectUpstream` / iRules
   `LB::select`) and **thin TLS ClientHello events** — the other phase-2 bindings.

The clean fix for 1 + 2 is a first-class **per-connection context + close hook**
in pilgrim; that becomes the model's true flow-local backing.

## Phase 2 (next)

- Add the pilgrim per-connection ctx + close hook; back flow-local on it.
- Wire L4 / TLS / LB events; close the LB-selection and ClientHello gaps.
- State: `table` → `nginx.shared` / SharedWorker (multi-worker) and the
  db-connect project (external); `session`/persistence → a COM persistence API.
- Then decision (A): the TCL/iRules → mirror transpiler.

> All commits for this project are prefixed `mirror:`.
