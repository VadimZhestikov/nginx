# mirror — iRules → mirror capability matrix

The scorecard for **thread 1** (API parity): how BIG-IP iRules constructs map onto
the mirror event/command model on pilgrim. "Live" = works end-to-end in the
`example/` acceptance test; "transpiled" = `mirror.transpile` emits it.

## Architecture, in one breath

```
   iRules TCL ──(mirror.transpile)──▶ mirror handlers (JS)
                                          │  mirror.attach / applyRule
                                          ▼
   pilgrim COM + hooks  (server.on, location.addHook/addResponseHook,
     server.ssl.onClientHello, upstream.onSelectPeer, stream handler, r.fetch)
                                          │
                                          ▼
             nginx  (workers, epoll, upstreams, shared memory)
```

Three layers: a clean **iRules-inspired JS event/command model** (`lib/mirror.js`),
a **TCL→JS transpiler** for the existing install base (`lib/transpile.js`), both
riding pilgrim's COM. Per-event **capability gating** (`CAPS`) makes "command X is
only valid in event Y" a machine-checked contract.

## Events (iRules `when` → mirror)

| iRules event | mirror event | pilgrim binding | status |
|---|---|---|---|
| `CLIENT_ACCEPTED` | `onClientAccept` | `server.on('accept', fn)` | ✅ live |
| `CLIENT_DATA` | `onClientData` | `mirror.attachStream` + `session.data` (stream preread) | ✅ live |
| `CLIENT_CLOSED` | `onClientClose` | `conn.onClose(fn)` | ✅ live |
| `CLIENTSSL_CLIENTHELLO` | `onClientHello` | `server.ssl.onClientHello(fn)` | ✅ live |
| `HTTP_REQUEST` | `onRequestHeaders` | `location.addHook(fn)` (access phase) | ✅ live |
| `HTTP_RESPONSE` | `onResponseHeaders` | `location.addResponseHook(fn)` | ✅ live |
| `LB::select` | `upstream.onSelectPeer(fn)` | custom balancer wrapping round-robin | ✅ live |
| `SERVER_CONNECTED`, `HTTP_REQUEST_DATA`, `HTTP_RESPONSE_DATA` | — | — | ⬜ not yet |

## Commands

| iRules | mirror | status |
|---|---|---|
| `IP::client_addr` / `IP::remote_addr` | `ev.clientAddr` | ✅ live + transpiled |
| `IP::remote_port` / `TCP::client_port` | `ev.clientPort` | ✅ transpiled |
| `HTTP::header X` (read) | `ev.header('X')` | ✅ live + transpiled |
| `HTTP::header insert/replace X V` | `ev.setResponseHeader('X', V)` | ✅ live + transpiled |
| `HTTP::uri` / `HTTP::path` | `ev.uri` | ✅ transpiled |
| `HTTP::method` | `ev.method` | ✅ transpiled |
| `HTTP::host` | `ev.header('host')` | ✅ transpiled |
| `HTTP::cookie X` | `ev.cookie('X')` | ✅ live + transpiled |
| `HTTP::respond CODE content BODY` | `ev.respond(CODE, {}, BODY)` | ✅ live + transpiled |
| `HTTP::redirect URL` | `ev.redirect(URL)` | ✅ transpiled |
| `pool NAME` | `ev.selectUpstream('NAME')` | ✅ live + transpiled |
| `persist source_addr / uie / cookie` | `mirror.persist(upstream, {key, ttl, via})` | ✅ live |
| `class match SUBJ OP GRP` | `mirror.classMatch('GRP','OP',SUBJ)` | ✅ live + transpiled |
| `class lookup KEY GRP` | `mirror.classLookup('GRP',KEY)` | ✅ live + transpiled |
| `table set/get/incr/delete [timeout]` | `ev.table.*` (cross-worker + TTL) | ✅ live + transpiled |
| `log FACILITY MSG` | `nginx.log(5, MSG)` | ✅ transpiled |
| `reject` / `TCP::close` | `ev.reject()` | ✅ transpiled |
| `set` / `incr` / `unset` (vars) | `ev.flow.*` (connection-scoped flow-local) | ✅ live + transpiled |
| `string tolower/toupper/length/trim*/range`, `substr` | JS `String` ops | ✅ transpiled |
| `expr {… eq/ne/equals/&&/‖/! ?:}` | JS expression | ✅ transpiled |
| `expr {X starts_with/ends_with/contains Y}` | `String(X).startsWith/endsWith/includes(Y)` | ✅ transpiled |
| `if/elseif/else`, `switch`, `foreach {literal}` | JS control flow (recursive) | ✅ transpiled |
| `return` | `return;` | ✅ transpiled |
| external session state | `mirror.kv(r, url)` async over `r.fetch` | ✅ live |

## State tiers

| Tier | API | Scope | Sync? | Durable? | Use |
|---|---|---|---|---|---|
| flow-local | `ev.flow` (= pilgrim `conn.ctx` / `r.connCtx`) | one connection | sync | no | per-connection vars across keepalive |
| request-local | `ev.ctx` (= `r.ctx`) | one request | sync | no | scratch within a request |
| `table` | `ev.table` / `mirror.table` (`nginx.shared`) | all workers, one node | sync | no (RAM) | counters, node-local shared state, TTL |
| external KV | `mirror.kv(r, url)` (`r.fetch`) | fleet-wide | **async** | yes | sessions/state that outlive nginx |

## Known limitations / not yet

- **Async only in the content phase.** `mirror.kv`/`r.fetch` `await` works in an
  async content handler; the synchronous access-phase hooks can't `await` yet
  (making them awaitable is a C-level change — deliberately deferred).
- **Transpiler:** `switch -glob` is treated as exact (warned); `foreach` supports
  only literal `{a b c}` lists (command-substituted lists warned); a string
  operator's operand must be a single value token (not a parenthesized group);
  no `regexp`/`regsub`, no `class` from external files, no `static::`/`local::`
  variable-scope distinction. Everything unrecognised is **warned, not dropped**.
- **Events not yet wired:** `SERVER_CONNECTED`, `HTTP_REQUEST_DATA` /
  `HTTP_RESPONSE_DATA`, richer `SSL::` beyond ClientHello.
- **Thread 2 (substrate interchange — nginx-on-TMM / BIG-IP-on-sockets)** is out
  of scope here; this prototype is threads 1 + 3 (the programmability layer).

## Where it's proven

- `example/test.sh` — 54 end-to-end checks in real nginx (2 workers).
- `transpile/run.sh` — 95 transpiler checks under `qjs` (structural + the
  generated handlers driven branch-by-branch against a mock `ev`), including the
  production-shaped `transpile/showcase.tcl`.
- The C-touching phases (2, 3, 5, 6, 8) each kept the full `t/` suite green.
