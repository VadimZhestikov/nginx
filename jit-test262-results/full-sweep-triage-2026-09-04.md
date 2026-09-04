# Full-suite JIT sweep — triage (2026-09-04, HEAD 7312525)

The full-suite `--jit-threshold-gcc=1` run (11h20m) crashed SIGSEGV at ~98% and,
diffed against the interpreter baseline, showed JIT-introduced failures. All
**reproduce deterministically in isolation with a fresh cache** (not cache/
aggregate artifacts). Correction to earlier notes: errors are on run-test262's
STDOUT (a prior monitor grep checked stderr → falsely read "0 new"); and a
`SLOW-OK` (threshold=100) confirmation can MASK a threshold=1 miscompile (e.g.
`reduce` is clean at thr=100 but fails at thr=1).

## Clusters (deterministic; reproduce in isolation)

1. **JIT `arguments`-object drops args beyond the declared formal count (9 tests).**
   `TypedArray/prototype/reduce` + `reduceRight` (×8, incl. BigInt) + `Array/prototype/some` (×1).
   ROOT-CAUSED: a JIT-compiled callee invoked by a C builtin, reading
   `arguments[i]` for `i >= arg_count` (declared formals), gets **undefined** —
   though `arguments.length` is correct. Minimal repro:
   ```js
   var s = new Float64Array([42,43,44]); var r = [];
   s.reduce(function(acc){ r.push(arguments); return acc+1; }, 7);
   // JIT: r[1][1] === undefined (should be 43); r.map(a=>a.length) === [4,4,4]
   ```
   `function(acc,val){…}` (explicit 2nd param) works. So the JIT prologue's
   arguments-object materialization copies only the formal-slot args, missing the
   excess (unmapped) args spilled by the C→JIT call path. **Highest value — one
   fix, 9 tests.**

2. **TypedArray `[[Delete]]` strict-mode (6 tests).** `TypedArrayConstructors/internals/Delete/*-strict`
   (+BigInt). Strict `delete ta[i]` of an out-of-bounds / detached-(S)AB index must throw
   `TypeError`; JIT throws nothing. A `[[Delete]]` internal-method JIT path bug.

3. **TypedArray subclass without `super()` (1 test).** `.../subclass/builtin-objects/TypedArray/
   super-must-be-called` — missing `ReferenceError` (TDZ on `this`).

4. **staging/sm edge cases (2 tests).** `Array/for_of_3` (SameValue NaN vs 453 — for-of miscompile);
   `Function/function-bind` (`TypeError: invalid property access` — bound-function).

## The SIGSEGV
Aggregate-only: **not reproduced** by isolating staging/sm/{Array,Function} (both complete rc=1).
Likely a stability/memory issue over the 11h / 77k-compile run (RSS 807 MB, 1.8 B minor page faults),
not a single-test miscompile. Needs the warm-cache full-run state to reproduce; lower priority than
the deterministic miscompiles.

## Priority
1. Cluster 1 (arguments-beyond-formals) — clean root cause + minimal repro, 9 tests, security-relevant
   (a miscompiled untrusted callback = wrong behavior). Fix the JIT arguments-object materialization.
2. Cluster 2 ([[Delete]] strict) — one rule, 6 tests.
3. Clusters 3/4 — edge cases, 1 each.
4. SIGSEGV — stability; hardest to repro, triage after the semantic fixes (may resolve or change).

## Cluster 1 — FIXED (2026-09-04, commit 76ee4cb)
Root cause: JS_CallInternal's arg_buf sizing. With COPY_ARGV (set by every C-builtin JS_Call), the
extension of arg_allocated_size to the full argc was gated on `!has_simple_parameter_list`, so a
simple-param callee got arg_buf truncated to arg_count — while js_build_arguments reads
rt->jit_actual_argc = argc slots => out-of-bounds (undefined) for arguments[i>=arg_count]. Fix: extend
to argc whenever argc > arg_count for simple params too (sf->arg_count stays arg_count, so GEN_PUT_ARG
gating unaffected; cleanup already frees arg_allocated_size). Verified: reduce 0/50, reduceRight 0/50,
some 0/219; no regressions (Array/prototype 0/2808 @thr100, forEach 0/190 @thr1). 9 tests cleared.
Remaining: cluster 2 ([[Delete]] strict ×6), cluster 3 (super ×1), cluster 4 (for_of/function-bind ×2),
and the aggregate-only SIGSEGV.
NOTE: the analogous truncation exists in js_jit_call() + js_jit_ic_direct_call() simple-param branches
(JIT->JIT paths, quickjs.c ~16354) — not hit by these C-builtin-invoked tests, but the same bug class;
fix for consistency if a JIT->JIT arguments case surfaces.

## Cluster 2 — FIXED (2026-09-04, commit 7680a4d)
Root cause: the P41.1 early JIT fast-path (simple params, var_ref_count==0, NORMAL, !COPY_ARGV) set
up its minimal JSStackFrame but never set sf->js_mode = b->js_mode (a comment wrongly said the JIT
ignores it). Runtime helpers called from JIT code consult is_strict_mode(ctx) = current_stack_frame->
js_mode & STRICT; with a stale js_mode a strict function ran as sloppy — JS_DeleteProperty(
JS_PROP_THROW_STRICT) didn't throw on a failed strict delete. Fix: set sf->js_mode in that path (the
standard hot-path already did). Cleared Delete 0/39; no regressions (Set 0/53, expressions/delete
0/69, expressions/assignment 0 new @thr100). GENERAL strict-mode fix — any strict-dependent runtime
helper reached from an early-fast-path JIT function was affected (broader than just delete).
Remaining: cluster 3 (super ×1), cluster 4 (for_of/function-bind ×2), aggregate SIGSEGV.

## Cluster 3 — ROOT-CAUSED, deferred (2026-09-04): JIT has no TDZ
`super-must-be-called` (derived-ctor `this` before super() must throw ReferenceError). Root cause:
the JIT does NOT implement the Temporal Dead Zone. (1) prologue inits all locals to JS_UNDEFINED,
not JS_UNINITIALIZED (quickjs-jit.c ~2649); (2) OP_set_loc_uninitialized is a no-op (~3928);
(3) OP_get_loc_check / OP_get_loc_checkthis are codegen'd identically to plain OP_get_loc (~3900) —
no `if (JS_IsUninitialized(v)) throw ReferenceError`. So reading a `let`/`const`/derived-ctor-`this`
before initialization reads UNDEFINED silently instead of throwing. Also affects OP_put_loc_check.
Fix = a JIT FEATURE (init TDZ locals to UNINITIALIZED + emit the checks + a throw helper), with
regression risk (local-init assumption changes from UNDEFINED). Deferred: 1 test, feature-sized,
risky — poor ROI vs clusters 1/2. Note: for_of_3 was already cleared by the cluster-2 strict fix.

## Status after clusters 1-2 (+for_of_3)
Fixed 16 of 18: cluster1 (9, 76ee4cb), cluster2 (6, 7680a4d), for_of_3 (1, via 7680a4d).
Remaining: cluster3 super×1 (deferred, JIT-TDZ feature), staging/sm function-bind×1, aggregate SIGSEGV.

## function-bind — FIXED (2026-09-04, commit fc1d991)
Root cause: js_jit_special_object built arguments for BOTH strict and sloppy (MAPPED_ARGUMENTS) via
the unmapped builder (callee = throw_type_error poison pill). JIT'd sloppy `arguments.callee` threw
TypeError "invalid property access". Fix: MAPPED case uses js_build_mapped_arguments(...,arg_count=0)
— proper MAPPED_ARGUMENTS with real callee/length/iterator; arg_count=0 makes each element a fresh
copy (no frame-aliased var_ref → no lifetime hazard with the freed JIT arg_buf). Live param aliasing
stays punted (unchanged). Fixes staging/sm/Function 0-new; no regress (arguments-object 0/263,
expr/function 0/264, stmt/function 0/451 @thr100). 

## FINAL: 17 of 18 fixed
cluster1 9 (76ee4cb) + cluster2 6 (7680a4d) + for_of_3 1 (7680a4d) + function-bind 1 (fc1d991).
Remaining: cluster3 super-must-be-called ×1 (DEFERRED — JIT has no TDZ; feature-sized), and the
aggregate-only SIGSEGV (not reproduced in isolation; stability over the 11h/77k-compile run).

## Cluster 3 — FIXED (2026-09-04, commit f1c7a9c): JIT now implements TDZ
The JIT had no Temporal Dead Zone: locals init'd to UNDEFINED, set_loc_uninitialized a no-op,
get_loc_check/get_loc_checkthis/put_loc_check == plain get/put_loc. Fix (3 parts): (1) jit_infer_types
TDZ pre-pass forces set_loc_uninitialized/get_loc_check/checkthis/put_loc_check target locals to JSVAL
(so they can hold JS_UNINITIALIZED); (2) OP_set_loc_uninitialized emits `_FREE(loc); loc=JS_UNINIT`
(derived-ctor `this` uses this opcode too — one mechanism for let/const AND this); (3) the _check
reads/writes emit `if(JS_IsUninitialized(loc)){js_jit_throw_uninitialized(ctx); _sp=..; goto _ex;}`
+ new runtime helper. Fixes super-must-be-called 0/2 AND general let/const TDZ. No regress: 0 new over
~28k tests @thr100 (expr/class 0/4049, stmt/class 0/4351, expr 13/10665, stmt 0/9119); let 0/145,
const 0/136 @thr1; earlier fixes intact.

## FINAL TALLY: 18/18 semantic failures fixed. Only the aggregate SIGSEGV remains.
cluster1 arguments (9, 76ee4cb) · cluster2 strict js_mode (6, 7680a4d) · for_of_3 (1, 7680a4d) ·
function-bind sloppy callee (1, fc1d991) · cluster3 TDZ (1 + general let/const, f1c7a9c).
Remaining: the aggregate-only SIGSEGV (not reproduced in isolation; stability over the 11h/77k run).
A future full-suite JIT sweep (11h) would confirm the delta is ~0 semantic + whether the SIGSEGV persists.

## Parallel-sweep cluster — Function.prototype.toString ×13 — FIXED (captured-local TDZ)
A per-bin parallel re-sweep (verifying the 18-fix state) surfaced 13 JIT-new failures:
`built-ins/Function/prototype/toString/{built-in-function-object, proxy-*, symbol-named-builtins}`
(12) + the `harness/nativeFunctionMatcher.js` self-test (1). Deterministic; interpreter clean.

ROOT CAUSE: a regression from the cluster-3 TDZ fix (f1c7a9c), NOT a toString bug. The tests run
the harness parser `validateNativeFunctionSource("" + fn)` — a pure-JS recursive-descent matcher —
whose `let pos` and mutually-referencing `const` helpers are all **captured** locals. The JIT's TDZ
ops (`get_loc_check` / `put_loc_check` / `set_loc_uninitialized`) always used the `_jsv_<name>`
shadow slot, but `GEN_GET_LOC`/`GEN_PUT_LOC` store captured locals in `_cap_buf[idx]` (the var-ref
cell). So `set_loc_uninitialized` wrote the sentinel to `_jsv_<name>` while the real value went to
`_cap_buf`; `get_loc_check` then always saw `JS_UNINITIALIZED` → spurious
`ReferenceError: cannot access lexical binding before initialization`, which `assertNativeFunction`
catches and rethrows as "Conforms to NativeFunction Syntax". Minimal repro: extract the parser, loop
it — interpreter 0/3000, JIT 2999/3000, first fail on the 2nd (JIT) call. `--jit-dump-c` confirmed
`_jsv_pos_6` set UNINITIALIZED and never rewritten while the value lived in `_cap_buf`.

FIX: make the three TDZ emission sites honor `_CAP_LOC(idx)` — test/write `_cap_buf[idx]` for
captured locals, else `_jsv_<name>` — so the TDZ sentinel lives in the slot the reads/writes consult.
VERIFIED (thr=1, fresh cache): toString 0/80, harness 0/116, minimal repro 0/3000; no regression —
expressions/class 0/4049, statements/class 0/4351, let 0/145, const 0/136. 13/13 fixed, 0 regressions.

NOTE (unrelated, pre-existing): reading a *captured* lexical while it is genuinely in TDZ (or
capturing a derived-ctor `this` before super()) trips a debug-build interpreter assertion
`get_var_ref: vd->is_captured` (quickjs.c). Independent of the JIT; the genuine-TDZ-throws direction
is covered by the let/const/class dirs. Worth a separate look.
