# COMCON v5.0 — Expected Performance Impact

*Companion to `FOUNDATION.md` §8 and `ROADMAP.md`. Distinguishes throughout between
**measured** (real benchmarks on real hardware, method in §2) and **modeled** (cost
reasoning to be confirmed by M7). v2's performance model (§8.2) was entirely argued;
v3 has measured endpoints.*

---

## 0. Executive summary

COMCON's performance story is a **gradient, not a number** — and the gradient is the
staging gradient (FOUNDATION Principle 8: soundness is stage-independent; moving a
check earlier never changes what is allowed, only what it costs). Its endpoints are
measured:

| Point on the gradient | Throughput vs stock nginx | Status |
|---|---|---|
| Fully static policy, compiled through (maxim target shape) | **90–96%** \* | **measured** (M1 hand-C) |
| Declarative-only policy, engine-consulted tables (stage 1) | ~85–95% *(model)* | modeled |
| Policy with residual runtime membranes on hot paths | strongly workload-dependent | modeled |
| Fully interpreted policy framework (mirror today) | **28–50%** | **measured** |

\* *ceiling measured before the v5.0 safety machinery (generation check, back-edge
gas, slot guards — §4.6); expected low-single-digit % lower; M7 re-measures.*

Read the two measured rows as the ceiling and the floor of the same policy: the M1
experiment ran the *identical* "count + tag" policy both ways — hand-lowered C at
**96%** of stock, interpreted at **28%** (cross-host 10 GbE; 90%/29% on loopback).
A directive-expressible control (the policy subset expressible as pure `map` +
`add_header` config) ran at parity with both stock *and* the hand-C — proving **the
entire gap is the interpreter**, not the policy semantics. Everything COMCON does at
compile/admission time is reload-time cost, amortized to ~zero per request.

---

## 1. The three governing principles

1. **Pay at the boundary, not in the loop** *(v2, retained)*. Policy costs concentrate
   at fragment boundaries and admission time; fragment-interior execution runs
   unchecked at full engine speed. The declarative environment (import list) makes
   this *provable*: an object that never escapes its fragment needs **zero** checks —
   statically known. Unlike JIT heuristics the cost is predictable and auditable:
   read the policy, know where the checks land.

2. **The performance gradient is the staging gradient** *(v3)*. One policy, four
   enforcement moments (static / admission / residual membrane / compile-through).
   What is resolved earlier costs less at runtime; what stays dynamic pays. The
   security argument is formally decoupled (SEMANTICS §3), so choosing a cheaper
   point on the gradient never weakens confinement — it only requires the policy to
   be more static.

3. **Restriction is optimization fuel** *(v2, now partially measured)*. A policy is a
   set of declared invariants — and **policy invariants never deoptimize**. A JIT
   *infers* "this prototype won't change" and pays deopt machinery when wrong;
   COMCON *enforces* the assumption. Frozen intrinsics (M-SES S1) mean inline caches
   never invalidate — paths can be *faster than stock JS*.

---

## 2. Measured anchors (what we actually know)

All numbers: nginx 4 workers, `h2load` HTTP/1.1 keepalive `-c100`, best of 3;
loopback on a dedicated server (n6) and cross-host over real 10 GbE (n6 DUT ← n8
generator). Artifacts: `t_performance/maxim_m1/`, `t_performance/RESULTS_baremetal_vm.md`.

**M1 — the same policy at both ends of the gradient** (count + tag: shared-memory
atomic counter, request-header read, two response headers, body):

| Config | loopback req/s | % | cross-host req/s | % |
|---|--:|--:|--:|--:|
| stock nginx (no policy) | 391,233 | 100% | 424,882 | 100% |
| **hand-written C policy** (compiled-policy target shape) | 350,165 | **90%** | 409,386 | **96%** |
| interpreted mirror policy | 113,327 | 29% | 119,044 | 28% |

**The reduced-policy control** (drop the counter; keep the directive-expressible tag):

| Config | loopback % | cross-host % |
|---|--:|--:|
| stock floor | 100% | 100% |
| nginx directives/vars (`map`+`add_header`) | 103% | 94% |
| hand-C (tag) | 101% | 96% |
| interpreted mirror (tag) | 47% | 44% |

Directives ≈ hand-C ≈ stock, all within noise ⇒ **the whole gap is the interpreter**;
the compiler's payoff is precisely the policies directives *cannot* express (shared
state, counters, dynamic routing).

**Framework context** (Table A′, real NIC, full mirror event framework after its ~3.6×
optimization): stock 432k / pilgrim JS handler 321k (74%) / njs 283k (65%) / mirror
216k (50%). This locates today's *interpreted framework* baseline that compiled COMCON
policies are measured against.

**What is NOT yet measured:** every intermediate point — stage-1 declarative tables and
membrane dispatch. That is milestone M7's whole purpose; expectations below are models.
**Compiler-produced (rather than hand-written) C is now measured — §2b.**

---

## 2b. The lowering ceiling: can typed JS reach the C it would replace? *(v5.97)*

The question a full proxy makes askable: re-implement part of a hot path nginx implements in
C — a header filter, a body step — in JS, purely to get extra functionality into it. Would
that be affordable if M5 shipped with types?

`t/tools/lowering-ceiling.t` (against **objs_jit**; in-process, because a throughput
benchmark on the dev box goes through WSL2's mirrored firewall and compresses every ratio
toward 1.0). Every arm asserted to return the same hash; the compiled arm asserted compiled
via `jitStatus`; C offered at both `-O` and `-O2` because nginx builds at `-O` while maxim
compiles its output at `-O2/-O3`, which would otherwise **flatter JS**.

**Arithmetic, ns per iteration (5M iterations, min of 3):**

| arm | ns/iter | vs C(-O2) |
|---|--:|--:|
| C pointer-free integer chain, `-O2` | 2.0 | 1× |
| **lowered JS, same algorithm** | 16.6 | **8.3×** |
| interpreted | 42.2 | 21× |
| kernel B (no `>>>`) lowered | 22.0 | 12.2× |
| **typed-SHAPE arm** (raw int32 locals, no boxing, **gas check kept**) | **2.0** | **1.00×** |

**Data plane, ns per BYTE (16 KB buffer, dependent chain so gcc cannot vectorise):**

| arm | ns/byte | vs C |
|---|--:|--:|
| C pointer walk | 0.73 | 1× |
| **zero-copy `Uint8Array` over nginx memory** | 12.65 | **17×** |
| copy-backed `Uint8Array` (what pilgrim has today) | 12.82 | 18× |
| one host call per byte | 20.35 | 28× |

**What these say:**

1. **The untyped gap is boxing, not code generation.** `--jit-dump-c` shows the accumulator
   living in a `double` and *every* operation materialising two `JSValue`s, tag-checking both
   operands, keeping a runtime-dispatch fallback, re-boxing, and writing a byte into a global
   type-feedback array. The typed-shape arm — the same algorithm with raw `int32` locals,
   still inside maxim's framing and still paying the back-edge gas check — is **at parity**.
   So **parity is reachable in principle, and the whole prize is whether inference can drop
   the boxing.** That is the M5 risk, now precisely stated.
2. **The zero-copy view works and buys nothing.** `JS_NewArrayBuffer` over nginx memory with
   a no-op free gives a real view, and it measures the same as a copy: the access path
   dominates, not the backing. One 16 KB copy is **0.2 µs = 0.012 ns/byte** against **12.65
   ns/byte to scan it** — the copy was never the problem.
3. **A host call per byte costs about the same as a compiled typed-array read** (20.4 vs 12.7
   ns/byte). "Avoid per-element crossings" is not the lever it looks like; in this engine a
   per-element read is already priced like a crossing.
4. **The data-plane gap (17×) is worse than the compute gap (8×)**, because the useful work
   per operation is smaller. The "add functionality to a hot path" scenario is the *least*
   favourable shape for untyped lowering.
5. **Today the affordable shape is the coarse hook.** For one 16 KB buffer: C scan 12 µs, JS
   scan 207 µs, one host call 0.04 µs. Letting C scan and handing JS the answer is ~17×
   cheaper than letting JS walk the bytes — the same *propose-don't-hold* pattern
   `allowHosts` and `std.config` already use.
6. **And `policy-compute-split.t`'s known-positive control is loop elimination.** Its
   `s + i*3` shape runs at 0.2 ns/iter in C and 0.4 lowered — neither compiler runs the loop.
   The control still proves that harness can see a win; it must not be read as
   "compute-bearing code gets 13× from lowering". **The shape maxim wins biggest on is the
   shape gcc deletes.**

## 2c. The confined invocation, measured *(v5.101)*

Until v5.101 this document had **no number for the tier tenants actually run.** M1's
"interpreted policy" is host JS, not a fragment invoked through `comcon`, so every
per-request cost quoted for confinement was a model — which is how an O(heap) walk on every
invocation stayed invisible (F14, ASSURANCE G7.10). Instrument: `t/tools/confined-invoke-cost.t`.

One worker, loopback, WSL2, `wrk -t1 -c10 -d4s`, one run per arm — so read the ratios, not
the absolutes. "Loaded" means a DIFFERENT fragment retains 200,000 objects in the shared
compartment.

| per request | before F14's fix | after |
|---|--:|--:|
| stock nginx (`return 200`) | 296,968 req/s — 100% | 298,931 req/s — 100% |
| host JS handler | 263,781 — 88.8% | 260,516 — 87.1% |
| host handler + one confined invocation | 65,198 — **22.0%** | 203,358 — **68.0%** |
| ...same, loaded | **476 — 0.2%** | 199,113 — **66.6%** |

| in-process, per call | before | after |
|---|--:|--:|
| host JS function call | 0.046 µs | 0.047 µs |
| confined invocation, idle | 10.8 µs | **0.78 µs** |
| confined invocation, loaded | 2,030 µs | **0.78 µs** |

Two readings. **The walk was over 90% of an invocation even with nothing retained**, so
every earlier impression of "confinement is expensive" was mostly this. And what remains —
host handler 87% of stock, one confined call 68% — is the real cost of the boundary: the
JSON round trip, the compartment enter/leave, the posture and identity bookkeeping, and the
two drains. That is the number any future tier (including M5) now has to be measured against.

## 2e. M5.0 — the go/no-go benchmark for typed lowering *(v5.114)*

M5 was unparked 2026-09-15 on an evidence-first order, and this is the measurement that
decides whether the codegen (M5.1) starts. The record going in (§2b): compiling a host-call-
dominated policy buys ~1.0×; untyped lowering is 8.3× off C on arithmetic and 17× on a byte
scan; a hand-written typed-shape arm reaches parity on arithmetic. Instrument:
`t/tools/m5-go-nogo.t` against `objs_jit`, in-process, the controls of §2b (same hash in every
arm — which caught a JS `*` that was not a 32-bit multiply, fixed with `Math.imul` in the
benchmark and in the SR-2 case alike — compiled arm proven compiled, interpreted arm proven
not, MIN of three).

Two candidate classes, the ones `t/comcon_include_faithfulness.t` already holds under the SR-2
differential. Four arms each; **the decision is the middle pair**, `lowered / typed`, with the
rule stated before the numbers: **≥ 3 → GO**. `typed` is the ACHIEVABLE typed-shape bound, not
the floor: for a byte scan, a C pointer walk with the back-edge gas check kept
(`nginx.bench.scanTyped`); for string code, one engine read per character (the K arm of §2b) —
because typed string code still reads its characters through the engine, and that is where a
typed lowering of class B would land. `floor` is C with nothing kept, for scale.

| class | floor | **typed (achievable)** | **lowered (today)** | interp | lowered/typed | decision |
|---|--:|--:|--:|--:|--:|---|
| **A** byte-scan validation, ns/byte (16 KB) | 0.57 | **0.61** | **11.72** | 16.48 | **19.2×** | **GO** |
| **B** token check (split · FNV · toString), ns/char | 0.47 | **20.16** | **50.00** | 53.13 | **2.5×** | **NO-GO** |

**What the numbers say.**

1. **The gas check costs nothing** (typed 0.61 vs floor 0.57 on the scan) — the resource gate
   the compiled tier must keep (G7.18) is not what stands between lowered JS and C. Boxing is,
   as §2b found for arithmetic: the untyped lowered scan is 11.72 ns/byte, 19× off a typed
   walk of the same bytes.
2. **Untyped lowering barely helps the data plane** — 11.72 lowered vs 16.48 interpreted, 1.4×.
   The tier tenants run on today buys the byte-scan shape almost nothing; the whole prize is
   in the typed shape.
3. **String-heavy code has no typed prize.** Class B's achievable bound is 20 ns/char — an
   engine read per character — and today's lowered arm is 50; a perfect typed lowering could
   buy 2.5×, below the rule, and the `split`/`toString` allocations around the loop are engine
   work no lowering removes. The JWT-check result of AOT-A (0.92×) was the same fact from the
   other side.
4. **So M5.1's target is narrow and specific**: typed-array element access and integer
   accumulators — the shape where `u8[i]` and `h ^ u8[i]` can be emitted as a byte load and an
   int32 op with the gas check kept. Not "typed policies" in general, and not strings. That is
   a smaller compiler than M5 was first drawn as (M1–M9), and a checkable one: every case it
   lowers is under the SR-2 differential, and every gate it could erase is under G7.18.

**Re-parked with its number:** class B, and with it the "compile the policy" reading of M5.
**GO with its number:** class A, the data-plane hook.

## 2d. The nested invocation, measured *(v5.108)*

The authoring tier (v5.106) adds a second boundary INSIDE the compartment: a parent fragment
invoking a sub-fragment it authored. Same instrument (`t/tools/confined-invoke-cost.t`, the
in-process half), one worker, idle compartment; the parent is cached across calls so only the
invocation is timed.

| in-process, per call | µs |
|---|--:|
| host JS function call | 0.046 |
| confined invocation (host → fragment) | 0.717 |
| a parent that returns without invoking anything | 0.699 |
| the same parent invoking one sub-fragment | 1.285 |
| **⇒ one nested invocation, on top of the outer one** | **≈ 0.59** |

The nested boundary costs LESS than the host boundary, which is what its shape predicts: it
has the JSON round trip and the identity/deadline/allowance push and pop, and none of the
rest — no compartment enter/leave, no posture push, no settle loop, no drain (it drains
nothing by design; a sub-fragment's jobs are the parent's). Two of them in a request cost
about one host boundary. The per-request half is unchanged by this tier (a request that does
not author anything pays nothing), so it is not re-measured here.

---

## 2f. M5.1a — the narrow compiler's first cut, measured *(v5.117)*

What shipped, in the engine (`JIT_CODEGEN_VERSION` 18), and why it assumes nothing:

1. **A bit op with ONE provably-numeric operand yields a typed int32.** `& | ^ << >>` apply
   ToInt32 to both sides; the only non-int32 outcome (BigInt with BigInt) needs BOTH operands
   to be BigInt, and a Number mixed with a BigInt throws — an exception, not a value. So when
   one side is a typed int or double, the result is int32 whatever the other side is, and an
   int accumulator no longer degrades to a boxed double at `h ^ u8[i]`. The generated code
   takes the inline path when both operands are int-tagged at run time and otherwise calls the
   SAME runtime the boxed path called — same conversions, same exceptions — and unboxes its
   int32. Before, one untyped operand made the whole op untyped, and every op after it.
2. **An in-bounds element of an integer typed array is read in place.** Uint8, Uint8Clamped,
   Int8, Int16, Uint16 and Int32 arrays share the fast array's layout (element pointer, count);
   the bounds check is against the count the engine keeps current — 0 once the buffer is
   detached, tracking a resizable buffer — so out of bounds, detached, a negative or fractional
   index, and every other class take the runtime path exactly as before.

Not done, and why: **loop versioning** (a typed copy of the loop chosen at entry) needs a
deoptimisation for the element read that goes out of bounds — `undefined` is not an int32 —
and **feedback-driven speculation** (maxim's warm-recompile element hint) substitutes 0 on a
miss, which is unsound for a confined tier and unused at include time anyway. Neither is
needed for the shape M5.0 measured.

Same instrument (`t/tools/m5-go-nogo.t`, `objs_jit`, three runs, best of three per arm):

| class A, byte scan (ns/byte) | v5.114 | **v5.117** |
|---|--:|--:|
| floor (C, nothing kept) | 0.57 | 0.61 |
| typed (gas-checked C walk) | 0.61 | 0.61 |
| **lowered** | 11.72 | **1.26** |
| interpreted | 16.48 | 15.5 |
| lowered / typed | 19.2 | **2.1** |

| class B, token check (ns/char) | v5.114 | **v5.117** |
|---|--:|--:|
| floor (C FNV) | 0.47 | 0.47 |
| "typed" (K: lowered JS, one host read per char) | 20.16 | 13.75 |
| lowered | 50.00 | 50.0 |
| interpreted | 53.13 | 54 |
| lowered / typed | 2.5 | 3.6 |

Three readings, the third a correction to the record:

- **Class A: 9.3× of the 19× is realised**, and by the rule stated before the numbers
  (`lowered / typed ≥ 3 → GO`) the remaining 2.1× is NOT worth a further typed cut. What is
  left is one box and unbox per element, the class-id and bounds checks, and the stack
  bookkeeping around the op — not typed work. Accepted, not scheduled.
- **Class B did not move** (50.0), as predicted: its loop is `Math.imul(h ^ c, p) >>> 0` —
  `imul` is a call and `>>> 0` is uint32 (NUMBER), so the accumulator stays boxed — and the
  split, the `toString(16)` and the allocations around the loop are engine work no lowering
  removes.
- **Class B's "typed bound" was not a bound.** The K arm is lowered JS (`h ^ byteAt(i)`), so
  this change moved it, from 20.16 to 13.75, and the class B ratio rose to 3.6 with the
  numerator unchanged. §2e's 2.5× NO-GO stands on the evidence that matters — the fragment's
  own number did not move — and the rule's ratio is recorded here as sensitive to how its
  denominator is built. A future re-measurement should use a C per-char kernel for that arm.

The gas check is untouched: `t/comcon_compiled_resource_gates.t`'s ten probes agree as
before, and `t/comcon_jit_uncatchable.t` still stops both forms on native code.

---

## 3. Cost model by enforcement moment

### 3.1 Compile / admission time (reload-time, not request-time)
Policy-program execution + a linear AST pass with hash/bitmap descriptor lookups +
meet-composition of stacked bindings (computed **once at bind**, so multi-policy
targeting does not stack runtime checks) + contract tests (run against capability
doubles, offline by construction) + content hashing for pin-by-hash. In the nginx
deployment this is the reload pipeline: compile → sign → cache → workers COW-inherit.
Amortized ~zero per request. Dynamic `eval`/`include` reopens episodes at runtime —
denied in tenant profiles by default, budget-capped where allowed.

### 3.2 Static residue (stage 1 declarative) — modeled, per v2 with v3 updates

| Check | Mechanism | Expected cost |
|---|---|---|
| Fragment identity | static per bytecode function | zero — "current fragment" = current function |
| Anchors | inert (parse-time attributes) | **zero at runtime** |
| Opaque values | value tags | ~zero marginal (qjs already tag-dispatches) |
| Property access on *shared* objects | per (fragment × shape) descriptor/bitmap, IC-cached | few cycles, branch-predictable — **the** make-or-break integration (§5.1) |
| Cross-fragment calls | per-arg descriptor check, cacheable per call site | small, amortized |
| Delegation | tag + delegability bits at pass/return | trivial |
| Revocation, dynamic-checked cap | mediation flag → lazy IC revalidation | ~free normally; cost lands at the rare event |
| Revocation, **static-baked** cap (v5.0 — R3) | per-fragment generation bump → fragment self-demotes to bytecode → re-AOT | instant safety; the *performance* cost is one re-AOT + an interpreted window for that fragment |
| Gas metering (M-SES S5) | interrupt-handler countdown | small, constant; measure in first slice |

### 3.3 Residual membranes (`mediate` interceptors on hot paths) — modeled
A JS call per checked operation — expensive by construction. The tri-state keeps
`allow`/`deny` at table speed; only `allow-with-check` pays. Two mitigations are
structural: (a) the expressiveness ladder pushes policies toward declarative forms;
(b) **static interceptors compile away** — M1's worked example (a constant URL-prefix
guard) lowers to a `strncmp` in the C stub, i.e. the membrane costs approximately one
string compare, not one JS call.

### 3.4 Compile-through (maxim) — endpoint measured
Typed static policy → unboxed C linking only the allowed stubs, capability constants
baked in, **no runtime policy interpreter at all**. Measured target shape: 90–96% of
stock. The maxim endgame from v2 stands with evidence: complete import/export/internal
descriptions + typed extensions = a closed world — direct calls, no shape checks, no
prototype walks. **The narrower the policy, the faster the code** — restriction and
optimization are the same declaration read twice.

---

## 4. Costs that are new in v3 (not in the v2 model)

1. **Live rewrite (class F, POM.md §3) — a transient, bounded degradation.** Rewriting
   a live node drops that node to its **bytecode fallback** until re-AOT: during the
   window, that fragment runs at the *interpreted* end of the gradient (~28% shape),
   then returns to compiled speed at the epoch switch. Never stop-the-world; cost is
   per-rewritten-node and bounded by re-AOT latency. Plan capacity for rewrite windows
   on hot tenants accordingly.
2. **Meet-composition** — resolved at bind time into one merged table per name; the
   runtime consults a single effective binding, not a stack. (Design intent; verify in
   the first slice.)
3. **Pin-by-hash** — one content hash per fragment at admission; zero at runtime.
4. **Determinism caps** (clock/RNG as grants) — a granted `Date.now` is one facet call;
   fragments that never receive it pay nothing.
5. **M-SES freezes** — expected *negative* cost on shared paths (never-invalidated
   ICs), small one-time lockdown cost at compartment init.
6. *(v5.0 — R3/R4/R5)* **Compiled-tier safety overhead**, previously uncounted: a
   per-fragment **generation check at entry** (one load + predictable branch),
   **back-edge gas** in compiled loops (absent entirely in the loop-free profile), and
   **write guards** where a lower tier stores into declared-typed shared slots.
   Expected total low single-digit %; must be measured in M7 — the 96% ceiling was
   measured *without* them. *(Framing worth keeping: this overhead is the native tier
   **buying back what WASM provides intrinsically** — meterability and revocability —
   so our-born code never needs the sandbox at all.)*
7. *(v5.2 — the wasm facet lanes, modeled)* **Foreign-code costs by lane:** embedded
   WASM runtime ≈ 70–90% of native on pure compute (guard-page bounds checks are
   nearly free on 64-bit) — but the real cost is the **host boundary** (copies through
   linear memory, no shared references), and chatty hook-style workloads sit in
   exactly that weakest quadrant; **wasm2c ingestion** ≈ 85–95% of native with the SFI
   checks preserved, plus our funnel's own item-6 overhead. Both modeled; the M7 WASM
   baseline column is where they become measurements. **Mass-tenancy note (tiering by
   heat):** a long tail of thousands of tiny *cold* fragments is served by
   compartment-per-tenant with per-compartment caps and instant reset (S2), not by
   per-tenant native `.so`s — compile heat, don't compile population.

**Memory** *(v2 model retained, one v3 addition)*: policy metadata ∝ policies +
fragments, never ∝ objects or operations; COW views ∝ actual write-divergence; grant
chains only for delegable caps; **POM nodes are lazy** — only module/function/block
are materialized C-side, statement/expression views synthesized on demand (POM.md §6),
so the program tree does not multiply resident memory by AST size.

---

## 5. Risks and the falsification plan

1. **COW-domain / inline-cache integration is the single decisive risk** (carried from
   v2 §10.3, still true). If per-(fragment × shape) caching fails to stay in the IC
   fast path, heavy cross-tenant object sharing degrades badly. Prototype early;
   microbenchmark in the first slice (ROADMAP §4.7); design together with
   revocation-epoch invalidation.
2. **The locality hypothesis** — "most objects never cross fragment boundaries" — is
   assumed by Principle 1 and unmeasured. Validate on real js_com workloads.
3. **Gas-metering overhead** is claimed small; measure, don't assume.
4. **Compiler-output gap** — M1 measured *hand-written* C, the ceiling. Real maxim
   output lands between 28% and 96%, closer to the top the more direct the typed
   lowering. **M7 exists to measure exactly this** and is the confirmation gate for
   this entire document.

## 6. Scenario summary (v2 table, updated with measured entries)

| Scenario | Expected net | Basis |
|---|---|---|
| Static multi-tenant nginx, stage 1, no eval | ~zero overhead; some paths faster (frozen ICs) | modeled |
| Fully-typed compiled tenant policy | **90–96% of stock** | **measured (M1 ceiling)** |
| Interpreted policy framework | **28–50% of stock** | **measured (M1/Table A′)** |
| Directive-expressible subset | parity with stock | **measured (M1 control)** |
| Heavy cross-tenant object sharing | decided by §5.1 — the performance risk | modeled |
| Hook/membrane-heavy policy | expensive by construction; ladder keeps you off this rung; static membranes compile away | modeled |
| Live rewrite window (class F) | transient drop to interpreted speed for the rewritten node until re-AOT | modeled, bounded |
| REPL / dynamic eval | episode-per-input, budgeted | modeled |
| Revocation storm (CVE response) | one epoch bump + IC re-warm; bounded, rare | modeled |

**Practical consequence (v2, retained):** cost-transparent descriptors — every
operation class in `describe()` carries a documented cost class (O(1) bitmap /
IC-cached / JS-call), so policy authors see the price as they write.
