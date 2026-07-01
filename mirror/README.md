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
| `onClientHello` | `server.ssl.onClientHello(fn)` | ✅ wired (phase 3) |
| LB selection command | `ev.selectUpstream(pool)` in `onRequestHeaders` | ✅ phase 4 |
| peer-level LB | `upstream.onSelectPeer(fn)` (custom balancer) | ✅ phase 5 |
| `onClientData` (L4) | `mirror.attachStream(streamServer, fn)` + `session.data` | ✅ phase 6 |
| cross-worker `table` | backed by `nginx.shared` (shmem KV) | ✅ phase 7 |
| `table` TTL / expiry | `table.set(k, v, ttl)` + `table.ttl(k)` | ✅ phase 8 |
| iRules → mirror | `mirror.transpile(tclSource)` (decision A) | ✅ phase 9 |

- **`lib/mirror.js`** — the framework. Canonical event lattice (full stack,
  HTTP+accept wired), a **capability table** (`CAPS`) gating which commands are
  valid per event (= pilgrim's `describe()`/safety-classes, indexed by event),
  per-connection **flow-local** store, a **cross-worker `table`** store (phase
  7, backed by `nginx.shared`), and `mirror.attach(server, location, handlers)`.
- **`example/`** — one iRule translated by hand as the acceptance test
  (`app.js`), plus `nginx.conf` + `test.sh`.

### Run

```bash
cd example && bash test.sh    # 30/30 pass
```

The test proves: accept→request→response **linkage**, per-connection flow-local
**persisting across keepalive requests**, per-request routing, a **cross-worker**
`table` counter with **TTL/expiry** (phases 7–8), and the **per-event capability
gate** firing.

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
3. **TLS ClientHello events — DONE (phase 3).** Added
   **`server.ssl.onClientHello(fn)`** (installs `SSL_CTX_set_client_hello_cb`);
   the hook fires at handshake time with a parsed ClientHello — `sni`, `version`,
   `cipherSuites`, `extensions`, `alpn`, `supportedGroups`, `ecPointFormats` (the
   JA3 inputs) — plus the per-connection ctx, and can abort the handshake by
   returning `false`. Serves the JA3/JA4 customer request and iRules
   `CLIENTSSL_CLIENTHELLO`.
4. **Per-request LB/upstream selection** (iRules `pool`) — DONE (phase 4, no new
   C): `ev.selectUpstream(pool)` — see the Phase 4 section below.

Phase-2 acceptance test (`example/test.sh`, 9/9): the per-connection **serial is
identical across keepalive requests** (proving one shared per-connection object)
and **differs across connections**, and **`onClientClose` fires** with the
accept-time flow-local still readable.

New pilgrim API added in phase 2:
- `NginxConnection.ctx` — persistent per-connection object (accept hook).
- `NginxConnection.onClose(fn)` — connection-close callback.
- `request.connCtx` — the same per-connection object, from request/response.

New pilgrim API added in phase 3:
- `server.ssl.onClientHello(fn)` — TLS ClientHello inspection; `fn(clientHello,
  connCtx)`, return `false` to abort. `clientHello` = `{sni, version,
  cipherSuites[], extensions[], alpn[], supportedGroups[], ecPointFormats[]}`.

The phase-3 acceptance test (`example/test.sh`, 13/13) drives an HTTPS server and
proves the SNI carried in the ClientHello (before the handshake completes) is
read back on the HTTP response — the spine now reaches from TLS to the response.

## Phase 4 — per-request LB / pool selection (DONE)

`ev.selectUpstream(pool)` (available in `onRequestHeaders`) picks the upstream
per request — the iRules `pool` command. It needed **no new C**: it sets the
nginx variable `$mirror_upstream` (via the existing `r.setVariable`), and the
location proxies with `proxy_pass http://$mirror_upstream`, which nginx resolves
to the named `upstream {}` at request time.

Config contract for an LB location (see `example/`):

```nginx
upstream mirror_poolA { server ...; }
upstream mirror_poolB { server ...; }
location /lb/ {
    set        $mirror_upstream mirror_poolA;   # default
    proxy_pass http://$mirror_upstream;         # no JS content handler here
}
```

```js
mirror.attach(server, lbLoc, {
    onRequestHeaders: function (ev) {
        ev.selectUpstream(ev.header('x-pool') === 'b' ? 'mirror_poolB' : 'mirror_poolA');
    }
});
```

`example/test.sh` proves the header-driven pool choice reaches the right backend.

## Phase 5 — peer-level LB (custom balancer, DONE)

`upstream.onSelectPeer(fn)` (new pilgrim C API in `ngx_js_com_upstream.c`) is a
real custom balancer — the iRules `LB::select`. It **wraps** the round-robin
`peer.init`/`peer.get`/`peer.free`, so upstream health / retry / accounting are
preserved; JS only influences *which* peer is chosen. Per pick, the balancer
calls `fn(peers, connCtx)`:

- `peers` — array of `{name, down, conns, weight}` for the upstream's peers.
- `connCtx` — the per-connection flow-local (so the choice can be request-driven,
  set from an `onRequestHeaders` rule earlier on the same connection).
- return value — the peer **index** to use, or `-1` to fall back to round-robin.

```js
nginx.http.upstreams.find(u => u.name === 'mirror_pool')
     .onSelectPeer(function (peers, flow) {
         return (flow && typeof flow.peerIndex === 'number') ? flow.peerIndex : -1;
     });
```

The location uses a normal `proxy_pass http://mirror_pool;` (the balancer does
the picking). `example/test.sh` (20/20) proves a request-chosen peer index routes
to that specific node, deterministically.

Note: the first cut targets **round-robin** upstreams (it installs the RR
per-request state directly, since pilgrim wraps `peer.init` for its dynamic
RR-peer COM). Zoned upstreams work but hold the shared peers lock only while
snapshotting (not during the JS call). Non-rr methods (hash/ip_hash) are future.

## Phase 6 — L4 data events (`onClientData`, DONE)

Raw-TCP inspection — the iRules `CLIENT_DATA` event — in the **stream** module
(where L4 belongs). `mirror.attachStream(streamServer, { onClientData })` runs
the rule with the client's initial bytes:

- `ev.data` — the preread L4 bytes; `ev.clientAddr`; `ev.finalize(code)` /
  `ev.reject()` to close.

New pilgrim C in `ngx_js_stream_module.c` / `ngx_js_stream_listener.c`:
- A **stream preread-phase handler** (registered in postconfiguration) that, for
  stream servers that **opted in via `server.captureData`**, waits for the
  client's first bytes and **snapshots** them into a per-session ctx (nginx
  rewinds `c->buffer` before the content phase, so the bytes must be captured
  during preread). A plain `server.handler` that does not set `captureData` is
  never delayed — a client may open a connection and send nothing.
- **`server.captureData = true`** — opt in to preread capture (set automatically
  by `mirror.attachStream`).
- **`session.data`** — the captured preread bytes, exposed to the stream handler.

```js
mirror.attachStream(nginx.stream.servers[0], {
    onClientData: function (ev) {
        var proto = ev.data.indexOf('SSH-') === 0 ? 'ssh'
                  : ev.data.indexOf('GET ') === 0 ? 'http' : 'unknown';
        // route/act on `proto`; ev.finalize()/ev.reject()
    }
});
```

`example/test.sh` (22/22) sends raw TCP and confirms `onClientData` detects the
protocol from the preread bytes.

## Phase 7 — state bindings (cross-worker `table`, DONE)

The iRules `table` is *cluster-shared* state; the nginx analog is *cross-worker*
state. Phase 1 backed `table` with a per-worker `Map`, so a counter incremented
on worker A was invisible to worker B — wrong for a shared table. Phase 7 binds
`table` to pilgrim's **`nginx.shared`** (a cross-worker shared-memory KV store,
auto-created whenever `js_source` is present — **no new C**):

- values are **JSON-encoded** on the way in / decoded on the way out;
- **`table.incr(k)`** maps to `nginx.shared.incr`, which is **atomic** under a
  spinlock — no read-modify-write race across workers;
- also `table.delete(k)` and `table.keys()`; `mirror.table.backend()` reports
  which tier is active.

The backend is **probed lazily**, not at load: `nginx.shared` throws at
config-eval time (`js_source` runs before the zone exists), so on the first
`table` op inside a worker mirror binds to `shared`; if that ever throws it falls
back to the per-worker `Map`, keeping mirror usable at config time or in a build
without the zone.

```js
mirror.table.incr('mirror:total');      // atomic, visible to every worker
mirror.table.get('mirror:total');       // JSON-decoded
mirror.table.backend();                 // 'shared' | 'map'
```

The example runs **`worker_processes 2`** with `listen ... reuseport`; the
acceptance test (`example/test.sh`, **25/25**) fires 40 close-connections, and
proves (a) the backend is `shared`, (b) **≥2 distinct workers** served
(`nginx.workerIdx`), and (c) all 40 running totals are **distinct** — i.e. a
single shared counter, not per-worker ones (which would repeat totals between
workers).

## Phase 8 — `table` TTL / expiry (DONE)

The iRules `table set <key> <val> <timeout>` — an entry that self-expires. This
extends `nginx.shared` in the C module (`ngx_js_com.c`):

- each shared entry gained an absolute `expires` (`ngx_time()` seconds; `0` =
  never);
- **`shared.set(key, val, ttlSeconds)`** — optional 3rd arg; `get`, `keys`,
  `incr` and the new **`shared.ttl(key)`** all honour expiry and **lazily
  reclaim** expired slots on access (under the zone spinlock), so a TTL'd key
  also frees its slot for reuse;
- **`shared.ttl(key)`** — Redis-style: `null` = absent/expired, `-1` =
  permanent, `>= 0` = seconds remaining.

mirror surfaces this as **`table.set(k, v, ttlSeconds)`** and **`table.ttl(k)`**
(the per-worker `Map` fallback is TTL-aware too):

```js
mirror.table.set('greeting', 'hi', 30);   // expires in 30s, all workers
mirror.table.ttl('greeting');              // ~30, then null once expired
```

`example/test.sh` (**30/30**) sets a key with a 3 s TTL, reads it back on a
*different* worker (cross-worker), checks the reported remaining lifetime, waits
past the TTL, and confirms the value and TTL are gone (self-reclaimed).

## Phase 9 — TCL/iRules → mirror transpiler (decision A, DONE)

The migration layer of the hybrid stance: translate the existing iRules install
base onto the mirror model built in phases 1–8. `mirror.transpile(tclSource)`
returns `{events, isStream, handlers, warnings}`, where `handlers` is JS source
for a `mirror.attach(...)` handlers object. It's a pure-JS layer (no C, no nginx
deps — also runs under `qjs`); see **`transpile/`** for the supported command
vocabulary.

Transpiling real iRules is also the sharpest test that the mirror model actually
**covers** the iRules surface. The acceptance test (`transpile/run.sh`, **32/32**)
transpiles the *same* spine iRule that `example/app.js` translates **by hand**,
asserts the generated mirror calls match, then **evals the output and drives it
with a mock `ev`** to prove behaviour — plus pool/TTL and L4 rules. Anything
outside the vocabulary is **warned, not silently dropped**.

```js
var out = mirror.transpile('when HTTP_REQUEST { pool p ; table incr hits }');
// out.handlers -> "{ onRequestHeaders: function (ev) {
//                      ev.selectUpstream(\"p\"); ev.table.incr(\"hits\"); } }"
```

## Phase 10 (next)

- `session`/persistence → a COM persistence API; the db-connect project
  (external KV) as a further `table` tier.
- Broaden the transpiler: `if`/`switch`/`foreach` control flow, more commands,
  and a live end-to-end path (transpile an iRule at config time and attach it).

> All commits for this project are prefixed `mirror:`.
