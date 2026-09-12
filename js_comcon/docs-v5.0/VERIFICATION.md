# COMCON — The Verification Track (V1–V15, v5.0)

> **UPDATE (v5.35).** The confinement was carried onto the `comcon.include` mechanism (the
> convergence), which re-exercised the audits: (a) **SR-2 faithfulness now holds for include** —
> `t/comcon_include_faithfulness.t` proves T2-refines-T1 over the confinement surface on both
> builds for confined include fragments; (b) **SR-1's HIGH-1 (getter-reach-leak) and MEDIUM-2
> (framing) were re-fixed in the SHARED response path** — they had been fixed only inside the old
> tenant content handler; migrating confined handlers onto `req.respond`/`location.handler`
> exposed that the general path lacked them (plus a CRLF-injection), now all three fixed in
> `ngx_js_request_respond` + the include invoke, protecting every js_com handler; (c) **TM-1
> denial-log quotas + sampling is IMPLEMENTED** and tested (`comcon_include_denial_log.t`: 100
> full records + 1/100 sampling, exact counters) — previously "specified, not implemented." SR-3
> (escape completeness) and M-SES-0/1/1b are unchanged. The standing open gate remains **SR-4**
> (assurance case) and **maxim finalization** (full test262 for untrusted-native).
>
> **UPDATE 2026-09-11:** maxim finalization's conformance blocker is **CLEARED** — the
> test262 JIT sweep that aborted at 54% on an atom-table assertion now completes
> 14/14 shards over 49,402 files with 55 failing files, all 55 in the known-errors
> baseline, 0 new, 0 crashes. And **M-SES now has a STANDING gate** (S6) rather than
> only the SR-3 pentest: see the gate table below. SR-4 remains the open gate.

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

**V3 — Executable reference semantics (before mechanization). ✅ BUILT 2026-09-12.**
The No-Amplification theorem is a hand sketch. The highest-value first step is not Coq:
it is an **executable reference implementation of the kernel rules** (a few hundred lines
of plain JS) used as an oracle, differentially tested against the real engine on every
admission-relevant operation — catching *implementation drift from the model*, which
proofs of the model alone never see. Mechanization (Lean/Coq — the monotonicity and
stone lemmas are small) follows for M8.

**The model:** `t/tools/kernel-oracle.js`, written from the RULES rather than from the
implementation (an oracle derived from the code it checks agrees by construction and
detects nothing). It shares no code with `src/js` and never calls `comcon`. Rules
modelled: **R-ENV** (a fragment sees exactly the granted names), **R-ADMIT** (admission
is on iff `imports` is present; every free global outside the manifest refuses),
**R-MEDIATE** (the vocabulary is closed; `revoke` withholds; unknown is refused),
**R-MEET** (re-mediation is the meet), **R-ZERO** (revoke absorbs).
`t/comcon_v3_oracle.t` runs a **generated** corpus — 14 mediation chains × 5 admission
settings — through both and compares field by field, after two instrument checks (the
corpus is large; the model's predictions *discriminate*, since an oracle that predicts
one answer agrees with an engine that does anything).

**IT FOUND A DIVERGENCE ON ITS FIRST RUN, and the divergence is a POLICY QUESTION, not
a bug to quietly fix.** The C3 gate has a deny list (`eval`, `Function`, `globalThis`,
`global`, `self`) and **no intrinsics allowance**: `undefined`, `JSON`, `Object`, `Math`
are free globals like any other and must appear in `imports`. So an ordinary
`x !== undefined` is refused unless the operator declares `undefined` — a name nothing
is granted for. Two consequences worth deciding on rather than inheriting:

1. `std.profiles.pure_library` (`imports: []`) can compute arithmetic but cannot use
   `JSON` or `Object` — it is stricter than "cap-free" suggests.
2. The gate is inconsistent with the compartment it guards: the intrinsics it refuses to
   let a fragment *declare* are ones the compartment *provides*, and a fragment with
   admission OFF uses them freely.

**RESOLVED BY DECISION (user, 2026-09-12): a third category.** A free name is now DENIED
(`eval`, `Function`, `globalThis`, `global`, `self` — no manifest re-admits them),
**INTRINSIC** (a language value to compute with: admitted without declaration), or
DECLARABLE (everything else, including every host name). The intrinsic list is short on
purpose — `undefined`/`NaN`/`Infinity`, `Object`/`Array`/`String`/`Number`/`Boolean`/
`BigInt`/`JSON`/`RegExp`, `Map`/`Set`/`WeakMap`/`WeakSet`, the `Error` types,
`parseInt`/`parseFloat`/`isNaN`/`isFinite`, the URI helpers — because the cost of omitting
one is a word in a manifest while the cost of wrongly including one is a silently wider
gate.

**`Date` and `Math` are deliberately NOT on it**: clock and RNG are the side channels §3
leaves open, so a fragment that wants them declares them. Nor are `Promise` (scheduling
past the invocation the deadline measures), `Symbol` (`Symbol.for` is a runtime-wide
registry — a channel between fragments, not a value), `Proxy`/`Reflect` (object-graph
tampering over held values, capabilities included), or the `ArrayBuffer` family
(`SharedArrayBuffer` is a channel, and the family travels under one rule).

This widens a **declaration requirement, not a reach** — every intrinsic was already
reachable inside the compartment, and a fragment with admission OFF used them freely; the
gate had been stricter than the boundary it guards. Recorded in `AUDIT_M-SES.md` §6 as a
change made after the signature, since the audited surface moved. The diagnostic also
changed: *"free name not declared in imports"* rather than *"not granted"*, because the old
wording sent a reader looking for a missing capability.

**The allowance NARROWS per contract** *(added the same day, on the question "can a policy
suppress the intrinsics?" — the answer was no, and that was a regression the allowance
introduced: `{imports: []}` used to mean "no free names at all", and afterwards the
strictest expressible setting was "intrinsics and nothing else")*. `contract.intrinsics`
takes a list and the effective allowance is **the static list MEET that list**:

```js
{ imports: ['s'] }                       // the full allowance (default)
{ imports: ['s'], intrinsics: [] }       // no free names at all — the old strictest
{ imports: ['s'], intrinsics: ['JSON'] } // exactly JSON; Object is refused
```

**`imports` declares, `intrinsics` narrows** — two knobs pointing one direction each, so
nothing here widens authority. Naming a non-intrinsic in `intrinsics` is **refused** with a
message saying to use `imports`, rather than ignored, because a contract word that reads
like policy and does nothing is the failure this project keeps meeting. A narrowing also
switches admission **on** by itself (it would otherwise be inert with the gate off), it
survives `realize()` — the path `bindAt` and `std.ops.rebind` take, where dropping it would
silently un-narrow a live rewrite — and a **present-but-malformed** narrowing reads as the
strictest setting, the same fail-closed direction a malformed `imports` takes.

`std.profiles.tenant` and `pure_library` pass `opts.intrinsics` through; **neither defaults
to it**, since tightening a shipped profile silently would break fragments already computing
with `JSON`. `pure_library({intrinsics: []})` is the strictest contract expressible.

The oracle models all of it, so the differential keeps the decision honest, and
**`check-enumerations.py` check 4 compares the engine's list with the model's copy and
fails if `Date`/`Math` are ever added** — a decision that nothing checks is a decision that
gets undone by a convenient edit.

**V4 — Monotonicity as an assertion, not only a theorem. ✅ BUILT 2026-09-12.** The
theorem holds *given* an unforgeable TCB; a TCB bug currently fails silently.
Environments are finite and capabilities registry-typed, so `A*(child) ⊆ A*(parent)` is
mechanically checkable: the kernel **asserts the lattice inclusion at every grant/bind at
admission time** (SEMANTICS §6, implementation note). Cheap; converts TCB bugs into loud
failures.

Asserted in the two places authority could grow on the way down:

- **Re-mediation** now computes the **attenuation meet** — field masks are a lattice, so
  the meet is an AND — and asserts the inclusion. Before this, re-mediating an
  already-mediated capability failed with *"grant is not a NginxSocket"*: fail-closed by
  accident, with a message about the wrong thing, because the translation unwraps one
  facet level and found another. A glob has no computable meet, so a `routes` facet is
  re-mediated only by an identical glob and otherwise **refused** — guessing would be the
  widening this exists to prevent. `revoke` is the zero and absorbs.
- **Realization** asserts that the restricted env is a **sub-map** of the realizer's, name
  by name and value by value. True by construction, which is exactly why it is checked: a
  substituted cap or an extra name is the shape a TCB bug takes, and it would otherwise
  confer authority nobody granted, silently.

`t/comcon_v4_monotonicity.t` asserts the *widening attempt is defeated* rather than that a
call throws: the fragment still cannot read the field the inner membrane hid.

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

**V7 — Enumerations are generated, never maintained. ✅ BUILT 2026-09-12.** The three
closed enumerations' *completeness* is load-bearing, and hand-maintained lists rot —
silently, since nothing fails when a list stops matching the code.
`t/tools/check-enumerations.py` derives each from the source and fails on drift;
`t/comcon_enumerations.t` runs it, so drift breaks the suite (this project's spelling of
"drift = build failure").

1. **p_symbol kinds** — the C enum, POM.md's documented schema, and the JS selector layer
   must agree, and `cstKind` may only ever produce kinds from the schema. This is where it
   already drifted: a `FunctionDeclaration` reported *stmt* at the CST tier and *function*
   at the bytecode tier, so `query('function')` silently found nothing at one of them.
2. **Compile portals** — every place `src/js` turns text into code, keyed by (file,
   enclosing function) so ordinary edits do not churn the list. A new compiling function
   is a failure until it is enumerated *with what it compiles*. 13 functions, 20 call
   sites today, including the operator REPL — an operator session **is** a compile portal.
3. **Ops-resource capabilities** — `std.ops` builds its session from `OPS_RES`, so that
   table decides which verbs exist; every entry must appear in FOUNDATION §8a, or the prose
   describes a smaller kernel than ships. **It found exactly that on the first run:** the
   audit/enforce/learn *mode switch*, which the rollout verbs decompose over, was in the
   code and missing from §8a's list of seven.

*Not yet by construction:* the p_symbol list is *checked* against three sources rather than
*emitted* from one, and the portal list is an allow-list rather than a generated one. Both
make drift loud, which is the property V7 is for; generation proper belongs with the M2.5
registry walk (E10, one generator two outputs).

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

**V11 — Policy mutation testing (the deny-suite's own verifier). ✅ BUILT 2026-09-12.**
A deny-suite with a hole is invisible today. Mechanically **widen one permit** in a policy
(add a name, relax a predicate): some deny-suite test must fail — the mutant must be killed.
Surviving mutants are an exact map of the cage's untested boundaries. Composes with
the asymmetric-failure story; candidate for a patent dependent claim.

*Built early — it is listed under M7/M8 but needs nothing from the compiler, and it is the
systematic form of the negative-control discipline this project already applies by hand.*
A negative control asks *does this test fail when I break the code*; mutation testing asks
the harder question: **does the suite notice when the POLICY gets weaker?** For a capability
system that is the question that matters, because a regression there does not look like a
crash — it looks like a permit nobody asked for. Every defect found by hand this month had
that shape: a typo'd mediation flavor that granted FULL authority, a mode switch that
reported success and changed nothing, an admission gate that refused ordinary JS.

`t/tools/policy-mutants.js` emits widen-one-permit variants of a base policy — one more field
through the membrane, the membrane removed, one more name in the manifest, the intrinsics
narrowing relaxed or dropped, `checkRequest` off, the meter off.
`t/comcon_v11_mutants.t` records the deny-suite's outcomes under the base policy and re-runs
them per mutant: **a mutant is killed if any outcome differs, and a survivor is the finding** —
the policy grants one more permit and the suite cannot tell. Result: **12 mutants, all killed,
none survived.**

**Equivalent mutants are declared, not discovered, and checked in the other direction.**
`imports+eval` cannot widen anything (the deny list refuses `eval` whatever a manifest says),
so it MUST survive; a suite that killed it would be reporting authority that does not exist.
Two-sided, so neither a lazy suite nor an over-eager one passes.

**It corrected a belief on its first run.** It killed `imports+JSON`, which had been labelled
equivalent on the assumption that `intrinsics: []` excludes `JSON` outright. It does not:
`intrinsics` removes the **no-declaration free pass**, it does not stop a name from being
DECLARED, so `{intrinsics: [], imports: ['JSON']}` permits `JSON`. Coherent, but not what "the
narrowing excludes JSON" sounds like — and the mutation run is what said so, about a policy
written two commits earlier. Three controls, all mutating the HARNESS rather than the engine,
since the suite is what V11 is about: drop a probe and the matching mutant survives; make every
probe report one outcome and everything survives; declare a real widening equivalent and the
two-sided check fires.

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
*(v5.4)* Its skeleton now exists: **`THREATS.md`** — the adversary × asset ×
mitigation matrix with every cell citing its closing mechanism and three residuals
accepted by name. V15 = that matrix extended with evidence links as the V-track
deliverables land.

---

## Placement (mirrors ROADMAP §12)

| Now / M2–M3 | M5–M6 | M7 / M8 / M-SES |
|---|---|---|
| V1 ✅ decided · V2 ✅ decided · V3 ✅ · V4 ✅ · V7 ✅ (all 2026-09-12) | V5a · V6 · V8 · V9 · V13 | **V11 ✅ (2026-09-12, built early)** · V5b · V10 · V12 · V14 · V15 |

**Meta-observation:** the R-review's critical findings clustered at *tier boundaries*
and *check-time↔use-time seams*; the V-track's biggest gaps cluster at **maintained-
metadata rot** (V7–V9) and **the distance between "tested" and "verified"** (V3–V6).
Both patterns say the same thing: wherever a human keeps two artifacts in agreement by
discipline, replace the discipline with generation, assertion, or proof.

---

## Security-review cadence (v5.12)

Reviews are **not** scheduled by step count — a review keyed to "every N steps"
becomes a rubber stamp. They are placed at **inflection points**: where a *new class
of adversary* becomes relevant, a *correctness property becomes load-bearing for the
first time*, or the *cost-to-fix a flaw jumps*. Between those points the load is
carried by the per-slice discipline (below), not by a checkpoint. There are only four
gate-reviews across the whole roadmap, and two already exist as milestones (M8, V15):

| Gate | Fires | Scope | Maps to |
|---|---|---|---|
| **SR-1 conformance** | end of increment B → **before C** | the A/B capability logic vs `THREATS.md`, *within* the current TCB assumption (an unforgeable engine). Finds gaps between claimed and implemented confinement. Explicitly **not** engine escapes. | new (this doc) |
| **SR-2 faithfulness** | **C7** | did compilation preserve the reach gates and not leak authority via the type/cap side-tables? T2 refines T1. | **= M8** — **PASSED profile-scoped 2026-09-01** (`t/comcon_faithfulness.t`: interp vs AOT-compiled over the confinement surface incl. A1 gated reach/mutator → identical responses + identical denials, 22/22). Scope = confined strict-module profile; full-test262-under-AOT + untrusted-native production still gated on M-SES + full maxim finalization. |
| **SR-3 adversarial pentest** | after **M-SES** | engine escapes, eval/Function/Proxy sandbox completeness, memory safety — closes the accepted residuals (`THREATS.md` T8/T4/T9). The full red-team pass. | **PASSED 2026-09-01** — no sandbox escape (dynamic-code routes tamed, no global reach, core intrinsics frozen, recursion bounded); one MEDIUM freeze-completeness gap (SR3-1 sibling iterator prototypes) **found + fixed**, one availability case (SR3-2 microtask loop) **gas-contained**. Both tiers. See the SR-3 audit record below. Full-test262-under-AOT untrusted-native still gated on maxim finalization. |
| **SR-4 assurance case** | before first untrusted-tenant **production** | assemble the whole claim→assumption→evidence tree; every leaf without evidence is a finding. | **= V15** |
| **S6 escape gate** (standing) | **every test run**, from 2026-09-11 | the M-SES gate as a REGRESSION gate rather than a moment: `t/comcon_mses_gate.t` (12 probes over gate conditions (a)/(b)/(c) + `.stack` + `Symbol.species`, plus the resource guard) and `bash t/run_sanitizers.sh` (the corpus under ASAN + UBSAN). | **NEW 2026-09-11** — see §"The standing gate" below |

### The standing gate (added 2026-09-11)

SR-3 passed as a **pentest**: it certifies a moment, not every subsequent commit.
Nothing failed when a facet was widened afterwards. `t/comcon_mses_gate.t` closes
that: it runs the probe battery on **every** suite run.

**Self-validating by construction.** Every probe runs twice — inside a confined
`comcon.include` fragment and in unconfined host JS — and the suite asserts the
two **differ**. A probe reporting "closed" in both contexts is not evidence of
confinement, it is a probe that never held the capability, and the suite fails on
it. Measured: **12/12 closed confined, 12/12 open unconfined**.

Coverage of the five M-SES conditions:

| | before | now |
|---|---|---|
| (a) value outside ρ | indirect (free-name gate) | probed (ambient roots, `.stack`, `Symbol.species`) |
| (b) mutate a frozen intrinsic | **not probed** | probed (prototype pollution, `Array.prototype` hijack, `String.prototype`) |
| (c) code from strings | 4 probes | 4 probes, in the gate |
| (d) COM facet beyond reach | route-glob + `comcon_include_grant.t` | unchanged (already covered) |
| (e) escape resource guards | **not probed** | probed — **and the probe found a real hole** |

**What (e) found.** The deadline was opt-in: `__invokeConfined` armed it only when
`contract.meter[...].timeoutMs > 0`, so a fragment with no meter ran **unbounded**
(measured 4474 ms to completion). An accidental infinite loop hung the worker with
no escape involved. Fixed — fragments are now always bounded; see
`OPERATOR_API.md` §3.

**Memory safety (`bash t/run_sanitizers.sh`).** 313 tests over the 34-file COMCON
corpus under each sanitizer: **ASAN 0 findings; UBSAN 0 findings in `src/js`**, 34
in stock nginx at one site (`ngx_pstrdup`, `src/core/ngx_string.c:84` — a
`memcpy(dst, NULL, 0)` at cycle init; upstream's, benign, reported not failed).
This replaces the `ok - no sanitizer errors` line the suites already printed, which
was **vacuous**: neither `objs/` nor `objs_jit/` is built with a sanitizer.

The script carries four guards, because a clean sanitizer run is the easiest false
negative there is — an inert build reports exactly what a clean one does: the
binary must carry the sanitizer's symbols; a **positive control** must land a
report through the same `prove` pipeline; the run must have **executed tests**;
and a file skipping with `no js module` means nginx could not start. The last two
exist because the script produced a vacuous PASS on itself twice while being
written (`prove` does not glob its arguments; `-fno-sanitize-recover` made stock
nginx's startup UB fatal, so every file skipped while the harness printed "All
tests successful").

**Why SR-1 before C, specifically:** A/B *are* the entire confinement surface; C is a
performance layer that must preserve it. And C's correctness argument is *differential*
— T2 is proven only to match T1 — so a hole in T1 (the interpreted A/B enforcement) is
faithfully reproduced in native code. Validating T1 is therefore a **prerequisite** for
C, not optional. (C5 compiling the confinement *in* also makes a late fix far more
expensive.) The full pentest is deliberately deferred to SR-3: pre-M-SES the TCB is not
yet real and untrusted tenants are not yet exposed, so an engine red-team would be
premature.

**Within increment C there is no separate review step** — the review *is* the
methodology: a differential test on every slice (interpreted ≡ compiled, identical
outputs **and** identical denials) plus the SR-2/M8 gate. Injecting a checkpoint at C3
or C5 would only duplicate it.

**The continuous piece is a per-change discipline, not a review:** every commit that
adds a *granted capability* or a *new reach edge* must touch `THREATS.md` — "does this
open a cell?" Cheap, per-commit, no ceremony; it keeps the ledger live so the gate
reviews have less to rediscover.

### Front-end soundness audit (2026-09-01, between SR-1 and SR-2)

Not a gate review — a focused adversarial audit of the increment-C admission front-end
(C3.0/C3-rest/C3-types/C4) run empirically (≈30 evasion fragments through `nginx -t` +
runtime probes) when the front-end was complete but before building the compiled tier on
it. **Verdict: confinement HELD — no capability escaped in any vector** (dynamic code and
`globalThis[…]` reach only the deny-by-default tenant global; every ungranted host name —
`nginx`/`fetch`/`require`/`process`/`createSocket` — reads `undefined`). The free-name
walk proved robust (default initializers, computed keys, class `extends`/fields,
destructuring defaults, nested arrows all caught). Findings, all about *soundness claims*,
not breaches:

- **A1 (MEDIUM — claim, not escape): C3-rest does not eliminate dynamic code.** The
  Function constructor is reachable via `[].constructor.constructor`, `(function(){})
  .constructor`, the async/generator function constructors, and `Reflect.construct`;
  `import()` is admitted (inert without a loader). All bypass the `eval`/`Function`
  name-deny-list and the opcode scan. So "no dynamic code ⇒ the static analysis is sound"
  is **false**; the manifest's completeness rests on the global being deny-by-default, not
  on the absence of dynamic code. **Root cause = full-intrinsic `JS_NewContext` (LOW-6).
  The real fix is M-SES (curated intrinsics), which this audit ELEVATES from optional
  hardening to a hard prerequisite for C5 erasure soundness and C4 env-signature
  completeness.** ✅ **CLOSED by M-SES-0 (v5.19):** the tenant context is now
  `JS_NewContextRaw` + curated intrinsics (Proxy omitted) + an SES-style lockdown taming
  the four evaluator constructors and deleting the `eval`/`Function`/`Reflect` globals;
  `[].constructor.constructor(...)` now throws, so the "no dynamic code" property holds.
  (`t/comcon_mses.t`; INCREMENT_MSES.md.)
- **A2 (MEDIUM→fixed as DiD): reflective global aliases.** `globalThis`/`global`/`self`
  let `globalThis[<computed>]` reach a bound name (incl. a granted capability) invisibly
  to the manifest. **Refused now** in `ngx_js_c3_free_name` (defense-in-depth — the
  Function-ctor route still reaches `this`, so this is not a soundness fix; M-SES is).
- **A3 (LOW — type-check completeness, no security impact): a destructured Request
  parameter** (`onRequest(function({secret}){…})`) and **computed/aliased member access**
  bypass the sealed-Request field check; **rest params** (`function(req,...r)`) bypass the
  ≤1 arity check (`.length` ignores rest/defaults). Folded into the C5 erasure-complete
  type remainder; pinned by `t/comcon_frontend_audit.t` so closure is a visible change.

Regressions: `t/comcon_frontend_audit.t` pins the containment guarantee (dynamic code sees
zero host authority) and the A2 fix. The escape-completeness of the intrinsics themselves
remains **SR-3** (post-M-SES), as scheduled.

### SR-3 escape-completeness audit (2026-09-01, after M-SES)

The scheduled adversarial pentest of the **now-hardened** tenant context (curated
intrinsics + M-SES-0 evaluator taming + M-SES-1 intrinsic freeze + execution gas),
run empirically — escape fragments served through a live tenant handler, each attempting
to reach dynamic code / host authority / a shared mutable intrinsic and reporting what it
reached. **Verdict: no sandbox escape.** Every dynamic-code route stays tamed
(`(new Error()).constructor.constructor(…)`, bound-function `.constructor.constructor`,
`Array[Symbol.species].constructor.constructor` all throw `TypeError`); strict-mode
`this` is `undefined` (no global object reach); the core intrinsic prototypes
(`Object`/`Array`/`Function.prototype`, `%IteratorPrototype%`, the shared
`%Generator|AsyncGeneratorPrototype%`) are frozen; unbounded recursion is caught
(`InternalError`, stack-overflow guard at the call-site poll). Two findings, both
completeness, neither an authority escape:

- **SR3-1 (MEDIUM — freeze completeness gap, FIXED): sibling iterator instance-prototypes
  were mutable.** M-SES-1's harden walks property *values* from `globalThis`, so it never
  reaches a prototype that exists *only* as the result of *calling* a method —
  `%StringIteratorPrototype%` (`""[Symbol.iterator]()`), `%Map|SetIteratorPrototype%`, and
  `%RegExpStringIteratorPrototype%` (`"".matchAll(…)`). A tenant could write to them, and
  the write **persisted into the next request** (verified cross-request). Not an escape
  (no authority/Function reach), but the same class as the M-SES-1 threat: cross-request /
  cross-tenant prototype pollution, incompletely covered. **Fixed** by adding those three
  as explicit harden roots (`ngx_js_module.c` lockdown IIFE). Re-audit: all frozen, no
  cross-request poison, on both tiers. The generator *instance* proto (`(function*(){})()`)
  looked mutable but is the generator function's **own per-function `.prototype`** —
  isolated (function `a`'s poison invisible to `b`'s instances) and chaining to the already
  -frozen shared `%GeneratorPrototype%`, so it is left alone. Pinned by two new cases in
  `t/comcon_freeze.t` (frozen + no cross-request pollution).
- **SR3-2 (availability — CONTAINED, no fix): Promise microtask loop.** A handler that
  self-reschedules `Promise.resolve().then(loop)` builds an unbounded microtask chain
  drained (in C) after the handler returns. It does **not** hang the worker: the
  per-request execution gas fires inside the running microtask at the deadline (~1.01s
  measured, budget 1s), breaks the chain, and the worker keeps serving. Bounded by the
  same budget as any other CPU path; no separate mechanism needed.

Regressions: `t/comcon_freeze.t` (+2 SR-3 cases) pins the sibling-iterator freeze and its
cross-request non-pollution, confirmed identical on the interpreter and JIT builds. With
SR-3 clean, the confined tier's confinement is **adversarially validated**, not just
argued — the last major assurance step before untrusted tenants (full maxim finalization
remains the separate compiler-conformance gate).
