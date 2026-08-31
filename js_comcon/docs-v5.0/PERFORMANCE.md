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
| Fully static policy, compiled through (maxim target shape) | **90–96%** | **measured** (M1 hand-C) |
| Declarative-only policy, engine-consulted tables (stage 1) | ~85–95% *(model)* | modeled |
| Policy with residual runtime membranes on hot paths | strongly workload-dependent | modeled |
| Fully interpreted policy framework (mirror today) | **28–50%** | **measured** |

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

**What is NOT yet measured:** every intermediate point — stage-1 declarative tables,
membrane dispatch, compiler-produced (rather than hand-written) C. That is milestone
M7's whole purpose; expectations below are models.

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
