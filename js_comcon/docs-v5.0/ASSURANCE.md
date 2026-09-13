# COMCON — The Assurance Case (V15 / gate SR-4)

> **Status: BUILT 2026-09-12 (v5.64). Not signed.** This is the tree; signing it is a
> separate act with a named signer, like `AUDIT_M-SES.md` §5. Read
> §"What this case does NOT establish" before quoting it anywhere.

VERIFICATION.md asks for "one GSN-style **claim → assumption → evidence** tree: the
artifact security reviewers actually want, and building it is itself a gap detector —
every leaf without evidence is a finding." ROADMAP's gate table calls the same thing
**SR-4**, fires it *before the first untrusted-tenant production*, and it has been the
one standing open gate since SR-3 passed.

**The rule that makes this document different from a summary:** every `EV:` line names an
artifact that must EXIST, and `t/tools/check-assurance.py` fails the build if one does
not, if a leaf has neither evidence nor a declared gap, if a gap has no home, or if a
`t/comcon_*.t` exists that no claim cites. A claim tree nobody checks decays into
marketing at exactly the rate the code moves. See §11 (G11.4) — this file is evidence for
its own integrity.

**It detected its first defect before it was written.** Sweeping the doc set for cited
test files found **20 of 83 citations pointing at files that do not exist** — every one a
pre-CONVERGENCE name whose test was deleted in P6a/P6b when its `comcon_include_*` sibling
took over. A reviewer following THREATS.md's T11 citation to `t/comcon_gas.t` found
nothing at all. See §12 (the rename table) and finding **F1**.

---

## 1. Assumptions

These are the load-bearing "given"s. Every claim below is conditional on them, and an
assumption that turns out false invalidates the subtree above it — which is the point of
writing them down separately from the evidence.

- **ASSUME: A1 — the TCB is unforgeable.** The QuickJS engine, the C host functions a
  fragment may call, and the nginx process boundary are assumed correct. Capability
  discipline is not memory safety: a heap bug in the engine defeats everything above it.
  Reduced (never eliminated) by M-SES S1–S6 and the standing S6 gate; named in THREATS T8.
- **ASSUME: A2 — the M-SES audit is ONE attestation, not two.** `AUDIT_M-SES.md` §5 is
  signed, but both rows carry the same signer, and the §4 commands were run by the
  authoring session rather than independently reproduced. It attests acceptance of
  reproducible evidence; it is not separation of duties.
- **ASSUME: A3 — three residuals are accepted, not closed.** Engine memory safety (T8),
  information flow and timing channels between co-resident tenants (T4/T9), and
  availability-within-reach for a controller over its own subtree (T6).
- **ASSUME: A4 — a pentest certifies a date; a gate certifies a run.** SR-1/SR-2/SR-3
  passed on 2026-09-01 and say nothing about any commit after. What carries forward is the
  standing S6 gate and the regression suites cited below, which run on both builds.
- **ASSUME: A5 — host JS is trusted code.** Everything here bounds a CONFINED fragment.
  A `location.handler` written in host JS runs with host authority by design and is
  deliberately not bounded the same way (see F6).
- **ASSUME: A6 — the tests below run on both builds** (`objs` interpreted-tier default and
  `objs_jit`), and a fixture that needs more than one worker says so. A single-worker
  fixture cannot see a fan-out bug, which is how the mode-switch hole shipped (G5.5).

---

## 2. G0 — the top claim

> **G0:** A fragment admitted through `comcon.include` can exercise **only** the authority
> its environment grants, cannot amplify it, and what it attempted is attributable —
> on both tiers, across live rewrites, under the assumptions in §1.

Decomposed into G1…G11. G1–G3 are the front door (naming, narrowing, admission), G4 the
compiled tier, G5 attribution, G6 availability, G7 the TCB layer, G8 live mutation, G9 the
data boundary, G10 the config surface, G11 the meta-claim that none of it rots silently.

---

## 3. G1 — a fragment cannot NAME authority it was not granted

The primary control, and the one everything else is defence in depth for.

#### G1.1 — every free name must be declared, and the gate refuses what is not
- **CLAIM:** A fragment's free global names must appear in `contract.imports`; anything
  else is refused at admission, before the fragment runs.
- **ARGUMENT:** A bytecode scan collects free globals and checks them against the manifest
  (deny-by-default); an independent oracle written from the rules agrees with the engine
  over a generated corpus.
- **EV:** `t/comcon_admit.t` — the `admit(fn, contract)` verdict for declared vs undeclared names.
- **EV:** `t/comcon_include_admit.t` — the same gate composed into `include`, incl. the reflective globals.
- **EV:** `t/comcon_v3_oracle.t` — differential against `t/tools/kernel-oracle.js` over 14×9 cases.
- **EV:** `t/tools/kernel-oracle.js` — the oracle itself, sharing no code with `src/js`.
- **THREAT:** T1, T3
- **V:** V3

#### G1.2 — an ungranted name is not merely refused, it is absent
- **CLAIM:** Inside the compartment there is no ambient host authority to reach for: the
  withheld name does not exist rather than being blocked.
- **ARGUMENT:** The compartment's global is built deny-by-default; the reach gates are the
  observable second layer, not the primary one.
- **EV:** `t/comcon_include_deny.t` — a grant-less fragment finds no host surface, no IO, no `nginx.shared`.
- **EV:** `t/comcon_include.t` — scope isolation between fragments and from the host.
- **EV:** `t/comcon_operators.t` — `env`/`grant`/`bind` semantics: a fresh env grants nothing.
- **THREAT:** T1, T4
- **V:** V3

#### G1.3 — dynamic code cannot smuggle a name past the scan
- **CLAIM:** A fragment cannot construct code at runtime to reach a name the static scan
  never saw.
- **ARGUMENT:** Admission refuses `eval`/`Function`/`with` (`E_ADMIT_DYNCODE`), and M-SES
  removes the reflective routes (`[].constructor.constructor`, generator/async
  constructors, `Reflect.construct`) that defeated the name deny-list alone — the
  correction recorded in VERIFICATION's front-end soundness audit.
- **EV:** `t/comcon_include_mses.t` — the lockdown surface: dynamic-code routes throw.
- **EV:** `t/comcon_declarative_fuzz.t` — property fuzz of the admission path (found a real comment-terminator escape).
- **EV:** `t/comcon_v12_denial_codes.t` — `E_ADMIT_DYNCODE` fires for a direct `eval` call.
- **THREAT:** T1, T8
- **V:** V12

#### G1.4 — a malformed contract fails CLOSED
- **CLAIM:** A contract field that is present but unusable refuses admission rather than
  being ignored.
- **ARGUMENT:** Two fail-open defects were found and closed by exactly this reasoning: an
  unknown mediation flavour once granted a capability IN FULL, and a non-string `tests`
  was silently skipped. Malformed `imports`/`intrinsics` read as the STRICTEST setting.
- **EV:** `t/comcon_include_contract_fuzz.t` — the contract's own shape under mutation.
- **EV:** `t/comcon_std_lib.t` — the closed mediation vocabulary; the fail-open it found.
- **EV:** `t/comcon_v12_denial_codes.t` — `E_ADMIT_CONTRACT` for a `tests` that cannot run.
- **THREAT:** T1, T3
- **V:** V12

---

## 4. G2 — authority can only narrow (monotonicity)

#### G2.1 — a membrane attenuates and never widens
- **CLAIM:** `mediate(cap, interceptor)` yields strictly less authority than `cap`.
- **ARGUMENT:** A closed flavour vocabulary, refused at the producer; the descriptor is
  snapshotted at `mediate()` so the check and the use cannot disagree (a TOCTOU that had
  reopened the fail-open).
- **EV:** `t/comcon_mediate.t` — attenuation-only membranes over a live capability.
- **EV:** `t/comcon_com_facet.t` — a COM node mediated into an attenuated facet.
- **EV:** `t/comcon_com_facet_mutate.t` — the gated mutation slice of that facet.
- **THREAT:** T1, T6
- **V:** V4

#### G2.2 — monotonicity is ASSERTED at runtime, not only proven on paper
- **CLAIM:** Re-mediation computes the attenuation meet; a composition with no computable
  meet is refused rather than guessed; `realize()` asserts the restricted env is a sub-map
  of the realizer's.
- **ARGUMENT:** The No-Amplification theorem holds given an unforgeable TCB; a TCB bug
  would otherwise fail silently, so the invariant is checked where it is used.
- **EV:** `t/comcon_v4_monotonicity.t` — the lattice inclusion as a live assertion.
- **THREAT:** T1, T6, T12
- **V:** V4

#### G2.3 — a proposal cannot borrow the realizer's authority
- **CLAIM:** Realizing a tenant's quotation binds it to the realizer's env RESTRICTED to
  the declared manifest — the confused-deputy fix (R6).
- **EV:** `t/comcon_realize.t` — least-authority realization; a closure is refused as arg0.
- **THREAT:** T5, T6
- **V:** V4

#### G2.4 — the deny-suite NOTICES when policy gets weaker
- **CLAIM:** A regression that widens a permit is caught, not merely a regression that breaks one.
- **ARGUMENT:** Mutation testing: widen-one-permit variants of the policy must all be
  killed by the deny-suite; declared equivalent mutants must survive.
- **EV:** `t/comcon_v11_mutants.t` — 12 mutants, all killed; equivalents declared.
- **EV:** `t/tools/policy-mutants.js` — the mutant generator.
- **THREAT:** T1, T3
- **V:** V11

#### G2.5 — only a mediatable capability may cross into a compartment
- **CLAIM:** What crosses the boundary is what the host can wrap; anything else is refused.
- **EV:** `t/comcon_include_grant.t` — live-cap grants and their refusals.
- **EV:** `t/comcon_v12_denial_codes.t` — `E_CAP_GRANT` for a grant of the wrong type.
- **THREAT:** T1, T4
- **V:** V12

---

## 5. G3 — what was reviewed is what runs (admission integrity)

#### G3.1 — the artifact is pinned by identity
- **CLAIM:** A fragment whose source (or admitted schema) drifts is refused at load; the
  old epoch keeps serving.
- **EV:** `t/comcon_include_admit.t` — the identity pin `H(H(source) ‖ schema-version)`.
- **EV:** `t/comcon_v12_denial_codes.t` — `E_PIN_IDENTITY` on mismatch.
- **THREAT:** T2, T12
- **V:** V2

#### G3.2 — dependencies are cages with hashes
- **CLAIM:** A pinned pure-library dependency is admitted only if its bytes hash to the
  pin, and it runs with what the consumer granted, never what it requests.
- **EV:** `t/comcon_include_deps.t` — pin-by-hash, refusal on drift, bare-env evaluation.
- **THREAT:** T2
- **V:** V2

#### G3.3 — behavioural admission runs with zero blast radius
- **CLAIM:** Contract tests run against the compiled fragment INSIDE the compartment, so a
  test cannot be the escape.
- **EV:** `t/comcon_admit_tests.t` — a throwing test refuses admission; the run is confined.
- **THREAT:** T3
- **V:** V12

#### G3.4 — the declarative profile is a SOUND REJECTER
- **CLAIM:** Everything the declarative checker accepts is in the subset; what it cannot
  prove, it refuses.
- **ARGUMENT:** Differential against `new Function(src)` as an independent oracle — and the
  fuzzer found a real escape (a `//` comment ended at LF only, so a bare CR hid live code
  from the review artifact).
- **EV:** `t/comcon_declarative.t` — the checker and its descriptor tables.
- **EV:** `t/comcon_declarative_fuzz.t` — the differential fuzz that found the escape.
- **THREAT:** T3, T10
- **V:** V5a

#### G3.5 — the parser is TCB, vendored and fail-closed
- **CLAIM:** The ES parser used for review is a pinned vendored artifact; if it is missing
  or altered, analysis refuses rather than degrades.
- **EV:** `t/comcon_parser_vendor.t` — provenance, sha256, fail-closed `comcon.__parse`.
- **THREAT:** T2, T8
- **V:** V14

#### G3.6 — call chains are typed at admission
- **CLAIM:** The admission-time check types whole call chains against the registry, not
  just leaf names.
- **EV:** `t/comcon_review_calls.t` — `reviewCalls(source, grants)` over chains.
- **THREAT:** T1, T10
- **V:** V8

---

## 6. G4 — compiling changes nothing that matters (tier faithfulness)

#### G4.1 — T2 refines T1 over the confinement surface
- **CLAIM:** The same fragment interpreted and AOT-compiled produces identical responses
  AND identical denials, gates included.
- **EV:** `t/comcon_include_faithfulness.t` — the SR-2 gate for `include`, on both builds.
- **THREAT:** T1, T7
- **V:** V5a, V6

#### G4.2 — a live rewrite is answered by the new epoch, never the old `.so`
- **CLAIM:** Replacing a fragment that was lowered to native C cannot serve the superseded
  compiled code; rollback restores the retained epoch.
- **ARGUMENT:** The re-AOT half is deliberately absent (the gcc thread does not survive
  `fork()`), so the new epoch runs the bytecode fallback and SAYS SO.
- **EV:** `t/comcon_aot_epoch.t` — NATIVE at load, BYTECODE at request time, same fragment.
- **THREAT:** T1, T7
- **V:** V6

#### G4.3 — "compiled" is a measured fact, not an assumption
- **CLAIM:** Claims about the compiled tier are made against a read-only tier probe, not
  against the intent to compile.
- **ARGUMENT:** `js_comcon_aot_compile()` returns 0 for any bytecode function — "eligible",
  never "compiled" — and the include site logged success on that 0 for weeks.
- **EV:** `t/js_jit_compile_aot.t` — AOT compilation of host JS at load.
- **THREAT:** T7
- **V:** V6

---

## 7. G5 — the platform can say what happened (attribution)

#### G5.1 — denials are counted exactly, under stable codes
- **CLAIM:** Every gate denial increments an exact per-code counter that a tenant's CI can
  pin to, and the code set cannot drift silently.
- **EV:** `t/comcon_v12_denial_codes.t` — the golden corpus: each probe fires its own code.
- **EV:** `t/tools/golden-denials.js` — the frozen corpus itself.
- **EV:** `t/comcon_enumerations.t` — checks [5] and [6] tie both code sets to the C tables.
- **THREAT:** T6, T12
- **V:** V12, V7

#### G5.2 — audit mode observes before enforcing
- **CLAIM:** An operator can see the full would-be-denied reach without denying anything yet.
- **EV:** `t/comcon_include_audit.t` — the same probe allowed-and-logged under audit.
- **THREAT:** T5, T6
- **V:** V9

#### G5.3 — the audit signal cannot be flooded (TM-1)
- **CLAIM:** A fragment looping on a denied name cannot exhaust disk or drown other
  tenants' audit signal, and counting stays exact.
- **EV:** `t/comcon_include_denial_log.t` — 100 full records, 1/100 sampling above quota, exact counters.
- **THREAT:** T11, T12
- **V:** V9

#### G5.4 — onboarding harvests what a fragment wants, without granting it
- **EV:** `t/comcon_include_learn.t` — learn mode records reaches into the withheld surface.
- **THREAT:** T5
- **V:** V9

#### G5.5 — a mode switch reaches EVERY worker
- **CLAIM:** `enforce()` means the fleet enforces, not "this process does".
- **ARGUMENT:** Measured before it was believed: four workers, one `shadow()`, 24 requests
  gave 16 audit / 8 enforce — the fleet in mixed modes, with the dangerous direction the
  common one. The mode now lives in shared memory and each worker reconciles lazily.
- **EV:** `t/comcon_mode_fanout.t` — distinct workers asserted from a per-process identity.
- **THREAT:** T5, T6
- **V:** V9

#### G5.6 — administration is library code over capabilities, with no backdoor
- **CLAIM:** There is no management plane: an ops verb whose resource was not passed is
  ABSENT, visible by `Object.keys()`.
- **EV:** `t/comcon_std_ops.t` — 15 verbs over 7 enumerated resources; withheld verbs named.
- **THREAT:** T5, T12
- **V:** V7

#### G5.7 — a refusal says why, in a code that survives rewording
- **CLAIM:** Admission refusals carry a stable machine code, not only prose.
- **EV:** `t/comcon_v12_denial_codes.t` — 13 refusal codes, each probed; `comcon.refusalCodes()` enumerates.
- **THREAT:** T3, T12
- **V:** V12

---

## 8. G6 — a fragment cannot take the worker down

#### G6.1 — a confined fragment is ALWAYS bounded
- **CLAIM:** Even with no meter in the contract, a fragment runs under a deadline.
- **ARGUMENT:** With no meter the timeout arrived as 0 and armed nothing, so the DEFAULT
  configuration ran untrusted code unbounded; the default is now host-imposed.
- **EV:** `t/comcon_fragment_deadline.t` — a runaway fragment with no meter is aborted.
- **THREAT:** T11
- **V:** V6

#### G6.2 — the meter contract bounds what it says it bounds
- **EV:** `t/comcon_include.t` — `contract.meter` and the metered-runaway abort.
- **THREAT:** T11
- **V:** V6

#### G6.3 — teardown does not crash the process
- **EV:** `t/comcon_include_teardown.t` — build + tear down a fragment in single-process mode.
- **THREAT:** T11
- **V:** V6

#### G6.4 — a capability can be bounded by RATE, fleet-wide
- **CLAIM:** `mediate(cap, uses(key, limit, window))` limits how many times a capability
  may be exercised, counted across every worker, denied as `budget.uses` when exhausted and
  logged-and-allowed in audit mode.
- **ARGUMENT:** The counter lives in `nginx.shared`, not on the wrapper, so the operator who
  wrote `limit: 10` gets ten — not ten per worker, which is the defect the audit/enforce
  mode switch shipped with. Charged on every gated operation (a redacted read is free,
  because a field the membrane hides was never an exercise of the capability). Nothing is
  defaulted: a budget with no limit is refused as a mistake rather than read as unlimited,
  and re-mediating with a DIFFERENT budget is refused because budgets are not ordered.
- **EV:** `t/comcon_budget_uses.t` — 4 workers, a limit of 10 spent exactly 10 times; the fixed window; the composition and validation rules.
- **EV:** `t/comcon_v12_denial_codes.t` — `budget.uses` fires its own code and nothing undeclared.
- **THREAT:** T11, T6
- **V:** V9

#### G6.5 — memory is bounded per RUNTIME, not per fragment
- **CLAIM:** *(partial)* `JS_SetMemoryLimit(comcon_rt, 64MB)` bounds the runtime shared by
  every fragment. One fragment can exhaust the budget of its siblings — denial of service
  against peers, not an authority escape.
- **GAP:** No per-fragment memory attribution, and nothing asserts a FRAGMENT hitting the
  cap (`t/js_worker_memory_limit.t` proves the mechanism on the HOST runtime only).
  **home:** HARDENING.md S5 · AUDIT_M-SES.md §3 · finding F2.
- **EV:** `t/js_worker_memory_limit.t` — the mechanism, on the host runtime.
- **THREAT:** T11, T4
- **V:** V6

---

## 9. G7 — the TCB layer holds (escape resistance)

#### G7.1 — the escape gate is STANDING, not a moment
- **CLAIM:** The five M-SES gate conditions are re-checked on every test run, on both
  builds, with the controls built in (each probe runs confined AND unconfined and must
  DIFFER).
- **EV:** `t/comcon_mses_gate.t` — 12 probes over conditions (a)–(e) plus the resource guard.
- **EV:** `t/run_sanitizers.sh` — the corpus under ASAN + UBSAN.
- **THREAT:** T8, T1
- **V:** V5b

#### G7.2 — intrinsics are frozen against cross-tenant pollution
- **EV:** `t/comcon_include_freeze.t` — frozen intrinsics, no cross-request pollution, incl. the SR-3 sibling-iterator cases.
- **THREAT:** T4, T8
- **V:** V5b

#### G7.3 — SR-1's findings stay fixed
- **CLAIM:** The getter-reach leak (HIGH-1), response framing (MEDIUM-2), size caps
  (MEDIUM-3) and socket mutators (MEDIUM-4) remain closed on the shared response path.
- **EV:** `t/comcon_include_sr1.t` — the SR-1 regression probes on the include path.
- **THREAT:** T1, T4, T10
- **V:** V5b

#### G7.4 — the lockdown surface is what the audit says it is
- **EV:** `t/comcon_include_mses.t` — the M-SES escape suite over the include compartment.
- **EV:** `AUDIT_M-SES.md` — the signed audit record (see ASSUME A2 for what the signature covers).
- **THREAT:** T8
- **V:** V5b

#### G7.5 — the compiled tier under the escape battery
- **CLAIM:** *(partial)* `t/comcon_mses_gate.t` runs on both builds, but AOT-compiled
  FRAGMENTS are not separately asserted against the probe battery; SR-2 covers faithfulness
  of the compiled tier over the confinement surface.
- **GAP:** No separate probe run against server-AOT-compiled fragments.
  **home:** AUDIT_M-SES.md §3 · finding F5.
- **EV:** `t/comcon_include_faithfulness.t` — what IS covered: identical denials across tiers.
- **THREAT:** T7, T8
- **V:** V5b

#### G7.7 — co-resident tenants and the channels between them
- **CLAIM:** *(partial, and the weakest claim in this document)* The DIRECT readout is
  closed — a peer's name is unresolvable, its values unreachable, and opaque secrets are
  unprintable. What is not closed is the INDIRECT one.
- **GAP:** Timing, cache and contention channels between co-resident tenants are not
  mitigated and not probed; the constant-response-time REL profile is specified for the
  strictest sessions and is not built. With the IFC gap (T4) this is the explicitly
  deferred confidentiality axis of the whole design, accepted rather than closed.
  **home:** THREATS.md T9 · FOUNDATION §13.4 (post-M9 IFC track) · finding F8.
- **EV:** `t/comcon_include_deny.t` — what IS closed: no peer name resolves, no shared surface.
- **THREAT:** T9, T4
- **V:** V13

#### G7.6 — cross-compartment identity: two claims, one structural and one enforced
- **CLAIM:** *(evidenced 2026-09-12)* **Host ↔ fragment** share no heap: the compartment is
  its own `JS_NewRuntime()`, so no JSValue can cross and the invoke's JSON marshalling is
  the only thing that *can* happen. **Fragment ↔ fragment** share a runtime AND a context —
  one global, one intrinsic graph — and are separated by the M-SES-1 transitive freeze,
  closure-bound grants, and an admission gate that refuses a fragment naming a neighbour.
- **ARGUMENT:** Eight shared surfaces are probed as CHANNELS (plant in one fragment, read in
  another): `Object.prototype`, a frozen constructor, `JSON`, an array index, `Error`,
  `String`, the function prototype, and a granted capability plus its class prototype. Each
  probe also runs UNCONFINED, where it works — so a clean confined result is a measurement.
  **Removing the freeze turns five of seven into live channels**, which is what makes it a
  load-bearing mechanism rather than a hopeful one. A fragment CAN write an own property on
  its own capability wrapper; that reaches nobody, because each include gets its own wrapper
  over the same C object and the host's carries no properties at all.
- **EV:** `t/comcon_cross_identity.t` — the battery, its built-in unconfined control, and the marshalling and scope checks.
- **EV:** `t/comcon_include_freeze.t` — the freeze itself, whose removal the battery shows to be decisive.
- **GAP:** **An operator who DECLARES `Symbol` for two tenants gives them a rendezvous** —
  `Symbol.for` is a runtime-wide registry, measured shared. Not an escape (nothing crosses
  that was not granted) and the reason `Symbol` is outside the intrinsics allowance, but a
  declaration that looks innocuous opens a channel and nothing warns the operator.
  **home:** FOUNDATION v5.54 (the intrinsics decision) · finding F3.
- **THREAT:** T4, T9
- **V:** V5b

---

## 10. G8 — live mutation is governed (increment D)

#### G8.1 — reflection returns quotations, never raw source
- **CLAIM:** Reading the program object model yields cap-free descriptions; SEMANTICS
  REFLECT, so a read cannot become authority.
- **EV:** `t/comcon_pom_substrate.t` — the bytecode-derived node tree (no parser).
- **EV:** `t/comcon_pom_nodeview.t` — the lazy NodeView; `text()`/`quote()` return quotations.
- **THREAT:** T1, T4
- **V:** V9

#### G8.2 — selectors name sites intensionally
- **EV:** `t/comcon_pom_query.t` — the selector grammar, born-bound (recomputed per call).
- **EV:** `t/comcon_pom_cst.t` — ESTree → POM CST mapping below function granularity.
- **EV:** `t/comcon_pom_anchors.t` — inline anchors survive edits above them; raw-spelling match.
- **THREAT:** T1
- **V:** V9

#### G8.3 — a quotation is inert, and a splice is data
- **CLAIM:** Spliced values are bound as JSON literals in an enclosing IIFE, so quoted code
  sees escaped DATA and a spliced string cannot smuggle code.
- **EV:** `t/comcon_pom_splice.t` — stone splices, deep-checked at the producer.
- **THREAT:** T10, T3
- **V:** V9

#### G8.4 — mutation is rebuild-on-write with epochs and bounded rollback
- **EV:** `t/comcon_pom_mutate.t` — `bindAt`/`replace`/`rollback`/`remove`, 500 cycles flat.
- **THREAT:** T5, T12
- **V:** V10

#### G8.5 — a rewrite fans out coherently to every worker
- **EV:** `t/comcon_pom_fanout.t` — 4 workers all-v1 before, all-v2 after one replace.
- **THREAT:** T11, T12
- **V:** V10

#### G8.6 — hardening produces TEXT that must be admitted like any other fragment
- **CLAIM:** `harden()` returns a report whose quotation installs through the normal
  admission path; the rewrite is never an install.
- **EV:** `t/comcon_pom_harden.t` — the rewrite, its limits, and the guard stopping the call.
- **THREAT:** T1, T10
- **V:** V9

#### G8.7 — the audit half: every call site of a name is enumerable
- **EV:** `t/comcon_pom_callsites.t` — exact callee↔call correlation from bytecode.
- **THREAT:** T1
- **V:** V9

#### G8.8 — a span says which base it counts in
- **CLAIM:** A location is either absolute with a file, or node-local and says so;
  `origin()` returns null rather than inventing a position.
- **EV:** `t/comcon_pom_origin.t` — cross-file provenance; the `<comcon-fragment>` origin.
- **THREAT:** T12
- **V:** V9

---

## 11. G9–G11 — the boundary, the config surface, and the meta-claim

#### G9.1 — what crosses the boundary is DATA
- **CLAIM:** A confined handler receives marshalled request data, not a live host object.
- **EV:** `t/comcon_include_request.t` — the confined request handler.
- **EV:** `t/comcon_include_headers.t` — data-in/data-out over headers.
- **THREAT:** T1, T10
- **V:** V13

#### G10.1 — a config proposal is admitted like code, and never executes
- **CLAIM:** A tenant's config sentence is reduced to a descriptor table and applied by the
  OPERATOR's authority; refusal is by safety class, not a blocklist; apply is atomic.
- **EV:** `t/comcon_config_instance.t` — review → diff → apply → rollback, end to end.
- **THREAT:** T5, T6
- **V:** V8

#### G10.2 — confinement needs no operator ceremony
- **EV:** `t/comcon_operator_handler.t` — a confined handler with no operator directive.
- **THREAT:** T5
- **V:** V13

#### G10.3 — a principal becomes an environment by ATTENUATION, never by minting
- **CLAIM:** The identity→environment mapping holds cap-free descriptors, resolves by
  narrowing the env the caller passes in, answers an unknown or expired principal with the
  EMPTY env, and refuses a mapping that names authority the base env does not hold.
- **ARGUMENT:** The registry carries no authority at all, which is what lets it live in
  `nginx.shared` and be fleet-wide (data crosses a process boundary; capabilities do not),
  and what makes stealing the whole table worth nothing. Monotonicity at the identity
  boundary is inherited from the kernel rather than re-argued: a session env is ≤ the env
  of whoever resolved it. **COMCON does not authenticate** — the host asserts the
  principal, and that assertion is the entire trust transfer (FOUNDATION §8b).
- **EV:** `t/comcon_std_sessions.t` — deny-by-default, narrowing, refusal-not-trimming, leases, revocation, and the absent-verbs property.
- **EV:** `t/comcon_std_ops.t` — `sessions` as the ninth ops-resource: no capability, no `grant`/`revoke` verb.
- **GAP:** Authentication, the principal namespace, and the login transport are the HOST's
  and are deliberately not provided; a deployment that passes a client-supplied identifier
  as the principal has handed the client the session, and nothing here can detect that.
  **home:** FOUNDATION §8b · P19 admin-shell substrate · finding F7.
- **THREAT:** T5, T12
- **V:** V13

#### G11.1 — closed enumerations are GENERATED, never maintained
- **CLAIM:** Six closed enumerations are derived from the source and break the suite on drift.
- **EV:** `t/comcon_enumerations.t` — runs the checker; asserts the LAST check ran (no early exit).
- **EV:** `t/tools/check-enumerations.py` — p_symbols, portals, ops resources, intrinsics, denial codes, refusal codes.
- **THREAT:** T12
- **V:** V7

#### G11.2 — the reference semantics is independent of the implementation
- **EV:** `t/tools/kernel-oracle.js` — written from the rules, sharing no code with `src/js`.
- **THREAT:** T1
- **V:** V3

#### G11.3 — negative controls are RUN, not asserted
- **CLAIM:** A guard whose control cannot go red is misinformation; controls are executed.
- **EV:** `t/tools/verify-negative-controls.sh` — makes the §2b corpus falsifiable in minutes.
- **THREAT:** T8
- **V:** V5b

#### G11.4 — THIS DOCUMENT is checked by machine
- **CLAIM:** Every `EV:` here names an artifact that exists; every leaf has evidence or a
  declared gap with a home; no `t/comcon_*.t` exists that no claim cites; every threat
  T1–T12 and every V-item V1–V15 appears; and no document in this set cites a test file
  that does not exist.
- **EV:** `t/tools/check-assurance.py` — the checker.
- **EV:** `t/comcon_assurance.t` — runs it, so drift breaks the suite.
- **THREAT:** T12
- **V:** V15

#### G11.5 — mechanical sweeps over the host surface
- **EV:** `t/tools/callback-return-sweep.py` — return-value handling across callbacks.
- **EV:** `t/tools/numeric-cast-sweep.py` — numeric casts (V1's numeric model).
- **THREAT:** T8
- **V:** V1

#### G11.6 — performance claims are measured, not argued
- **CLAIM:** Cost claims about the host path cite an instrument, and the instrument is
  re-run A/B on one box rather than compared against a remembered number.
- **EV:** `t/tools/host-call-cost.t` — the per-call decomposition.
- **EV:** `t/tools/policy-compute-split.t` — interpreted vs compiled, per policy.
- **THREAT:** T11
- **V:** V6

---

## 12. Evidence renames (CONVERGENCE P6)

The convergence deleted 20 tenant-path tests whose scenarios moved onto the one
`comcon.include` primitive. The documents kept citing the old names for weeks. This table
is the redirect, and `check-assurance.py` reads it: a citation of an old name resolves
only if its successor exists.

| cited (gone) | successor | deleted by |
|---|---|---|
| `t/comcon_admission.t` | `t/comcon_include_admit.t` | `38ab240ca` |
| `t/comcon_artifact.t` | `t/comcon_include_admit.t` | `38ab240ca` |
| `t/comcon_dependency.t` | `t/comcon_include_deps.t` | `38ab240ca` |
| `t/comcon_faithfulness.t` | `t/comcon_include_faithfulness.t` | `38ab240ca` |
| `t/comcon_freeze.t` | `t/comcon_include_freeze.t` | `38ab240ca` |
| `t/comcon_learn_mode.t` | `t/comcon_include_learn.t` | `38ab240ca` |
| `t/comcon_lowering.t` | `t/comcon_include_faithfulness.t` | `38ab240ca` |
| `t/comcon_mses.t` | `t/comcon_include_mses.t` | `38ab240ca` |
| `t/comcon_sr1_regression.t` | `t/comcon_include_sr1.t` | `38ab240ca` |
| `t/comcon_restricted.t` | `t/comcon_include_admit.t` | `6ac90d9b0` |
| `t/comcon_schema_conformance.t` | `t/comcon_include_admit.t` | `6ac90d9b0` |
| `t/comcon_types.t` | `t/comcon_include_request.t` | `6ac90d9b0` |
| `t/comcon_operator_tenant.t` | `t/comcon_include_audit.t` | `6ac90d9b0` |
| `t/comcon_operator_dependency.t` | `t/comcon_include_deps.t` | `6ac90d9b0` |
| `t/comcon_operator_artifact.t` | `t/comcon_include_admit.t` | `6ac90d9b0` |
| `t/comcon_frontend_audit.t` | `t/comcon_include_admit.t` | `b05cb6764` |
| `t/comcon_teardown.t` | `t/comcon_include_teardown.t` | `b05cb6764` |
| `t/comcon_gas.t` | `t/comcon_include.t` | `b05cb6764` |
| `t/comcon_onboard.t` | `t/comcon_include_learn.t` | `b05cb6764` |
| `t/comcon_deprecation.t` | `t/comcon_include_teardown.t` | `7d3270d4f` |

`t/comcon_deprecation.t` is the one entry with no true successor: it asserted the
`js_tenant_*` deprecation warnings, and the directives themselves were removed. Its row
points at the nearest live test so the citation resolves; the CLAIM it evidenced is gone.

---

## 13. Findings — the ledger

Every leaf above that lacks evidence appears here. This list IS the deliverable: an
assurance case whose findings section is empty has not been built honestly.

| # | Finding | Where | Status |
|---|---|---|---|
| **F1** | **20 of 83 evidence citations in the doc set pointed at files that do not exist** — pre-CONVERGENCE names, deleted in P6a/P6b. A reviewer following THREATS T11 to `t/comcon_gas.t` found nothing. | this document, §12 | **FIXED + now checked** (`check-assurance.py`) |
| **F2** | No per-fragment memory attribution; nothing asserts a fragment hitting the 64 MB runtime cap | G6.4 | OPEN — deferred by design (S5) |
| **F3** | Cross-compartment identity not probed | G7.6 | **PROBED 2026-09-12** — no channel found on eight shared surfaces, and removing the freeze opens five of them, so the mechanism is identified rather than assumed. **Residual:** declaring `Symbol` for two tenants gives them `Symbol.for` as a rendezvous, unwarned |
| **F4** | `guarded` / `irreversible` COM members are excluded from the setter fuzz | AUDIT_M-SES.md §3 | OPEN — deliberate scope choice |
| **F5** | AOT-compiled fragments not separately run against the escape battery | G7.5 | PARTIAL |
| **F6** | Host JS (not fragments) is unbounded by default — a runaway `location.handler` hangs the worker | ASSUME A5, AUDIT §3 | OPEN — deliberate scope choice |
| **F7** | **TM-2:** session identity → environment mapping was unspecified and unowned | THREATS.md → FOUNDATION §8b, G10.3 | **SPECIFIED + BUILT 2026-09-12** (v5.65): `std.sessions`, descriptors-not-envs, attenuation-only, deny-by-default, leases. **Residual:** authentication, the principal namespace and the login transport remain the host's, by design and by statement |
| **F8** | Information flow / timing channels between co-resident tenants | ASSUME A3, THREATS T4/T9 | ACCEPTED residual (post-M9) |
| **F9** | V-track items with no machinery yet: V5b, V6, V8, V9, V10, V13, V14 | VERIFICATION.md | OPEN — scheduled |
| **F10** | `E_CAP_FLAVOR` / `E_CAP_ESCALATE` (the JS capability layer's own refusals) have no codes | MANUAL §3.2 [TBD-2] | OPEN — next tranche. **`E_BUDGET_*` is RESOLVED by placement (v5.67):** budget exhaustion is a DENIAL (`budget.uses`), not an admission refusal, so that family stays empty by design |
| **F11** | The M-SES audit is one attestation with one signer; §4 not independently reproduced | ASSUME A2 | ACCEPTED — stated in the audit |

---

## 14. What this case does NOT establish

- **It is not signed.** No one has attested to it. `AUDIT_M-SES.md` §5 is the model for
  what signing looks like, including its own caveats.
- **It does not establish memory safety** (A1), information-flow confidentiality (A3/F8),
  or availability against a controller inside its own subtree (A3).
- **It does not cover host JS** (A5): everything here bounds a CONFINED fragment.
- **Evidence exists ≠ evidence is sufficient.** The checker proves each cited artifact is
  there and runs; whether a test's assertions are strong enough is a human judgement, and
  the V-track (V11 mutation testing especially) is the only mechanical pressure on it.
- **A green suite is not a pentest.** SR-3 certified 2026-09-01; S6 carries the standing
  part of it forward. Nothing here substitutes for the next adversarial pass.
