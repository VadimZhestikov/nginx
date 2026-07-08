# mirror — Phase 19 scoping: health monitors (passive + active probe engine)

Status: **scoping / design only.** Health checks are the largest BIG-IP-parity
gap the capability matrix does not cover: LTM monitors (`monitor http/tcp`,
interval/timeout, rise/fall marking) have no counterpart — OSS nginx never had
active checks at all (Plus-only), and the feature-request-coverage doc does not
list them. Yet the platform is unusually well-shaped for it: SharedWorker
timers exist, per-peer `down/weight` setters exist in C, and demo A2.4 (canary)
already proves the observe→decide→write loop end to end. This doc scopes a
monitor engine that is **zero-C in its first two tiers**.

## 0. Definition of done

`mirror.monitor(upstream, opts)` marks unhealthy peers down and revives them,
cross-worker, with rise/fall damping — passively (from live traffic) and
actively (probes on a timer) — surviving SIGHUP reload, with health state
observable via `nginx.shared` and the A4.1 admin console. Tier M.0–M.2 with no
new C; the one C ask (context-free fetch) is quarantined in M.3.

## 1. Grounding — what the tree already provides (verified)

- **Peer marking (C, done):** `weight`, `maxFails`, `down`, `failTimeout`,
  `maxConns` getters/setters on both zoned peers and plain RR peers
  (`src/js/ngx_js_com_upstream.c:186-190` and `:485-490`), plus
  `addPeer(opts)` / `removePeer`. Safety classes: `zoned-shared` when the
  upstream has `zone` (one write, every worker sees it via shpool), else
  `worker-local` (needs fan-out).
- **Scheduling (done):** `setTimeout`/`setInterval` are installed as globals in
  SharedWorker threads (`src/js/ngx_js_sw.c:1039`, fired from the SW loop at
  `:1151`); persistent SW timers proven in demo A2.3.
- **Verdict store (done):** `nginx.shared` / mirror `table` with TTL (phase 8)
  — cross-worker, spinlock-atomic `incr`.
- **The proof-of-loop (done):** demo A2.4 — self-adjusting canary shifts
  upstream weight on observed 5xx. The monitor engine is that demo,
  generalized and hardened.
- **The gap — probe transport:** SharedWorkers have **no `fetch`**; `r.fetch`
  is request-scoped (the mirror.kv boundary, phase 17). The only context-free
  HTTP client is `std.urlGet`, which **shells out to `curl`, blocking**
  (js-dom-manual.adoc:1255-1291). Workers do have `nginx.setTimeout` on the
  event loop, but no request-free fetch either.
- **A subtlety that shapes the architecture:** SW pthreads live in the
  **master process**, which typically retains root. Naive SW-side probing =
  network egress from a root process. Avoid by construction (§2).

## 2. Architecture: scheduler in the SW, prober in a worker

```
        SharedWorker "monitor" (master pthread)
        - owns the schedule (setInterval per upstream)
        - owns rise/fall state machine + flap damping
        - publishes verdicts to nginx.shared: mon:{up}:{peer}
        - never touches the network itself
                 |  "probe now" msg (AF_UNIX channel)
                 v
        designated prober = one worker (lowest live worker id)
        - executes the probe (tier-dependent transport, §3)
        - reports {peer, ok, rtt, detail} back to the SW
                 |  verdict crosses rise/fall threshold
                 v
        marking:
        - zoned upstream: prober writes peer.down once (zoned-shared;
          all workers see it via shpool)  <- REQUIRE zone in v1, like Plus
        - non-zoned: SW broadcasts; every worker applies locally
          (eventual; window = broadcast latency)
```

Splitting scheduler from prober buys three things at once: no master-process
egress, no blocking of the SW message loop by probe I/O, and a single point
(the SW) that owns state — workers stay stateless and replaceable. Prober
failover: the SW pings the designated prober; on worker death/reload it
re-designates (worker ids are already visible to the SW via `onconnect`).

## 3. Probe transport — three tiers

| Tier | Transport | C needed | Caveats |
|---|---|---|---|
| **M.0 passive** | none — live traffic. mirror `onResponseHeaders` / upstream-failure observation records per-peer outcomes into a TTL `table` window | none | complements (not replaces) native `max_fails`: adds **cross-worker shared counters** + programmable policy where native counters are per-worker |
| **M.1 active, today** | `std.urlGet` from the **prober worker** (not the SW): one curl subprocess per probe, synchronous | none | blocks the prober's loop for `timeout` — bound it: serialize probes, cap `peers × interval` envelope (fine for tens of peers at seconds-scale intervals); curl must exist |
| **M.3 active, clean** | context-free `nginx.fetch(url, opts)` usable outside a request (reuse the `r.fetch` machinery unbound from `r`), async on the worker event loop | **the one C ask** — contained | unlocks concurrent probes, HTTPS probes with cert checks, timeouts without subprocesses; also independently wanted by db-connect |

TCP-connect and TLS-handshake probe types (BIG-IP `monitor tcp`) ride M.3's
machinery later; v1 is HTTP(S) status/body matching.

## 4. API sketch

```js
mirror.monitor('backend', {
    type: 'http',                    // v1: 'http' | 'https'
    uri: '/healthz',
    interval: 5000, timeout: 2000,   // ms
    rise: 2, fall: 3,                // consecutive-verdict damping
    expect: { status: [200, 204] },  // later: body regex, headers
    onChange(peer, healthy, detail) { /* optional hook: log, page, shed */ }
});
```

- Config-time (`js_source`) or runtime (COM) registration; dedupe by
  `(upstream, uri)`.
- iRules parity hooks for the transpiler: `LB::status` (read
  `mon:` verdicts), `active_members` (count peers not down) — both map onto
  the verdict store with no further C.

## 5. State, consistency, reload

- **Verdict store:** `mon:{upstream}:{peer}` → `{state, since, fails, rises,
  rtt}` in `nginx.shared` (TTL = a few intervals, so a dead engine's data
  visibly goes stale rather than lying). Mind the 256-entry / 511-byte zone
  limits — one key per peer, compact JSON; large fleets need the shared-zone
  sizing work (matrix recommendation #8).
- **Marking rule:** require `zone` on monitored upstreams in v1 (exactly
  NGINX Plus's own constraint for `health_check`) so a single `peer.down`
  write is fleet-visible within the instance. Non-zoned support via broadcast
  is a documented degraded mode, not the default.
- **Flap damping:** rise/fall consecutive counts (haproxy-style) live in the
  SW only; workers never make marking decisions from raw probe results.
- **Reload:** SW threads restart after reload (already handled by
  `ngx_js_sw_threads_start` re-arming); the engine re-registers from
  `js_source`. Health state survives as data in `nginx.shared` (preserved
  across hot reload) — on restart the SW seeds its state machine from the
  store instead of assuming all-healthy, avoiding a thundering revive.
  Persistent-checkpoint (file) export is optional for full restarts.
- **Peer churn:** `addPeer`/`removePeer` (and tombstoned locations pattern)
  must invalidate `mon:` keys; the SW rebuilds its peer list from the COM
  snapshot each cycle rather than caching forever.

## 6. Failure semantics

- **Prober dies:** SW re-designates within one interval; missed probes count
  as neither rise nor fall (absence ≠ failure).
- **SW dies:** probes stop; peers **hold last state** (fail-static — never
  mass-revive or mass-down on engine failure); `mon:` TTL expiry makes the
  staleness observable.
- **All peers down:** never mark the last live peer down (BIG-IP and Plus both
  special-case this); optional `minLive` knob.
- **Probe ≠ traffic:** probes see `/healthz`, traffic sees everything else —
  passive tier (M.0) stays on even when active probing is enabled, and either
  signal can trip `fall`.

## 7. Observability (feeds matrix recommendation #5)

The verdict store *is* the status API: any JS handler can render
`mon:*` as JSON; the A4.1 admin console gains a health panel (state, since,
rtt, last detail per peer) with the same per-worker convergence stamps it
already uses for config. This is the first concrete producer for a future
`nginx.metrics` subtree.

## 8. Phased sub-plan

| Sub-phase | Deliverable | C? | Risk |
|---|---|---|---|
| **M.0** | Passive tier: generalize A2.4 into `mirror.monitor` (observe-only + mark via zoned-shared write); rise/fall; verdict store; tests in `t/` style | no | low |
| **M.1** | Active tier: SW scheduler + designated-prober protocol + `std.urlGet` transport; reload re-seed; prober failover | no | med |
| **M.2** | Hardening: zone requirement + degraded broadcast mode, peer-churn invalidation, last-peer guard, A4.1 health panel | no | low |
| **M.3** | Context-free `nginx.fetch` (the C ask); concurrent async probes, HTTPS; retire curl | **yes** | med |
| **M.4** | Parity finish: `LB::status` / `active_members` in the transpiler; BIG-IP monitor param mapping (send/recv strings → uri/expect) | no | low |

M.0–M.2 ship value with **zero C** — the whole engine is falsifiable before
the C investment. M.3 is contained and shared with db-connect.

## 9. Non-goals

- Cross-node health aggregation (fleet health belongs to the future remote
  control plane; this engine is per-instance, like Plus's).
- UDP/ICMP/gRPC probe types in v1.
- Replacing native `max_fails`/`fail_timeout` — the passive tier layers on
  top; native machinery keeps working untouched.
- Auto-scaling/service discovery (that is the dynamic-upstreams track; this
  engine only marks peers it is given).

---
*Companion to `CAPABILITIES.md` (threads 1+3 scorecard — monitors would be the
first phase-19 row) and `js_com_docs/js-com-safety-classes.adoc` (the
`zoned-shared` propagation rule this design leans on). Design-only until M.0
is opened.*
