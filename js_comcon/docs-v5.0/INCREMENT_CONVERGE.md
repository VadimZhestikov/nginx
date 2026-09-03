# INCREMENT — CONVERGE: one confined mechanism (`include`) — scoping

**Status:** ✅ COMPLETE (2026-09-02). All phases P1–P6 done. There is now ONE confined-fragment
mechanism — `comcon.include(...)` bound via `location.handler` — on both tiers (interpreted + AOT);
the `js_tenant_*` directives and the tenant compartment subsystem are removed. Follows the
directive-retirement work in `INCREMENT_MCFG.md` (steps 4–6) and the Option-1 landing
(`js_tenant_handler` retired via `location.handler`, `t/comcon_operator_handler.t`).

## 1. Goal

Collapse the **two parallel confined-fragment mechanisms** into **one primitive** — `include`
(host-JS, driven from the single `js_source` root) bound to a location through the existing
`location.handler`. Then delete the `js_tenant_*` directives. This is the shell fundament taken to
its conclusion: one recursive primitive, no directives, no parallel subsystem.

The end-state a root script writes:

```js
const h = comcon.include(src, { imports, grants, meter, deps, identity, mode });
loc.handler = req => { const o = h(shape(req)); req.respond(o.status, o.headers, o.body); };
```

## 2. The two mechanisms today

| | **A — tenant path (OLD)** | **B — include path (NEW, M-CFG)** |
|---|---|---|
| entry | `js_tenant_source` / `comcon.tenant()` | `comcon.include(src, contract)` |
| compartment | `tenant_rt` + `tenant_ctx` (shared) | `comcon_rt` + `comcon_ctx` (shared) |
| request | tenant `onRequest(fn)` + `js_tenant_handler` routes to it (C-side marshal + `{status,body}` contract + size cap, `ngx_js_http_module.c:8547–8749`) | fragment returns a value; `location.handler` wrapper marshals/responds (host-JS) |
| grants | `nginx.grantToTenant(name, sock)` — reach-gated | live-cap closure params: masked sockets + COM facets (**strictly better**) |
| admission | full C3/C4 gate in `ngx_js_eval_tenant_sources` (free-names, dyn-code, request-sealed, onRequest-sig) + artifact identity pin | **none** — `include` only compiles + wraps; the C3 gate lives in the *separate* `admit` operator (`ngx_js_com.c:2849`) |
| deps | `js_tenant_dependency` / `comcon.dependency()` — pinned pure libs bound on the tenant global | — |
| mode | `js_tenant_mode` / `comcon.mode()` — process-global (`policy_init`) | inherits the same global mode via the reach gate |
| compiled tier | **C5 lowers the tenant `onRequest` handler to native C** (`ngx_js_module.c:2252`) | interpreted only |

B already wins on grants and on the directive-free entry. A still owns admission, deps, the
compiled tier, and the request contract.

## 3. Parity gaps (tenant feature → `include` realization)

| # | Gap | Realization on `include` | Effort | Risk |
|---|---|---|---|---|
| **G1** | Admission (C3) | `include` composes `admit`: run `js_comcon_uses_dynamic_code` + `js_comcon_collect_free_globals` (free-names ⊆ `contract.imports`) + request-field checks at compile time; refuse on violation | M | low — reuse the `admit` C funcs verbatim |
| **G2** | Identity pin (`js_tenant_artifact`) | `contract.identity`: after admit, compute `H(H(source) ‖ schema)` and refuse on mismatch | S | low |
| **G3** | Deps (`js_tenant_dependency`) | `contract.deps=[{name,path,sha}]`: load each pinned pure lib in a bare cage, verify hash, bind by name as a **closure param** (same mechanism as grants) | M | med — pure-cage eval in `comcon_rt` |
| **G4** | Mode (`js_tenant_mode`) | keep `comcon.mode()` **global** (recommended) — the compartment is shared, per-fragment mode needs per-fragment policy state; revisit only if multi-tenant isolation demands it | XS | low |
| **G5** | Request/response contract | **NON-GAP — already js_com.** `location.handler` binds the handler; `req.respond`/`req.json`/`req.text`/`req.html` shape the response; the request→data marshal is a one-object field read. Proven in Option 1 (`t/comcon_operator_handler.t`). No kernel `comcon.serve` — baking it in would grow the kernel for what composition already gives. If the `{status,body}`+cap ergonomics are wanted, ship a **library** snippet (showcase docs), not a `comcon.*` primitive. Body cap is backstopped by the tenant runtime's 64 MB memory limit. | — | — |
| **G6** | **Compiled tier (C5)** | lower an `include` fragment to native C like the tenant `onRequest` handler — the fragment is already a pure `req→{status,body}` function, the ideal lowering shape | **L** | **high** — the C5 entry is wired to `eval_tenant_sources`/`tenant_request_handler`; retargeting to `comcon_ctx` fragments is the hard part |
| **G7** | Test migration | port the tenant-based `comcon_*.t` (source/handler/deps/artifact/admission/lowering/faithfulness) to `include + location.handler`; keep both green during the transition | L | med — many files; do file-by-file |

## 4. Sequencing (phases)

1. **P1 — admission into `include` (G1+G2). ✅ LANDED (2026-09-02).** The C3 gate was factored into
   `ngx_js_comcon_admit_check(ctx, fn, imports, check_request, reason, len)` (non-static in
   `ngx_js_com.c`), shared by the `admit` operator and `include`. `__includeConfined` now takes a
   5th arg `{ imports, checkRequest?, identity? }` and, **when present** (opt-in, so un-admitted
   `include` stays backward-compatible), runs `admit_check` on the compiled fragment **in the
   compartment** (`sctx`) — where grants are closure var-refs, auto-excluded from the free-name
   check — plus an optional identity pin `H(H(source) ‖ NGX_JS_C4_SCHEMA_VERSION)` compared to
   `contract.identity`. **Verified** (`t/comcon_include_admit.t`; comcon 34/267): a clean fragment
   is admitted, an ungranted free name (`nginx`) is refused (unless listed in `imports`), `eval` is
   refused, a correct identity pin admits, a wrong one refuses. The `admit` operator + all existing
   include tests stay green (the refactor preserved its exact reject messages).
2. **P2 — serve helper (G5). ✅ RESOLVED as a NON-GAP (2026-09-02).** Request/response is already
   js_com (`location.handler` + `req.respond`/`req.json`/…), proven in Option 1. No kernel
   `comcon.serve` — reuse the existing primitive per the recursive-inclusion fundament
   ([[feedback-reuse-jscom-primitive]]). An optional library snippet may ship in the showcase docs.
3. **P3 — deps (G3). ✅ LANDED (2026-09-02).** `include`'s `contract.deps = [{name, path,
   sha256}]` loads pinned pure libraries onto the include primitive.
   `ngx_js_comcon_eval_dep(ctx, cycle, path, sha256, out, …)` reads the file, verifies its bytes
   hash to the pin, evaluates it as a bare-global pure script (a lib reaching for host authority
   throws), and its completion value is bound as a **per-fragment closure param** (the dep names
   join the wrapper's param list after the grants; the eval'd values join the closure args) — not
   on a shared global, so deps don't leak across fragments. **Verified**
   (`t/comcon_include_deps.t`; comcon 35/271): a pinned lib (`lib.greet(...)`) is bound and usable
   in the fragment, and a hijacked update (hash mismatch) refuses the include. Since deps are
   closure params, admission (P1) auto-excludes them from the free-name check, exactly like grants.
4. **P4 — migrate `comcon_*.t` (G7). 🔨 IN PROGRESS (2026-09-02).** Add include-based parity
   siblings file-by-file (keeping the tenant tests until P6, so both paths stay green). Landed:
   `t/comcon_include_request.t` (the core confined request handler — live-cap grant + A1 reach-gate
   isolation + admission + host-unreachable + call-stability + host-locations-unaffected),
   `t/comcon_include_headers.t` (data-in/out headers + CRLF drop), `t/comcon_include_deny.t`
   (deny-by-default: `typeof nginx === "undefined"`, withheld `createSocket` unreachable). Together
   with the earlier include tests (grant/admit/deps/handler/mediate/facet) this now covers the
   interpreted tenant scenarios. **Security fix surfaced by P4:** `req.respond` did NOT drop
   CR/LF-injected response headers (only `js_tenant_handler`'s C path did) — a response-header
   CRLF-injection affecting *every* js_com handler. Added `ngx_js_header_has_crlf` +
   a drop-with-warning guard in `ngx_js_request_respond` so the whole js_com response path is safe
   (host + confined). Added `t/comcon_include_denial_log.t` — a granted fragment hits the A1 gate
   250× and the shared TM-1 denial machinery counts them exactly (100 full records + 1/100 sample);
   `nginx.tenantDenials()` reports identically (the denial subsystem is shared, now exercised via
   include). **Audit mode** is already covered by `t/comcon_operator_tenant.t` (a granted socket's
   reach edge is log-and-allowed under `comcon.mode('audit')`); **restricted** is covered by P1
   admission (`t/comcon_include_admit.t` — eval/`Function`/`with` refused).
   **Two tenant tests do NOT map to a test rewrite — they need feature work or don't apply:**
   (a) **learn mode** — ✅ **now CLOSED (2026-09-02).** Added the feature: the include compartment
   registers the recorder class in `comcon_rt` and, in learn mode, seeds recorders for the withheld
   host surface (`ngx_js_learn_seed`); `__includeConfined` skips admission in learn mode (relaxed,
   non-enforcing discovery). Both compile-time checks read **`jcf->tenant_mode`**, not the process-
   global — the compartment is built during the host eval, before `policy_init` applies the mode
   (the P1 reorder). **Verified** (`t/comcon_include_learn.t`; comcon 40/309): a confined fragment
   configured with `comcon.mode('learn')` runs to completion and its references to
   `nginx.http.addServer` / `createSocket()` / `fetch()` are harvested into the wishlist
   (`nginx.tenantLearning()`) and logged — the same B0 discovery as the tenant, via operators only.
   (b) **schema_conformance** (`comcon_schema_conformance.t`) tests the tenant's *sealed
   Request object*, whereas include hands the fragment JSON-marshaled request *data* — a different
   (also-safe) shape, so its assertions don't transfer directly. **freeze/mses/sr1** test the M-SES
   lockdown, which is **shared code** (`ngx_js_tenant_lockdown` runs in both `tenant_ctx` and
   `comcon_ctx`), so the property holds on include automatically.
   **P6 lockdown-test strategy — DECIDED: migrate the probes to include (2026-09-02).** The
   alternative (keep a minimal tenant harness just for these tests) keeps a parallel mechanism alive
   and defeats "one mechanism", so it is rejected. Since the lockdown is shared, the probes transfer
   with identical results — proven by migrating the two pure-lockdown suites: `t/comcon_include_mses.t`
   (every dynamic-code escape route — `[].constructor.constructor`, `Object.constructor`, generator/
   async `.constructor` — stays tamed; curated std JS all works) and `t/comcon_include_freeze.t`
   (Object/Array/String + the call-only iterator instance-prototypes frozen, pollution writes throw,
   own-object mutation unaffected). **`comcon_sr1_regression.t` — ✅ MIGRATED (2026-09-02),
   surfacing TWO security fixes** (`t/comcon_include_sr1.t`): HIGH-1 (reach hidden in a return-value
   getter) + MEDIUM-4 (`socket.close()` denied) + MEDIUM-2 (framing). **Fix 1 (HIGH — reach-gate
   bypass):** the include invoke JSON-materialized the fragment's result *after*
   `ngx_js_compartment_leave`, so a getter in the returned object
   (`get status(){ return granted.listener === null ? 200 : 599 }`) fired as HOST_ROOT — bypassing
   the A1 gate (leaked the listener → 599). Fixed by moving `compartment_leave` to *after*
   `JS_JSONStringify`, so result materialization (incl. getters) runs under the TENANT compartment.
   **Fix 2 (MEDIUM — response framing):** `req.respond` did not drop handler-set framing headers, so
   a fragment-set `content-length: 999` was emitted *alongside* nginx's real `Content-Length` — a
   duplicate-CL request-smuggling / desync vector. Added `ngx_js_header_is_framing` (content-length /
   transfer-encoding / connection) to the `req.respond` drop guard (with the CRLF guard). Both fixes
   are in the shared paths — they protect every include fragment and every `req.respond` caller.
   `schema_conformance` stays as noted (sealed-Request vs marshaled data).
   Compiled-tier (`faithfulness`, `lowering`) stay for P5. **Net:** at P6 the M-SES lockdown code
   stays (used by include); its coverage moves to the `comcon_include_*` probes, and the tenant
   harness is deleted.
5. **P5 — compiled tier (G6). ✅ LANDED (2026-09-02).** `include` fragments now lower to native C on
   `objs_jit`, so the compiled tier no longer depends on the tenant `onRequest` path. One
   `js_comcon_aot_compile(sctx, fn)` call in `__includeConfined` (`#ifdef CONFIG_JIT`). **R1 — the
   main risk (does maxim lower the cap-closure shape?) — resolved POSITIVELY:** a fragment closed
   over a granted socket AOT-compiles (verified by probe + the `all_compiled` gate). **SR-2 gate
   green:** `t/comcon_include_faithfulness.t` runs both builds and asserts identical responses AND
   denial counters across the confinement surface (string/JSON/object/compute, granted-socket scalar
   reads, A1 gated reach `.listener`, A1 gated mutator `close()`), with `all_compiled` non-vacuous.
   The full `comcon_*.t` suite is green on `objs_jit` too (45/335). Details in **§ P5 below.**
6. **P6 — remove the directives. ✅ DONE (2026-09-02).** The `js_tenant_*` directives are gone
   (`js_tenant_source` is now "unknown directive") and the tenant compartment mechanism is deleted.
   Sequence: **P6a** deleted 14 tenant tests with include siblings; **P6b** folded the last unique
   tests onto include (`frontend_audit`→`include_admit`, `teardown`→`include_teardown`, new
   `include_audit`, deleted `gas`/`onboard`); **P6c-1** removed the 5 directives + their setters + the
   3 tenant-only operators (`comcon.tenant`/`dependency`/`artifact`; kept `comcon.mode`) +
   `ngx_js_tenant_content_handler` + the deprecation test; **P6c-2** removed ~490 lines of dead
   machinery (`ngx_js_eval_tenant_sources` + its `init_conf` call, `onRequest`, `report`,
   `load_tenant_deps`, the C3 callback/struct). Shared functions
   (`tenant_context_new`/`lockdown`/`learn_seed`/recorder/`com_install_protos`) + `comcon.mode` +
   `tenant_mode` + `nginx.tenantDenials`/`tenantLearning` are kept — the include compartment uses
   them. Residual dead-but-entangled bits (`nginx.grantToTenant`+`tenant_grants` read by the live
   `tenantLearning`; `tenant_teardown` no-op) left as harmless no-ops. **Confined fragments are now
   exclusively `comcon.include(...)` bound via `location.handler`, on both tiers.** Full regression
   green (t/ 266/3412; comcon 21/159 include-only; t_stress 16/80). Commits 38ab240ca / b05cb6764 /
   7d3270d4f / f0e6d6c97. *(the note below is the pre-removal record.)* Full removal was
   **gated on P5**: the compiled tier (`#ifdef CONFIG_JIT`, `ngx_js_module.c` C5.0-b) still lowered
   **gated on P5**: the compiled tier (`#ifdef CONFIG_JIT`, `ngx_js_module.c` C5.0-b) still lowers
   the tenant `onRequest` handler on the `objs_jit` build, so `js_tenant_handler` + the
   `tenant_ctx`/`onRequest` machinery cannot be deleted until P5 retargets lowering to `include`
   fragments. The safe first step (no P5 needed, loses no coverage): all five `js_tenant_*` directive
   setters now log a **config-time deprecation warning** pointing to the host-JS replacement
   (`js_tenant_source`→`comcon.tenant`, `_mode`→`comcon.mode`, `_dependency`→`comcon.dependency`,
   `_artifact`→`comcon.artifact`, `_handler`→`location.handler = comcon.include(...)`), while
   staying fully functional (thin sugar over the same `jcf` fields). Safe because Test::Nginx's
   "no alerts" only matches `[alert]`, not `[warn]` (verified — every tenant test stays green).
   `t/comcon_deprecation.t` asserts the warnings fire and the directives still serve. **Removal
   sequence (post-P5):** (i) migrate `faithfulness`/`lowering`/`schema_conformance`/remaining
   originals off the directives onto `comcon.tenant()` (+ delete the redundant originals covered by
   `comcon_include_*` siblings); (ii) once nothing but the (now include-lowered) compiled tier uses
   them, delete the five directives + the `tenant_ctx`/`onRequest`/`tenant_request_handler`
   machinery. This is the payoff: one mechanism.

## 5. Open decisions (with recommendations)

- **Mode granularity (G4):** keep **global** `comcon.mode()`. The shared compartment makes
  per-fragment mode a larger change with little near-term value; revisit if/when fragments from
  mutually-distrusting authors must run under different modes in one worker.
- **Compiled tier (G6/P5):** treat P5 as a **separate gated increment**, not part of the first
  convergence pass. Landing P1–P4 already unifies the *interpreted* path and lets us retire 4 of 5
  directives' machinery; the compiled tier is the one feature that genuinely only exists on the
  tenant path and carries the SR-2 faithfulness gate.
- **Keep the tenant path during migration:** yes — do not remove anything until P4+P5 are green.
  A/B coexist behind identical behavior (the directives already forward to the same fields).
- **`admit` vs `include` composition (G1):** `include` should *call* the existing `admit` C
  helpers rather than duplicate them, so there is one C3 implementation.

## 6. Non-goals / risks

- **Not** changing the confinement model (reach gates, M-SES lockdown, gas) — only unifying the
  *entry* + *handler* path onto `include`.
- **Risk:** the compiled tier (G6) is the tall pole; if it proves too coupled, the fallback is to
  keep the tenant path *solely* as the compiled-tier backend and route everything else through
  `include`, deferring full removal.
- **Risk:** test migration (G7) is broad; mitigate by porting one `comcon_*.t` at a time with both
  paths green.

## 7. Definition of done

Every confined fragment — request handler or config fragment — is a `comcon.include(...)` bound
through `location.handler` (or another COM setter); the `js_tenant_*` directives and the
`tenant_ctx`/`onRequest` subsystem are deleted; the full `comcon_*.t` suite runs on the one
mechanism on both builds (interpreted + JIT); SR-2 faithfulness holds for the retargeted compiled
tier.

## § P5 — compiled-tier retarget to `include` (scope)

**Status:** ✅ LANDED (2026-09-02). The tall pole is cleared. P5.1 = the one `aot_compile` call;
P5.2 = `t/comcon_include_faithfulness.t` (22 tests, both builds); P5.3 = `comcon_*.t` green on
`objs_jit` (45/335). **R1 (cap-closure lowering) resolved positively** — granted fragments compile.
The scope below is retained as the record.

### Goal
Lower `include` fragments to native C on the `objs_jit` build at SR-2 faithfulness parity, so the
compiled tier no longer depends on the tenant `onRequest` handler. Then P6 can delete the tenant
subsystem, leaving one mechanism on both tiers.

### Why the code change is small
The engine already lowers **any** function: `int js_comcon_aot_compile(JSContext *ctx, JSValueConst
func)` (quickjs.h:855). The tenant path calls it once at load on `jcf->tenant_request_handler`
(`ngx_js_module.c` C5.0-b, `#ifdef CONFIG_JIT`); the interpreter then dispatches `JS_Call` to the
installed `jit_func` transparently. So the retarget is: in `ngx_js_comcon_include_confined`, after
the fragment `fn` is compiled + admitted, under `#ifdef CONFIG_JIT` call
`js_comcon_aot_compile(sctx, fn)`. `ngx_js_comcon_invoke_confined`'s existing `JS_Call` then runs
the compiled code. **Confinement is preserved by construction** — the compiled code calls the same
gated host C functions under the same `ngx_js_compartment_enter(TENANT)` the invoke already sets,
identical to the tenant lowering; the getter-materialize-under-TENANT fix (§SR-1) covers both tiers.

### Phases
- **P5.1 — lower.** The one `js_comcon_aot_compile(sctx, fn)` call in `__includeConfined` (JIT only).
  Log a NOTICE on success (as the tenant does). Best-effort: on failure the fragment runs
  interpreted (maxim skips-to-interpreter).
- **P5.2 — SR-2 faithfulness for include.** `t/comcon_include_faithfulness.t` running BOTH builds
  (`objs/nginx` interpreted, `objs_jit/nginx` compiled), asserting **identical responses AND denial
  counters** across the confinement surface via `include + location.handler`: report/compute,
  request reads, response shapes, granted-socket scalar reads, gated reach (`.listener`), gated
  mutators (`close`), mediate field-masks + route facets, deps. Mirrors `comcon_faithfulness.t`.
  Assert `all_compiled` (the gate is non-vacuous — every fragment actually lowered).
- **P5.3 — dual-build regression.** The whole `comcon_*.t` (esp. the `comcon_include_*` suite) green
  on `objs_jit` as well as `objs`; full `t/` green on both.

### Risks
- **R1 (closure lowering) — the main unknown.** An include fragment is a CLOSURE: the inner
  `function(req){…}` is closed over the wrapper's grant/dep params
  (`(function(<names>){"use strict";return(<src>);})`). Must confirm maxim's AOT lowers this shape
  (closed-over `var_ref`s) rather than skipping-to-interpreter — else P5.2's `all_compiled` is
  vacuous. Mitigations if it can't: lower grant-free fragments first; or hoist grants differently;
  or accept interpreter fallback for cap-bearing fragments and scope the gate to what compiles.
- **R2 (differential).** Compiled ≡ interpreted for grants/facets/deps/reach/gas — same C funcs, so
  expected to hold; P5.2 proves it.
- **R3 (marshaling boundary).** The compiled fragment returns a JSValue → `JS_JSONStringify` host-
  side; faithfulness is on the marshaled output. Getters materialize under TENANT (already fixed).
- **R4 (gas/back-edge).** Compiled code uses maxim back-edge gas under the same
  `request_deadline_ms` meter; parity with the interpreted interrupt gas.

### Gate & non-goals
Gate: SR-2 (compiled ≡ interpreted responses + denials) on both builds, `all_compiled` non-vacuous.
Non-goals: not removing the tenant lowering (that is P6 step ii); not changing the confinement model.

### Effort
Code: **small** (one call + a NOTICE). Validation: **medium** (dual-build differential suite).
Risk: **medium–high**, concentrated in R1 (whether maxim lowers the cap-closure fragment shape).
