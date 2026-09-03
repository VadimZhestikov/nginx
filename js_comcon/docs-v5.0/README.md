# COMCON documentation, v5.0

> **This set is the single normative spec** (user decision, 2026-08-23 — E6). It is
> revised **in place**: each revision adds a vN.M entry to FOUNDATION's delta log
> (current: **v5.45** — **increment D5a: call-site audit.** `node.references(name)` / `node.callsites(name)` enumerate every reference to a free name or method `name` in a fragment (with line numbers) and the subset that are actual call sites — the intensional **audit** side of SHOWCASE §38. Bytecode scan, **no parser** (`js_comcon_pom_callsites`); callee↔call correlation is exact via operand-stack tracking, so `fetch(helper(2))` is still attributed to `fetch`. Records `{name,line,method,call}`. Reframing: §38's *enforcement* is already the capability kernel's (`grant(env,"fetch",mediate(cap,guard))`); D5a is the *audit* half → "audit + enforce" with no parser. Full-CST source-rewrite hardening (D5b) stays deferred. `t/comcon_pom_callsites.t`. v5.44 — **increment D4b: class-F multi-worker fan-out.** `comcon.bindShared(key, quotation, contract, onRequest)` — the multi-worker spelling of `bindAt`. The current `{epoch, source}` is the single source of truth in `nginx.shared` (lock-free, visible to every worker); each worker's handler **reconciles lazily** per request — reads the shared epoch and, if newer, recompiles the shared source in its own compartment and swaps (rebuild-on-write per worker). So a `replace()` in one worker fans out to all coherently — no torn state; only the source string crosses. Lazy-pull over eager push; reuses `nginx.shared` (no new broadcast). `t/comcon_pom_fanout.t` (4 workers: all-v1 before, all-v2 after one replace). v5.43 — **increment D4a: POM mutation = rebuild-on-write + epochs.** `comcon.bindAt(site, quotation, contract)` installs an admitted quotation at a live **site** and returns a frozen **epoch handle**: `replace(q)` recompiles a new epoch + swaps the site (retaining the prior), `rollback()` restores exactly, `remove()`/`revive()` tombstone; `describe()` classifies ops R/L/F/X. The site wires to the existing `loc.handler` setter (no parallel install path). Bounded rollback history; superseded fragments freed (`__freeConfined`) so live rewrite doesn't accumulate. Fixed a pre-existing pool-lifetime bug that request-time replace exposed (dedicated `comcon_frags_pool`). `t/comcon_pom_mutate.t` (12, incl. 500 cycles flat). Reuse: install/tombstone/fan-out/rollback already in js_com; D4b (multi-worker fan-out) + D4c (compiled-tier re-AOT) pending. v5.42 — **increment D3: POM-node quotations + stone splices.** `comcon.quote(source, splices?)` accepts producer **splices** — deep-checked **stone** (cap-free: no functions/caps/accessors; a stage-0 error at the producer). `realize` binds each as a **JSON literal in an enclosing IIFE var**, so the quoted code sees escaped *data*, never text — a spliced string can't smuggle code (JSON.stringify escaping = parameterized-SQL defense), and splice names are bound closure vars (invisible to the admit gate). A POM node's `quote()` is now **realizable** — `realize(node.quote(), K, env)` compiles + runs a real subtree. Pure-JS, no engine change. `t/comcon_pom_splice.t`. v5.41 — **increment D2: the POM selector language.** `node.query(sel)` over the NodeView subtree: `selector := term ('within' term)*`, `term := factor+` (AND), `factor := module|function|*|name(glob)`, glob = exact/prefix/suffix/contains. `A within B` = nodes matching A with an ancestor matching B (intensional composition — the hardening pattern). Value-level DSL under the handle (no new host grammar); results are NodeViews (a query result is quote()-able); **born-bound (R9)** — recomputed live per call, so D4 governs newly-admitted matches at admission. Pure-JS, no engine change. `t/comcon_pom_query.t`; POM.md §6 Q2 resolved. v5.40 — **increment D1: the lazy NodeView surface.** `comcon.pom(fragment)` → a reflective `NodeView` tree (module/function granularity): `kind/id/hash/span/childCount/name`, lazy `children`/`parent`, methods `text()/quote()/describe()`, redacted `binding`, frozen nodes. Load-bearing: **reads return quotations** — `text()`/`quote()` return `comcon.quote()` values (cap-free), never raw source (SEMANTICS REFLECT). `describe()` lists read ops with class `R`. Creation-ordered stable `id`s; content `hash` (R7 pin). One GC-safe C accessor `js_comcon_pom_node_at` (no held pointers; root kept alive by the JS closure) → flat navigation (`t_stress/com_pom_navigate.t`). `t/comcon_pom_nodeview.t`. v5.39 — **increment D0: POM substrate.** The reflective Program Object Model gets its foundation: each compiled fragment's bytecode carries its own source slice + pc→line + nested-function cpool constants, so the granularity floor (module + function) is materialized from the bytecode tree with NO parser (`js_comcon_pom_inspect`). p_symbol enumeration fixed as schema `comcon-pom-1` (module=1,function=2,block=3,stmt=4,expr=5; append-only); block/stmt/expr synthesized later (D5 CST). Content hash = FNV-1a over source (R7 pin). Diagnostic bridge `comcon.__pomInspect`; lazy NodeView is D1. `t/comcon_pom_substrate.t`; scope in `INCREMENT_D.md`. v5.38 — **realize + quote resolved:** the quotation half of closure-vs-quotation ships. `comcon.quote(source)` = an inert, frozen, cap-free description (zero authority); `comcon.realize(q,contract,realizerEnv)` gives it force under the REALIZER's authority (the operator-realizes-a-tenant-proposal path, showcases 46–47) — distinct from bind/include (producer's env). Least-authority (R6) enforced: mandatory contract + realization env = realizer grants RESTRICTED to the declared free-name manifest (`ρ_R ↾ imports`) → confused-deputy fix; realize refuses a closure as arg0. `t/comcon_realize.t`. `includeAt` deferred by the reuse-the-primitive fundament (concrete-node includeAt = `loc.handler=include(...)`; only query-targeting is new, awaits POM); structured splices + POM-node quotations await POM (increment D). v5.37 **bind resolved:** `bind(env,source,opts)` is now the env-first spelling of include — compiles the source in the confined compartment under the env (grants/imports/meter/tests/identity), the real kernel bind; `bind(grant(env(),"x",cap),src,{meter})` ≡ `include(src,{grants:{x:cap},meter})`; dead __runMetered removed; `t/comcon_operators.t`. v5.36 **admit test-phase (behavioral admission):** `include(source,{tests})` runs the contract's `function(fragment){…}` tests against the compiled fragment IN the confined compartment (zero blast radius — no host authority, IO denied), refusing admission if any test throws; `t/comcon_admit_tests.t`. Remaining: clock/RNG doubles during the run. v5.35 **CONVERGENCE COMPLETE (INCREMENT_CONVERGE.md): ONE confined mechanism.** The five `js_tenant_*` directives + the whole tenant compartment subsystem (eval_tenant_sources / onRequest / js_tenant_handler / grantToTenant / tenant_ctx-rt) are REMOVED; every confined fragment is now `comcon.include(source, contract)` bound via the EXISTING `location.handler` — the shell fundament (Principle 11 "extend by granting, never by syntax") realized: no js_tenant_* directives, one recursive primitive on BOTH tiers. Full parity migrated onto include (admission+identity, deps, live-cap grants, learn mode, the M-SES lockdown, the compiled tier). comcon 21/159 include-only, t/ 266/3412, t_stress 16/80; commits 38ab240ca..cda23134b.
> **v5.34 compiled include tier (CONVERGE P5 = SR-2 for include):** `js_comcon_aot_compile(comcon_ctx, fn)` lowers an include fragment to native C on objs_jit — R1 (does maxim lower the CAP-CLOSURE shape, a fragment closed over granted-socket/dep params?) resolved POSITIVELY. `t/comcon_include_faithfulness.t` runs BOTH builds: identical responses AND denial counters over the confinement surface (string/JSON/object/compute, granted-socket scalar reads, A1 gated reach .listener, gated mutator close()), all_compiled non-vacuous; the compiled tier no longer depends on the tenant onRequest path. comcon green on objs_jit (45/335).
> **v5.33 include interpreted-parity (CONVERGE P1–P4):** include composes the C3 gate (`ngx_js_comcon_admit_check` shared with the `admit` operator) when `contract.imports` present + an optional identity pin `H(H(source)‖schema)` via `contract.identity`; `contract.deps=[{name,path,sha256}]` loads pinned pure libraries as per-fragment closure params (`ngx_js_comcon_eval_dep`); learn-mode recorder seeding added to the include compartment (checks jcf->tenant_mode, not the process-global, since the compartment is built during host eval before policy_init); the request/response "serve" helper resolved as a NON-GAP (location.handler + req.respond already suffice — [[feedback-reuse-jscom-primitive]]). Tenant tests migrated to `comcon_include_*` siblings. `t/comcon_include_admit.t / _deps.t / _learn.t / _request.t / _headers.t / _deny.t / _denial_log.t / _mses.t / _freeze.t / _sr1.t / _audit.t / _teardown.t`.
> **v5.32 mediate() membrane + COM-node facets:** `mediate(cap, interceptor)` enforced as an attenuation-only membrane. Socket: per-wrapper field mask (`comcon.revoke/redact/allow` → bit-mask in `ngx_js_socket_wrap_masked`, redacted field reads undefined). COM node: a `NginxComFacet` (`comcon.routes(glob)`) that BORROWS the ONE canonical server op (a stateful server node is never re-wrapped cross-runtime — that would diverge the per-wrapper prefix_locs/dyn_pool + UAF at teardown) and gates `paths()`/`allowed()`/`addLocation`/`removeLocation` to the route glob. `t/comcon_mediate.t`, `comcon_com_facet.t`, `comcon_com_facet_mutate.t`.
> **v5.31 live-cap grants for include + M-SES-1b + 3 response-path security fixes:** `include(src,{grants:{name:sock}})` re-wraps a granted socket compartment-native (a fresh wrapper around the C handle, reach-gated) as a closure param; the invoke runs under `ngx_js_compartment_enter(TENANT)`. **M-SES-1b** cap-proto non-extensibility (`ngx_js_comcon_harden_cap_protos`, JS_PreventExtensions on socket-family + facet protos). **Security (all in the shared req.respond / include-invoke paths → protect EVERY js_com handler):** (1) response-header CRLF injection dropped (`ngx_js_header_has_crlf`); (2) response-framing smuggling — handler-set content-length/transfer-encoding/connection dropped (`ngx_js_header_is_framing`), no duplicate Content-Length; (3) HIGH — a reach attempt hidden in a return-value getter fired as HOST_ROOT because the result was JSON-materialized AFTER compartment_leave; fixed by materializing under TENANT. `t/comcon_include_grant.t`, `comcon_include_sr1.t`. (Note: TM-1 denial-log quotas+sampling is IMPLEMENTED — `comcon_include_denial_log.t`.)
> **v5.30 comcon.include (scope-isolated confined fragments):** a fragment compiled in its OWN runtime compartment (`jcf->comcon_rt`, mirroring the tenant compartment — curated intrinsics + `ngx_js_tenant_lockdown` + COM protos), held C-side (`jcf->comcon_frags`), invoked IN-compartment with arg/result JSON-marshaled (only strings cross → no JSValue crosses the realm/runtime); authority isolation (typeof nginx→undefined) + metered abort + clean own-runtime `JS_FreeRuntime` teardown. Own runtime after two reverted attempts (cross-realm crash; shared-runtime leak); `jcf` cached in a module-static because ngx_cycle isn't the current cycle at init_conf. `t/comcon_include.t`.
> **v5.29 M-CFG kernel operators + scope (INCREMENT_MCFG.md, OPERATOR_API §8):** `comcon.{env,grant,mediate,bind,admit,include,meter}` realized as GRANTED NAMES on the host `comcon` object (FOUNDATION §4's four operators; `include = parse∘admit∘bind`); `admit` reuses the C3 gate; `bind`/`meter` tighten the worker gas deadline. The shell fundament recorded: pilgrim = a non-invasive shell around an EXISTING nginx whose role is DYNAMIC config via the live COM; `nginx.conf` stays untouched save `js_source`; **NEVER add nginx directives** — the `js_tenant_*` directives violate this and are retired for host-JS operators; timeout = `meter` mediation, not a directive. §8 decisions resolved (imported comcon module, root cap set, meter units=timeoutMs now/gas later, closure default, L-rights init-time scope). `t/comcon_admit.t`, `comcon_operators.t`.
> v5.28 SR-3 escape-completeness audit PASSED — the adversarial pentest of the hardened tenant context found NO sandbox escape (dynamic-code routes tamed, no global reach, core+iterator+shared-generator prototypes frozen, recursion bounded); one MEDIUM freeze-completeness gap fixed (SR3-1: sibling String/Map/Set/RegExp-string iterator instance-prototypes were reachable only by calling a method so the value-walk missed them → cross-request pollution; added as explicit harden roots; generator per-function .prototype is isolated + shared %GeneratorPrototype% already frozen), one availability case gas-contained (SR3-2: Promise microtask loop bounded by the ~1s per-request budget); re-audited clean on both tiers; t/comcon_freeze.t +2 SR-3 cases, comcon 22/187 both builds; confined-tier confinement now adversarially validated, full-test262-under-AOT untrusted-native gated on maxim finalization only; v5.27 gas — per-request execution-time budget on BOTH tiers: a tenant while(true) no longer hangs a worker (interrupt handler wired onto the tenant runtime + host deadline NGX_JS_TENANT_TIMEOUT_MS default 1s; JIT emits BACK-EDGE gas on backward gotos, inline-counter-gated → negligible perf cost, compiled compute still ~75K); memory already bounded (64MB); interpreted+compiled infinite loops both interrupted; t/comcon_gas.t, comcon 22/184 both builds; fuller metered budget model + js_tenant_timeout directive = S5; v5.26 M-SES-1 intrinsic freezing — the tenant lockdown transitively Object.freezes the intrinsic graph (constructors/prototypes/methods off globalThis; globalThis stays extensible for caps), closing a demonstrated cross-request prototype-pollution leak (Object.prototype.x set in req1 was visible in req2; now the write throws + never persists); ordinary JS unaffected; t/comcon_freeze.t, comcon 21/180 both builds; v5.25 C7 = M8 = SR-2 the compiler-faithfulness gate PASSED (profile-scoped):
> t/comcon_faithfulness.t runs a suite over the confinement surface (incl. A1 gated reach
> .listener + gated mutator close()) interp-vs-AOT-compiled, asserting identical responses AND
> identical denials — confinement provably survives compilation, 22/22; scope = confined
> strict-module profile, untrusted-native production still gated on M-SES + full maxim
> finalization; the compiled tier is now erasure(C5.0)+perf(C6)+faithful(C7); v5.24, C6 benchmark — interpreted vs AOT-compiled confined handler under load
> (t_performance/comcon_c6): ~13.5× on compute-heavy JS (hot typed-int loop 5.3K→72K req/s),
> tied on I/O/builtin-bound handlers (~186K both); matches the tier's design profile,
> confinement identical in both tiers (erasure); v5.23, C5.0 DONE — the compiled confined tier: a confined tenant handler is
> server-AOT-compiled to native C at load (js_comcon_aot_compile: js_jit_compile_all→drain→
> install; CONFIG_JIT objs_jit build) and the differential test t/comcon_lowering.t proves
> BYTE-IDENTICAL responses AND identical denial counters interp-vs-compiled (erasure
> soundness on a real fragment); confinement preserved by construction; full comcon green on
> both builds (18/148); FIXED a pre-existing single-process-mode teardown crash (both builds; multi-process was
> clean); v5.22, C5 SCOPED (INCREMENT_C5.md) — lowering the compiled confined tier:
> confinement is preserved by construction (it lives in the host boundary + gated callees,
> not the handler bytecode), so the MVP compiles the handler via maxim server-AOT
> (js_jit_compile_all→drain→install, no background thread → sidesteps C6-lite) + a
> differential test asserting identical responses AND denial counters; gas + two-clocks
> revocation deferred (no budget/epoch machinery exists yet, interpreted or compiled);
> v5.21, maxim finalization go/no-go EXECUTED — Bucket-1 closure var_ref crash
> FIXED (maxim b07ca5d) + T0 measurement harness built (73bba6d); with the fix the common
> JIT surface is 0-new and an in-profile strict handler ran 200k clean incl. server-AOT
> cache-load; residual delta is small + OUT-OF-PROFILE (sloppy-mode `this`, a species/
> resizable cache-load crash, a BigInt edge) → COMCON profile-scoped gate MET, C5 UNBLOCKED,
> residuals to a parallel maxim full-suite track; v5.20 maxim finalization SCOPED (INCREMENT_MAXIM.md) — the JIT correctness
> gate for the compiled tier: JIT compiles nearly everything (bails only on eval), so the
> ~200 test262 failures are MISCOMPILES concentrated at the JIT-closure/callback-from-builtin
> boundary (Bucket 1 = var_ref refcount-underflow crashes, Bucket 2 = callback this/arg
> marshaling), not missing coverage; done = errors(JIT) ⊆ interp 72; M8 gate adopted
> PROFILE-SCOPED (Buckets 1-2 in-profile must-fix, Atomics/DataView/Promise deferred);
> proceeding via a bounded T0-measurement + Bucket-1 root-cause go/no-go spike; v5.19 M-SES-0 dynamic-code lockdown — tenant context is now
> JS_NewContextRaw + curated intrinsics (Proxy omitted) + an SES-style lockdown that tames
> the Function/generator/async constructors and deletes the eval/Function/Reflect globals;
> closes front-end audit A1 so C3-rest's "no dynamic code" is now SOUND (prerequisite for
> C5 erasure); Eval intrinsic stays (module compiler), only the eval global removed;
> t/comcon_mses.t + INCREMENT_MSES.md; v5.18 the front-end soundness audit — adversarial audit of the C
> admission front-end: CONFINEMENT HELD (no capability escaped — dynamic code + globalThis
> reach only the deny-by-default global), but C3-rest does NOT eliminate dynamic code
> (`[].constructor.constructor` etc.), so M-SES (curated intrinsics) is promoted to a hard
> prerequisite for C5 erasure soundness; fix: reflective globals globalThis/global/self
> refused; t/comcon_frontend_audit.t; v5.17 C4 the fragment artifact ("fat bytecode") — after the C3 checks
> pass the admitted fragment gets a content-addressed identity H(H(source)‖schema-version)
> + an admission certificate (env-signature size + which C3 checks cleared), logged at
> load; the `js_tenant_artifact <hex>` directive pins the identity so content drift OR
> schema drift refuses the config (generalizes B/E1 pin-by-hash from a dependency file to
> the whole fragment); no lowering yet — the artifact is the T1 record C5 lowers;
> t/comcon_artifact.t; v5.16 C3-types type-checking the tenant against the C2 schema — the
> env.onRequest signature (handler is (Request)=>Response: a function of ≤1 param,
> registered exactly once) enforced at registration, and the sealed types.Request
> (a direct read of a non-schema field on the handler's Request parameter refused:
> `js_comcon_check_request_fields` bytecode scan; sound rejecter — interprocedural /
> Response / Socket typing deferred to C5); recovered two tests that had silently
> skipped since C3.0 (comcon_tenant_request, comcon_dependency — obsolete `typeof nginx`
> probes); t/comcon_types.t; v5.15 C3-rest the restricted-construct admission check (dynamic code —
> `eval`/`Function`/`with` — refused at load so the C3.0 free-name analysis is sound:
> `js_comcon_uses_dynamic_code` bytecode scan + an `eval`/`Function` name deny-list;
> t/comcon_restricted.t); v5.14 C3.0 static free-name admission check (js_comcon_collect_free_globals
> + the admission gate; ungranted refs refused at load); v5.13 C2 the typed tenant-env schema (SCHEMA.md +
> schema/tenant-env.schema.json, grounded); C1 M-UNIFY done (pilgrim on maxim, T1 green);
> v5.12 C0 gate PASSED + C1.0 M-UNIFY analysis (INCREMENT_C.md §4-5: same Bellard base → clean 3-way merge, benign opcode divergence; the engine merge C1.1-C1.4 is scoped/next) +
> the security-review cadence in VERIFICATION.md (SR-1..SR-4 at inflection points); v5.11 INCREMENT B COMPLETE — the dependency workflow (js_tenant_dependency
> pin-by-hash + pure_library cages); v5.10 B1 generated grant-stub; v5.9 B0 learning mode; v5.8 INCREMENT A COMPLETE — A3.1 + dogfood; v5.7 A4 denial log + audit→enforce; v5.6, the COMCON-lite core BUILT + TESTED through the
> request path; build log INCREMENT_A.md §6, status SPEC.md §13; before it
> v5.5, SPEC.md + the ground-truthed INCREMENT_A.md build plan from the
> nginx reality check; before it v5.4 convergence actions — THREATS.md, two-clocks pin,
> adaptive deferred to M9; v5.3 consistency pass, v5.2 WASM ruling, v5.1 engineering
> review + increment re-cut). Full new directories happen only at genuine architectural
> reframes. Older sets (`docs-v0/`, `docs-v3/`, `docs-v4/`) are history.

*Supersedes `../docs-v3/` (and `../docs-v0/` v2). v3 integrated the 2026-08-18
architecture refinement: the possession kernel, the Program Object Model, the formal
semantics with the No-Amplification theorem, worked authority-traced examples, the
live-mutation safety classes, the M-SES hardening scope — and the first measured
performance gate (M1: hand-C policy at 96% of stock nginx vs 28% interpreted).
**v4 adds the symmetry correction** (rev 3.1, user-spotted): one governed-language
pattern with N instances instead of two mirrored trees; the empty-environment principle
("data is code bound to ∅"); COM gains its missing admission hinge — a typed,
admissible config surface (new work item M-CFG, new scenarios 46–47, new manual §3.6).
**v4.1 (in place) adds the comconctl closure**: there is no management plane —
administration is admitted episodes, tool verbs are `std.ops` library programs, and the
ops-resource capabilities become the third closed enumeration (FOUNDATION §8a,
Principle 10). **v5.0 is the pre-implementation design-review hardening** — twelve
adversarial findings (R1–R12) adopted in whole: stone-based quotation safety,
one-adaptive-per-node composition, revocation/gas/type-boundary fixes for the compiled
tier, least-authority realization, never-unbound nodes, creation-ordered ids, the
born-bound rule, monotone rollout, and the admission front-end named as attack surface
(ROADMAP §11 for the full table).*

## Reading order

| Doc | What it is |
|---|---|
| **SPEC.md** *(the normative read — start here)* | The whole design stated once, cleanly, no revision archaeology: the one axiom, four operators, the tree, the vocabulary, the three axes, tiers/artifact, WASM, administration, the layered core, the honest edges. Normative for *what*; the rest is *why*. |
| **FOUNDATION.md** | The **argued** architecture (rationale, the full vN.M delta log): thesis, principles, the POM, the 4-operator kernel, closure vs quotation, anchors & profiles, enforcement pipeline, run-time object model, multi-language, AI contracts, open questions. Read for *why*. |
| **SEMANTICS.md** | Formal companion: domains, the authority measure, evaluation rules, the No-Amplification theorem + proof sketch, three worked examples with authority traces, and what the formalization itself discovered. |
| **POM.md** | The Program Object Model: node interface, COM→POM mirror table, R/L/F/X mutation safety classes, lifecycle, implementation-reuse plan. |
| **HARDENING.md** | M-SES: the engine-hardening milestone (S1–S6), the gate, scheduling. Unforgeability is the *enforcement mechanism* of the possession axiom, not hygiene. |
| **ROADMAP.md** | Milestones M1 ✅ …M9 (+M2.5, +M-SES) with the measured M1 numbers, delivery staging, and the v3 minimal first slice. |
| **PERFORMANCE.md** | Expected performance impact: the measured gradient endpoints (96% compiled vs 28% interpreted vs stock), the cost model per enforcement moment, v3-specific costs (live-rewrite windows, meets, hashes), risks and the M7 falsification plan. |
| **SHOWCASE.md** (1–7), **SHOWCASE17.md** (8–17), **SHOWCASE37.md** (18–37) | The look & feel scenarios, reworked for v3 (envs/grants, include = parse∘admit∘bind, anchors/queries, guarantees-as-theorems). Illustrative, not normative. |
| **SHOWCASE45.md** (38–45, new) | v3-native scenarios: intensional query hardening, closure vs quotation, pin-by-hash, live-rewrite epochs, the self-auditing plugin (base≡meta), compile-through ("the policy that vanished"), contract admission for AI code, meet-composition. |
| **SHOWCASE47.md** (46–47, new in v4) | The config-instance scenarios: typed tenant config (grammar+types cage over config sentences) and propose-the-config-you-can't-apply (quotation proposals realized by operators). |
| **SHOWCASE49.md** (48–49, new in v4.2) | The typed-profile scenarios: what the 96% tier looks like to write (schema + inference + one JSDoc annotation; type errors as denials; erasure = runs anywhere) and the per-fragment `any` gradient (hybrid tier as a report, not a punishment). |
| **SHOWCASE50.md** (50, new in v5.2) | The border crossing: a foreign (Rust-built) WASM module admitted through the `wasm` facet — validation = admit, imports = grants, fuel = budgets; cold lane (embedded runtime) vs hot lane (wasm2c into the one C funnel); and why our own JS never crosses out (JS→WASM = category error). |
| **SCHEMA.md** (new in v5.13) | C2 — the typed schema a policy is written against (input to C3's type-checker, C5's erasure oracle). The small real tenant environment (report/onRequest/Request/Response/granted Socket) with the V1 numeric discipline; machine form in `../schema/tenant-env.schema.json`, grounded by `t/comcon_schema_conformance.t`. The host COM surface (from `describe()`) is a later extension. |
| **INCREMENT_C.md** (new in v5.12) | The ground-truthed PLAN for increment C (the typed/compiled maxim tier): the reality check on the real maxim tree (same Bellard base ~4% delta, compiler in a separate `quickjs-jit.c`, type-inference + bytecode-hash already present, gcc+tcc available), the erasure-soundness invariant + differential-test discipline, and the risk-ordered C0 (integration spike/gate) → C1 M-UNIFY → C2–C7 sequencing. |
| **INCREMENT_A.md** (new in v5.5) | The first construction plan, ground-truthed against `nginx/src/js/`: what the reality check confirmed (single load point, favourable QuickJS compartment factoring, extensible describe registry), the real work in leverage order (owner-field the global handle registries; the four omnipotent members; two confinement bugs), and the A0–A4 task order with file:line anchors. |
| **THREATS.md** (new in v5.4) | The threat model: 12 adversaries × assets × mitigations, every cell citing its closing mechanism; three residuals accepted by name (engine memory safety, IFC/side channels, availability-within-reach); the completeness ledger and the V15 assurance-case skeleton. Found TM-1 (denial-log quotas) and TM-2 (session identity → env mapping). |
| **VERIFICATION.md** (new in v5.0) | The verification track V1–V15: what would convince a skeptic of each claim — the numeric-model and schema-pinning decisions (V1/V2), executable reference semantics, monotonicity-as-assertion, translation validation, generated enumerations, schema conformance tests, the TLA+ epoch model, policy mutation testing, reproducible builds, and the assurance case. |
| **MANUAL.md** (draft) | The user's manual, written as-if-shipped (working-backwards artifact): the five-minute mental model, quick start, tenant / policy-author / operations handbooks (v4: §3.6 writing config, proposals), performance guide, reference (verbs, denial anatomy, glossary) — and Appendix B, the [TBD] harvest of decisions it forced into the open. |

## One paragraph

COMCON runs JavaScript from parties that do not trust each other — tenants, vendors,
AI generators — inside one server, each fragment caged by a policy the host attaches.
v3's form: one **possession axiom** (no operation mints authority), **four operators**
(grant / mediate / bind / admit) over two unforgeable resource kinds (host capabilities
and program-tree handles), policies as first-class values in exactly two modes
(**closure** = carries authority, bounded by its producer; **quotation** = describes
it, bounded by its realizer), attached by query or inert anchor so the target program
always remains plain runnable JavaScript — and the whole discipline **compiles away**:
a fully-typed static policy lowers (via maxim) to C that runs at ~96% of stock nginx
with no runtime policy interpreter, while remaining exactly as confined as the
interpreted form.

## Status

Design documentation, with **a substantial build shipped**. Increments A/B/C are done
(COMCON-lite core — compartment identity, deny-by-default environments, grants,
reach-cycle gates, denial log + audit/learn/enforce; the typed admission front-end;
the compiled tier C5–C7 with SR-2 faithfulness), the confined tier is adversarially
validated (SR-1/SR-2/SR-3 passed, M-SES-0/1/1b), and the **M-CFG operator kernel +
CONVERGENCE are complete (v5.29–v5.35)**: FOUNDATION §4's operators are realized as the
granted `comcon.{env,grant,mediate,bind,admit,include,mode}` names, and there is now
**one confined-fragment mechanism** — `comcon.include(...)` bound via `location.handler`,
on both the interpreted and AOT tiers. **The `js_tenant_*` directives and the tenant
compartment subsystem have been removed** (Principle 11); a reader should ignore the
older `js_tenant_*` / `onRequest` / `comcon_load` / `comconctl` surfaces in the
increment/manual docs — the live surface is `js_source root.js;` + the `comcon` operators
+ `location.handler` (`OPERATOR_API.md`, `INCREMENT_MCFG.md`, `INCREMENT_CONVERGE.md`).
Design-only edges remain: **POM nodes (increment D)** — parsed-subtree quotations,
structured splices, `query()`/anchor targeting, and live rewrite/epochs. (`includeAt` is
**not** a separate edge: a concrete-node `includeAt` is just `loc.handler=include(...)`, and
its only non-redundant forms — anchor-splice and query-targeting — are exactly this POM
targeting; there is nothing left to build *called* `includeAt`.) Also:
`mediate` `rateLimit`/`transform`/`audit` flavors, `admit`'s test-phase
under determinism caps, the full M2 typed schema, WASM ingestion, and adaptive profiles
(M9). Tests `t/comcon_*` (interpreter default `objs`; JIT `objs_jit`). The M1 spike lives
in `t_performance/maxim_m1/`. The showcase scenarios remain illustrative/hypothetical by
design. Running decision log = project memory `comcon-architecture-refinement`.
