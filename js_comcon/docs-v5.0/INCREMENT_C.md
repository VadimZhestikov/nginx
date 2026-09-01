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

- **C4 — M4: the fragment artifact ("fat bytecode").** bytecode + type/cap
  side-table + env-signature (we have this — the learn/grant machinery) + content
  hash (**reuse maxim's `jit_hash_*`**) + schema hash + admission cert. Generalizes
  B/E1's pin-by-hash from "a file" to "the artifact."

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

- **C7 — M8: the safety gate.** Compiler faithfulness = T2 refines T1, per fragment,
  by differential testing (same inputs → same outputs → same denials), plus proof
  the A1 gates survive compilation. **Increment C is not done until C7 passes.**

## 3. Gating and honest scale

- **M-SES gates *production* compiled-untrusted tenants**, not the build-out.
  C0–C6 can proceed as a **dev tier** (compiling trusted/first-party fragments);
  compiling genuinely untrusted tenants to native waits for the engine-hardening
  milestone. State this in the shipped config (a compiled tenant needs either
  M-SES-complete or a trusted-author declaration).
- **Scale, stated plainly.** A/B slices were day-scale and self-contained. C0 is
  the same (a spike). **C1 (M-UNIFY) and C5 (lowering the confinement semantics
  faithfully) are the two multi-day, genuinely hard pieces** — C1 is engine
  integration, C5 is the research-adjacent correctness core. The plan front-loads
  C0 precisely so the C1 scope decision is made on evidence (the measured tree
  delta + a working .so), not a guess.

## 4. Immediate next action

**Do C0.** It is cheap, it is a gate, and it converts every downstream estimate
from speculation to measurement: build maxim here, compile one trivial handler to a
`.so`, call it from a harness. If C0 is green, commit to C1 with the tree-delta
evidence in hand; if not, the increment is reassessed before any engine surgery.

*(Design references: ROADMAP §2 M-UNIFY/M2/M3/M4/M5/M6/M7/M8; SPEC §7 typed
profile, §8 artifact & tiers; PERFORMANCE.md the M1 endpoints; VERIFICATION V5
translation validation / V13 erasure spot check — the differential-test discipline
this plan leans on.)*
