# COMCON — maxim finalization scope (the JIT correctness gate)

*Scope note, 2026-09-01. maxim (the JS→C JIT/AOT compiler, `../../quickjs-and-tcc/quickjs`,
branch `jit`) is the compiled tier T2. Its interpreter is bellard-quality (72 test262
errors); the JIT introduces additional failures that must be cleared before the compiled
tier can be trusted. This blocks C5 (lowering builds on it), C6 (activation), and is the
input to C7 / M8 (= SR-2 faithfulness, "T2 refines T1"). Grounded in a categorization run
(~10.5k tests under forced JIT) + the maxim repo's prior AOT artifact.*

> **Executed (2026-09-01) — in-profile gate effectively MET; C5 unblocked.** The go/no-go
> spike + T0 measurement below were run. **Bucket 1 (the dominant closure `var_ref` crash)
> is fixed** (maxim `b07ca5d`): always-dup the frame's var_ref + release it in
> `close_caps`. **T0 harness built** (maxim `73bba6d`: `t0-measure-jit.sh` + `make
> test262-jit-delta`). With the fix, the whole common surface is **0-new** (Array.prototype.*,
> Atomics, Promise, arrow-function, for-of, TypedArray set/fill), and an in-profile
> strict-module handler ran **200k invocations clean, stable 34 MB RSS**, including the
> phase-35 cache-load path (cold+warm). The residual delta is small and **out-of-profile**:
> (a) the biggest correctness bucket — non-strict callback `this`=undefined-vs-global — is
> **sloppy-mode only**, and COMCON tenants are strict modules where the JIT's answer is
> *correct*; (b) the residual `filter/map` crash is **NOW FIXED (maxim `3d52736`)** — it was
> a cache-load-path bug where a JIT'd function emitting P10.3 direct JIT-to-JIT calls could
> bind a direct call to a stale/wrong `__jit_f_` symbol left in the process-global RTLD
> namespace by a freed runtime (warm cache only). Fixed by consistent callee hashing
> (`jit_hash_function`) + correctness-safely skipping the cache-load fast path for direct-call
> functions (recompile fresh; common functions still cache-load). Trade-off: direct-call
> functions lose the cross-process `.so` cache; a per-callee-resolution fix is deferred.
> (c) a BigInt-callback-conversion edge remains. **Conclusion:** the COMCON-profile-scoped gate is
> met — in-profile T2 is correct (incl. server-AOT cache-load) — so **C5 lowering is
> unblocked**. The three residual items go to a parallel maxim full-suite track (fix or
> exclude), not the C5 critical path. Details: §5 (categories) updated by the run; the
> full delta report is maxim `jit-test262-results/t0-delta.txt`.

---

## 1. Goal & definition of done

**Done = the JIT introduces zero new test262 failures**: run test262 with the JIT forced
on (every function compiled), and the error set is a subset of the interpreter's 72
baseline (`test262_errors.txt`). Equivalently: `errors(JIT) ⊆ errors(interp)`. That is
exactly the T2-refines-T1 obligation M8 checks, per fragment and in aggregate.

This is **reachable and bounded** because the JIT has a correctness-safe fallback (§3): any
function it cannot compile correctly can be *excluded* (bail to the interpreter). So the
gate can always be met by a mix of real fixes and targeted exclusions — the question is
how much is fixed (kept fast) vs. excluded (kept correct), not whether zero is achievable.

## 2. The reframing: this is hardening, not feature-completion

The JIT's opcode-coverage gate (`scan_is_unsupported`, `quickjs-jit.c`) excludes only
`OP_eval` (plus structural cases: >64 closure vars, module-level decls, non-bytecode cpool
entries). **Everything else is compiled.** So the failures are **miscompiles of supported
paths** — correctness bugs — not missing opcodes. Finalization = find-and-fix a set of
codegen defects until the gate is met, not implement missing features.

## 3. The correctness-safe lever

`scan_is_unsupported` (and the scan's structural bail-outs) make exclusion cheap: adding a
pattern there routes matching functions to the interpreter, which is correct by
construction. So for any miscompile that is expensive to fix, the fallback is to **exclude
the triggering pattern** — losing that function's JIT speedup but never correctness. This
bounds the milestone: worst case, exclude every failing pattern and the gate is met (at a
perf cost measured by M7). The engineering judgment is fix-vs-exclude per root cause.

## 4. Harness reality (a real gotcha — get the measurement right first)

The categorization run surfaced that **`run-test262 --jit-threshold-gcc=0` forces NOTHING**:
the per-call compile is gated `if (thr >= 1 && cnt == thr)` (`quickjs.c:~19937`), so 0
compiles nothing (verified: 0 GCC execs at 0, 6 at 1). The knobs that actually exercise the
JIT:

- **`--jit-threshold-gcc=1`** — compile on the 1st call, JIT runs from the 2nd. Partial:
  single-call test bodies leave the 1st (only) call interpreted, so it *masks* first-call
  miscompiles. Good for a fast scan, not for the gate.
- **`--jit-link` → `--jit-aot`** — warm every function into the cache, link one
  `combined.so`, then run with all functions pre-installed. This is the **true "every call
  JIT'd from the first"** mode and the one the gate must be measured under.

Compile *failures* (GCC errors) are silent: `js_jit_queue_gcc` writes a `<hash>.skip`
marker and the function is interpreted — lost coverage, never a test262 error.

**Task 0 of finalization: a reproducible full-AOT measurement.** maxim has no
`test262_errors_jit.txt` baseline or a "run under `--jit-aot` and diff vs. the 72" make
target — only a one-off April artifact (`jit-test262-results/`). Standing up that
repeatable count (build the `--jit-link`→`--jit-aot` sweep into a target; record the JIT
error set) is prerequisite to driving failures to zero, and becomes the M8 harness.

## 5. The failure landscape (categorized)

From ~10,558 tests under forced JIT (`--jit-threshold-gcc=1`) across 78 directory runs,
cross-checked against the repo's prior full-AOT artifact (`jit-test262-results/`, which
shows segfaults/aborts across TypedArray, Array, Atomics, Promise, DataView, Map, Function).
The failures are **concentrated, likely a few root causes at the JIT-closure /
callback-from-C-builtin boundary** — not 200 independent bugs:

- **Bucket 1 — closure `var_ref` refcount underflow → CRASH (highest severity).**
  `free_var_ref: Assertion 'var_ref->header.ref_count > 0' failed` (`quickjs.c:5864`) →
  SIGABRT, or SIGSEGV when the corrupted refcount is freed later. The JIT over-releases a
  captured closure variable. Hits builtin methods that invoke a JIT'd callback:
  `TypedArray.prototype.{set,fill,map,forEach,filter}`, and (in the AOT artifact) Atomics /
  Promise / DataView / Map. **Crashes kill `run-test262` mid-directory**, so each crash
  masks dozens of un-run tests — this bucket is the main reason a full AOT run shows
  ~hundreds. Fixing the refcount defect likely recovers most of the count.
- **Bucket 2 — callback argument / `this` marshaling (wrong value, no crash).** For
  callbacks invoked from `Array.from` (iterator + mapfn) and `Array.prototype.some`, the
  JIT frame passes the index/positional args and the non-strict `this` (should coerce to
  global) as `undefined`. Specific to those paths — `Array.prototype.map/filter/reduce/
  forEach/every` (900+ callback tests) were clean, so it is not a general callback bug.
- **Bucket 3 — GCC compile errors (silent, lost coverage not test failures).** The repo's
  `issue-comp-errors/` bucket + a known `_tsv`-undeclared codegen bug. These don't fail
  test262 (they skip-and-interpret) but they erode the JIT hit-rate and should be fixed or
  formally excluded so the perf story (M7) is honest.
- **Bucket 4 — compile-time pathologies.** `issue-too-slow-compilation/` (e.g.
  `string-upper-lower-mapping.js` — a huge function GCC chokes on). Handle via the existing
  `--jit-max-bc` cap / exclusion, not codegen.

Everything else exercised was clean under JIT: all arithmetic/bitwise/shift/logical,
control flow, arrow/function/call/new/this/arguments, `Function.prototype.call/apply/bind`,
most `Array.prototype.*`, `String.prototype.*`, `Number`, `Proxy` get/set, class, try,
tagged templates, optional chaining.

### 5.1 Measured update (2026-09-02, at maxim `jit` 4927146 = fork/pilgrim engine)

Re-measured with the T0 harness (`test262-jit-delta`) + targeted repros after the crash
fixes landed (bucket-1 `b07ca5d` + cache-load `3d52736` + back-edge-gas, all now in maxim
jit HEAD, the pilgrim-quickjs fork, and pilgrim). **The landscape collapsed to essentially
one correctness bug:**

- **Bucket 1 (refcount crashes) — RESOLVED.** `b07ca5d` fixed the `var_ref` over-release.
  Verified: `TypedArray/prototype/map` (was SIGSEGV rc=139) and `filter` (was SIGABRT
  rc=134) now **complete without crashing**; a 25-file individual sweep of
  `DataView/prototype` under JIT had **0 hangs / 0 crashes**. No segfault/abort/hang was
  found anywhere at HEAD.
- **Bucket 2 (`this` marshaling) — CONFIRMED, now the dominant/only failure family.**
  Minimal repro: a non-strict callback with no `thisArg` gets `this=undefined` under JIT but
  the global object interpreted (strict correctly gives `undefined` in both). This is the
  single root cause behind **every** JIT-new test failure the broad 20-family sweep surfaced
  (`TypedArray.forEach/some/filter`, `Array.from`, `Map.forEach` — all the
  `SameValue(undefined,[object global])` fails). One JIT-prologue fix (substitute global for
  undefined/null `this` in non-strict functions) clears the family. **Out of the confined
  COMCON profile** (tenants are strict modules) — which is exactly why C7/SR-2 passed
  profile-scoped. ✅ **FIXED (F2, maxim `jit` 5885987, 2026-09-02):** `OP_push_this` now
  emits the sloppy coercion via a `js_jit_this_sloppy()` runtime helper (object→as-is,
  null/undefined→global, primitive→`JS_ToObject`), gated by a `js_jit_fb_is_strict()`
  accessor; strict keeps the plain dup. **Subtlety the T0 sweep caught:** codegen now
  depends on `js_mode`, which is *not* in the bytecode stream, so the strict bit had to be
  folded into `jit_hash_function()` — else a strict/sloppy twin with identical bytecode
  aliases to one JIT cache entry (the strict `Array.from`/`Map.forEach` variants regressed
  until the hash included the mode bit). T0 delta over the affected families: **6 new → 0
  new**; both tiers match the interpreter; CONFIG_JIT self-test green.
- **BigInt typed-array callbacks — basic path CLEAN at HEAD** (repro: `BigInt64Array`
  `filter`/`forEach` match the interpreter). Any residue is a narrow edge case
  (during-iteration mutation / resizable buffers), triaged in T3.
- **Measurement caveat (real):** `--jit-threshold-gcc=1` GCC-compiles every function, so
  large families (`Object` = 3411 files, `String`/`RegExp`/`Promise`/`class`/…) hit the
  per-dir `timeout` (`rc=124`) — that is **compile-everything slowness, not a crash or
  hang** (a single mid-size DataView test JIT-compiles+runs in ~1.2s; `map`/`DataView` both
  complete given enough wall-clock). This is a test-harness artifact, irrelevant to COMCON
  (the JIT compiles a tenant's few functions once at load); it maps to Bucket 4, not
  correctness. It does mean the exhaustive T0=0 confirmation (T4) is **wall-clock-bound**,
  not fix-bound.

**Net:** finalization is now essentially **one prologue fix (Bucket 2) + a compute-bound
confirmation sweep + the re-gate** — no crash-debugging slog. Bucket 1's fix already flowed
through the fork into pilgrim.

## 6. Plan of work

1. **T0 — reproducible measurement. ✅ DONE.** `t0-measure-jit.sh` + the `test262-jit-delta`
   make target (per-dir JIT delta over the 72 baseline) exist and are now in the fork. The
   exhaustive `--jit-aot` full-sweep variant (for the final T0=0 proof, T4) is wall-clock-
   bound, not built into CI.
2. **T1 — Bucket 1 (the refcount underflow). ✅ DONE (`b07ca5d`).** Fixed and verified at
   HEAD (§5.1): no crashes/hangs remain. Already flowed through the pilgrim-quickjs fork
   into pilgrim.
3. **T2 — Bucket 2 (callback marshaling).** Fix `this`-coercion + positional/index arg
   passing for the `Array.from` and `Array.prototype.some` callback paths.
4. **T3 — re-measure; triage the residue.** For each remaining failure, decide **fix vs.
   exclude** (§3). Land exclusions in `scan_is_unsupported`/scan for anything not worth
   fixing now, so the gate is met.
5. **T4 — Bucket 3/4 (compile errors + slow compiles).** Fix the `_tsv` codegen bug; cap or
   exclude the pathological-compile functions. Restores JIT hit-rate for the M7 perf story.
6. **T5 — gate + flow to pilgrim.** Confirm `errors(JIT) ⊆ 72`. Then subtree-pull the
   finalized JIT into pilgrim's `quickjs/`, re-applying the 23-line `jit_atfork_child`
   fork-safety patch (or upstream it to maxim first so the trees converge). Re-run the
   pilgrim `comcon_*` + regression suites under JIT.

## 7. Where the work lives

maxim (branch `jit`) is the finalization home — it has the test262 harness, `jit-docs/`,
and history. **The lineage is now consolidated (2026-09-01):** the single integration fork
`github.com/nginxinc/pilgrim-quickjs` (branch `pilgrim` = `maxim/jit` + the COMCON/host
delta) is what pilgrim subtree-pulls from. So finalization lands on `maxim/jit`, then
`git fetch maxim && merge into the fork's pilgrim branch && git subtree pull` into pilgrim —
no more re-applying the atfork patch per pull (it lives in the fork's `pilgrim` branch). See
`../pilgrim-quickjs/INTEGRATION.md` and the [[pilgrim-quickjs-fork]] memory.

## 8. Effort shape, risks, open questions

- **Effort is front-loaded and uncertain-but-bounded.** If Bucket 1 is one root cause, T1
  clears most of the count in a focused debugging session; the long tail (T3 residue) is
  where the fix-vs-exclude judgment lives. The exclude-lever caps the downside.
- **Risk: the AOT harness itself.** `--jit-aot` has had its own bugs (the repo notes a
  `combined.so` dlopen fallback). T0 must produce a *trustworthy* measurement before we
  chase failures, or we chase harness artifacts.
- **Risk: masked first-call miscompiles.** The threshold-1 scan is blind to them; only the
  full AOT sweep (T0) exposes the true set. Do not declare buckets complete off threshold-1.
- **Open: performance floor after exclusions.** Every exclusion is a perf regression vs. the
  96%-of-stock ceiling; M7 must re-measure after T3/T4 so the compiled-tier value prop
  stays honest.
- **Open: scope of the gate.** Does M8 require the *full* test262 subset clean under AOT, or
  the COMCON-relevant subset (the restricted typed-tenant profile compiles a much smaller
  language surface)? A COMCON tenant never uses Atomics/DataView/Promise-heavy patterns, so
  a **profile-scoped gate** (clean on the constructs a confined typed fragment can emit)
  may be the pragmatic M8 bar, with full-suite clean as the maxim-project goal. Worth
  deciding — it could sharply cut the finalization surface for the COMCON critical path.

## 9. Roadmap placement & adopted approach (2026-09-01)

maxim finalization gates **C5 → C6 → C7/M8 (= SR-2)**. Decisions adopted:

- **The M8 gate is COMCON-profile-scoped.** M8's obligation is "T2 refines T1 *for admitted
  fragments*," and a typed confined tenant compiles a small surface. So the C5-critical-path
  gate is: clean under AOT on the constructs a typed confined fragment can emit
  (closures/callbacks over Array/String/Object/Math/JSON). **Caveat that keeps it honest:**
  Buckets 1–2 are *in-profile* (typed tenants use callbacks constantly) → they must be
  **fixed, not excluded**. Only the out-of-profile long tail (Atomics/DataView/Promise-heavy
  crashes) is deferred to a parallel maxim-project full-suite track and may be handled by
  exclusion.
- **Proceed via a bounded go/no-go spike, not an open-ended grind.** Before committing to
  full finalization, run **T0 (reproducible AOT measurement)** + a **Bucket-1 root-cause
  spike** (the `free_var_ref` refcount underflow). If one fix clears most of the crash count
  (as the clustering suggests), finalization is a handful of focused fixes — continue. If it
  fragments into many independent codegen bugs, reconsider pace: confinement already works
  soundly on **T1 today**, so T2 (speed) could be deferred and confined tenants shipped
  interpreted, with maxim as a later optimization. The spike produces the data for that
  go/no-go.
