# Increment A — the ground-truthed build plan

*The first construction artifact, not a design doc. It grounds COMCON-lite (SPEC §11,
ROADMAP §13 increment A) in the actual `nginx/src/js/` code via a source reality check
(2026-08-31, the v2-§9.4 integration check). Everything below cites real file:line so the
plan survives contact with the codebase. Paths: the nginx tree **is** the repo root —
sources are at `src/js/` (not `nginx/src/js/`), vendored QuickJS at `../quickjs/`.*

---

## 1. What the reality check confirmed (the design holds)

- **Single, clean load point.** Runtime/context/eval all happen in `ngx_js_init_conf()`
  (`src/js/ngx_js_module.c:494`; `JS_NewRuntime` :509, `JS_NewContext` :525, COM install
  :544, `js_source` eval loop :563–580). It re-runs *in full* on SIGHUP, tearing down the
  old runtime before the new workers fork (:1519, :1536–1567). **Consequence that drives
  everything:** confinement must be baked **before fork** — after `init_conf` returns,
  every worker is a bit-identical COW image; nothing is confinable later without
  per-worker re-instrumentation. This is exactly the SPEC's master-init / worker-COW
  model, confirmed.
- **Per-tenant compartments are feasible, and cheaper than feared.** Class IDs are
  allocated **once per runtime** (`ngx_js_com.c:519`, static-guarded), but prototypes are
  installed **per context** (`ngx_js_com_install_protos(ctx)`, `ngx_js_com.c:2915`, ~50
  `JS_SetClassProto` calls). So *N contexts in one runtime already work*: register classes
  once, install a **per-tenant prototype with a per-tenant method subset** per context —
  which is precisely S2's compartment story, achievable without touching class
  registration. This is the single biggest favourable finding.
- **The describe registry is extensible.** `ngx_js_member_class_s`
  (`ngx_js_com.h:373–382`) already carries `name / type / klass / flags / propagation /
  note` on three orthogonal axes, plus a per-class **refine hook**
  (`ngx_js_prop_refine_pt`) — a precedent for runtime-resolved row semantics that a
  reach/facet rule reuses directly. The `type` column is already a stringly-typed
  signature slot: M2's type signatures are a pure data extension.
- **`r.location` is deliberately defanged** (`srv_op = NULL` at `ngx_js_com_http.c:4691`;
  `addLocation` bails on NULL at :6389). The team already reasons in per-facet reach —
  COMCON generalizes an existing pattern, it doesn't invent one.

## 2. What it surfaced (the real work, in leverage order)

**Finding: the reach graph is cyclic where the mutation graph is a tree — and separate
JSContexts do not fix it.** A single granted socket walks
`sock.listener → listener.serverByName → NginxServer → server.locations → addLocation`
to full config mutation (`ngx_js_socket.c:100–125`, `ngx_js_listener.c:2456`,
`ngx_js_com_http.c:7688`/:6389). The cycle is mediated by **process-global handle
registries** — `ngx_js_socket_reg[]` (`ngx_js_socket.c:41`), `ngx_js_listener_reg[]`
(`ngx_js_listener.c:55`), `ngx_js_stream_listener_reg[]` — that the `sock.listener` scan
walks with **no ownership check**. Two compartments with separate contexts still resolve
the same handles to the same objects. **So the highest-leverage fix is not context
splitting — it is adding an `owner`/compartment field to the three registries and gating
the scan.** This reorders the plan.

**The four omnipotent, level-conflating members** — cannot be property-gated, must be
refactored or withheld before deny-by-default means anything:
1. `config.write` — parses arbitrary nginx directives (`ngx_js_module.c:1727`).
2. `nginx.repl.eval` / `.listen` / `.listenRaw` — arbitrary eval + network-exposed shell
   (`ngx_js_repl.c:209`, :789).
3. `nginx.use` / `nginx.install` — arbitrary code load/broadcast (`ngx_js_com.c:2090`).
4. `Worker` / `SharedWorker` — spawn **fresh unconstrained runtimes** with
   `JS_SetCanBlock(rt, TRUE)` and unrestricted `JS_Eval` of an arbitrary path
   (`ngx_js_worker.c:719`, :791) — a total confinement escape.
These are S3 (dynamic-code taming) made concrete: withheld-by-default from tenant
environments, grantable only to the host root.

**Two self-defeating grants** (confinement bugs in the current code):
- `nginx.workerMemoryLimit` / `nginx.workerRequestTimeout` are **script-writable** and
  read live before every `JS_Call` (`ngx_js_com.c:2696`) — a tenant raises its own
  resource ceiling. Must become read-only-from-JS (or per-compartment).
- `nginx.shared` is **one flat 256-slot namespace, no key prefixing** (`ngx_js.h:56–80`)
  — a cross-tenant covert channel and collision surface. Needs per-compartment key
  namespacing (the M8 "table-key namespacing" item, pulled earlier — increment A needs
  it for isolation).

**The registry omits read-only members on purpose** (`ngx_js_com_describe.c:41–46`) — but
the read-only *getters* are exactly the leak paths (§2 offenders). M2+S4's registry walk
must **add rows for the read-only getters**, not just annotate the mutating ones.

**Engine-level restriction is available but unused.** `JS_NewContext` unconditionally adds
`JS_AddIntrinsicEval` and `JS_AddIntrinsicProxy` (`../quickjs/quickjs.c:2220`, :2225);
`JS_NewContextRaw` + selective `JS_AddIntrinsic*` removes eval/Function/Proxy for free —
the S1/S3 lever, present in the API, called nowhere today.

## 3. Increment-A task order (revised by the findings)

The reality check reorders the ROADMAP §13 increment-A contents. Registry ownership
precedes compartments; the four omnipotent members are gated first because they make
everything else moot.

- **A0 — withhold the escape hatches.** In the tenant compartment, the environment simply
  does not bind `config.write`, `repl.*`, `use`/`install`, `Worker`/`SharedWorker`,
  `suspend*`, `createSocket`, `broadcast*`. Deny-by-default makes this the *absence* of a
  grant, not a new check — but it only works once the environment is per-tenant (A2).
- **A1 — owner-field the three handle registries** (`ngx_js_socket_reg[]`,
  `ngx_js_listener_reg[]`, `ngx_js_stream_listener_reg[]`) and gate the `sock.listener`
  scan + `nginx.cycle.sockets` / `http.sockets` builders on ownership. Highest leverage;
  independent of contexts; closes the cyclic reach path. Make `workerMemoryLimit`/
  `Timeout` read-only-from-JS and add a compartment key-prefix to `nginx.shared` here too.
- **A2 — per-tenant contexts + prototype subsetting.** Register classes once per runtime
  (unchanged); call a *parameterized* `ngx_js_com_install_protos(ctx, compartment_policy)`
  per tenant that installs only the granted methods on the per-context prototype. Use
  `JS_NewContextRaw` + selective intrinsics to drop eval/Function/Proxy. The obstacle is
  the single `{rt, ctx}` per `ngx_js_conf_t`/`ngx_js_worker_t` (`ngx_js.h:42`, :113) and
  the overloaded `JS_SetContextOpaque` — resolve with a compartment id in a widened opaque
  or a `JSContext*`-keyed side table.
- **A3 — `{compartment, idx}` handler indices.** `handler_idx` and the seven `__ngx_*__`
  global hook arrays (`ngx_js_com_http.c:1497` etc.) assume one context; generalize the
  index to a pair. Well-localized (written in two places, read in three).
- **A4 — the deny-by-default env + registry allow/deny + denial log + audit→enforce loop**
  (the COMCON-lite deliverables proper), now standing on A0–A3.
- **Dogfood**: a mirror demo (A2.8) caged under the above.

Parallel, from the threat model: **TM-2** (session identity → environment mapping) rides
the `nginx.repl` substrate (`ngx_js_repl.c`) — the same code A0 gates — so specify it
alongside A0.

## 4. What this changes in the plan

1. **Registry ownership is a prerequisite, promoted ahead of context-splitting** (it was
   implicitly inside S4; the reality check shows it's both higher-leverage and independent).
2. **M2 + S4's registry walk must add read-only-getter rows** — the current tables skip
   exactly the leak paths. (Refines the M2 deliverable.)
3. **Two confinement bugs to fix in A1**: writable `workerMemoryLimit`/`Timeout`, and
   un-prefixed `nginx.shared` — the latter pulls the M8 table-key-namespacing item earlier.
4. **The four omnipotent members are named** as the concrete content of S3's "withhold by
   default"; none can be property-gated.
5. **S2 compartments validated**: register-once / install-proto-per-context is real and
   supports per-tenant method subsets — no class-registration surgery needed.

Nothing here contradicts the SPEC; it grounds increment A's *sequence* and names the
specific nginx refactors S3/S4 face. The design survived contact with the code.

## 5. First-unit refinement (from reading the source, not just the map)

Reading `ngx_js_com.c:2700` and `ngx_js_socket.c:99` closely refined the A-order once more:

- **The env model is the PRIMARY control; the registry gate is defense-in-depth.** The
  reach cycle (`sock→listener→…→addLocation`) is only reachable if a tenant *holds* a
  socket — which deny-by-default already prevents by not binding `createSocket`/socket
  handles into the tenant environment. So A1's registry gate is belt-and-suspenders for
  a *leaked* handle, not the front-line control. Front line = the environment.
- **The two "confinement bugs" are the env model, not standalone edits.**
  `workerMemoryLimit`/`Timeout` are *legitimately* host-writable at config time; the fix
  is that a *tenant* env doesn't expose the writable property. Same for `nginx.shared`
  prefixing. Neither is a patch — both are consequences of per-compartment environments.
- **Everything in A1 needs a compartment identity that today does not exist**
  (`JS_SetContextOpaque` is overloaded; one shared namespace). So the true first buildable
  unit is **A1.0 — the compartment-identity primitive**: an owner token
  (`ngx_js_compartment_t`, `HOST_ROOT` default) + a `ngx_js_current_compartment()`
  accessor (the seam — returns HOST_ROOT today, per-tenant later, set around handler
  dispatch the way the request-timeout deadline already is). Registry owner-fields,
  env-withholding, and the gate are all *consumers* of this token.

**Revised first slice (a thin vertical, not horizontal plumbing).** Horizontal
"owner-field everything" compiles but cannot be tested until a *second* owner exists.
The testable MVP proves the model end-to-end on one leak path:
1. **A1.0** the compartment token + `ngx_js_current_compartment()` accessor (returns
   HOST_ROOT).
2. one **second compartment** with a reduced-method prototype (`JS_SetClassProto`
   per context/env — no class surgery, confirmed) and a **deny-by-default env** lacking
   `createSocket`/`repl`/`use`/`Worker`/writable-limits.
3. **A1.1** owner-field the socket registry + gate the `sock.listener` scan
   (defense-in-depth).
4. a `t/` test: host creates a socket; the tenant handler cannot *name* it, and even
   handed the handle, `sock.listener` returns empty.

The reality check optimized A1 for smallest diff; this optimizes for smallest *testable*
increment. They converge (the vertical uses A1's registry field) but sequence
identity-primitive-first.

## 6. Build log — what is now real (v5.6)

The vertical slice of §5 is **built, tested, and non-regressing** (each unit behind a
`t/comcon_*` test that proves the confinement claim it implements; sighup stress green):

| Unit | Commit | What exists |
|---|---|---|
| **A1.0** identity seam | `b2f135c11` | `src/js/ngx_js_compartment.{c,h}`: the owner token, `current/enter/leave`, `may_reach` |
| **A1.1** reach gates | `a053740e5`, `a3b8c2bb6` | socket-owner field; the whole `sock↔listener→serverByName→server` cycle gated on it (a listener's reach domain *is* its socket's — no separate listener field needed); the `cycle.sockets`/`http.sockets` enumerators host-only |
| **A2.0** deny-by-default env | `33c4f0d52` | `js_tenant_source`: a reduced tenant context (no `nginx`, no module loader, granted names only) — *the primary control*, tested (`comcon_tenant_deny.t`) |
| **A2.1** grants | `14f7a25b7` | `nginx.grantToTenant(name, sock)` (host-only); tenant holds + uses the socket, yet `.listener` is null cross-compartment — *the gate isolates by compartment, not by holding* (`comcon_tenant_grant.t`) |
| **A4** denial log + audit loop | `0730ba201` | denial events at all seven gate sites; **TM-1 to spec** (exact per-code counters always; 100 full records then 1/100 sampling; quota-exceeded reported once — verified: 250 denials → 101 records, counters exactly 250); `js_tenant_mode audit\|enforce;` (audit = log-and-allow, the observe-then-enforce loop); `nginx.tenantDenials()` host report; the tenant runtime now carries the full COM class set (classes per-runtime, protos per-context, still no `nginx` global) so audit-allow can hand wrapped objects in (`comcon_denial_log.t`, `comcon_audit_mode.t`) |
| **A3.1** request headers | `2d5a81877` | headers as DATA both ways — `req.headers` in (a copy), response headers out via the return value's `.headers`, with CRLF/non-token names dropped (showcase-4 guard); still zero-capability |
| **dogfood** acceptance | `2d5a81877` | `js_com_demos/COMCON_dogfood/`: a caged mirror tenant (count+tag+echo, the M1 shape as untrusted code) on real 2-worker traffic; `test.sh` (12 checks) proves policy works + cage holds live + injection dropped + host reads `tenantDenials()` |
| **A3.0** request path | `39a496cab` | persistent tenant runtime (COW into workers, torn down at the same four sites as the host runtime); granted `onRequest(fn)`; `js_tenant_handler;` location directive; deny-by-default + gates active **during live requests** (`comcon_tenant_request.t`) |

**A design decision made in code, now recorded:** the A3.0 tenant handler receives
plain request *data* (`{method, uri, args}`) and its **entire authority over the
response is its return value** (`"body"` or `{status, body}`). There is no request
capability object to confine — the zero-capability request path is the confinement
floor, and the graded request *facet* (headers, variables, subrequest — each a
separate grant) is the later widening, not the starting point.

**Lessons the code taught (carried forward):**
- An ephemeral or secondary evaluation needs a **fully isolated runtime** (the
  `js_preprocess` pattern); a second context on the shared master runtime trips
  QuickJS's `list_empty(&rt->gc_obj_list)` assertion at teardown. Every GC-tracked
  JSValue held in C structs must be freed *before* `JS_FreeContext`.
- **Pre-existing bug fixed en route:** `failed_ctx` in `init_conf` violated exactly
  that rule for `master_handlers`, so any SIGHUP whose `js_source` throws (e.g.
  `createSocket` EADDRINUSE on reload) *aborted the master* instead of rolling back.
  Failed reloads now leave the master serving the old cycle.
- Reload semantics verified both ways for the tenant: idempotent config → new tenant
  runtime serves, old freed cleanly; throwing config → master survives.

## 7. Increment B — onboarding (build log)

| Unit | Commit | What exists |
|---|---|---|
| **B/E1** dependency workflow | `d4c2379ab` | `js_tenant_dependency <name> <path> <sha256>;` — a **pure library** evaluated in a bare no-capability environment, bound on the tenant global as `<name>`, admitted only if its bytes match the pin. Hijacked update (bytes changed) → config refused (last good config serves); a lib reaching for host authority → not admitted. Test `t/comcon_dependency.t`. |
| **B1** generated grant-stub | `9fcc9adcd` | `nginx.tenantLearning()` gains a `grants` list (already-granted names); a generator (plain library JS, `js_com_demos/COMCON_onboard/onboard.js`) turns the learning record into a paste-ready contract stub — each wanted path classified REFUSE (omnipotent) / REVIEW (narrow-to-a-facet), the wanted-vs-granted delta, the enforce next-step. Demo + `t/comcon_onboard.t`. |
| **B0** learning mode | `afd2aaa46` | `js_tenant_mode learn;` (the A4 flag is now a tri-state enforce\|audit\|learn). Learn seeds the tenant global with a **recorder** for each withheld host name — a callable catch-all exotic object that records every access *path* the fragment walks (`nginx.http.addServer()`, `createSocket()`, …) and lets it run to completion instead of throwing. `nginx.tenantLearning()` (host-only) returns `{mode, wants:[{path,hits}]}` — the onboarding wishlist. Test `t/comcon_learn_mode.t`. |

Together A4 + B0 are the **observe → onboard → enforce** loop: *audit* shows a
tenant's granted-but-gated reaches; *learn* shows the host surface it wants but
does not have; the operator grants the safe subset and flips to *enforce*.

**Increment B is COMPLETE** (learning mode, generated grant-stub, dependency
workflow). Next: **increment C** — the typed/compiled tier (typed profile, the
fragment artifact, T1/T2), which needs M-UNIFY + the front-end/binder/lowering.

**Increment A is COMPLETE** — accepted by the dogfood demo (a confined tenant serving
real multi-worker traffic, cage proven on live requests, the audit→enforce loop closed
by the host). Beyond A: **increment B** (onboarding — learning-mode harvest, generated
docs, dependency workflow); and the deferred widenings — multi-tenant (N named
compartments; the `{compartment, idx}` handler generalization is only needed then),
request-facet/capability grants (e.g. a cross-worker shared counter — the demo's honest
gap), budgets/gas (S5), and per-member registry policy (the M2+S4 schema walk supersedes
today's hard-coded gate set).
