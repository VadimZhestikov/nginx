# Increment C — the typed/compiled tier (plan)

*The ground-truthed build plan for increment C, written in the style of
`INCREMENT_A.md` (reality check → risk-ordered sequencing → what-changes) but for
a categorically larger effort: the maxim JS→C compiler. Nothing here is built yet.
Reality check performed 2026-08-31 against the actual maxim tree
(`/home/vadim/fixes.github/quickjs-and-tcc/quickjs`) and the vendored engine
(`../quickjs/`).*

---

## 0. The reframe that makes C safe to attempt

Increments A and B delivered **working, tested confinement + onboarding on the
interpreted tier (T1)**. Increment C adds the **compiled tier (T2)** for the ~96%
performance the M1 spike measured — and it must change **only how fast, never what
is allowed**. The security is already done and inherited; C is a performance layer
over it.

The one load-bearing invariant, and the discipline that keeps C honest:
**erasure soundness** — a compiled fragment must behave identically to its
interpreted self with types ignored (types only *reject* at admission and
*accelerate* at runtime). So **every C slice ships with a differential test**: run
the same tenant policy interpreted and compiled, assert identical outputs *and
identical denials*. That test is how the A/B security work is preserved through
compilation rather than re-implemented — and it is the M8 gate in miniature.

## 1. What the reality check found (the risk is smaller than "two forks")

- **maxim shares the vendored engine's base.** Both are Bellard 2017-2025 QuickJS.
  `quickjs.c` differs by ~2.4k lines (~4%), not a rewrite.
- **The compiler is a separate translation unit.** maxim's codegen is
  `quickjs-jit.{c,h}` (~9.5k + 1.3k lines), `#include`d by `qjs.c`/`qjsc.c`/
  `quickjs.c`; the intermingling is ~629 `jit` hook references in `quickjs.c`, most
  under a build flag. This is the single biggest favourable finding — M-UNIFY is
  "vendored Bellard + maxim's additions," not "reconcile two aliens."
- **maxim already has the pieces C needs.** `jit_infer_types()` (type inference),
  `jit_hash_bytecode()`/`jit_hash_function()` (the fragment artifact's content
  hash — the same primitive B/E1's pin-by-hash uses), a JIT cache, `jit_rt_*`
  runtime helpers, per-function `.so` emission via GCC/TCC. v8bench passes
  (Richards…Splay, correct).
- **The toolchain is present.** `gcc` 11.4 and `tcc` (`/usr/local/bin/tcc`) — maxim
  uses TCC for fast compiles, GCC for hot ones (`CONFIG_JIT=y`, a JIT threshold).
- **M1 was hand-C, not maxim.** `t_performance/maxim_m1/` hand-wrote the C a
  compiled policy *would* become (96% of stock). So the entire maxim→nginx pipeline
  is ahead of us; M1 only proved the payoff is worth building it.
- **maxim is not finished.** It still fails ~200 test262 tests and needs a
  finalization pass. This is **non-blocking for C0–C6** (a dev tier compiling
  trusted fragments, where the interpreted tier is always the correct fallback) but
  **blocking for C7 and for any compiled *untrusted* tenant** — both need semantics
  trustworthy across the full language. See §3.

## 2. Risk-ordered sequencing (spike first, like M1 was a gate)

The biggest unknowns are front-loaded. Each slice is a differential-tested vertical.

- **C0 — the integration spike (a GATE, cheap, high-information).** Before merging
  anything: (a) build maxim standalone here (`make CONFIG_JIT=y`) and confirm it
  emits a callable `.so` for a function + passes its own tests; (b) take *one*
  trivial tenant handler (`onRequest(req => "hi")`), drive it through maxim's AOT to
  a `.so`, and load+call that `.so` from a tiny standalone C harness (not nginx
  yet). **Gate:** if a trivial fragment cannot be compiled and called, stop and
  reassess the whole increment. Analogous to M1 being a payoff gate; this is a
  feasibility gate.

- **C1 — M-UNIFY: one engine tree (E5).** Bring `quickjs-jit.{c,h}` into the
  vendored `quickjs/`; reconcile the ~4% `quickjs.c` delta as `CONFIG_JIT`-guarded
  hooks; add a `CONFIG_JIT=y` variant to pilgrim's build. **The critical
  correctness point:** the fat-bytecode artifact requires T1 bytecode to be *exactly*
  what T2 consumes — so the opcode/bytecode definitions of the merged tree must be
  the single source of truth for both tiers. **Deliverable:** pilgrim builds with
  the JIT available; JIT-off is byte-identical behaviour (all existing `t/` +
  `comcon_*` green), JIT-on is same-semantics (differential green).

- **C2 — M2: the typed host-API schema (fused with S4).** Machine-readable type
  signatures for the policy-visible surface. **Grounded in what A/B actually
  expose** — start with the tiny real tenant surface: `onRequest(fn)`, `report`,
  `req.{method,uri,args,headers}`, the `{status,body,headers}` return contract, and
  granted sockets — not the whole COM. Add the read-only-getter rows (the v5.5
  finding), the ms-not-ns numeric rule (V1). Data-only extension of the existing
  `describe` registry (`type` column already present).

- **C3 — M3: the typed-profile front-end.** Restricted parser (Misty-core subset) +
  the **static** capability check (every free name must resolve in the bound
  environment — A/B enforce this at *runtime*; C3 makes it an admission-time
  rejection) + erasure-sound JSDoc annotations. **Deliverable:** a typed tenant
  fragment is type-checked against the C2 schema at load (rejected on mismatch) and
  runs identically interpreted.

  **Landed so far (built + tested):**
  - **C3.0 — static free-name admission.** `js_comcon_collect_free_globals`
    (quickjs.c) recursively walks the registered handler's `closure_var` globals
    (into nested `cpool` functions); `ngx_js_module.c` refuses the fragment at load
    if any free name is absent from the bound tenant environment. *(2026-09-12: a
    free name now falls in one of THREE categories — DENIED (`eval`, `Function`,
    `globalThis`, `global`, `self`; no manifest re-admits them), INTRINSIC (a short
    list of language values, admitted without declaration — NOT `Date`/`Math`,
    whose clock and RNG stay declarable), or DECLARABLE (every host name). See
    VERIFICATION.md V3 and AUDIT_M-SES.md §6.)* The A/B runtime
    deny-by-default becomes an admission-time refusal. Test: `t/comcon_admission.t`.
  - **C3-rest — restricted constructs.** Dynamic code in its naive forms is refused at
    load: `js_comcon_uses_dynamic_code` (quickjs.c) scans the handler's bytecode for
    direct `eval`/`with` (OP_eval/OP_apply_eval/OP_with_*, recursing), and an `eval`/
    `Function` *name* deny-list catches indirect references; reflective global aliases
    (`globalThis`/`global`/`self`) are refused too. (`with` is also a strict-mode syntax
    error in the tenant module.) Test: `t/comcon_restricted.t`. **Made sound by M-SES-0
    (v5.19):** on its own C3-rest stops only the naive forms — dynamic code was still
    reachable via `[].constructor.constructor`, the async/generator constructors and
    `Reflect.construct` (front-end audit A1). M-SES-0's curated-intrinsics + lockdown
    (INCREMENT_MSES.md) neutralizes those, so "no dynamic code" now holds and the free-name
    manifest is a complete over-approximation.
  - **C3-types — type-checking against the C2 schema (first slice).** The front-end now
    checks *types*, not only names/constructs. Two schema contracts, each soundly
    decidable at admission: **(a) the `env.onRequest` signature** `(Request) => Response`
    — enforced at registration (`ngx_js_tenant_onrequest`): the handler is a function of
    ≤1 parameter, registered exactly once; **(b) the sealed `types.Request`** —
    `js_comcon_check_request_fields` (quickjs.c) refuses a *direct* read of a field the
    Request type does not declare (method/uri/args/headers). A **sound rejecter** (fires
    only where the base is provably the handler's `arg0`; no false positives). Test:
    `t/comcon_types.t`. Also recovered `comcon_tenant_request` + `comcon_dependency`,
    which had silently skipped since C3.0 (obsolete `typeof nginx` probes now refused at
    load).
  - **Still open in C3 (the erasure-complete remainder, for C5):** aliased/interprocedural
    value-flow typing, the `Response` return type, granted-`Socket` member typing, and
    computed member keys — i.e. the full type inference the compiled tier needs to erase
    against. C3 today *rejects* on types where it can prove a violation; it does not yet
    *certify* a fragment fully-typed.

- **C4 — M4: the fragment artifact ("fat bytecode"). LANDED (v5.17).** bytecode + type/cap
  side-table + env-signature (we have this — the learn/grant machinery) + content
  hash + schema hash + admission cert. Generalizes B/E1's pin-by-hash from "a file" to
  "the artifact."

  **Built + tested:** at admission (after the C3 checks pass) the fragment is sealed into
  `ngx_js_artifact_t` on `jcf`: **content hash** = SHA-256 over the tenant sources;
  **identity** = `H(content_hash ‖ schema-version)` — `NGX_JS_C4_SCHEMA_VERSION` =
  the C2 `version`; **env-signature** = the C3.0 free-name count; **certificate** = a bit
  per C3 clearance (free-names / dyn-code-free / Request-sealed / onRequest-sig). The
  certificate is logged at load. New directive **`js_tenant_artifact <hex>`** pins the
  identity: content OR schema drift refuses the config (one match, both drifts). Reuses
  the B/E1 SHA-256 path; no engine change. Test: `t/comcon_artifact.t`. **Deferred to C5:**
  the type/cap *side-table* is today the certificate bits + the free-name manifest, not yet
  a per-value type table — that fills in with the erasure-complete typing lowering needs;
  and the content hash is over source (a bytecode-level `jit_hash_*` identity is a C5/M8
  refinement once lowering exists).

- **C5 — M5: lowering (the hard, valuable core).** maxim lowers the typed fragment
  → C → `.so`, with the confinement compiled *in*: static mediations partial-
  evaluated to inline checks, the A1 reach checks preserved (never optimized away),
  back-edge gas in compiled loops, the generation check at entry (two-clocks
  revocation). **Deliverable + differential test:** a compiled `onRequest` handler
  produces identical responses *and identical denial counters* to its interpreted
  self.

- **C6 — M6/M7: dispatch + real benchmark.** Prefer the C function pointer in
  `ngx_js_tenant_content_handler`, bytecode fallback (the hybrid tier); class-F
  epochs for live re-AOT on revocation; benchmark the real pipeline — the M1
  numbers, now measured not hand-written.

- **C7 — M8: the safety gate. PASSED profile-scoped (2026-09-01, v5.25).** Compiler
  faithfulness = T2 refines T1, per fragment, by differential testing (same inputs → same
  outputs → same denials), plus proof the A1 gates survive compilation. `t/comcon_faithfulness.t`
  runs a suite over the confinement surface (report / Request reads / Response shapes /
  compute / granted-socket reads / **A1 gated reach `.listener` / gated mutator `close()`**),
  each interpreted vs AOT-compiled, asserting byte-identical responses AND identical denial
  totals (the gates fire the same number of times in both tiers) AND that every fragment
  actually compiled (non-vacuous). 22/22. **Scope:** the COMCON tenant profile (strict-module
  confined fragments) — the maxim surface that is in-profile-clean (INCREMENT_MAXIM.md). Full
  test262 conformance under AOT is a parallel maxim track; **untrusted-native *production*
  additionally waits on M-SES + full maxim finalization** (§3). So: the *increment-C* gate is
  met for the confined profile; the *production-untrusted* bar is the wider one.

## 3. Gating and honest scale

- **M-SES gates *production* compiled-untrusted tenants**, not the build-out.
  C0–C6 can proceed as a **dev tier** (compiling trusted/first-party fragments);
  compiling genuinely untrusted tenants to native waits for the engine-hardening
  milestone. State this in the shipped config (a compiled tenant needs either
  M-SES-complete or a trusted-author declaration).
- **maxim must be finalized before C7 / production.** maxim currently fails ~200
  test262 tests (an incomplete engine, to be finished later). C0–C6 tolerate this —
  they compile trusted fragments and always keep the interpreted tier as the correct
  fallback. But **C7 (T2-refines-T1 faithfulness) cannot pass, and no untrusted
  fragment may be compiled to native, until maxim clears those failures.** Sequencing:
  build C0–C6 on maxim-as-is; **insert a "finalize maxim" milestone immediately
  before C7.** A second, independent reason (besides M-SES) that native-untrusted is
  the last thing to light up.
- **Scale, stated plainly.** A/B slices were day-scale and self-contained. C0 is
  the same (a spike). **C1 (M-UNIFY) and C5 (lowering the confinement semantics
  faithfully) are the two multi-day, genuinely hard pieces** — C1 is engine
  integration, C5 is the research-adjacent correctness core. The plan front-loads
  C0 precisely so the C1 scope decision is made on evidence (the measured tree
  delta + a working .so), not a guess.

## 4. C0 result — GATE PASSED (2026-09-01)

Ran the spike. Every question C0 was meant to answer is now a measured fact, not a
guess:

- **maxim builds here.** `make CONFIG_JIT=y` — clean (one harmless incompatible-pointer
  warning at `quickjs.c:16331` `js_jit_call`, to tidy during the merge); `libquickjs.a`
  rebuilt; `qjs` advertises the full `--jit-*` flag set (`--jit-aot`, `--jit-warmup`,
  `--jit-compile-all`, `--jit-dump-c`, `--jit-threshold-gcc=N`, …).
- **A trivial function compiles to a persistent `.so` and runs correctly.**
  `qjs --jit-compile-all add.js` and `--jit-warmup` both produced GCC-compiled
  `<hash>.so` files (`c7f3c39…​.so` for `add`, one per function) and executed correct
  results (`result=45`). The fragment → C → GCC → `.so` → dlopen → call pipeline works
  end to end in this environment.
- **The ABI is fully characterized** (`--jit-dump-c`): every compiled function is
  `JSValue __jit_f_<hash>(JSContext *ctx, JSValue this_val, int argc, JSValue *argv,
  JSValue *cpool, JSVarRef **var_refs)` (the `JSJITFunc` convention). All runtime ops
  route through a **single data symbol `js_jit_rt`** (a vtable), plus a handful of
  extern helpers. Type specialization is real and visible: `add(a,b)` has an unboxed
  `int`+`int` fast path — this is the typed-tier acceleration C2/C3 will feed.
- **The C5 integration path is real** (the key de-risk): the `add()` `.so`'s only
  non-libc undefined symbols are `JS_GetRuntime` and `js_jit_rt`, and **every** symbol
  it (and the install path) needs — `js_jit_rt`, `js_jit_fb_set_func`/`get_func` (install
  a compiled function onto a `JSFunctionBytecode`), `js_jit_create_closure`,
  `js_jit_ic_direct_call`, `js_jit_callIC_fill` — is a **defined symbol in
  `libquickjs.a`**. Unresolved-from-libquickjs count: **0**. So a maxim-enabled
  `libquickjs.a` lets nginx `dlopen` a compiled fragment and install+call its function
  with no additional runtime.

**Decision: proceed to C1 (M-UNIFY).** The gate is green and the two hard-piece
estimates are now evidence-based: the merge is bringing `quickjs-jit.{c,h}` + the
`js_jit_*` surface into the vendored tree over a ~4% `quickjs.c` delta, and C5's
"call the `.so` from nginx" reduces to "link the maxim-enabled `libquickjs.a` and use
`js_jit_fb_set_func` / the `jit_func` dispatch." Artifacts: scratchpad `c0/` (add.js,
the dumped C, the cache `.so`s).

## 5. C1 (M-UNIFY) — the diff analysis + strategy (2026-09-01)

Ran the C1 reconnaissance (the analysis before touching the 60k-line engine). It
settles the merge shape:

**It is a clean 3-way merge, not a fork reconciliation.** Both trees share the **exact
same Bellard base, `VERSION 2025-09-13`** — so there is *no base-version drift*. The
delta is entirely `base + {pilgrim's patches} + {maxim's JIT}`:
- **`quickjs.h` (public ABI): identical.** No consumer-facing surface change.
- **Vendored is its own git repo with pilgrim patches that maxim lacks** (js_std_tick_
  timers, JSON.parse source-text access, oversized-serialized-bytecode protection, +
  the opcode/atom below). These **must be preserved** — so vendored is the merge *base*,
  never overwritten. (It also carries the A/B code and the SR-1 fixes.)
- **The one sharp edge — opcode/atom divergence — is BENIGN.** Vendored adds one opcode
  (`set_loc_check`) and one atom (`rawJSON`) that maxim's tree lacks. maxim's JIT
  references *neither* (0 hits in `quickjs.c`/`quickjs-jit.c`), and its code generator
  **default-bails**: `js_jit_gen_c`'s `switch(op)` sets `*unsupported=1; return -1` for
  any opcode without an explicit case, excluding that function from JIT (it runs
  interpreted — correct, just unaccelerated). So keeping vendored's opcode/atom tables
  and compiling maxim's JIT against them is safe: worst case, a `set_loc_check`-using
  function is not accelerated. T1≡T2 is preserved because the JIT only ever *matches or
  declines*, never reinterprets.

**Merge strategy (decided):** **vendored is the base**; add maxim's `quickjs-jit.{c,h}`
and the `js_jit_*` glue in `quickjs.c` (the `jit_func` field on `JSFunctionBytecode`,
the `JS_CallInternal` dispatch, `js_jit_fb_set_func`/…) **all under `#ifdef CONFIG_JIT`**;
keep vendored's opcode/atom tables; add a `CONFIG_JIT=y` variant to the `../quickjs`
build and a pilgrim build flavour. The `quickjs.c` glue is the ~3128 maxim-only lines
*minus* whatever is base/pilgrim-independent — extracted as JIT hunks, applied onto
vendored.

**Sub-steps (the multi-day construction, C1.1–C1.4):**
- **C1.1 — DONE** (now vendored at pilgrim `quickjs/`; originally `../quickjs` branch `comcon-jit` `f6a2646`). Vendored
  `quickjs-jit.{c,h}` + ported maxim's CONFIG_JIT Makefile stanza. Verified: JIT-off
  build byte-identical (archive has no `quickjs-jit.o`, no `js_jit_*`); JIT-on dry-run
  compiles it with the right defines. A JIT-on build won't *link* yet (needs C1.2 glue)
  — expected.
- **C1.2 — IN PROGRESS: merge mechanics solved, reconciliation scoped (2026-09-01).**
  The port is done as a **3-way merge** (auto-combines pilgrim patches + maxim JIT).
  Key findings:
  - **The correct BASE is maxim's true base**, commit `b226856` (parent of maxim's first
    JIT commit `731c0f2` "JIT Phase 1"; find via
    `git -C <maxim> log --diff-filter=A -- quickjs-jit.c | tail -1`, then `^`). Using
    vendored's base instead leaks maxim's base-drift into the merge (820-line JIT-off
    diff); using maxim's base isolates **pure JIT** (2873 lines).
  - The two forks have **538 lines of genuine base drift** (different bellard commits
    despite the same `2025-09-13` VERSION label) — so the merge must keep *vendored's*
    base, adding only the pure JIT.
  - **Reproduce:** `git merge-file -p <vendored quickjs.c> <maxim@b226856:quickjs.c>
    <maxim@HEAD:quickjs.c>` → **0 conflicts**.
  - **The remaining work — reconcile ~97 unguarded lines.** The clean merge still pulls
    in maxim's *unguarded* base changes: `new_target`/`jit_new_target` (JIT refs it 11×
    in quickjs-jit.c), the `shape_gen` IC counter + `_shape_pad`, a debug `fflush`, and
    an `sf`-NULL-check refactor. **Rule per unguarded addition:** wrap in `#ifdef
    CONFIG_JIT` if JIT code references it (new_target, shape_gen), else revert to
    vendored (fflush, the sf-check unless the JIT needs it). **Verify** by preprocessing
    the merged file with `CONFIG_JIT` undefined and diffing vs the current vendored
    `quickjs.c` → target **0 differing lines** = JIT-off provably identical.
  - **Then:** install, build JIT-OFF (**acceptance: full `t/` + `t_stress/` + `comcon_*`
    green**), then build JIT-ON — which links `quickjs-jit.c` and may surface further
    guarded base-support the JIT needs (iterative build-debug). Not installed this
    session: a subtly-wrong 62k-line engine file would break the whole product; the
    reconciliation + JIT-on bring-up is executed with a full build/test loop.
- **C1.3** — pilgrim builds a `CONFIG_JIT=y` `libquickjs.a` variant; nginx links it;
  same suites green (JIT present but threshold-gated → same semantics).
- **C1.4** — differential: a hot pilgrim/tenant function gets JIT-compiled and produces
  identical results (the erasure-soundness invariant, first live instance).

**Where the merged engine lives — DECIDED (b): vendored into pilgrim (2026-09-01).**
`quickjs/` is now a subdirectory of the pilgrim repo (brought in with `git subtree
--prefix=quickjs --squash` from the `comcon-jit` branch, so it carries the pilgrim
patches + C1.1). Rationale: the engine and pilgrim's COMCON code co-evolve tightly
(C4/C5 ABI), so atomic co-commits + a self-contained clone matter more than easy
upstream rebases (this fork has diverged far anyway). nginx builds `-Iquickjs -Lquickjs`;
all comcon_* + js_com tests pass against the vendored engine. Upstream pulls later via
`git subtree pull`. The sibling `../quickjs` checkout is retained only as a rebase base.

## 6. C1.2/C1.3 execution result — Option A done, JIT-thread task identified (2026-09-01)

**C1.2 executed via Option A (maxim-as-base) — DONE, T1 green.** Test262 validated maxim's
interpreter is bellard-quality (72/83257 known, 0 unexpected; ~200 fails are JIT-only; vs
vendored's 60 the delta is ~6 non-core staging tests). So instead of guarding maxim's
base changes onto vendored (Option B, which the JIT-coupling made a two-interpreter
reconciliation), **maxim became the base**: workbench branch `comcon-engine` = `maxim/jit`
+ cherry-picked pilgrim patches (oversized-bytecode, JSON source-text, `js_std_tick_timers`
— load-bearing), all clean. Pilgrim's `quickjs/` was replaced with it; nginx relinked
(maxim's public `quickjs.h` ≡ vendored's → src/js ABI-compatible). **Acceptance met:**
~505 tests green on the maxim interpreter (comcon_* 74, core js 117, sighup stress 15,
broad js_com 299). Commit `3d1e9c08c`. (`set_loc_check` intentionally gone — vendored's
opcode; maxim's table is self-consistent, no pilgrim C references it.)

**C1.3 — DONE (JIT-safe under nginx) + a C6 boundary clarified.** The JIT-enabled engine
builds + links into nginx; the master/worker hang was maxim's JIT background thread
(started in the master via JS_NewRuntime, not surviving fork). Fixed engine-side (commit
`34642f087`): a `pthread_atfork` CHILD handler zeroes `jit_worker.started`, so forked
workers run pure interpreter (both enqueue paths + drain/free already guard on
`started`) — no thread, no hang, no inherited-mutex use; master + standalone qjs
unaffected. Acceptance: the previously-hanging tests pass in ~1s; full comcon_* (74) +
sighup stress incl. SW reload + core js (64) green on the CONFIG_JIT build. CONFIG_JIT is
opt-in; the default build stays T1. **C6 boundary made explicit:** JIT *install* is
driven only by an explicit `js_jit_install_results()` call (qjs main loop) — so nginx
workers would not *activate* JIT even absent the hang. Real per-worker activation (a
worker-local thread post-fork + install wired into the event loop + benchmark) is C6.
The prior partial note follows.

**C6-lite JIT-activation spike (2026-09-01) — result: activation is real C6 work, not a
quick win (throwaway probes reverted).** Wired a spike (`-DCOMCON_JIT_SPIKE`): per-worker
`js_jit_init()` on first request, runtime `js_jit_set_threshold(3)`, `js_jit_install_
results()` after each tenant `JS_Call`, + engine probes. Findings: (1) the worker-local
JIT thread starts fine; (2) but the threshold trigger misbehaves for tenant functions —
call counts don't accumulate (`cnt=1` every event) and the runtime threshold setter
didn't reach the request path (`thr=100`), so no function is ever enqueued; (3) with the
activation wiring, **single-process JIT nginx segfaults during tenant eval**; (4) the
install path was never exercised (no compile happened / crash). Conclusion: real
per-worker JIT **activation** — the trigger/count/threshold behaviour for functions
invoked via C `JS_Call` from a separate tenant runtime, the crash, and the install
wiring — is dedicated C6 engineering. The spike did its job: it de-risked by revealing
activation is non-trivial *before* C2–C5 were built on the assumption it was easy. The
interpreted tier (T1) is solid and unaffected.

**C1.3 (prior partial note): the JIT-enabled engine builds + links into nginx, the
background-compile thread needed nginx-lifecycle adaptation —** now done (above). `make CONFIG_JIT=y
libquickjs.a` builds clean (has `js_jit_rt`); nginx relinks against it (+`-ldl -lpthread`)
with no undefined refs; a **single-process** JIT nginx runs and serves. But under the
Test::Nginx master/worker harness a tenant test **hangs** — maxim's JIT spawns a
background GCC-compile pthread, which conflicts with nginx's `fork` + signal model (a
thread doesn't survive fork cleanly; clean `-s stop`/reload stalls). **Remaining C1.3/C6
task:** adapt the JIT thread to nginx — start it *per-worker post-fork*, join it on
`exit_process`, or use synchronous compile — before the JIT can run under nginx's real
process model. The working pilgrim binary is restored to the non-JIT (T1) engine.

**Status: C1.0/C1.1 done; C1.2 (Option A base-engine swap) DONE + T1 green; C1.3 DONE (JIT-safe under fork; workers interpret; real JIT activation deferred to C6); C1.4 differential — the genuinely multi-day
piece, now scoped and de-risked (same base, benign opcode divergence, additive
CONFIG_JIT-guarded glue).** Deliberately not started mid-session: a half-merged 60k-line
engine that does not build is worse than a validated plan. It is the next focused
construction task, with the CONFIG_JIT-off byte-identical build (C1.2) as its first hard
checkpoint.

*(Design references: ROADMAP §2 M-UNIFY/M2/M3/M4/M5/M6/M7/M8; SPEC §7 typed
profile, §8 artifact & tiers; PERFORMANCE.md the M1 endpoints; VERIFICATION V5
translation validation / V13 erasure spot check — the differential-test discipline
this plan leans on.)*
