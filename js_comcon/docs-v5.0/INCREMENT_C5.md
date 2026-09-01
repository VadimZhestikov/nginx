# COMCON — C5 scope (lowering: the compiled confined tier)

*Scope note, 2026-09-01. C5 = M5, "the hard, valuable core": maxim lowers the admitted
typed fragment → C → `.so`, with the confinement compiled *in*, such that a compiled
`onRequest` handler produces **identical responses AND identical denial counters** to its
interpreted self (the erasure-soundness / M8 obligation, demonstrated on a real handler).
Grounded in the current confinement implementation + maxim's synchronous AOT API, with the
maxim JIT correctness gate now met for the in-profile (strict-module tenant) surface
(INCREMENT_MAXIM.md; maxim `b07ca5d`/`73bba6d`/`3d52736`).*

---

## 1. What "confinement compiled in" actually means here (the key reframing)

In the shipped model the confinement is **not in the handler's bytecode**. It is:

- the **compartment boundary** the host sets around every handler call
  (`ngx_js_compartment_enter(TENANT)` … `leave`, in `ngx_js_tenant_content_handler`);
- the **A1 reach gate** `ngx_js_compartment_may_reach()`, checked inside the *host C
  callees* the handler invokes (granted socket `.listener`/`close`/`broadcast`, etc.);
- the **denial counters** (A4), incremented in those same host gates;
- the **deny-by-default global** (M-SES-0) the handler runs against.

Therefore **compiling the handler preserves confinement by construction**: the compiled C
calls the *same* gated host functions, under the *same* host-set compartment, against the
*same* locked-down global. C5's correctness deliverable — identical responses *and* denial
counters — holds without inlining anything, because the mediations live in the callees, not
the handler. That makes the MVP tractable; the SPEC's "partial-evaluate mediations to inline
checks" is a **later perf optimization** (§4), not a correctness prerequisite.

## 2. What exists to build on — and what does NOT

- **maxim lowering + synchronous AOT** — present and now correct in-profile. The clean
  path is **server-AOT at load**, no background JIT thread:
  `js_jit_compile_all(ctx, handler_bc)` → `js_jit_drain()` → `js_jit_install_results()`
  compiles the handler + nested functions and installs `jit_func`; `JS_CallInternal` then
  dispatches to the compiled function. This **sidesteps the C6-lite activation blockers**
  (per-worker background thread, lazy in-request trigger, segfault) — those were about the
  *lazy JIT worker thread*, which AOT-at-load does not use.
- **The A1 gate** (`ngx_js_compartment_may_reach`) and **denial counters** — present; the
  compiled handler inherits them via its callees.
- **NOT present anywhere yet (interpreted or compiled):** **gas / back-edge budget** and a
  **generation / two-clocks revocation** counter (grep confirms only `may_reach` exists).
  The SPEC lists "back-edge gas in compiled loops" and "generation check at entry" as C5
  goals, but they presuppose machinery COMCON has not built at either tier. They are
  therefore **out of the C5 MVP** and named-deferred (§4) — you cannot "compile in" a
  mediation that does not yet exist interpreted.
- **pilgrim builds interpreter-only today** (`nm objs/nginx` → 0 JIT symbols; `auto/lib/
  quickjs/conf` links `libquickjs.a` without `CONFIG_JIT`). So **C5 step 0 is a JIT-enabled
  build** (`make -C quickjs CONFIG_JIT=y libquickjs.a`; nginx ld-opt `-ldl -lpthread`).

## 3. The C5 MVP (C5.0) — the deliverable — **DONE (2026-09-01)**

1. **C5.0-a — JIT-enabled pilgrim build. DONE.** A separate `objs_jit/` builddir links the
   `CONFIG_JIT` engine (`-DCONFIG_JIT` + `-ldl`); the interpreter `objs/` stays the default.
   Full `comcon_*` + regression + SIGHUP-reload suites pass on the JIT build. Recipe +
   gotchas (CONFIG_JIT needs a clean quickjs rebuild; ABI match; one lib at a time):
   memory `build-and-test`.
2. **C5.0-b — AOT-compile the handler at load. DONE.** `js_comcon_aot_compile()`
   (quickjs.c, CONFIG_JIT) runs `js_jit_compile_all → drain → install_results` on the
   registered handler after admission (`ngx_js_eval_tenant_sources`), `#ifdef CONFIG_JIT`;
   logs "handler lowered to native C". Workers inherit the installed `jit_func` + `.so` via
   fork/COW; multi-process serves compiled and shuts down cleanly.
3. **C5.0-c — the differential test. DONE.** `t/comcon_lowering.t` runs the same confined
   tenant interpreted (`objs/nginx`) vs AOT-compiled (`objs_jit/nginx`) and asserts
   **byte-identical responses AND an identical denial-counter total** (erasure soundness on
   a real fragment), plus that the JIT build actually compiled the handler (non-vacuous) and
   shut down cleanly. Full comcon suite green on both builds (18 files / 148), AOT active for
   every tenant on the JIT build.

**Done when:** the compiled handler is behaviorally indistinguishable from interpreted
(responses + denials) on the differential suite, on the JIT build, with the confinement
(A1 gates, deny-by-default, denial log) intact.

## 4. Deferred layers (honest scope edges)

- **C5.1 — partial-eval inline gate checks (perf).** Inline/specialize the host gate checks
  into the compiled handler using the C4 type/cap side-table. Optimization only; correctness
  already holds via the callees. Measured by C6/M7.
- **Back-edge gas** — requires a **budget model** COMCON has not built at either tier. Build
  it interpreted first (so erasure holds), then compile it in. Post-C5.
- **Two-clocks revocation (generation check at entry)** — requires a **generation/epoch
  counter** (also not built). The compiled `.so` would check it at entry and bail/recompile
  on bump. Needs the epoch machinery first (relates to the deferred binding-epoch work in
  FOUNDATION §8.8). Post-C5.
- These three are exactly the SPEC's C5 wishlist items that presuppose absent machinery; the
  MVP delivers the lowering + erasure-soundness demonstration, and names these as the road to
  the full compiled-tier safety surface.

## 5. Risks / open questions

- **Single-process-mode teardown crash — FIXED (2026-09-01).** Surfaced while validating
  C5.0-b (the differential test used `master_process off`), but it was a **pre-existing bug
  in both builds, not JIT/AOT specific**: in single-process mode one process runs both
  `ngx_js_exit_process` and `ngx_js_exit_master` on the same `jcf`, and `w->rt`/`w->ctx`
  alias `jcf->rt`/`jcf->ctx` — so exit_process freed the runtime and exit_master then
  dereferenced the freed opaque in `js_std_free_handlers` (SIGSEGV). Fix: exit_process nulls
  the shared `jcf->` handles when it frees them, so exit_master's NULL-guarded teardown
  skips (harmless in multi-process, where `jcf` is the worker's COW copy). Regression guard:
  `t/comcon_teardown.t` (clean single-process shutdown, both builds). The C5.0 differential
  test uses multi-process (production-representative) regardless.
- **`.so` across fork (COW/RTLD).** AOT installs in the master pre-fork; the `.so` is
  dlopen'd in the master and must remain valid in workers post-fork. Confirmed working:
  workers serve the compiled handler and shut down cleanly (C5.0-c multi-process). The
  `jit_atfork_child` handler (C1.3) zeroes the worker's JIT-thread state.
- **Reload.** On SIGHUP the tenant runtime is rebuilt; re-run AOT at each config load. Verify
  no `.so`/fd leak across reloads (the existing `sighup_*` leak tests should extend to cover
  the JIT build).
- **Build-time cost.** AOT drains GCC synchronously at config load → slower reload/startup
  for the compiled tier. Acceptable (server-AOT is the intended production trade); measure.
- **maxim correctness scope.** The in-profile surface is gate-clean (post the b07ca5d/3d52736
  fixes); the differential test is itself the per-fragment M8 check, so any residual miscompile
  surfaces as a diff, not a silent wrong answer — the erasure design contains the risk.
- **Interpreter fallback.** If a handler doesn't compile (unsupported construct, GCC failure),
  it must transparently run interpreted (maxim already skips-to-interpreter on failure) — the
  differential test's "compiled" arm should assert the handler actually compiled, else the
  test is vacuous.

## 6. Placement

C5.0 delivers the compiled confined tier + the erasure-soundness demonstration, on server-AOT
(no background thread), so it does **not** re-enter the C6-lite lazy-activation problem. **C6**
then becomes mostly the **benchmark + the live-dispatch polish** (and any lazy/tiered
activation, if wanted beyond AOT). **C7 = M8 (= SR-2)** generalizes C5.0-c's differential test
into the faithfulness gate. Gas + revocation (post-C5) complete the compiled-tier safety
surface the SPEC envisions.
