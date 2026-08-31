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
