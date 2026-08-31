# COMCON — The Verification Track (V1–V15, v5.0)

*Companion to `ROADMAP.md` §12. The R-review (ROADMAP §11) hunted design bugs; this
track answers a different question: for every claim the design makes, **what would
convince a skeptic it is true** — and where is that machinery missing? Two items
turned out to be design decisions (V1, V2 — adopted immediately); the rest are
verification obligations attached to milestones. Rule of thumb throughout:
**generated over maintained, asserted over assumed, verified over tested, tested over
argued.***

---

## Design decisions (adopted in v5.0)

**V1 — The numeric model (user decision: JS semantics, doubles).** Erasure soundness
("types never change semantics") was unverifiable because the numeric semantics of the
typed integer was unspecified: a counter crossing 2⁵³ saturates in T1 doubles but would
keep counting in a naive T2 int64 — T1 ≢ T2 exactly where differential testing looks.
Resolution (normative, SEMANTICS §6): **JavaScript double semantics governs both
tiers**; BigInt stays explicit-only and outside the profile; the typed integer is
**`int` — a safe-integer refinement of double** (integral, |x| ≤ 2⁵³−1); compiled C
uses native `int64_t` representation *only under proven range bounds*, else computes
in IEEE doubles (still native-fast). Verified by boundary-value conformance tests
(2⁵³ ± 1, −0, NaN propagation) in the differential suite.

**V2 — Schema-hash pinning at artifact load.** Pin-by-hash protects against *content*
drift but nothing verified an artifact against the *current schema*: a registry/engine
upgrade changing an op's type or effect class would leave cached signed artifacts
serving with stale assumptions baked into their C. Resolution (ROADMAP §10): the
fragment artifact records the **schema hash it was admitted under**; the loader
verifies compatibility at every load; incompatible ⇒ loud re-admission, old epoch
keeps serving (R7).

---

## Verifying what we prove

**V3 — Executable reference semantics (before mechanization).** The No-Amplification
theorem is a hand sketch. The highest-value first step is not Coq: it is an
**executable reference implementation of the kernel rules** (a few hundred lines of
plain JS) used as an oracle, differentially tested against the real engine on every
admission-relevant operation — catching *implementation drift from the model*, which
proofs of the model alone never see. Mechanization (Lean/Coq — the monotonicity and
stone lemmas are small) follows for M8.

**V4 — Monotonicity as an assertion, not only a theorem.** The theorem holds *given*
an unforgeable TCB; a TCB bug currently fails silently. Environments are finite and
capabilities registry-typed, so `A*(child) ⊆ A*(parent)` is mechanically checkable:
the kernel **asserts the lattice inclusion at every grant/bind at admission time**
(SEMANTICS §6, implementation note). Cheap; converts TCB bugs into loud failures.

## Verifying the compiler (upgrading M8 from "tested" to "verified")

**V5 — Beyond allow-suite differential testing.** (a) **Translation validation for
the loop-free profile**: straight-line typed fragments over a small stub vocabulary
are exactly where per-artifact symbolic equivalence (T2 vs T1) is decidable and cheap
— upgrading the flagship claim to *"for the declarative profile, T2 ≡ T1 is verified
per artifact."* (b) For everything else: **coverage-guided differential fuzzing**
(random typed programs and inputs, csmith-style), not only the curated suites.

**V6 — Gas-placement invariant on emitted C.** R4 promises back-edge gas; verify it
mechanically: a CI check over every emitted C function's CFG asserting **every cycle
contains a gas check** and **every allocation goes through a metered stub**. Without
this, R4 is a promise.

## Verifying maintained metadata (the quiet rot vector)

**V7 — Enumerations are generated, never maintained.** The three closed enumerations'
*completeness* is load-bearing, and hand-maintained lists rot. By construction:
p_symbols exported **from the parser's production table**; compile portals enforced by
a build-time assertion that every call site of the internal compile entry is
enumerated; ops-resource caps emitted by the same S4 registry walk. Drift = build
failure.

*(v5.1 — E10)* **One generator, two outputs:** the conformance-test generator below and
the `comconctl dev` capability-doubles generator are the same registry walk with two
emitters — build it once (the walk's fourth consumer, after types, facet audit, and
config-surface types). *(v5.1 — E7)* **Two-lane CI:** fast lane per commit (unit +
kernel oracle + drift/enumeration checks); slow lane nightly (differential fuzzing,
mutation testing, TLA+, reproducibility) — the verification track must not make the
edit-test loop slow.

**V8 — Schema conformance tests, generated per registry row.** The typed tier trusts
the schema's word about C stubs. Auto-generate property tests from the registry (the
third consumer of the M2+S4 walk): call each op across its typed domain; verify result
types **and effect classes** — "pure-read mutated nothing," "worker-local didn't leak
cross-worker" (two-worker observation harness). A misclassified safety class currently
fails silently; this makes it fail in CI.

**V9 — The describe⊇mutable drift check extends to POM node-kind rows** (the js_com
discipline, automated, covering the program instance too).

## Verifying the protocols

**V10 — Model-check the epoch machinery.** The formal semantics is single-threaded;
class-F fan-out, two-phase epoch groups (R10), rollback — and especially **worker
crash/respawn mid-flip and master reload during a rollout** — have no model. One small
TLA+/Spin spec; the monotone-rollout property ("partial meet = meet") becomes a
checked invariant of that model rather than a slogan. Lands with M6.

## Verifying the policies themselves

**V11 — Policy mutation testing (the deny-suite's own verifier).** A deny-suite with a
hole is invisible today. Mechanically **widen one permit** in a policy (add a name,
relax a predicate): some deny-suite test must fail — the mutant must be killed.
Surviving mutants are an exact map of the cage's untested boundaries. Composes with
the asymmetric-failure story; candidate for a patent dependent claim.

**V12 — Golden denial-code corpus.** Denial codes are the tenants' CI contract
(MANUAL §3.2); a frozen (probe → expected code) corpus verifies code stability across
releases.

**V13 — Erasure spot check.** Run allow-suites on plain `qjs`/node (annotations
ignored, capability doubles) vs admitted T1; must agree — keeps Principle 11 honest
mechanically, forever.

## Verifying the build

**V14 — Reproducible builds.** The compile→sign→cache story assumes deterministic
compilation; nothing verifies it. CI: same fragment artifact + pinned toolchain ⇒
bit-identical `.so`. Without it, signatures attest provenance but not content
equivalence. (Trusting-trust accepted as residual, noted.)

## Assembling it

**V15 — The assurance case (the umbrella, at M8).** Claims are scattered across a
theorem, two gates, probe suites, and a dozen invariants. One GSN-style
**claim → assumption → evidence** tree: the artifact security reviewers actually want,
and building it is itself a gap detector — every leaf without evidence is a finding.
U1–U3, F, the generated enumerations, and V1–V14 get their permanent home here.

---

## Placement (mirrors ROADMAP §12)

| Now / M2–M3 | M5–M6 | M7 / M8 / M-SES |
|---|---|---|
| V1 ✅ decided · V2 ✅ decided · V3 · V4 · V7 | V5a · V6 · V8 · V9 · V13 | V5b · V10 · V11 · V12 · V14 · V15 |

**Meta-observation:** the R-review's critical findings clustered at *tier boundaries*
and *check-time↔use-time seams*; the V-track's biggest gaps cluster at **maintained-
metadata rot** (V7–V9) and **the distance between "tested" and "verified"** (V3–V6).
Both patterns say the same thing: wherever a human keeps two artifacts in agreement by
discipline, replace the discipline with generation, assertion, or proof.
