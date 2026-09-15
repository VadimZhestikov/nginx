# COMCON — The Assurance Case (V15 / gate SR-4)

> **Status: BUILT 2026-09-12 (v5.64). SIGNED 2026-09-12 (v5.68) — see §15.** The gate SR-4
> is closed on the evidence of §15's re-run, by one signer, accepting the residuals the
> findings table names. Read §14 before quoting this anywhere: a signature here attests
> that the evidence is there and was re-run, not that the system is secure.

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
- **EV:** `t/comcon_v12_denial_codes.t` — 15 refusal codes, each probed; `comcon.refusalCodes()` enumerates.
- **EV:** `t/comcon_v4_monotonicity.t` — the glob refusal carries `E_CAP_ESCALATE`.
- **EV:** `t/comcon_budget_uses.t` — so does the budget refusal: one code, one rule.
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

#### G6.9 — a capability can be bounded by a RECURRING schedule
- **CLAIM:** `mediate(cap, window(spec))` makes a capability work only on the named days between
  the named UTC hours; outside them the operation is denied as `cap.window`, logged-and-allowed
  in audit mode. Two different windows are refused rather than composed.
- **ARGUMENT:** `ttl` bounds a capability by a countdown, which is the wrong shape for the thing
  THREATS.md actually wants bounded — a signing key should be usable in hours, not for an hour.
  It is a separate denial code from `cap.expired` on purpose: the two are different operational
  facts. **UTC is a decision, not an oversight:** a gate reading the host's TZ cannot be tested
  identically on two machines and shifts under daylight saving with nothing edited.
- **EV:** `t/comcon_cap_window.t` — 16 assertions, every window computed FROM THE CURRENT TIME so
  the file asserts the same thing at any hour: open, closed, the day mask tested **by its
  complement** (every day but today, hours open — so the day list must really be read), a window
  that WRAPS midnight open and closed, `from === to` as a whole day, composition with a lifetime
  and a budget, the meet, five malformed specs, and audit mode.
- **EV:** `t/tools/golden-denials.js` — `cap.window` frozen, with a capability the harness
  COMPUTES (a window reliably closed cannot be written as a constant: an empty day mask is refused
  and a one-minute slot is a flake).
- **GAP:** UTC only — there is no per-capability timezone, so an operator with a genuine local
  schedule converts by hand and re-converts when their offset changes. And a window cannot express
  a date range ("until the end of the quarter"); that is `ttl`'s shape, and the two compose rather
  than merging.
  **home:** OPERATOR_API.md §8f · THREATS.md (the signing key).
- **THREAT:** T5, T6, T11
- **V:** V9

#### G6.10 — a fragment returning `undefined` returns undefined, not a syntax error
- **CLAIM:** A confined fragment whose result JSON cannot represent — `undefined`, a function —
  yields `undefined` in the host, not a parse failure.
- **ARGUMENT:** The invoke marshals results by stringifying in the compartment and re-parsing in
  the host, because only data crosses. `JSON.stringify(undefined)` is `undefined` — not JSON text
  — and handing that to the parser produced `SyntaxError: unexpected token: 'undefined' at
  <result>:1:1`, naming neither the fragment nor the cause. **`undefined` is what every DENIED
  GATE produces**, so the path an operator is most likely to hit while tightening a policy was the
  one reporting an internal parse error.
- **EV:** `t/comcon_invoke_undefined.t` — the literal, a function with no `return`, a value a
  mediation redacted away, a function value, and the ordinary cases still marshalling — including
  `null` staying NULL, which is the distinction the fix must not erase.
- **GAP:** It survived because every existing probe wrapped its result in an object or a string.
  Nothing systematically checks the invoke's value contract across types; this file covers the
  values JSON declines plus the common ones, not the whole surface.
  **home:** G6.9's evidence (whose probe found it) · `ngx_js_comcon_invoke_confined`.
- **THREAT:** T3
- **V:** V13

#### G6.11 — a capability can require TWO PRINCIPALS
- **CLAIM:** `mediate(cap, cosign({key, quorum, within, as}))` makes an operation execute only
  once `quorum` **distinct** principals have attempted it; short of that it is denied as
  `cap.cosign`, logged-and-allowed in audit mode. The consent record is fleet-wide; the quorum
  meets by MAX and `within` by MIN; a different key or a different acting principal is refused.
- **ARGUMENT:** `ttl` and `window` bound WHEN and `uses` bounds HOW OFTEN; this is the first that
  bounds WHO, and the only mediation in the vocabulary that **the holder cannot satisfy alone**.
  The hard part is not the counter, it is who is counting. COMCON does not authenticate (TM-2) —
  the host asserts the principal — so `as` is written by the operator's configuration on the
  trusted side and there is **no path from inside a compartment that sets it**. A fragment
  therefore holds exactly one identity per invocation and can cast exactly one vote:
  **distinctness is structural, not checked.** Had `as` been a string the fragment could write,
  the word would be theatre — one fragment voting twice under two names.
  Consequently the quorum assembles **across invocations**, which is what a two-person rule looks
  like in an operations room: alice runs the policy and is denied pending a cosignature, bob runs
  the same policy and it executes. There is no `approve()` verb because **the attempt is the
  consent** — so `cap.cosign` is the one denial in the set that is a WAITING STATE rather than a
  verdict, and the one **with a side effect**. There is deliberately no `of:[...]` allow-list:
  **holding the capability is the membership**, and a list inside the descriptor would re-state in
  a weaker place what the grant already decided.
- **EV:** `t/comcon_cap_cosign.t` — 25 assertions. The rule (first denied, second executes); the
  distinctness control (**one principal attempting twice is still denied**, then a different one
  executes it, so those two denials recorded exactly one consent between them); quorum 3; the
  **meet's direction measured by behaviour** rather than by not-throwing; gate order (a
  destination the glob refuses records NO consent); the meet's refusals; seven malformed specs; a
  hand-built descriptor; composition with `allowHosts` + `ttl` + `uses` + `window` at once; the
  socket kind as well as the outbound one; audit mode; expiry across a real sleep; and
  fleet-wideness proved **without naming a worker** — the principal is the worker index, so no
  single worker can cast two votes and a success anywhere is itself the proof.
- **EV:** `t/tools/golden-denials.js` — `cap.cosign` and `E_CAP_PRINCIPAL` frozen; the cosign
  capability is COMPUTED by the harness, relying on distinctness for its own determinism.
- **GAP:** The record is keyed by the operator's `key`, so two capabilities given the same key
  cosign each other — the same naming residual a shared `uses` counter has, and the same
  mitigation (it is a deliberate act). The `within` window is FIXED and **anchored at the first
  consent**, not sliding, which is the `uses` disclosure repeated: a quorum must assemble within
  `within` seconds of the FIRST signature. And nothing records WHICH operation was cosigned —
  the key names a decision, not a call, so two operations under one key are one decision.
  **home:** OPERATOR_API.md §8g · MANUAL.md (the vocabulary) · THREATS.md (the signing key).
- **THREAT:** T5, T6, T11
- **V:** V9

#### G6.12 — a mediation word carries no capability KIND, and a budget name is one counter
- **CLAIM:** Every mediation word works applied ALONE to every capability kind it is meaningful
  for; and `uses(key, …)` names ONE fleet-wide counter regardless of which kind of capability
  carries it.
- **ARGUMENT:** `uses`, `ttl` and `cosign` normalize to an allow-everything MASK — they attenuate
  how many times, how long and by whom, never WHAT — and a mask is the socket shape. So a bare one
  of those over an OUTBOUND capability reached the translation as kind 0 and was refused with
  *"grant is not a NginxSocket or NginxServer"* for a grant that was a perfectly good outbound
  capability. **Fail closed, so nothing was ever widened** — but the operator was told their
  capability was the wrong type when the real answer is that the WORD carries no type at all.
  The kind belongs to the capability, so it is read back from the capability.
  Separately, the outbound path did not namespace its budget key the way the socket path did, so
  `uses('k')` on a socket and `uses('k')` on an outbound capability were **two counters** — against
  the documented rule that two capabilities share a budget exactly when the operator names the same
  name. A budget an operator believed was one limit of 10 was two limits of 10.
- **EV:** `t/comcon_bare_mediation.t` — the matrix: five words × two capability kinds, each word
  ALONE; that a promoted grant is a MEDIATED wrapper whose destination set is everything (its
  budget gate still bites) and not the host's own unmediated capability; that an UNMEDIATED
  outbound grant is still refused, so the fix smuggles in no widening; and four spends across two
  capability KINDS against one named counter of 3.
- **GAP:** The matrix covers the five words that are meaningful on both kinds. `routes` (a COM
  facet) and `allowHosts` (outbound only) are each meaningful on one kind, and applying one to the
  other is refused rather than promoted — correct, but the file asserts the promotion rule, not the
  whole cross-product of word × kind.
  **home:** G6.9's evidence (the probe whose question this repeats one axis over) ·
  `ngx_js_comcon_include` (the grant loop).
- **THREAT:** T6, T11
- **V:** V4, V13

#### G6.13 — a capability can require its operations IN ORDER
- **CLAIM:** `mediate(cap, protocol(step…))` makes the capability's operations legal only in the
  declared sequence; a step out of order is denied as `cap.protocol`, logged-and-allowed in audit
  mode. Once the last step is consumed every further operation is denied. The cursor is per
  WRAPPER, and a denied operation does not advance it.
- **ARGUMENT:** The tenth and last of MANUAL's mediation words — a session type over the
  capability's own operations, where `allow` says which exist, `uses` how often, `ttl`/`window`
  when and `cosign` by whom. Two consequences are worth stating because they are not obvious:
  **`protocol('fd')` is a ONE-SHOT capability**, which `uses(1)` cannot express (a budget is
  fleet-wide and resets with its window; a protocol is per-wrapper and never resets); and the
  cursor is deliberately **not** fleet-wide the way a cosign record is, because a session type
  describes ONE conversation and two holders sharing a cursor would interleave into nonsense.
  **THE GATE'S POSITION IS THE DESIGN.** Every other gate's decision is also its effect — a budget
  charge happens when it is decided, and a cosign consent *is* the decision — so each of them can
  only ever be last. A protocol's effect can be DEFERRED, and must be: an operation a later gate
  still refuses did not happen and must not move the conversation on. So the transition is CHECKED
  before cosign and COMMITTED after the budget.
- **EV:** `t/comcon_cap_protocol.t` — 19 assertions: the declared order; a step out of order; a
  starred step taken zero times; the one-shot; that a violation does not advance the cursor; the
  per-wrapper cursor; **the check/commit split measured on ONE wrapper** (denied for want of a
  cosignature, the signature arrives elsewhere, and the retry of the same step is legal); a
  budget-denied step; the outbound one-shot; the meet; six malformed protocols; the wrong
  capability kind in both directions; and audit mode.
- **EV:** `t/tools/golden-denials.js` — `cap.protocol` frozen, with a computed capability whose
  single step is `port` and whose probe reads `address`: out of order on the first operation, with
  no clock, quorum or budget involved.
- **GAP:** **It enforces ORDER, not COMPLETION.** "You cannot take the fd before looking at the
  address" is checkable at the moment of the call; "you must eventually close" is not — a fragment
  can simply return and there is no event at which the host could notice. The grammar is also
  deliberately tiny (distinct names, each required once or starred), so an alternation or a loop
  over two operations is inexpressible; the meet refuses rather than approximating. And the
  outbound namespace has one member, because `pending`/`clear` are the host's reach-gated half.
  **home:** OPERATOR_API.md §8h · MANUAL.md (the vocabulary) · G6.11's evidence.
- **THREAT:** T5, T6, T11
- **V:** V4, V9

#### G6.14 — a posture is a property of the BINDING, not of the fleet
- **CLAIM:** `include(src, {onViolation:'audit'|'deny'|'learn'})` sets the audit/enforce posture
  for that binding's invocations only, in both directions, restored afterwards including on the
  exception path — and covering the whole invocation, **result marshalling included**, because a
  getter on the returned object is fragment code (G6.17). `profile` is read: `restrictive`/`declarative` are accepted, `adaptive` and any
  unknown word are refused with `E_ADMIT_CONTRACT`.
- **ARGUMENT:** INCREMENT_MLIB §4 withheld both words for a good reason — *nothing read them, and a
  posture assembled from ignored keys would read like a policy and do nothing, which is worse than
  its absence: it would be believed, and by exactly the reader least able to check.* What changed is
  that there is now something to be a posture OF: ten mediation words enforce. The audit/enforce
  switch existed but only FLEET-WIDE, which is the wrong granularity for MANUAL's rollout —
  **shadowing one tenant's new policy by putting the fleet in audit also stops enforcing every other
  tenant's**, a strictly worse posture than the one the operator is carefully trying to reach.
  `onViolation` can WEAKEN as well as strengthen, and that is acceptable only because the contract
  is written on the trusted side: the fragment's SOURCE is untrusted, the contract around it is the
  operator's own configuration — the same argument `cosign`'s `as` rests on.
  `profile` is read by being REFUSED where it cannot be honoured. Every mediation here attenuates
  and none transforms, so `restrictive` means what it says; accepting `adaptive` would make "this
  program runs standalone without COMCON" unfalsifiable for exactly the fragments where it matters.
- **EV:** `t/comcon_posture.t` — 10 assertions. **One request, two postures**: with the fleet in
  enforce, the `audit` binding is shadowed while the binding beside it enforces; with the fleet in
  audit, the `deny` binding still enforces and a binding with no opinion inherits. That the mode
  does not leak past the invocation, **including when the fragment throws**. Each profile value, and
  separately the REASON each is refused, because both refusals share one code.
- **GAP:** `std.postures.*` is still absent, and the reason has changed: it is no longer that
  nothing enforces, but that *what `lockdown` should narrow to is a decision nobody has made* —
  MANUAL says "writes: deny, exports: freeze", which needs a per-member mutating/reading split over
  a whole env rather than one capability. Inventing that here would be inventing policy. Also
  `onViolation` is per-BINDING, not per-TENANT: two bindings for one tenant carry their own, and
  nothing groups them.
  **home:** INCREMENT_MLIB.md §4 · OPERATOR_API.md §8i · MANUAL.md §4.3.
- **THREAT:** T6, T11
- **V:** V9, V11

#### G6.15 — an async fragment is ADMITTED, ANALYSED, and settled or reported
- **CLAIM:** A fragment may be an `async function`; the C3 admission analysis runs on its body;
  its promise is settled by draining the compartment's own jobs; a rejection takes the throw path;
  and a promise nothing in reach can settle is reported as `E_INVOKE_PENDING` rather than
  stringified. Both the deadline and a job cap bound the drain.
- **ARGUMENT:** ROADMAP carried this as blocked on the SYNCHRONOUS INVOKE. That was true and it was
  not where an async fragment stopped — it never reached the invoke, being refused at admission
  with *"admit: arg0 not a bytecode function"*, **which is not true of the thing in front of it.**
  An async function, a generator and an async generator are all bytecode functions — the engine says
  so in `js_class_has_bytecode()` — carrying a different class id. Six COMCON analysis entry points
  tested one id instead of asking the engine, so the whole analysis **refused to look** at a class of
  functions and reported that as a property of the function. A fragment the analysis cannot read must
  be refused; a fragment it *will not* read is a different and worse thing, because the message sends
  the operator to rewrite code that was never the problem.
  The drain is safe because the compartment has its OWN runtime: it runs the fragment's microtasks
  and cannot schedule a host job or another tenant's continuation. **It drains microtasks, not the
  world** — which is why `allowHosts` still records intent instead of fetching, and why a promise
  only a timer or a response could settle is reported rather than waited on.
- **EV:** `t/comcon_async_fragment.t` — 16 assertions: an async fragment admitted and settled; an
  await and a chain of awaits; **an undeclared free name inside an async body still refused**, which
  is the assertion that says the gate is reading the body rather than waving it through; a generator
  body analysed too; a rejection carrying message and fragment origin; `E_INVOKE_PENDING` for an
  unsettleable promise, from an async fragment and from a synchronous one that returns a promise; a
  synchronous fragment untouched; and **the two bounds shown to be different bounds** — a runaway
  microtask loop stopped by a 300ms meter, and the same loop under a 30s meter stopped by the job cap
  in under a second, reporting the honest outcome instead of a timeout.
- **EV:** `t/tools/golden-denials.js` — `E_INVOKE_PENDING` frozen, the one code on the refusal axis
  raised after the fragment ran, with the reason that does not blur the two axes written in the row.
- **GAP:** **This is not `fetch`, and it is now clear exactly why not.** Draining microtasks settles
  only what the fragment itself queued; nothing in a compartment can settle an await on real I/O,
  and making one possible means suspending the nginx request handler across a fragment call — which
  touches the F6/F12 deadline and the F2 per-invocation allowance on the most safety-critical path.
  Also: an async fragment's `await` does not extend the deadline (the interrupt is the compartment
  runtime's), but nothing asserts the interaction with F12's inherit-don't-extend rule for a
  fragment that awaits inside a host continuation, because that composition has no path yet.
  **home:** ROADMAP.md (the fetch prerequisite) · `ngx_js_comcon_invoke_confined` ·
  `js_comcon_collect_free_globals`.
- **THREAT:** T3, T4
- **V:** V13, V15

#### G6.16 — an invocation leaves the compartment QUIESCENT
- **CLAIM:** Every invocation drains the compartment's pending jobs to quiescence before it
  returns, inside the same compartment scope and before the posture and memory limit are restored.
  A fragment's continuations are therefore charged to, and gated at, the fragment that created
  them. A continuation that fails is reported (unhandled-rejection tracker); a job that cannot RUN
  is fatal to the invocation.
- **ARGUMENT:** A fragment can queue a job and return without awaiting it, and nothing else in the
  process drains the compartment runtime — the host's drains are a different runtime. So the job
  sat pending until some LATER, UNRELATED invocation returned a promise, and ran inside it.
  Measured before the fix: the capability was untouched when the fragment returned and exercised
  during the next fragment's settle loop.
  **Everything an invocation bounds was therefore the wrong invocation's.** The deferred use ran on
  a stranger's deadline and memory allowance; it was gated at a stranger's wall-clock time, so
  `ttl` and `window` were evaluated at the wrong moment; and it ran under a stranger's
  `onViolation` POSTURE — so a shadowed fragment's deferred work could execute under an enforcing
  binding, or an enforced fragment's under audit. It is also a channel: the first fragment spends
  the second one's job budget. **This was unreachable until async fragments were admitted (G6.15),
  because a fragment that cannot name `Promise` cannot queue a job** — so closing it belongs with
  that change rather than in a backlog.
  **Quiescence here is BEST-EFFORT, and the first version of the fix was not.** It ran the drain with
  no job cap, on the argument that an invariant with a cap is not an invariant. The argument is
  correct and the consequence was not affordable: an uncapped drain over a self-queueing chain runs
  until a bound the operator set, and a fragment refused in milliseconds became a ten-second request
  the client abandoned — caught by the full suite, not by the test written for the change. The drain
  now shares the settle loop's job budget, which fully attributes every fragment whose continuations
  are bounded and **reports** one that outruns it.
- **EV:** `t/comcon_deferred_jobs.t` — 14 assertions. The deferred use attributed to the fragment
  that deferred it, and the bystander changing nothing; **volume attributed exactly** (100 deferred
  requests land as 32 recorded + 68 dropped against the right fragment); **the posture the job is
  gated under**, with the fleet in AUDIT and the binding in DENY so the two disagree — a control
  that restored the posture before the drain passed while they agreed; a continuation that throws
  not failing the invocation but being logged; a self-queueing chain terminating promptly; and the
  loud report when a fragment outruns the budget.
- **EV:** `t/comcon_mses_gate.t` — the S6 escape battery gains an ASYNC arm: the same 12 probes in
  an async fragment, asserted **identical to the synchronous arm probe by probe**. Admission
  accepted no async function before G6.15, so the gate had never seen the shape it now admits, and
  the question worth asking is not "is anything open" but "does the shape change what the cage
  allows".
- **GAP:** A fragment that outruns the job budget still leaves work behind, and that work runs
  inside a later invocation, on its deadline and under its posture. The drain reports it loudly and
  cannot remove it. **The structural half was owed here and is now PAID — see G6.17:** every granted
  wrapper is bound to its fragment and the gates refuse it to anyone else, so a leftover job runs
  and gets nothing. **The accounting half is now PAID TOO — see G6.18:** leftovers are drained at
  the START of the next invocation under bounds of their own and nobody's identity, so they cannot be
  charged to a stranger's budget, deadline or rejection report. Measuring that found one thing this
  gap had not named: a leftover's AUTHORITY depended on who arrived next, because `cap.owner`
  compares against the fragment now running — allowed when its own fragment was invoked again, denied
  when anyone else was. What remains is that quiescence is still best-effort: a fragment leaving more
  than 10,000 behind pushes the remainder into the next invocation's trailing drain.
  **A bound nobody measured is a bound nobody knows the order of.** G6.15's commit claimed the
  deadline was the real bound and the job cap the belt; measurement says a `.then` chain exhausts the
  **memory allowance** after 354,885 promises, while an `await` chain reaches the **request**
  deadline. Corrected in place at v5.93. Also: the `jrc < 0` path — a job that cannot run at all — is
  unreachable today, because every job here is a promise reaction and those catch their own throws
  (the deadline interrupt included, which is why it surfaces as a rejection); it is kept as a guard
  for a future non-reaction job source, with that written down. And a generator fragment gets no
  battery arm, because a generator returns a generator object and cannot report results as JSON.
  **home:** G6.15's evidence (the increment that opened it) · `ngx_js_comcon_invoke_confined`.
- **THREAT:** T3, T4, T6, T9
- **V:** V13, V15

#### G10.4 — the fleet-wide mode protocol converges, and cannot be walked backwards
- **CLAIM:** Under every interleaving of concurrent switches, reconciles and worker respawns, no
  worker can be left holding the cell's epoch with a different mode; and a reconcile adopts only a
  GREATER epoch, so a stale publish cannot move the fleet backwards.
- **ARGUMENT:** This is V10, and it is the last V-item that did not depend on the parked compiler
  track. The mode fan-out is a fleet-wide protocol over shared memory with concurrent writers,
  worker respawn and master reload; the formal semantics is single-threaded, so "monotone rollout"
  was a slogan with nothing behind it.
  **The model found a defect.** The epoch bump was three operations from JS — `get`, `+1`, `set` —
  so two operators switching concurrently both read epoch N and both wrote N+1 with their own mode.
  The second write won the cell and the first worker kept N+1 locally with the mode nobody else had.
  That would have been a transient lost update **had the reconciler not early-returned on epoch
  EQUALITY**: it did, so the worker and the cell agreed on the only thing the reader compares, and
  the divergence was **permanent and silent** — a fleet moved to `enforce` could leave one worker in
  `audit` for the rest of its life, unshielded.
  **The first fix was insufficient and the model said so before the code was written:** relaxing the
  early return to `<=` left the identical violation count, because no reading rule repairs a state
  where worker and cell hold the same epoch. The publish is now ONE critical section under the
  store's own lock, as the budget charge and the consent record already were.
- **EV:** `t/tools/check-epoch-model.py` — exhaustive interleaving over the protocol read off the JS
  bootstrap, with **three arms**: pre-fix (168 violations), reconciler-only (still broken), shipped
  (none). The control is built in, like the M-SES battery's unconfined arm: a model that reports no
  violation for a protocol nobody changed is measuring nothing.
- **EV:** `t/comcon_v10_epoch_model.t` — runs the checker (so drift is a build failure) and tests the
  LIVE protocol, because a model nobody compared against the code is a paper exercise: the cell's
  shape (`<epoch>:<mode>`, which a revert to the JS write path would not produce), monotone epochs,
  an OLDER cell ignored and a NEWER cell adopted — the same probe both ways, so "ignored" cannot pass
  by nothing ever being adopted. Three controls.
- **GAP:** **Two-phase epoch groups (R10) and rollback are still unmodelled, because neither ships.**
  The model covers the protocol that does, with three workers and one respawn — a defect needing four
  workers would be a different defect. Master reload is modelled only as "the cell is absent"; the
  real behaviour depends on whether nginx re-creates the shared zone, which is a question about
  nginx's zone reuse rather than about this protocol. And the live half cannot force a concurrent
  write, so the atomicity itself rests on the model plus the cell-shape assertion.
  **home:** VERIFICATION.md §V10 · `ngx_js_shared_mode_publish`.
- **THREAT:** T6, T11
- **V:** V10

#### G6.17 — a granted capability belongs to ONE fragment
- **CLAIM:** Every granted wrapper — socket, outbound, COM facet — records the fragment it was
  granted to, and every gate refuses it to any other fragment's code (`cap.owner`). A leftover
  continuation therefore runs and obtains nothing. The host's own wrappers are unbound and always
  usable; one mediated capability granted to two fragments works for both.
- **ARGUMENT:** This is the structural half G6.16 named and owed. That drain is best-effort: a
  fragment which outruns the job budget leaves work behind, and no bounded loop can fix that,
  because the jobs are ordinary JS and nothing can un-queue them. So the question changes from *can
  we stop the code running* to *can we stop it having authority* — and the second is answerable
  cheaply. **The drain decides who is charged; this decides who can spend.**
  `cap.owner` is the first denial code that names a STRUCTURAL invariant rather than a policy the
  operator wrote. It is checked FIRST, before the mask and before every other gate: a capability
  that is not yours is not yours redacted, budgeted or scheduled — it is not yours at all.
  The binding uses the handle the fragment *will* be given, because wrappers are built before the
  handle is assigned; the prediction is **asserted** against the real handle after the push, and a
  mismatch kills the fragment rather than publishing it. A wrapper bound to the WRONG fragment
  would be worse than one bound to none, because the gate would look like it was working.
  **And the fragment's identity ends where the COMPARTMENT does, not where its call does** — a
  distinction the first version got wrong: the result marshalling runs inside the compartment by
  SR-1's design, so a getter on the returned object is fragment code. The posture and the
  per-invocation allowance move with it: *a boundary that is in three places is a boundary you have
  to be reminded of by a test.*
- **EV:** `t/comcon_cap_owner.t` — 12 assertions, and the probe **forces the residual rather than
  arguing about it**: a fragment queues 10,100 deferred requests, the budget lets 10,000 through
  (32 recorded + 9,968 dropped, attributed to it), and the leftovers run inside the next fragment
  adding **nothing** while counting as `cap.owner`. The same shape for a COM facet, because
  otherwise the facet's check would be code no control can break. Plus the other half of the
  control: the host's own socket and outbound capability, one mediated capability granted to TWO
  fragments working for both, and a facet — with `cap.owner` firing **exactly zero** times. *A gate
  that fires on correct use is not a gate, it is an outage.* Five controls.
- **EV:** `t/tools/golden-denials.js` — `cap.owner` frozen, with a row shape of its own
  (`leftover`) because its only reachable path needs two invocations, as the `ttl` row's
  `sleepBefore` needed two requests.
- **EV:** `t/comcon_include_sr1.t` — **it caught the boundary being in the wrong place.** SR-1
  deliberately materializes the result INSIDE the tenant compartment, so a getter on the returned
  object is fragment code running during `JS_JSONStringify`. With the identity restored before the
  marshal, such a getter held capabilities that were no longer "its own" and was refused — a
  fragment could not read its own grant from its own return value. That test distinguishes `null`
  (the reach gate denying) from `undefined` (something else denying) and got the wrong one. The
  identity, the posture and the allowance now all end where the COMPARTMENT does; `t/comcon_posture.t`
  pins the posture half of the same boundary in both directions.
- **CLAIM (added):** `cap.owner` **denies in every mode**, audit and learn included, and says so in
  the log (`mode=audit … unconditional=1`).
- **ARGUMENT (added):** Audit exists so an operator can OBSERVE what their policy would deny before
  it denies, so the test cannot be "is this structural" — the A1 reach gates are structural too and
  are audit-able for exactly that reason. The test is **is there anything here for an operator to
  observe and then enable?** Every other code answers *"may this fragment do this?"*, a question
  about the GRANT — and the grant is the operator's lever, so watching a denial and then changing the
  grant is a real workflow (`sock.listener` and `out.drain` are both that). `cap.owner` answers *"is
  this even this fragment's capability?"*, and **no grant can change that answer**: the only ways to
  trip it are a leftover continuation or a bug in the binding. Allowing it in audit would hand out
  authority no configuration asked for — not observation, a different policy, silently. The exception
  lives with the denial machinery rather than at the gates, so one place says which codes are
  unconditional and a reader of that function need not hunt for gates ignoring its return value.
- **EV (added):** `t/comcon_cap_owner.t` — the DISTINCTION, both halves under the same audit mode in
  the same request: a closed window (a policy) is logged and allowed, and a foreign capability is
  denied anyway. Asserting only the second would pass on a build where audit had stopped working at
  all. Plus the log line, because one reporting the mode and not the action would tell an operator
  the opposite of what occurred. Three further controls: the residual restored, every code made
  unconditional, and the action dropped from the log.
- **GAP:** The binding is per WRAPPER, so a capability granted to two fragments is two wrappers and
  neither can reach the other's; there is no notion of a capability deliberately SHARED between
  fragments, and adding one would need a different mechanism than an owner id. And `cap.owner` is now
  the one code whose behaviour does not follow `comcon.mode()`, which is a special case however well
  argued: a second such code should force a list rather than another `if`.
  **home:** G6.16's GAP (which named this fix) · `ngx_js_cap_foreign`.
- **THREAT:** T3, T4, T6, T9
- **V:** V4, V13

#### G6.18 — a fragment's leftovers are charged to NOBODY
- **CLAIM:** Pending jobs left by an earlier invocation are drained at the START of the next one,
  before it arms its deadline, narrows its allowance, pushes its posture or claims its identity —
  under a job budget and a 50 ms deadline of their own, the FLEET posture, and nobody's identity. So
  a fragment's continuations cannot be paid for out of another fragment's job budget, deadline,
  memory allowance or rejection report, and they obtain nothing from ANY invocation including its own
  author's next one.
- **ARGUMENT:** G6.17 closed the AUTHORITY half of what the best-effort drain left behind. The
  ACCOUNTING half is a channel in the other direction, and the queue order is what makes it one:
  jobs are FIFO (`list_add_tail` to enqueue, `job_list.next` to dequeue), so leftovers run FIRST.
  **Measured: with 12,000 jobs queued ahead of it, a fragment's entire 10,000-job allowance goes on
  a stranger's work and its own continuations never run at all** — the original escape's shape with
  the arrow reversed: instead of one fragment reaching into the next invocation, one fragment SPENDS
  the next invocation. They also ran on that fragment's deadline, and the unhandled-rejection counter
  is reset per invocation, so a leftover that rejected was logged as *"this fragment's queued jobs"*
  against a fragment that had never seen it — a line naming the wrong author sends an operator to the
  wrong place.
  **AND MEASURING IT FOUND SOMETHING THE BACKLOG HAD NOT NAMED: a leftover's AUTHORITY depended on
  who arrived next.** `cap.owner` compares the capability's owner against the fragment NOW RUNNING,
  so the same leftover was DENIED when a different fragment was drained into and ALLOWED when its own
  fragment happened to be invoked again — with `ttl` and `window` then evaluated at that later
  moment, under that invocation's posture, on its clock. Whether unfinished work kept its authority
  was decided by traffic order, which is worse than either answer consistently. Draining as nobody
  makes it deterministic: **an invocation's work belongs to that invocation.** The cost falls only on
  a fragment that outran a 10,000-job budget, whose continuations were best-effort already.
  It runs INSIDE the compartment, which is not optional: these jobs are fragment code, and running
  them between compartment scopes would run them as HOST_ROOT with the A1 reach gate off — turning an
  accounting fix into the escape it is tidying up after. It runs AS NOBODY by construction rather
  than by assignment: `cur_frag` is zero outside any invocation and the nested-invoke guard is what
  establishes that, so there is no `frag_set()` to get wrong. And a leftover that CANNOT RUN does not
  fail the invocation, unlike the trailing drain's: there it means the fragment being invoked did not
  finish, here it means a previous one did not, which the current caller is not answerable for.
- **EV:** `t/comcon_leftover_accounting.t` — 9 assertions. The two that carry the weight are the ones
  that changed: **B's own 100 continuations all run** (32 recorded + 68 dropped, where the control
  measures `{queued:0, dropped:0}`), and **A's own 3,000 leftovers are denied on A's own next
  invocation** (3,000 `cap.owner`, where the control measures 0 — they were allowed because A was
  next). Plus: B is not STOPPED, which is the control on the restore — forget it and every invocation
  after a leftover drain dies on a deadline that had already passed; `cap.owner` still the only code
  firing, so the authority answer is unchanged; and the log line naming them as leftovers. **Reaching
  the condition takes FOUR invocations**, because one cannot leave more behind than the next
  fragment's budget: its own 16 MB allowance caps how many jobs it can queue and its own drain burns
  10,000 of them. The first version of this test used one invocation of 20,100 and its control did
  not fire — the numbers were asserted from a model instead of measured, and the model was wrong
  about both the memory ceiling and how much was left behind.
- **GAP:** The leading drain has a budget too, so a fragment that leaves more than 10,000 behind
  still pushes the remainder into the next invocation's trailing drain — bounded and reported, not
  zero. Quiescence remains BEST-EFFORT; what is now exact is who pays for the part that runs. And
  the 50 ms leftover deadline is a constant nobody can configure, deliberately (it bounds work that
  belongs to no binding, so no contract may set it) — which also means an operator with a
  pathological tenant cannot trade latency for faster cleanup.
  **home:** G6.16's GAP (which named this fix) · `ngx_js_comcon_drain_leftovers`.
- **THREAT:** T3, T6, T9
- **V:** V4, V13

#### G6.19 — a fragment's failure reaches the host as what it is
- **CLAIM:** An exception raised while a fragment is compiled, evaluated or invoked reaches the host
  as an Error carrying the fragment's own message; and a fragment that runs out of memory is
  reported as out of memory, distinguishably from a fragment that throws `null`.
- **ARGUMENT:** Two defects, found while working out why a memory probe printed `undefined`.
  **Out of memory read as `null`:** at a hard limit the engine cannot allocate the InternalError it
  means to throw, so `JS_ThrowError2()` throws `JS_NULL` — and the host reported
  `comcon: fragment: null`, byte-for-byte what `throw null` produces. The per-invocation allowance
  exists to stop a fragment, and the operator could not see that it had. The engine now COUNTS its
  out-of-memory throws (`JS_GetOutOfMemoryCount`), and a non-object exception thrown while the count
  moved is named as the allocation failure; a real Error is left alone, because then the engine did
  say "out of memory" itself. **An exception during `include()` arrived with no value at all:** the
  fragment's expression is evaluated by a call on the COMPARTMENT context, and the failure path was
  `return fn` — `JS_EXCEPTION` means "pending on THIS context", returned to the HOST, where nothing
  was pending. The host caught `typeof e === "unknown"` and `String(e)` = `[unsupported type]`, and the
  real exception stayed pending in the compartment. Any top-level `throw` took that path; memory
  exhaustion only happened to be how it was found.
- **EV:** `t/comcon_fragment_error_report.t` — 7 assertions, with the control arm first: `throw null`
  must STILL read `null`, or "out of memory" would just be what every null now says. Two controls:
  the `return fn` restored (the include and top-level-throw assertions fail) and the out-of-memory
  test disabled (the allowance and include assertions fail).
- **GAP:** A fragment that CATCHES an out-of-memory and then throws `null` itself is reported as out
  of memory — which is still true of the call, and cannot be told apart without an engine flag that
  would have to be cleared by every catch. **And the investigation turned up F15, which this leaf
  does not close:** the fragment's top-level expression is evaluated before admission, outside the
  tenant compartment scope, and — at config phase — with no bound at all.
  **home:** finding F15 · `ngx_js_comcon_include_confined` · `ngx_js_comcon_exc_text`.
- **THREAT:** T6, T11
- **V:** V13

#### G7.9 — the kernel operators are not reachable from a fragment, so authoring does not nest
- **CLAIM:** `comcon` and `nginx` are absent from a fragment's global; declaring either in
  `imports` does not produce them; granting `comcon` is refused with `E_CAP_GRANT`. Attenuation,
  by contrast, nests without limit from the host side and each level only narrows.
- **ARGUMENT:** SHOWCASE17 §8 headlines *"Resellers: your tenant becomes a host — cages nest for
  free"*, with a sample in which ACME calls `env`/`grant`/`mediate`/`include` on its own customers.
  That is worth measuring rather than assuming, because a reader plans around it — and it turns out
  to be **the opposite of something the S6 escape gate asserts**: a fragment reaching `comcon` is an
  escape, and `t/comcon_mses_gate.t` requires that probe CLOSED. So nesting authorship cannot be
  "free"; it needs the operators deliberately re-exposed to a confined fragment, which is
  INCREMENT_MLIB §4's *"raw operators withheld"* and is not built.
  The security half is a claim we make; the capability half is a limitation we now state instead of
  implying its opposite. **Attenuation nests without limit; authoring does not nest at all.**
- **EV:** `t/comcon_nesting.t` — 8 assertions. `comcon`/`nginx` read `undefined` with **no contract
  at all**, so it is a real read of the compartment global rather than a free-name refusal;
  declaring the name changes nothing; granting it is `E_CAP_GRANT`. And the **positive control**,
  without which the refusals would read as "nothing works here": a three-level host-built chain
  (`allow(port,address)` → `allow(port)` → `uses`) whose innermost fragment still reads what each
  level left it, with `address` — hidden at level 2 — invisible at level 3.
- **GAP:** This measures today's boundary, not a decision. A second compartment tier that holds
  `comcon.std.*` without the raw kernel operators would move it, and that tier is unbuilt and
  unscoped. Until then the platform must write a reseller's policy on their behalf: the cages are
  real and nest properly, only the authorship is centralised.
  **home:** SHOWCASE17.md §8 (corrected in place) · INCREMENT_MLIB.md §4.
- **THREAT:** T4, T9
- **V:** V4

#### G7.10 — what one fragment retains does not set what another's invocation costs
- **CLAIM:** The cost of a confined invocation is independent of how much memory OTHER fragments
  hold in the shared compartment: the same trivial call with and without 200,000 objects retained
  by a different fragment costs within 4×, and measures 1.01×.
- **ARGUMENT:** Every invocation narrows the compartment limit to "allocated now + this call's
  allowance", and learned "allocated now" from `JS_ComputeMemoryUsage()` — the right number, reached
  by walking every context, module and live GC object. The compartment is SHARED, so each invocation
  was O(everyone's heap). **It was a cross-tenant channel of a different kind from F8's**, and a
  worse one: F8's medium is CPU a sender must burn WHILE the receiver waits, so the execution deadline
  caps it; this medium is memory a sender merely HOLDS, so it persists across requests with the
  sender idle, and no deadline touches it — the walk happened inside the receiver's own call.
  Measured before the fix, one worker, a handler making one trivial confined invocation: **22.0% of
  stock throughput with an idle compartment, and 0.2% (476 req/s) while another fragment retained
  200,000 objects** — a 137× modulation any tenant could set. In-process, 10.8 µs per invocation
  idle and 2,030 µs loaded, against 0.78 µs for the call itself: the walk was over 90% of an
  invocation even with nothing retained. Nothing had ever measured invocation cost against heap
  size; PERFORMANCE.md had no number for a confined invocation at all. The fix reads the same
  counter in O(1) (`JS_GetMallocSize`, added to the vendored engine), so the allowance semantics are
  unchanged byte for byte.
- **EV:** `t/comcon_invoke_heap_independence.t` — the ratio, gated (a ratio, not a time, because the
  suite runs on loaded machines). Its loops stop at 250 ms rather than at a count, so the control
  build finishes. Control: the walk restored at the invocation site measures **173.57×**.
- **EV:** `t/tools/confined-invoke-cost.t` — the absolute numbers, evidence not a gate: after the
  fix 68.0% of stock idle and 66.6% loaded, 0.78 µs per invocation either way (PERFORMANCE.md §2c).
- **GAP:** This closes the heap-size medium only. F8's CPU medium is unchanged and re-measured on the
  same build (0.3 → 319.9 ms, ~3.1 bits/s, capped at 50.0 ms by a 50 ms deadline). And the same
  class of defect can exist anywhere a per-call path consults a whole-runtime statistic; this leaf
  found one by suspicion, not by a sweep. `nginx.jsMemUsage()` still walks, deliberately — it is an
  explicit diagnostic that needs `objectCount`, and it is called by the host, not per invocation.
  **home:** finding F14 · finding F8 · PERFORMANCE.md §2c.
- **THREAT:** T4, T9, T6
- **V:** V13

#### G7.11 — a fragment cannot reassign a shared global binding for every OTHER fragment
- **CLAIM:** No fragment, admitted or not, can change what identifier ANOTHER fragment resolves a
  shared intrinsic name to. Every binding present on the compartment's globalThis at the moment the
  compartment is built is non-writable and non-configurable before any dependency or fragment ever
  runs; ordinary reads and computation with those intrinsics are unaffected.
- **ARGUMENT:** F15's first measurement turned up something worse than an unmetered top-level eval
  (its original finding, still open — see F15's row below). M-SES-1 freezes intrinsic VALUES
  (`Object.prototype`, ...) but deliberately never freezes globalThis itself, so every BINDING
  stayed writable and configurable: the name `Promise` pointing at `Promise`, not `Promise`'s own
  properties. **An ordinary, PROPERLY ADMITTED fragment body — `imports: ['Promise']`, no wrapper
  tricks — did `Promise = function(){ return 'EVIL'; }`, and every other fragment reading `Promise`
  afterwards got the attacker's function.** `imports` governs whether a name may be REFERENCED at
  all; nothing asked whether the reference was a read or a write, and admission's own INTRINSIC
  category is spelled "a value to compute with, not authority it acts through" — true of reading
  Math or JSON, false of reassigning them for every co-resident tenant. **The same failure reached
  UN-ADMITTED fragments too** (`comcon.include(src, {})` skips admission entirely by design, so
  `JSON = {...}` needed no declaration at all) — this is a runtime, value-level protection,
  orthogonal to admission, so it closes both paths with one mechanism.
  What is frozen is EVERYTHING PRESENT on globalThis at that one point — enumerated
  (`Object.getOwnPropertyNames`) rather than hand-listed, so there is no second name list to drift
  out of sync with admission's own intrinsics table (V7's rule). globalThis stays EXTENSIBLE:
  dependency loading (`ngx_js_comcon_eval_dep`) still declares each dependency's OWN names via a
  plain global-code eval, repeated on every `include()` call that names it for as long as the
  worker lives (no caching), and those names are not in the frozen set because they do not exist
  yet at freeze time — measured, five repeated loads of the same dependency file keep working
  identically. A dependency that collides with a frozen name fails
  GlobalDeclarationInstantiation before any of its code runs, reported by the existing "dependency
  is not a pure library" path with no new handling needed.
- **EV:** `t/comcon_global_binding_freeze.t` — 8 assertions: the admitted overwrite refused and its
  victim intact, the un-admitted overwrite refused and its victim intact, ordinary reads and
  `typeof` unaffected, and the residual below unchanged. One control (the freeze removed) fails
  exactly the four assertions that depend on it and none of the other four.
- **GAP:** Freezing protects an EXISTING binding, not a NEW one. A fragment that explicitly imports
  `globalThis` and does `globalThis.newName = X` can still plant a brand-new rendezvous point —
  but `globalThis` (like `eval`/`Function`/`self`) is already on admission's DENY list, refused even
  when listed in `imports` (`t/comcon_include_admit.t`), so this does not open through admission. It
  remains open for UN-ADMITTED fragments, which have no free-name gate of any kind by design.
  Closing it needs an architectural change (a private scope per fragment, not a property on a
  shared object), which belongs with the runtime-per-tenant question rather than this patch.
  **home:** finding F15 (phase 1 of its fix) · `ngx_js_comcon_compartment`.
- **THREAT:** T3, T4, T6, T9
- **V:** V13, V4

#### G7.12 — a fragment's own text cannot escape the wrapper it is compiled inside
- **CLAIM:** `comcon.include()` builds `(function(g0,...){"use strict";return(` + source +
  `)})` and compiles the whole buffer as one script. A `source` whose own text closes that
  function expression early and supplies more script-level code afterward is refused before
  any of that code runs, regardless of the contract — including `imports: []`, the strictest
  an operator can write, and `{}`, which skips admission entirely. The same protection applies
  to `contract.tests`, wrapped as a bare `(tests_source)`.
- **ARGUMENT:** Admission only ever inspected the RESULT of compiling `source` (the returned
  function), never the rest of the script that produced it — so a source that closed the
  wrapper early and reopened it to keep the completion value callable ran its escaped text
  with NO ADMISSION GATE EVER APPLIED, no matter how strict the contract asked to be.
  Measured: `imports: []` admitted a fragment whose escaped text read another fragment's
  declared free names and, before G7.11, reassigned a shared intrinsic for every fragment.
  **The fix compiles first and runs only if the shape is right**, using
  `JS_EVAL_FLAG_COMPILE_ONLY` (nothing executes yet, so a breakout's injected code cannot run
  before — or instead of — being refused) then `js_comcon_is_single_toplevel_closure()`, an
  engine helper verified against the compiler's OWN bytecode output rather than a second
  parser: the legitimate shape's root unit is exactly three opcodes (`fclosure8`; `set_loc0`;
  `return` — create the one closure, store it as the completion value, return it) and nothing
  else, because grouping parentheses compile to nothing and a function's own parameter list
  and body are entirely the CHILD closure's concern. Any breakout, with or without a second
  closure, adds bytecode before that tail. **A first, narrower version checked only the
  constant pool's closure count (must be exactly one)** — sufficient for the fragment
  wrapper, since it is itself function-shaped and closing it early always consumes that
  closure, forcing a second one to keep the result callable — but insufficient for `tests`'s
  bare-paren wrapper, where a comma expression can smuggle in a side effect
  (`(1), (globalThis.__x = 1), (function(fragment){ return true; })`) with no second closure
  at all. Found and closed the same session, before shipping: disabling the opcode check and
  keeping only the closure count reproduces exactly that one gap and nothing else.
- **EV:** `t/comcon_wrapper_breakout.t` — 13 assertions, three breakout shapes (comma with a
  replacement closure, comma with a bare side effect, a statement-level declaration) against
  both wrapper sites, the original severity (a shadowed intrinsic) refused at the source
  rather than merely caught afterward by G7.11's freeze, and the legitimate shapes an IIFE
  fragment body and a real `contract.tests` still need to keep working. Two controls: the
  whole mechanism reverted (5 of 13 fail — everything that depends on it, nothing else), and
  the opcode check alone disabled, closure-count-only (exactly 1 of 13 fails — the `tests`
  bare-side-effect case, and only that one).
- **GAP:** This closes the ADMISSION-BYPASS severity of F15's third finding. What remains
  named there and NOT touched here: the wrapper's own evaluation still runs with no deadline
  or memory allowance of its own (phase 3), and at config phase with no interrupt handler
  installed at all.
  **home:** finding F15 (phase 2 of its fix) · `js_comcon_is_single_toplevel_closure` ·
  `ngx_js_comcon_include_confined`.
- **THREAT:** T3, T6, T9, T11
- **V:** V13

#### G7.13 — the compartment meters itself, whether or not a worker exists
- **CLAIM:** Every place fragment-adjacent code runs on comcon_rt — a wrapper's own top-level
  evaluation, an admission test that invokes the fragment it tests, a confined invocation, and
  the leftover drain — is bounded by a deadline of its own, reachable and enforced with or
  without a worker.
- **ARGUMENT:** comcon_rt's interrupt handler used to require a worker (`w != NULL`) before it
  was installed at all, because it read `w->request_deadline_ms` — the same field the HOST
  runtime's handler reads. A worker does not exist at CONFIG PHASE (`js_source` evaluation,
  including `nginx -t`), so anything reaching comcon_rt there ran with NO interrupt handler
  whatsoever. Measured, each hanging until killed: the wrapper's own body (`comcon.include(
  "(function(){ for(;;){} })()", {imports:[]})` — the wrapper IS `"use strict";return(source)`,
  so a looping source runs during `include()` itself, before the fragment is even admitted); a
  confined invocation of an already-admitted fragment; and an admission test that calls the
  fragment it is testing (`tests` exists precisely to invoke it). Request-time paths were not
  unbounded — a worker's own ambient deadline (F6/F12) already covered them — but only loosely,
  as a side effect of sharing that field rather than by a budget of their own.
  **The fix gives comcon_rt a deadline that belongs to the COMPARTMENT, not the worker:** a new
  field, `jcf->comcon_deadline_ms`, checked by a new interrupt handler keyed on `jcf` rather than
  `w`. `jcf` is a stable pointer across `fork()` — the same property that already lets
  `jcf->worker` be set post-fork into a struct that existed before it — so installing the
  handler ONCE, at compartment creation, needs no post-fork re-wiring the way the old
  worker-gated handler did. `ngx_js_comcon_deadline_push()`/`_pop()` tighten and restore it
  around each risky call, min'd against the ambient `w->request_deadline_ms` when a worker
  exists (so a fragment still cannot outlive its enclosing request), and defaulting to the
  fragment's own timeout alone when it does not.
  **A push around the wrong call was found and corrected during the same session, by testing
  the claim rather than trusting it.** The first attempt wrapped `JS_EvalFunction()` on the
  compiled wrapper — but that call only MATERIALIZES the wrapper's closure (its whole job, per
  G7.12's three-opcode shape, is "make one closure and return it"); it does not CALL it, so no
  fragment-adjacent code runs there at all. The wrapper's body — `"use strict";return(source)`,
  where the looping IIFE actually loops — runs at the LATER `JS_Call(sctx, outer, ...)` that
  invokes the materialized wrapper with its grant arguments. The first attempt's own debug
  logging showed `JS_EvalFunction()` returning in milliseconds with no exception, which is what
  said the push was on the wrong statement rather than merely too generous.
- **EV:** `t/comcon_deadline_without_worker.t` — 9 assertions. The config-phase case is driven
  by a DIRECT `nginx -t` subprocess, deliberately outside Test::Nginx's own `run()`: that harness
  waits up to 5 seconds for nginx's pid file, written only after `js_source` finishes, so a
  single 5-second config-phase timeout already sits at that budget's edge and stacking more than
  one inside it would make the harness time out ambiguously instead of failing the test cleanly.
  The three request-time cases run one per request for the same reason in the other direction
  (Test::Nginx's `http()` carries its own 8-second alarm). A fourth case confirms the compartment
  is not left wedged by any of the pushes that preceded it.
- **GAP:** Only the four call sites this investigation reached (`include()`'s wrapper body and
  admission test, `invoke_confined`, the leftover drain) push a deadline; a future comcon_rt
  entry point that runs fragment-adjacent code must remember to push one too — there is no
  structural guarantee every future caller will.
  **v5.105 (nesting readiness):** the push now also mins against the deadline ALREADY in
  force on the compartment, and the memory allowance follows the same discipline
  (`ngx_js_comcon_mem_push()`/`_pop()` over a mirrored `jcf->comcon_mem_limit`, restoring the
  value found rather than a literal 64 MB); a depth counter (`jcf->comcon_depth`) keeps the
  settle loop and both drains at depth 1. Nothing in this tree nests, so the existing EV is
  unchanged; the tests that pin the stacked behaviour arrive with the nesting that exercises
  them (the authoring tier, phase 2).
  **home:** finding F15 (phase 3 of its fix, and the last of its three parts) ·
  `ngx_js_comcon_deadline_push` · `ngx_js.h`'s `comcon_deadline_ms` field comment.
- **THREAT:** T6, T9, T11
- **V:** V13

#### G6.8 — a fragment's reach OUTWARD is a capability, attenuated by destination
- **CLAIM:** A confined fragment can ask for an outbound request only through a granted
  capability; `allowHosts(glob)` attenuates it by destination, the refusal is a counted denial
  (`out.host`), and the queue the host reads is the host's alone (`out.drain`).
- **ARGUMENT:** `allowHosts` was the last vocabulary word blocked on a missing mechanism
  rather than on enforcement — there was nothing outbound for it to mediate, because a
  fragment held no way to reach the network at all. **It is not a fetch, and that is a
  finding, not a shortcut:** fragment invocation is strictly synchronous (`JS_Call`, then
  JSON-stringify), so a capability that performs I/O cannot be handed to a fragment without
  making invocation asynchronous — which would touch the F6/F12 deadline and the F2
  per-invocation allowance on the most safety-critical path here. So the capability RECORDS
  INTENT and the host performs the I/O, which is M-CFG's pattern: *the tenant proposes what it
  cannot apply.* The glob is checked **in the compartment**, where the capability is
  exercised, so this is an attenuation of authority and not a filter over data.
- **EV:** `t/comcon_outbound.t` — 19 assertions: the glob admits and denies, the denied
  destination never reaches the host queue, the drain half is refused by the reach gate,
  composition with `uses` and `ttl`, the glob meet, no default for an empty glob, credentials
  in a URL refused outright, audit mode logging and allowing, and the capability's own
  `describe()` rows. Four controls, each breaking a different assertion.
- **EV:** `t/tools/golden-denials.js` — `out.host` and `out.drain` frozen, each with its own
  probe rather than a written reason.
- **EV:** `t/comcon_outbound_roundtrip.t` — the ROUND TRIP: a policy asks for three destinations
  (one of them the link-local metadata address, denied), the host performs the permitted two
  against a backend in the same nginx, and the **same policy is invoked again with the responses
  and computes a verdict from them**. Plus the scheme-qualified glob. Four controls, two of which
  did not fire on their first run and are the reason two claims changed — see the GAP.
- **EV:** `OPERATOR_API.md` §8e — the host half (`std.outbound.perform`), which takes a request
  because `fetch` lives there, so draining belongs inside a handler.
- **GAP:** A policy that needs a RESPONSE needs **two fragment invocations** (ask, then be given
  the result). That is not a workaround, it is the shape the synchronous invoke imposes, and a
  real `fetch` capability waits on asynchronous fragment invocation — its own increment. A host
  glob cannot express "this host but not that path": `allowHosts` attenuates destination only.
  **And two properties here were DOCUMENTED BEFORE THEY WERE TRUE**, which is why the controls
  matter more than the assertions: `perform()` claimed to clear only what it performed while
  `clear()` took no count at all, and the scheme was claimed to be matched exactly while no test
  distinguished exact from globbed. Both controls failed to fire; `clear(n)` was implemented and
  both claims are now asserted. A property nobody can break is a property nobody has checked.
  **home:** ROADMAP M-LIB · VERIFICATION.md V8 (the classified surface).
- **THREAT:** T5, T6, T11
- **V:** V9

#### G6.7 — a capability can be bounded by LIFETIME
- **CLAIM:** `mediate(cap, ttl(seconds))` makes a capability stop working when its lifetime
  passes; composing two lifetimes takes the shorter, in either order; expiry is denied as
  `cap.expired` and logged-and-allowed in audit mode.
- **ARGUMENT:** This is what makes a session lease bite on authority already handed out —
  `std.sessions` expires a mapping, but `include()` binds grants at admission, so without a
  lifetime a fragment holds its capabilities for as long as it lives. The clock starts when
  the capability crosses into the compartment, so there is one clock rather than two.
  Lifetimes are ordered, so `min` is a genuine meet (unlike budgets, which are refused).
- **EV:** `t/comcon_cap_ttl.t` — expiry, the min-meet in both orders, composition with a mask and a budget, audit behaviour, and the refusal of zero/negative/fractional/absent lifetimes.
- **EV:** `t/comcon_v12_denial_codes.t` — `cap.expired` fires its own code, pinned after a first version of the row silently reported "alive".
- **THREAT:** T5, T6, T11
- **V:** V9

#### G6.5 — HOST JS is bounded too, by default
- **CLAIM:** A host `location.handler` — operator code, not a tenant fragment — runs under a
  per-request execution deadline that is **on by default** (10 s). `0` is an explicit
  opt-out; a missing or malformed value reads as the default rather than as unbounded.
- **ARGUMENT:** The guard existed and defaulted to OFF, which protects only the operators who
  already knew they needed it; one accidental `while(true)` hung a worker until SIGKILL and
  took every other client on it down. Ten seconds rather than the tenant's one, because host
  JS is trusted and may legitimately spend real synchronous time in a request. The knob now
  reads as its own default, so its value is discoverable without a document.
- **EV:** `t/js_host_request_deadline.t` — the default stops a runaway (evidenced in the error log, because the client gives up first); a 300 ms setting stops it in 0.3 s; `0` lets 1.7 s of work through.
- **EV:** `t/js_host_request_deadline.t` — and F12: a runaway in the continuation after
  `await req.readBody()` is stopped at the deadline, with the body sent LATE so the handler
  genuinely suspends (a GET settles synchronously and would test nothing).
- **THREAT:** T11
- **V:** V6

#### G6.6 — memory: a per-invocation allowance, over a shared runtime cap
- **CLAIM:** *(partial)* `JS_SetMemoryLimit(comcon_rt, 64MB)` bounds the runtime shared by
  every fragment. One fragment can exhaust the budget of its siblings — denial of service
  against peers, not an authority escape.
- **CLAIM (added 2026-09-13):** a fragment also gets a per-INVOCATION allowance — 16 MB by
  default, `contract.meter.memoryBytes` may only narrow it — enforced by narrowing the
  runtime limit for the duration of one call and restoring it after. The invoke is
  single-threaded, so growth in that window is attributable to that fragment, which is as
  much attribution as one shared runtime can honestly give.
- **EV:** `t/comcon_fragment_memory.t` — over-allowance is refused with `InternalError: out of memory`, the compartment survives it, a contract narrows but cannot widen, and the bound resets per call.
- **GAP:** It bounds a **burst, not a leak**: a fragment retaining memory across calls still
  walks the shared 64 MB runtime cap upward, and nothing attributes that to a fragment.
  **home:** HARDENING.md S5 · finding F2.
- **EV:** `t/js_worker_memory_limit.t` — the runtime-level mechanism, on the host runtime.
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
- **GAP:** A sixth condition, (f) — a fragment's SOURCE escaping the wrapper `comcon.include()`
  concatenates around it, defeating admission before it ever runs (F15, closed by G7.12) — is
  deliberately NOT folded into this file's battery. Every probe here assumes a well-formed
  fragment is already running; a wrapper-breakout source never becomes one, so the
  confined-vs-unconfined comparison this gate is built around does not apply to it, and forcing
  one would have meant a second escape battery living beside this one — the exact drift
  `t/tools/mses-probes.js`'s own header warns against. (f) is verified on its own terms, with its
  own two controls, in `t/comcon_wrapper_breakout.t`; recorded here so a reader of "the standing
  escape gate" knows a sixth condition exists and where it lives, rather than concluding the gate
  is now stale for not naming it.
  **home:** finding F15 · G7.12 · `t/comcon_wrapper_breakout.t`.
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

#### G7.5 — the escape battery against code that is ACTUALLY native
- **CLAIM:** *(evidenced 2026-09-12)* The M-SES battery closes every probe inside a fragment
  **lowered to native C**, and the compiled and interpreted tiers agree probe by probe.
- **ARGUMENT:** Running the gate on a JIT-capable BINARY was never the same claim as running
  it against COMPILED CODE. Server-AOT happens in the master pre-fork, and
  `js_comcon_aot_compile()` returns 0 for any bytecode function — "eligible", never
  "compiled" — so a gate that does not check whether lowering happened reports a green
  compiled tier while measuring an interpreted one. The precondition is therefore asserted
  first: the compiled arm must report `compiled >= 1` from `aotStatus()` (measured: **20
  functions**) and the interpreted arm must report `0`, same fragment, same battery, two
  demonstrably different tiers. The battery is read from one file shared with the standing
  gate, because two copies of an escape battery is how one quietly stops testing what the
  other still does.
- **EV:** `t/comcon_mses_gate_aot.t` — the battery against native code, on both arms, with the precondition and the unconfined control.
- **EV:** `t/tools/mses-probes.js` — the single definition of the battery.
- **EV:** `t/comcon_include_faithfulness.t` — SR-2: identical responses and denials across tiers over the confinement surface.
- **THREAT:** T7, T8
- **V:** V5b

#### G7.8 — the GUARDED class, fuzzed one process at a time
- **CLAIM:** Hostile writes to a `guarded` COM member fail like an engineered operation, not
  like a memory error: no crash, no alert, and the getter still answers afterwards.
- **ARGUMENT:** The setter fuzz excludes `guarded` and says why — those members rewire live
  dispatch, so writing to them in the shared instance degrades the server under test and
  every probe after the write measures wreckage. The exclusion was about SHARED STATE, not
  about the members, so each one gets **its own nginx**: enumerate the guarded members from
  the live registry, then start a fresh instance per member and fuzz that one alone.
- **EV:** `t/js_com_guarded_fuzz.t` — 3 guarded members reached by the same walk the setter fuzz uses, one process each; the control (a setter that dereferences NULL) shows the log check is what detects a crash, because nginx respawns the worker and the liveness probe passes anyway.
- **EV:** `t/js_com_setter_fuzz.t` — the safe/reversible class, and the skip counts that named this gap.
- **THREAT:** T1, T8
- **V:** V5b

#### G7.7 — co-resident tenants and the channels between them
- **CLAIM:** *(partial, and the weakest claim in this document)* The DIRECT readout is
  closed — a peer's name is unresolvable, its values unreachable, and opaque secrets are
  unprintable. What is not closed is the INDIRECT one.
- **GAP:** Timing, cache and contention channels between co-resident tenants are not
  mitigated; the constant-response-time REL profile is specified and not built. With the IFC
  gap (T4) this is the explicitly deferred confidentiality axis of the design, accepted
  rather than closed — but **no longer unquantified**: measured at 0.3 ms → 347 ms on a
  peer's latency (1227× idle, ~2.9 bits/s), narrowed to 49.8 ms by a 50 ms execution
  deadline. **Re-measured 2026-09-14 on the build that closed F14:** 0.3 → 319.9 ms (1105×,
  ~3.1 bits/s), 50.0 ms under the 50 ms deadline — unchanged, as it should be. **And a second medium
  existed that this measurement could not see (F14, closed — G7.10):** memory a peer merely HOLDS used
  to set every invocation's cost, persistently and beyond the deadline's reach.
  **home:** THREATS.md T9 · FOUNDATION §13.4 (post-M9 IFC track) · finding F8 · finding F14.
- **EV:** `t/tools/ifc-timing-channel.t` — the measurement (evidence, not a gate).
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
- **CORRECTED 2026-09-13 — the `Symbol.for` gap this leaf used to carry is WITHDRAWN.** It
  read: "an operator who declares `Symbol` for two tenants gives them a rendezvous." The arm
  that measured it compared `Symbol.for(k) === Symbol.for(k)` **within one fragment** — true
  of any registry, private or shared, and never a look next door. A probe whose read cannot
  be false is not a probe. Rewritten to attempt the whole exploit — take the symbol, use it
  as a property KEY on every surface both fragments touch — the writes come back
  `proto:refused,json:refused,array:refused` and the read is `clean`. **A shared registry
  gives two fragments the same key; a key is not a channel without a STORE to unlock, and
  every store they share is frozen.** Two controls make that a measurement: the same text
  unconfined reads the mark back (so the key really did match across two calls — the registry
  IS runtime-wide), and with the freeze disabled the confined arm becomes a live channel.
  **A per-fragment `Symbol` facet was built and then rejected:** it would have broken
  **erasure (G11.7)** — two fragments of one program calling `Symbol.for('k')` would get
  different symbols under COMCON than in plain node, an annotation changing what code
  computes — to close something no probe can show is open.
- **A NINTH SURFACE WAS MISSING, FOUND 2026-09-14, NOW CLOSED — see G7.11.** The eight
  surfaces above all probe VALUE MUTATION on a shared object (`JSON.__chan = 'x'`,
  writing an own property). None of them probed BINDING REASSIGNMENT (`JSON = evil`,
  replacing what the NAME points to) — and that channel was wide open: an admitted
  fragment declaring `imports: ['Promise']` (for READ, the only reason `imports`
  documents) could do `Promise = evil`, and a co-resident fragment reading `Promise`
  afterwards got the attacker's function. So "separated by ... an admission gate that
  refuses a fragment naming a neighbour" was TRUE of every mutation this leaf tested and
  INCOMPLETE as a description of the boundary: admission gates whether a name may be
  REFERENCED, not whether the reference is a read or a write. Closed by freezing every
  binding on globalThis, independent of admission (`t/comcon_global_binding_freeze.t`).
  This is the same shape of gap the `Symbol.for` erratum above records — a battery
  measuring a real mechanism against an incomplete set of operations — and is recorded
  here for the same reason: the eight-surface count in this leaf's own EV should not be
  read as nine were tried and eight held.
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
- **EV:** `t/comcon_cap_ttl.t` — the lease's other half: `resolve()` can stamp its remaining
  seconds onto the capabilities it returns, so a lease bites on authority already bound into
  a fragment (G6.7). Without it the mapping expired while the capability did not.
- **THREAT:** T5, T12
- **V:** V13

#### G11.1 — closed enumerations are GENERATED, never maintained
- **CLAIM:** Six closed enumerations are derived from the source and break the suite on drift.
- **EV:** `t/comcon_enumerations.t` — runs the checker; asserts the LAST check ran (no early exit).
- **EV:** `t/tools/check-enumerations.py` — p_symbols, portals, ops resources, intrinsics, denial codes, refusal codes.
- **THREAT:** T12
- **V:** V7

#### G11.7 — ERASURE: annotations decide whether code runs, never what it computes
- **CLAIM:** A policy's source computes the same answers admitted-and-confined inside COMCON
  as it does in plain **node**, with no annotations and no confinement at all.
- **ARGUMENT:** Principle 11 — extend by granting, never by changing the language. If an
  annotation could change semantics, "T2 refines T1" would be comparing two different
  programs and SR-2 would measure nothing. A *different engine* is the point: an in-process
  comparison shares the runtime whose behaviour is in question. The corpus targets where
  erasure could plausibly break — the numeric model at its boundaries (V1: 2⁵³, −0, NaN),
  string/JSON round-trips, RegExp state, sort and enumeration order, try/catch/finally.
- **EV:** `t/comcon_v13_erasure.t` — seven rows, two engines, byte-identical; its control perturbs the node arm and the diff is reported.
- **EV:** `t/tools/erasure-corpus.js` — the single corpus both arms read.
- **THREAT:** T3, T7
- **V:** V13, V1

#### G11.8 — the registry's READ-ONLY rows are held to account, per row
- **CLAIM:** Every read-only member the live walk reaches declares a type, that type is
  what a real read returns, and reading it mutates nothing.
- **ARGUMENT:** The typed tier and the config-review path reason from the registry's word.
  The SETTABLE half has had an instrument since the setter fuzz; the read-only half never
  did — and the read-only half is where the reach paths live (`proxy`, `ssl`, `upstream`,
  `sockets` all hand back a handle that reaches further). Its types come from a static
  name→type map of 24 entries standing in front of 126 getters, which is the shape of a
  claim that is true when written and quietly false later. The corpus is generated from the
  live tree, so it cannot go stale; what is hand-written — the unclassified inventory and
  the rows the walk cannot reach — is **pinned**, because a coverage number that moves
  silently is not coverage.
- **EV:** `t/js_com_schema_conformance.t` — 14 assertions, five instrument controls (a row
  that lies, the honest version of the same row, a getter that mutates a sibling on read, an
  undeclared row, and the volatility control firing on a value that really moves) plus two
  end-to-end controls run against the C map itself.
- **FOUND:** `names` declared `object[]` and returns `string[]`; **20 read-only rows the
  walk reaches had no declared type at all**; and `nginx.describe(req)` returns **zero
  rows**, so the request — `remoteAddr`, `uri`, `headers`, `body` — is entirely outside the
  registry.
- **GAP:** The read-only descriptor hardcodes `requestScoped: false` and
  `propagation: "worker-local"` for every row. Those are unfalsifiable today *because* there
  are no request rows to be wrong about; the test pins that at zero so adding them cannot
  quietly leave the field lying. The map is also keyed by bare member name across all types,
  which cannot express `server` being a string on a peer and a handle elsewhere — survivable
  only because a table-listed member never consults the map.
  **home:** finding F13 · M2 read-only rows · `ngx_js_ro_types[]` in ngx_js_com_describe.c.
- **THREAT:** T1, T10
- **V:** V8

#### G11.10 — the PROPAGATION column is held to account, across real workers
- **CLAIM:** A member the registry calls `worker-local` does not become visible in another
  worker when written; a member it calls `zoned-shared` does; and the per-object refine hook
  classifies the same member correctly on a zone-backed and a non-zoned upstream.
- **ARGUMENT:** This is the COW-trap axis and 306 rows make the claim, but no
  single-process observation can test it — within one worker "my memory" and "the fleet" are
  indistinguishable. It is also the one claim that is **conditional**: `describe()` does not
  report the table's value for peers, a refine hook resolves it from
  `peers->shpool != NULL`. So the two arms are each other's control in one fixture, one
  request pattern, one run, and the run asserts they **DISAGREE** — without that, "no other
  worker saw it" would also pass if the write did nothing, if the fan-out reached one
  worker, or if propagation never worked at all.
- **EV:** `t/js_com_propagation.t` — 17 assertions. Phase 1 is the conditional pair on
  `peer.weight` (zoned vs plain, same config); phase 2 is **generated per registry row** — a
  sentinel stamped into every eligible `worker-local` member in one worker, and every worker
  independently reporting anything holding one, so no record has to cross processes.
  Clean under ASAN and UBSAN.
- **RESULT: the claim holds.** 67 rows swept, not one leaked; the writer's own worker sees
  all 67; the conditional pair classifies and behaves correctly on both sides. **No defect** —
  this leaf is evidence that a registry column is true, not a bug report.
- **GAP:** The sweep covers **67 of the 154** settable `safe`+`reversible`+`worker-local`
  rows the walk reaches (44%). The other 87 are excluded for stated reasons, not overlooked:
  **52 booleans cannot carry a distinguishable sentinel** (`true` is also a natural value),
  so a boolean leak is invisible to this method; and 35 non-number rows are excluded because
  a sentinel written into a routing member (a `serverName`, a `root`) can stop the very
  worker under test from matching the next request, and a worker that cannot serve cannot
  report. **`auto-shared` has ZERO rows in the registry** — a declared enumeration value
  with no instances, so it is untestable by construction rather than untested.
  **home:** VERIFICATION.md V8 · `ngx_js_com_describe.c` propagation refine hooks.
- **THREAT:** T1, T10
- **V:** V8

#### G11.11 — the TESTS are checked for assertions that cannot fail
- **CLAIM:** No assertion in `t/` is dead in one of the four ways this tree has already been
  burned by: a discriminator comparing an expression with itself, an assertion true by
  construction, a corpus with no reader or a drifted row scraper, or a coverage tally scraped
  from the whole payload and described as a fixed total.
- **ARGUMENT:** V11 mutation-tests the policies; nothing tested the tests. Five assertions
  that could not fail were found in this arc, **every one by accident while doing something
  else** — the forged-argument `realize` probe, the `cap.expired` row that reported "alive"
  forever, the `Symbol.for` arm comparing a value with itself, that file's payload-wide
  coverage tally, and the F9 ledger row that counted five where the table showed six.
  **A dead probe does not fail; it reassures.** Every "closed" in the findings ledger rests on
  an instrument, so the instruments are now themselves gated.
- **EV:** `t/tools/check-dead-probes.py` — the four checks, each with its own control.
- **EV:** `t/comcon_dead_probes.t` — runs the tool as a gate and pins that each of the four
  checks actually RAN, the same argument `t/comcon_enumerations.t` makes about its own tool,
  and the reason two missing checks were found in that list.
- **FOUND, on the first run:** **39 assertions that claim something and verify nothing.** 27
  were padding ("nginx started without crash", "all checks passed", hand-written duplicates of
  the harness's own no-alerts checks); **12 named a specific behaviour and checked none of
  it** — 8 restating a claim the neighbouring assertion already proves, and **4 genuinely
  untested**: Content-Length suppression under a body filter, a chained filter running after
  an empty intermediate body, non-string filter returns passing through, and header-filter
  ordering surviving a restart. All four now have real assertions; all four claims turned out
  to be TRUE, which is why nobody noticed.
- **GAP:** It is a STATIC reader. It cannot tell whether an assertion's subject is reachable,
  whether a control fires, or whether a corpus row is compared with its expectation at run
  time — only a mutation answers that. A clean run means "not dead in the four known ways",
  never "every assertion is live". The dynamic half is
  `t/tools/verify-negative-controls.sh` (six rows automated, six manual) and the inline
  controls in the `comcon_*` suites.
  **home:** VERIFICATION.md V11 · `t/tools/verify-negative-controls.sh` for the dynamic half.
- **THREAT:** T3
- **V:** V11

#### G3.7 — the REQUEST is in the registry, and says it is request-scoped
- **CLAIM:** Every member of the tenant-facing request object carries a declared type, a
  safety class, and `requestScoped: true`; the declared type is what a real read returns.
- **ARGUMENT:** Every config-phase node had a type and a class while the object a tenant
  actually touches had neither — `nginx.describe(req)` returned zero rows. That is the reach
  surface S4 needs (`req.location` is a live COM handle into the config tree) and the input
  surface M4 types. The rows are **table** rows, not entries in the read-only name map,
  because the map is keyed by bare member name across all classes: a request `headers` and a
  location `headers` would have to agree, and `requestScoped` would come from the discovery
  path's hardcoded `false`. A table row is per-class and carries its own flags.
- **EV:** `t/js_com_schema_conformance.t` — 54 rows, every one request-scoped, every declared
  type checked against a real read; three controls (registry entry removed, one row removed so
  the discovery pass emits it, one row made to lie).
- **FOUND:** nothing wrong in the 54 — every type was read off the getter's `JS_New*` rather
  than inferred from its name, and the first run was clean. The contrast with the read-only
  map (one misdeclaration in 24 hand-written rows) is the argument for reading implementations.
- **GAP:** The classes for METHODS are a judgment: reads are `readonly`, response writes are
  `safe` and not reversible (bytes sent cannot be recalled), and the five that change where the
  request goes — `pass`, `redirect`, `subrequest`, `fetch`, `hijack` — are `guarded`, matching
  `proxy.pass` on the config surface. Nothing mechanically checks that mapping; it is stated in
  the table so it can be argued with.
  **home:** `ngx_js_request_members[]` in ngx_js_com_describe.c · finding F13.
- **THREAT:** T1, T10
- **V:** V8

#### G11.12 — the same fragment compiles to the same bytes
- **CLAIM:** One fragment, one toolchain, two cold compiles ⇒ a bit-identical `.so`.
- **ARGUMENT:** The compile→sign→cache story rests on it. Without it a signature over an
  artifact attests *which compile produced it*, not *what is in it* — so two honest compiles of
  one fragment disagree and signature equality cannot decide that a cached artifact matches a
  fragment.
- **EV:** `t/tools/check-jit-reproducible.sh` — hand-run (it needs a JIT-capable `qjs`), three
  controls: the defect restored, a compile error (refuses), and no artifact produced (refuses).
- **FOUND:** the claim was **false**, in six bytes. GCC records the translation unit's filename
  as an `STT_FILE` symbol and the name came from `mkstemps`. Fixed by deriving the basename from
  the bytecode hash. **Half the first fix was inert** — a `chdir` on the theory that absolute
  paths are recorded; GCC records only the basename, and the control is what established that.
- **GAP:** One fragment, one compiler, one host. It does not check reproducibility across
  toolchain versions or machines, and trusting-trust remains accepted as residual. It is
  hand-run, so it constrains nothing per-commit.
  **home:** VERIFICATION.md V14 · `jit_write_repro()` in quickjs/quickjs-jit.c.
- **THREAT:** T3, T12
- **V:** V14

#### G11.13 — describe ⊇ mutable holds for the PROGRAM instance too
- **CLAIM:** Every callable member of every program-instance surface appears in that surface's
  own `describe().ops` with an op and a rights class from the closed vocabularies, and every row
  names a member that exists.
- **ARGUMENT:** `t/js_com_describe.t` holds the COM tree to this discipline. The program
  instance has the same shape and had no check: four surfaces each carry a hand-written op list
  sitting inches from the members it describes, which is the drift vector the enumeration
  checker exists for — and two of the four are siblings with separate copies of one list.
- **EV:** `t/comcon_v9_pom_describe.t` — four surfaces, both directions, closed vocabularies
  restated in the test rather than imported (importing them would agree by construction), four
  controls over planted drift.
- **FOUND:** seven undescribed ops — `cst`, `origin`, and `describe`/`epoch`/`tombstoned` on
  `bindAt`, `describe`/`epoch` on `bindShared`. **`describe` was a classified row on both
  NodeViews and absent on both handles**: the same op classified on one surface and not its
  sibling. Auditing the fourth surface found the last two.
- **GAP:** It checks that a class is FROM the vocabulary, never that it is the RIGHT one — a
  `replace` row marked `R` instead of `F` would pass. The rights semantics (R/F/X/L) are not
  formalised anywhere a checker can read, so only presence and vocabulary are mechanical.
  **home:** VERIFICATION.md V9 · POM.md's rights classes.
- **THREAT:** T1, T10
- **V:** V9

#### G11.14 — the evidence can be re-run by someone else, in one command
- **CLAIM:** Everything §15 and `AUDIT_M-SES.md` §4 rest on can be reproduced by a reviewer
  who has not read either document first, from one entry point, with an objective verdict.
- **ARGUMENT:** F11 is the only finding no engineering closes — it needs a second person. But
  what stood between a willing reviewer and a reproduction was not evidence, it was *work*:
  eleven hundred lines across two documents, commands to extract by hand, knowing which
  builddirs are stale, and judging which numbers matter. Doing that once converts F11 from
  "nobody has reproduced this" into "reproducing this costs an afternoon".
- **EV:** `t/tools/reviewer-pack.sh` — refuses on a dirty tree, rebuilds every builddir and
  checks each binary is newer than the newest source, then runs the suites, the three standing
  checkers, the sanitizers and the negative controls. **GATE** results decide its exit code;
  **REPORTED** counts are printed and deliberately not judged, so it cannot fail for the wrong
  reason and cannot become a second copy of numbers the documents own.
- **EV:** `REVIEW.md` — the procedure and the sign-off block, pointing INTO
  §15/§16/§14, the ledger and the audit's §3/§6 rather than restating them.
- **GAP:** This closes the *reproduction* half of F11 only, and closing it needs a signature
  this repository cannot produce for itself. **Two attestations of the DESIGN** would need a
  reviewer who disagrees with the argument and says where — a different and larger exercise,
  and one the sign-off block is explicit about not being.
  **home:** finding F11 · REVIEW.md §4.
- **THREAT:** T3
- **V:** V15

#### G11.9 — THE SPEC cannot silently fall behind the code
- **CLAIM:** Every member of a set `SPEC.md` calls closed appears in `SPEC.md`, so the
  normative read cannot quietly stop describing the shipped system.
- **ARGUMENT:** G11.4 machine-checks THIS document; nothing checked the spec. A document
  whose stated job is "current truth, stated once" is the one most exposed to silent decay —
  it was accurate when written, the code moved eleven times, and no test ever failed. The
  check is deliberately narrow: it matches a **backticked identifier**, not a bare word,
  because `log` and `mode` are ordinary English and a bare-word search would pass on any
  prose at all while reporting green; and it forces the spec to name the key a caller
  actually passes rather than a paraphrase.
- **EV:** `t/tools/check-enumerations.py` check [7] — mediation flavors, denial codes and
  ops-resource identifiers by name; `comcon.refusalCodes()` as the delegated authority for
  the refusal codes. Four controls, one per branch.
- **EV:** `t/comcon_enumerations.t` — pins that check [7] actually RAN, on its own standing
  argument that "no drift" from a checker which bailed out early is a vacuous pass.
- **FOUND:** the spec had no mention of `routes`, `ttl`, the refusal codes, `cap.expired` or
  the session registry — all shipped — and still called the identity→environment mapping a
  future host deliverable; and `bindings` was described only as "binding/epoch store".
- **GAP:** This is a PRESENCE check, not a correctness one: it can tell that the spec names
  `ttl`, never that it describes `ttl` correctly. Nothing machine-checks prose against
  behaviour, and nothing here pretends to.
  **home:** VERIFICATION.md V2 (the reference semantics) · ROADMAP M2.5.
- **THREAT:** T3
- **V:** V2

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
- **EV:** `t/tools/lowering-ceiling.t` — the lowering ceiling against hand-written C, both on
  arithmetic and on a byte scan (PERFORMANCE.md §2b). It also **corrects the reading of the
  instrument above**: that file's known-positive control is a multiply-add loop both compilers
  eliminate, so its 13–23× is loop elimination and not code quality. An instrument whose control
  is stronger than the effect it certifies will make every negative result look like a property
  of the subject.
- **GAP:** The typed arm is a hand-written STAND-IN for typed output: it shows maxim's framing
  (gas check included) costs nothing, not that inference can prove `h : int32`. And the ceiling is
  measured on one box at one moment — the numbers are an order, not a constant.
  **home:** PERFORMANCE.md §2b · `nginx.bench`.
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
| **F2** | No per-fragment memory attribution; nothing asserts a fragment hitting the 64 MB runtime cap | G6.6 | **PARTLY CLOSED 2026-09-13:** a per-INVOCATION allowance (16 MB default; `contract.meter.memoryBytes` may only narrow) enforced by narrowing the runtime limit for one call. **It bounds a BURST, not a leak** — a fragment retaining a little per call still walks the shared cap up, and that residue is the part of F2 still OPEN |
| **F3** | Cross-compartment identity not probed | G7.6 | **PROBED 2026-09-12; CLOSED 2026-09-13** — no channel found on seven shared surfaces, and removing the freeze opens them, so the mechanism is identified rather than assumed. **The `Symbol.for` residual is WITHDRAWN: it was an artefact of a probe that compared a value with itself.** Rewritten to attempt the real exploit — a shared registry gives the same KEY, and a key is no channel without a STORE, which the freeze denies. Both a live unconfined control and a freeze-disabled control back that |
| **F4** | `guarded` / `irreversible` COM members are excluded from the setter fuzz | AUDIT_M-SES.md §3 → G7.8 | **CLOSED 2026-09-13:** each guarded member is now fuzzed in **its own nginx instance**, which is what the shared-state objection actually required. `irreversible` remains untested because the live walk reaches **none** — the class exists in the registry but no member on those paths carries it |
| **F5** | AOT-compiled fragments not separately run against the escape battery | G7.5 | **CLOSED 2026-09-12** (after the §15 signature — see §16): the battery now runs against a fragment with 20 natively-lowered functions, the precondition is asserted, and the tiers agree probe by probe |
| **F6** | Host JS (not fragments) is unbounded by default — a runaway `location.handler` hangs the worker | ASSUME A5, AUDIT §3 → G6.6 | **CLOSED 2026-09-13** (after the §15 signature — see §16): the deadline defaults ON at 10 s, `0` opts out, a malformed value reads as the default. Superseded in part by **F12** |
| **F7** | **TM-2:** session identity → environment mapping was unspecified and unowned | THREATS.md → FOUNDATION §8b, G10.3 | **SPECIFIED + BUILT 2026-09-12** (v5.65): `std.sessions`, descriptors-not-envs, attenuation-only, deny-by-default, leases. **Residual:** authentication, the principal namespace and the login transport remain the host's, by design and by statement |
| **F8** | Information flow / timing channels between co-resident tenants | ASSUME A3, THREATS T4/T9 → G7.7 | **ACCEPTED — and now QUANTIFIED (2026-09-13):** a co-resident tenant's CPU burn moves a peer's latency from **0.3 ms to 347 ms** (1227× idle, ~2.9 bits/s) because the worker is single-threaded. Under a 50 ms execution deadline the separation falls to 49.8 ms. The deadline is the only mitigation in the tree and it narrows, never closes **See F14:** a second medium — memory a peer merely HOLDS — set every invocation's cost beyond the deadline's reach, and was closed 2026-09-14; this CPU medium re-measured unchanged on that build (0.3 → 319.9 ms). |
| **F9** | V-track items with no machinery yet: **V5a, V5b, V6** (three — **V10 built 2026-09-13**, and it found a defect; was four — **V14 built 2026-09-13**, and it found its claim FALSE; was six — this row said five until 2026-09-13, omitting V5a, which §Placement has always listed as unbuilt; a ledger that undercounts its own backlog is the quiet kind of wrong) | VERIFICATION.md | **REDUCED TWICE 2026-09-13: V13 built** (G11.7) **and V8 built COMPLETE** — both halves: G11.8 (read-only schema conformance) and G11.10 (the propagation column, across real workers). **V14 built 2026-09-13** (G11.12 — and its claim was FALSE: the same fragment compiled to different bytes). **V9 built 2026-09-13** (G11.13 — seven undescribed ops found). **Four remain, and ALL FOUR (V5a, V5b, V6, V10) sit on the parked compiler/protocol track** — the reachable V-track backlog is empty |
| **F10** | `E_CAP_FLAVOR` / `E_CAP_ESCALATE` (the JS capability layer's own refusals) have no codes | MANUAL §3.2 [TBD-2] | **CLOSED 2026-09-12** (after the §15 signature — see §16): both ship, thrown by one `capRefuse()` that mirrors the C helper's shape. **`E_BUDGET_*` stays empty by placement** (budget exhaustion is a DENIAL) and the deadline abort has no refusal of ours to label — [TBD-2] is fully resolved |
| **F11** | The M-SES audit is one attestation with one signer; §4 not independently reproduced | ASSUME A2, G11.14 | **STILL OPEN, but no longer expensive (2026-09-13).** It needs a person, so it cannot be closed here — what has changed is the cost: `t/tools/reviewer-pack.sh` + `REVIEW.md` turn "read 1100 lines, extract the commands, know which builddirs are stale" into one command and a verdict table. **Reproduction is what a signature there buys; two attestations of the DESIGN would need a reviewer who disagrees and says where**, and the sign-off block says so rather than implying otherwise |
| **F13** | The REQUEST was outside the registry: `nginx.describe(req)` returned **zero rows**, so `remoteAddr`, `uri`, `method`, `headers` and `body` — the tenant-facing surface — carried no declared type and no class. The read-only descriptor hardcoded `requestScoped: false` for every row, unfalsifiable only *because* there were no request rows to be wrong about. | G11.8, G3.7 | **CLOSED 2026-09-13.** All **54** rows classified — 28 getters, 25 methods, one settable (`statusCode`) — as TABLE rows, which are per-class and carry their own `RQS`, rather than through the bare-name read-only map. **Every type was read off its getter, and none was wrong on the first run** (`startTime` is a number not a Date; `location` is a live handle, not a path string). The pin at zero is now the real count, plus an assertion that every request row declares `requestScoped` — so a getter added without a table row is emitted by the discovery pass with `false` and fails the day it lands. Three controls |
| **F12** | The host-JS deadline bounded one SYNCHRONOUS ENTRY — a runaway *after* an `await` was unbounded | G6.5 | **CLOSED 2026-09-13.** `w->current_request` is the chokepoint (8 entry sites, not the 19 `JS_Call`s first counted): one helper arms at each, nested entries INHERIT rather than extend, and the body-read completion — where post-`await` code actually runs — arms too. The time-gap heuristic stays rejected: under load the worker never idles |
| **F14** | Every confined invocation walked the WHOLE shared compartment heap (`JS_ComputeMemoryUsage`, twice per call) to read one counter — so one tenant's retained memory set every other tenant's per-request cost, persistently and beyond the execution deadline's reach | G7.10, G7.7 | **FOUND AND CLOSED 2026-09-14.** Measured before: a handler making one trivial confined invocation ran at **22.0% of stock** with an idle compartment and **0.2% (476 req/s)** while another fragment retained 200,000 objects. After: 68.0% and 66.6%. The same counter is now read in O(1) (`JS_GetMallocSize`). Found by reading the invoke path for a proposal, not by any test — nothing had measured invocation cost against heap size, and PERFORMANCE.md had no confined-invocation number at all |
| **F15** | A fragment's TOP-LEVEL expression is evaluated before admission, outside the tenant compartment scope, and unmetered — AND a shared global binding was reassignable across fragments — AND a source could escape the wrapper it was compiled inside, defeating admission entirely | G6.19, G7.11, G7.12, G7.13 | **CLOSED 2026-09-14, ALL THREE PARTS.** Investigating the original finding turned up two defects worse than it, closed alongside it. **G7.11:** an ADMITTED fragment body (`imports:['Promise']`) could do `Promise = evil` and corrupt every co-resident fragment; an UN-ADMITTED fragment (`{}`) could do the same to any intrinsic with zero gating — fixed by freezing every binding on the compartment's globalThis once, at creation. **G7.12:** the wrapper is built by string concatenation, so a source could close it and run script-level code before admission ever ran, even under `imports: []` — fixed by compiling with `JS_EVAL_FLAG_COMPILE_ONLY` and checking the compiled unit's own bytecode is exactly "create one closure, return it" before ever running it. **G7.13, the originally-found defect:** the wrapper's body — where a looping `source` actually runs — had no deadline independent of a worker existing, so it (and a confined invocation, and an admission test that calls its fragment) hung indefinitely at CONFIG PHASE. Fixed by giving comcon_rt a deadline of its own (`jcf->comcon_deadline_ms`), pushed and restored around every place fragment-adjacent code runs on it, installed once at compartment creation rather than re-wired post-fork. **No authority leaked** through any of this — a grant read at top level is `undefined` (`cap.owner` refuses it; every grant is bound to the fragment's future handle and `cur_frag` is 0 there) — but that was `cap.owner` holding for a reason it was not built for, not the reach gate that is supposed to. |

---

## 14. What this case does NOT establish

- **It does not establish memory safety** (A1), information-flow confidentiality
  (A3/F8), or availability against a controller inside its own subtree (A3).
- **It does not cover host JS** (A5): everything here bounds a CONFINED fragment.
- **Evidence exists ≠ evidence is sufficient.** The checker proves each cited artifact is
  there and runs; whether a test's assertions are strong enough is a human judgement, and
  the V-track (V11 mutation testing especially) is the only mechanical pressure on it.
- **A green suite is not a pentest.** SR-3 certified 2026-09-01; S6 carries the standing
  part of it forward. Nothing here substitutes for the next adversarial pass.

---

## 15. Sign-off

### The re-run of 2026-09-12 — the evidence this signature points at

All four builddirs rebuilt first (`objs`, `objs_jit`, `objs_asan`, `objs_ubsan`) and each
confirmed to carry the newest change (`budget.uses`, the `uses()` validation string), per
the rule that stale binaries invalidate everything after them. Tree at `d6ed62395`.

| check | result |
|---|---|
| `check-assurance.py` — the case matches the tree | **PASS** — 55 leaves, 69 cited artifacts, 55/55 `comcon_*.t` cited, 20-row rename table resolving |
| `check-enumerations.py` — six closed enumerations | **PASS** |
| `t/` on `objs` | 317 files / 4221 tests **PASS** |
| `t/` on `objs_jit` | 317 files / 4233 tests **PASS** |
| `t_stress/` on `objs` | 18 / 90 **PASS** |
| `t_stress/` on `objs_jit` | 18 / 90 **PASS** |
| ASAN over the COMCON corpus | 55 files / 683 tests **PASS** — **0 findings in `src/js`**, 0 elsewhere |
| UBSAN over the COMCON corpus | 55 files / 683 tests **PASS** — **0 in `src/js`**; 53 upstream, all `src/core/ngx_string.c:84` |
| sanitizer positive control | **fired** — leak detection landed a report through the same `prove` pipeline, so a clean run is not an inert one |
| `verify-negative-controls.sh` | **6 verified, 0 failed, 2 INCONCLUSIVE** (see below), 4 manual rows named with reasons |
| `t/tools/host-call-cost.t` | **PASS** on `objs` |
| `t/tools/policy-compute-split.t` | **PASS** on `objs_jit`, control at **21.6×** (`installed:1`) |

### Two things the re-run found, recorded because they are the point of re-running

1. **The M5 instrument measures only on `objs_jit`.** Run on `objs` it reports every arm at
   ~1.0× with `installed:0` — no native code anywhere, because that build lacks the
   server-AOT call. It did **not** quietly return a plausible 1.0×: its own guards (the
   control must exceed 5×; the two arms must be on different tiers) failed the run. The
   instrument caught the operator, which is what a guarded instrument is for — but the
   binary it needs is now written down here rather than remembered.
2. **Automated falsifiability dropped from 8/8 to 6/8.** `t/js_com_grant_declare.t` and
   `t/comcon_include_contract_fuzz.t` are now INCONCLUSIVE: their inverse patches no longer
   apply, because *this session's own commits rewrote the same lines* (the grant-wrapping
   path for budgets; the include contract path for refusal codes and the fail-closed
   `tests` check). Both tests still pass; what is lost is the automated proof that they can
   tell the difference. They join the four manual rows, and re-basing those patches is
   maintenance debt this signature is accepting, not closing.

### Findings, as accepted

| closed | probed, with a named residual | accepted as residual risk |
|---|---|---|
| **F1** (dead evidence citations — fixed and now checked) · **F7** (TM-2, specified + built) · **F3** (no channel on seven shared surfaces; its `Symbol.for` residual WITHDRAWN 2026-09-13 as a dead-probe artefact) | **F2** per-fragment memory attribution · **F4** guarded/irreversible COM members unfuzzed · **F5** compiled tier not separately probed · **F6** host JS unbounded by default · **F8** IFC/timing channels · **F9** seven unbuilt V-items · **F10** `E_CAP_FLAVOR`/`E_CAP_ESCALATE` uncoded · **F11** the M-SES audit's single signer |

| Role | Name | Date | Scope signed |
|---|---|---|---|
| Assurance case (SR-4) | **Vadim Zhestikov** | 2026-09-12 | The tree of §2–§11 as of `d6ed62395`, on the re-run above. Every `EV:` resolves to an artifact that exists and runs; every leaf carries evidence or a GAP with a home; no `comcon_*.t` is an orphan; T1–T12 and V1–V15 are each addressed; no document in the set cites a test that is gone. **The assumptions of §1 are accepted as stated** — in particular A1 (the TCB is assumed unforgeable) and A2 (the M-SES audit is one attestation, not two). **The findings table above is ACCEPTED AS RESIDUAL RISK, not closed.** §14 states what this case does not establish, and that statement is part of what is signed. |

> **ONE SIGNER, AND THE COMMANDS WERE RUN BY THE AUTHORING SESSION.** As with
> `AUDIT_M-SES.md` §5, this attests **acceptance of reproducible evidence**, not an
> independent reproduction: the re-run above was executed by the session that wrote the
> code, the tests and this document. Every command is named and re-runnable in minutes, and
> a reader who needs separation of duties should treat this row as outstanding and re-run it
> themselves — starting with `python3 t/tools/check-assurance.py`, which fails on drift, and
> `bash t/tools/verify-negative-controls.sh`, which is what makes the fixes falsifiable.
>
> **A signature on an assurance case is an acceptance of the residuals it names.** The
> findings table is exactly what is being accepted. A signature applied without reading it
> converts "we know these holes exist" into "someone looked and found nothing", which is
> worth less than no signature at all.

### What would invalidate this signature

Any of: a new `t/comcon_*.t` that no claim cites (check [3] fails); a code added to either
closed enumeration without a corpus row (checks [5]/[6]); an evidence citation that stops
resolving (checks [1]/[5]); a finding removed from the ledger without being closed; or a
change to the assumptions in §1 — most of all A1, since every claim above is conditional on
an engine that is not forged.

---

## 16. Changes after the signature (not covered by it)

§15's signature is dated 2026-09-12 and attests the tree as of `d6ed62395`. This section
records changes made **after** that point, on the model of `AUDIT_M-SES.md` §6, so the
signature is never quietly credited with work it did not see.

| change | effect on §15 |
|---|---|
| **F5 CLOSED** — `t/comcon_mses_gate_aot.t` runs the M-SES battery against a fragment with **20 natively-lowered functions**, asserts the precondition (`aotStatus().compiled >= 1` on the compiled arm, `0` on the interpreted one), and asserts the two tiers agree probe by probe. Two controls: both arms on a non-compiling binary (the precondition assertion refuses), and the intrinsic freeze disabled on the compiled build only (probes open on native code, tier agreement breaks). G7.5 gains evidence and loses its GAP; the battery moves to `t/tools/mses-probes.js` so the standing gate and this one cannot drift. | **Strictly narrows what was signed.** One accepted residual is now evidenced; nothing else changes. The signature's scope — the assumptions of §1, the remaining findings, and §14 — is unaffected. |

| **F10 CLOSED** — `E_CAP_FLAVOR` and `E_CAP_ESCALATE` ship, thrown by a JS `capRefuse()` that mirrors the C helper (code on `.code`, bracketed at the end of the message). E_CAP_ESCALATE has four raising sites: the two REACHABLE ones are pinned (`comcon_v4_monotonicity.t`, `comcon_budget_uses.t`), and the two that are defence-in-depth are recorded as unprobeable rather than given a dead probe. [TBD-2] is fully resolved; the two empty families are answers, not omissions. | **Strictly narrows what was signed.** A second accepted residual is now closed. |

| **F6 CLOSED, F12 OPENED** — the host-JS request deadline now defaults ON (10 s; `0` opts out; malformed reads as the default), with `t/js_host_request_deadline.t` and two controls (the default reverted to 0; the interrupt handler made inert). Closing it exposed the narrower gap that F12 now names: the deadline covers one synchronous entry, not a continuation re-entered from an event callback. | **Narrows one residual and names a smaller one.** F6 as signed ("host JS is unbounded by default") is no longer true; the remainder is F12, which did not exist as a separate row when §15 was signed. |

| **F12 CLOSED, F2 PARTLY CLOSED** — the host deadline is now armed at every entry that runs request JS (`w->current_request` is the chokepoint: 8 sites, one helper, nested entries inherit rather than extend), so a runaway after an `await` is stopped; and a confined fragment gets a per-invocation memory allowance (16 MB default, narrowable by contract, enforced by the engine). **Both probes were wrong first and their controls caught it:** the F12 probe used a GET (no body → no suspension → nothing tested) and the F2 probe allocated 64 MB (which hits the pre-existing runtime cap, so it passed with the new bound disabled). | **Narrows two residuals.** F2's remainder — a slow leak across calls — stays open and is named in its row. |

| **F4 CLOSED, F9 REDUCED** — the guarded COM members are fuzzed one process at a time (G7.8), which is what the shared-state objection actually required rather than an exemption; and V13 is built (G11.7), so F9 drops from seven unbuilt V-items to six. `irreversible` stays untested because the live walk reaches none of them, which is a fact about the walk and is recorded as such. | **Closes one residual and shrinks another.** |

| **`ttl` SHIPPED — G6.7 added** (a capability lifetime; `t/comcon_cap_ttl.t`, 3 controls). G0 is now decomposed into 59 leaves. It also closes the half of TM-2's session lease that G6.6's GAP could not reach: a lease can now bite on authority already bound into a fragment. | **Adds a leaf and narrows a GAP.** Nothing signed becomes untrue; §15's evidence table gains one row it did not see. |

| **The two INCONCLUSIVE negative-control rows are RE-BASED to MANUAL** (2026-09-13). §15 records "6 verified, 0 failed, 2 INCONCLUSIVE"; on the current tree the battery reports **6 verified, 0 failed, 0 skipped**, with the two rows moved into the MANUAL list *with the reason their inverse patch no longer applies* — so the count is honest rather than quietly two short. `git apply -R -3` was tried as an automated re-base and is now recorded in the script's header as a **trap**: it applied one file, failed the other, and left the partial revert in the tree. **The maintenance debt §15 accepted is not paid — it is now accurately labelled**, which is a different and lesser thing: six rows require a hand revert to check. | **Corrects an accounting, closes nothing.** The falsifiability that was lost is still lost; what changes is that the report no longer has an "inconclusive" bucket that reads like a transient failure. |

| **V8 BUILT — G11.8 added; F9 down to five** — `t/js_com_schema_conformance.t` generates a conformance check per registry row for the READ-ONLY half of the surface, which no instrument had ever covered. It found a real misdeclaration (`names`: `object[]` → `string[]`), **20 read-only rows with no declared type**, and that `nginx.describe(req)` returns **zero rows** — the tenant-facing request surface is outside the registry entirely. All 21 type rows are now classified against their implementations, and the inventory is pinned at empty so a getter added without a type fails CI the day it lands. Seven controls: five over planted objects, two run end to end against the C map (a row removed, a row made to lie). | **Closes one V-item and narrows F9.** It also converts part of F9 from "no machinery" into a standing gate. Two NEW gaps are recorded in G11.8 rather than left implicit: the hardcoded `requestScoped`/`propagation` on read-only rows, and the map's bare-name keying. |

| **M2.5 RE-STATED — G11.9 added** — `SPEC.md` had fallen behind the code (no `routes`, `ttl`, refusal codes, `cap.expired` or session registry; §10 still calling the identity→environment mapping a future deliverable; §13 stamped v5.35 against a delta log at v5.75). §2/§10/§13 now state current truth, and check [7] of the enumeration checker makes the currency of its closed sets machine-checked, with four controls. | **Adds a leaf; corrects a document, not a mechanism.** Nothing signed changes: §15 attested the code and the instruments, not the spec's prose. The new GAP is stated in G11.9 — presence is checked, correctness is not. |

| **V8 COMPLETED — G11.10 added** — the effect-class half. `t/js_com_propagation.t` holds the `propagation` column to account across four real workers: the conditional pair (the refine hook's `zoned-shared` vs `worker-local` on two upstreams in one config) and a sweep generated per registry row. **The claim holds — 67 rows swept, none leaked, no defect.** Four controls, two of them at the CONFIG level (remove the zone, add a zone) so they exercise the mechanism rather than mutating the test. Clean under ASAN and UBSAN. | **Adds a leaf that is positive evidence rather than a fix.** §15's evidence table gains a row; nothing it attested changes. The new GAP is quantified in G11.10: 67 of 154 rows, with both exclusions named, and `auto-shared` recorded as an enumeration value with no instances. |

| **F3 CLOSED — a residual WITHDRAWN, not fixed** — the `Symbol.for` rendezvous was an artefact of a dead probe: its read compared `Symbol.for(k) === Symbol.for(k)` inside ONE fragment, which cannot be false. The rewritten arm attempts the whole exploit and is refused on every surface; a shared registry is a shared NAME, and a name is not a channel without a store. Backed by two controls — the unconfined arm reads the mark back (proving the key matched, so the registry is genuinely shared) and the freeze-disabled arm turns the confined case into a live channel. `AUDIT_M-SES.md` §3's attested row is left as signed and carries an ERRATUM marker pointing at its §6. | **Removes a residual by retracting it.** This is the one kind of change that should make a reader MORE careful, not less: a signed audit recorded a finding that was not there, so the fix is an erratum plus a probe that can now fail. Nothing else §15 attested is affected. |

| **THE TESTS ARE NOW GATED TOO — G11.11 added.** `check-dead-probes.py` hunts assertions that cannot fail, after five such defects surfaced by accident in this arc. First run: **39 assertions claiming something and checking nothing** — 27 padding, 8 restating a proven claim, and 4 genuinely untested behaviours that now have real assertions (all 4 claims were true, which is why they survived). Five controls, one per check. | **Strengthens the basis of every other row in this ledger, and weakens confidence in none of them — but it should temper how the word "closed" is read.** Each closure rests on an instrument, and roughly one instrument in ten in this tree was measuring nothing. §15 attested the code and the instruments as they were; this is the first gate on whether an instrument measures at all. |

| **F13 CLOSED — G3.7 added.** The request surface is classified: 54 rows (28 getters, 25 methods, one settable), every one `requestScoped`, every type read off its getter and none wrong on the first run. V8's pin at zero — placed precisely so this could not happen quietly — is now the real count, and a new assertion means a getter added without a table row fails the suite. Three controls. | **Closes the last OPEN finding in the ledger.** What remains is two ACCEPTED residuals (F8 timing channels, F11 the single signer) and F9's six unbuilt V-items. Nothing §15 attested changes; a claim that was latent-false is now checked. |

| **V14 BUILT — G11.12 added, and the claim it checks was FALSE.** The same fragment compiled to a different `.so` on every run, differing in six bytes: GCC records the translation unit's filename and that name came from `mkstemps`. Fixed by deriving the basename from the bytecode hash. Three controls; half the first fix was inert and the control is what said so. | **Closes a V-item and fixes a real defect in the signing story's premise.** §15 attested the instruments as they were; this one did not exist, and what it found means any earlier reasoning that treated artifact bytes as a content identity was wrong. F9 drops to five, only one of them reachable. |

| **V9 BUILT — G11.13 added.** The describe⊇mutable discipline now covers the program instance: four surfaces, both directions, closed vocabularies. **Seven undescribed ops found**, including `describe` itself being a classified row on both NodeViews and absent on both epoch handles. Four controls; two rules of the auditor had to be corrected first (it reported the correct implementation of lazy materialization as drift). | **Closes the last REACHABLE V-item.** F9 drops to four, all of them on the parked compiler track. The new GAP is worth reading: this checks that a class is FROM the vocabulary, never that it is the RIGHT one. |

| **G11.10's instrument had a 4%-under-load flake, found by hunting and fixed.** `t/js_com_propagation.t` test 13 looked the sweeping worker up in a later fan-out that need not have reached it (measured: absent in 4 of 40 runs under load); it now reports its own count in the request that wrote. A second assertion in the same file required ≥2 distinct workers where the leak check only needs ≥1 OTHER than the writer. A/B under load: fixed 0/40, pre-fix 3/40. | **Makes an existing leaf's evidence trustworthy; changes no claim.** Worth recording for how it was missed: the original stability check was three runs, and sixty standalone runs of the broken code also pass — the condition needs full-suite load. A denominator means nothing except against the conditions the failure requires. |

| **THE REVIEWER PACK — G11.14 added; F11 made cheap rather than closed.** `reviewer-pack.sh` runs everything §15 and the audit's §4 rest on, from one entry point, refusing on a dirty tree and rebuilding every builddir first because the committed binaries are stale artifacts. `REVIEW.md` is the procedure and the sign-off block, pointing into the canonical lists rather than copying them. | **Changes no claim and closes no finding.** F11 stays OPEN: it needs a second person, and this only makes their afternoon cheap. Worth noting what it deliberately does NOT do — it does not summarise the residuals a signer accepts, because a signature on a summary is worth less than no signature. |

| **M-LIB `allowHosts` SHIPPED — G6.8 added.** The outbound capability: a fragment records an INTENT through a granted cap, the glob is checked in the compartment, and the host performs the I/O. Two new denial codes (`out.host`, `out.drain`), both frozen with probes. It composes with `uses` and `ttl`; two different host globs are refused rather than guessed. **Seven of the ten vocabulary words now ship.** | **Adds a leaf and a new authority surface.** The GAP in G6.8 is the honest part: this is not a `fetch`, because fragment invocation is synchronous, and a policy needing a response needs two invocations. Nothing §15 attested changes; a new capability is new surface, and its escape-relevant edges (the reach gate on the drain half) are evidenced rather than argued. |

| **The outbound ROUND TRIP is demonstrated, and the scheme can be pinned (v5.86).** `std.outbound.perform()` drains a capability through `req.fetch`, and `t/comcon_outbound_roundtrip.t` shows a policy asking, the host performing, and the policy deciding **from the responses**. `allowHosts` globs may be scheme-qualified, with the scheme matched exactly — which is NOT MANUAL's `protocol` (enforced operation order), still unbuilt. | **Closes the gap G6.8 named for itself.** Two of its four controls did not fire on the first run: one claim was false (`clear()` took no count) and one was unmeasured (exact vs globbed scheme). Both are now true and asserted — recorded because the lesson is about the claims, not the feature. |

| **M-LIB `window` SHIPPED — G6.9 — and it found a defect in the INVOKE, G6.10.** A recurring lifetime beside `ttl`'s countdown, with its own code `cap.window`, UTC by decision, wrapping midnight, whole-day, and refusing two different schedules. **Eight of ten vocabulary words now ship.** Its probe returned a denied call directly — the most natural thing to write — and exposed that a fragment returning `undefined` produced `SyntaxError: unexpected token: 'undefined'`. `undefined` is what every denied gate returns. | **Adds two leaves; one of them is a pre-existing defect on the invoke path, which §15 attested.** The defect was never reachable by any existing test because every probe wrapped its result, so nothing §15 relied on was wrong — but an operator tightening a policy would have met it immediately. Recorded here rather than quietly fixed. |

| **M-LIB `cosign` SHIPPED — G6.11.** The two-person rule, and the ninth of ten vocabulary words. The interesting property is that **distinctness is structural**: `as` is written on the trusted side and unreachable from inside a compartment, so a fragment holds one identity and casts one vote, and the quorum assembles across invocations. `cap.cosign` is the first denial that is a waiting state rather than a verdict, and the first **with a side effect** — the denied attempt records consent. `E_CAP_PRINCIPAL` is the 14th refusal code. **A control caught a wrong instrument again:** the expiry probe read `req.args.as`, but `req.args` is the raw query string, so both requests voted as the same principal and the test passed identically whether the `within` meet took the shorter window or the longer one. | **Adds one leaf and one refusal code.** Nothing signed becomes untrue; the new code is appended, which the frozen-contract rule permits, and §15's evidence table gains one row it did not see. |

| **THREE DEFECTS FOUND BY ASKING `window`'s QUESTION ONE AXIS OVER — G6.12.** Probing each word ALONE found a `window` gap at v5.87; probing each word alone **on each capability KIND** found that a bare `uses`/`ttl`/`cosign` over an outbound capability was refused as "not a NginxSocket", that the outbound budget key was **not namespaced** so one `uses` name was two counters, and that `JS_ToCStringLen` on a missing property returns the string `"undefined"` — so a descriptor with no glob was wrapped with the literal host glob `undefined` and the refusal that claimed to catch that never ran for it. | **All three were FAIL-CLOSED**, so no authority was ever widened and nothing §15 attested becomes untrue. The budget one is the material find: a documented property (*one name, one counter*) was false across capability kinds. Adds one leaf. |

| **M-LIB `protocol` SHIPPED — G6.13. THE MEDIATION VOCABULARY IS COMPLETE: ten of ten.** Enforced operation order, denial code `cap.protocol`. Its gate is the first here to **separate its decision from its effect** — checked before cosign, committed after the budget — because an operation another gate still refuses must not advance the conversation. **And its test found a defect in `cosign`:** an already-consenting principal was judged by its POSITION in the record rather than the record's LENGTH, so once a quorum was met the FIRST signer's retry was denied forever — breaking the actual ops-room sequence (alice tries, bob cosigns, **alice retries**). Every cosign assertion had the SECOND principal perform the operation, which is exactly the shape that passes with that bug present. | **Adds one leaf and corrects G6.11's evidence**, which now pins the retry. The `cosign` defect was FAIL-CLOSED — it denied an operation that should have been allowed — so nothing §15 attested about confinement becomes untrue; what was untrue was that the feature was usable as documented. |

| **THE POSTURE WORDS SHIPPED — G6.14.** `onViolation` and `profile` were written in MANUAL since v5.0 and read by nothing; §4 withheld them because *a posture assembled from ignored keys would be believed by exactly the reader least able to check.* Both are read now. The material change is granularity: the audit/enforce switch was FLEET-WIDE, so **shadowing one tenant's new policy also stopped enforcing every other tenant's** — a strictly worse posture than the one being carefully reached. `profile` is read by being refused where it cannot be honoured. | **Adds one leaf, and closes a §4 abstention with the reason it was taken.** `onViolation` can WEAKEN, which is safe only because the contract is written on the trusted side — stated in the leaf rather than assumed. `std.postures.*` stays absent with a SHARPER reason: not "nothing enforces" but "what lockdown should narrow to is a decision nobody has made". |

| **ASYNC FRAGMENTS SHIPPED — G6.15, and the blocker was not where the roadmap said.** ROADMAP recorded the synchronous invoke; an async fragment never reached it, being refused as *"not a bytecode function"* — **untrue of an async function**, which is a bytecode function with a different class id. Six COMCON analysis entry points tested one id where the engine has a four-class helper, so the C3 analysis **refused to look** at async and generator bodies. Fixed at all six; the promise is then settled by draining the compartment's own jobs, bounded by the deadline AND a job cap, and an unsettleable promise is reported as `E_INVOKE_PENDING` rather than stringified into `{}`. | **Adds one leaf and one refusal code, and WIDENS what admission accepts** — which is the one direction that needs saying out loud. It is not a weakening: the analysis now RUNS on bodies it previously refused to read, and the test pins that by asserting an undeclared free name inside an async body is still refused. The escape battery (§15/G11) has not been re-run against async fragment shapes; that is recorded in the leaf's GAP, not claimed. |

| **THE DEFERRED-JOB ESCAPE, CLOSED — G6.16, and it was opened by G6.15 one day earlier.** A fragment could queue a job and return; nothing else drains the compartment runtime, so the job ran inside the NEXT unrelated invocation — on a stranger's deadline and memory allowance, gated at a stranger's wall-clock time, and under a stranger's `onViolation` posture. Every invocation now drains to quiescence inside its own compartment scope. Also: nothing in this process installed a promise-rejection tracker, on either runtime, so a failed continuation was silent everywhere. | **Closes a hole this project's own increment opened, found by probing that increment rather than by a report.** Two of the fix's first attempts were wrong and their controls said so: the "job threw" signal is unreachable (a promise reaction catches its own throw, so the failure is an unhandled rejection), and the posture assertion could not discriminate until the fleet and the binding were made to DISAGREE. Adds one leaf and corrects a v5.92 claim about which bound fires. |

| **V10 BUILT — G10.4 — AND IT FOUND A DEFECT.** The last V-item independent of the parked compiler track. The mode fan-out's epoch bump was three operations from JS, so two concurrent switches both wrote the same epoch with different modes — and because the reconciler early-returned on epoch EQUALITY, the loser's divergence was **permanent and silent**: a fleet moved to `enforce` could leave one worker in `audit` for the rest of its life. The publish is now one critical section under the store's lock. | **Reduces F9 from four unmodelled V-items to three, and closes a silent-divergence hole in a rollout mechanism §15 relied on.** The model's own control is built in (three arms), and the model said the obvious one-line fix was insufficient *before* the code was written — which is the first time a model in this project has been ahead of the implementation. |

| **THE OWED STRUCTURAL FIX, PAID — G6.17.** G6.16 closed the deferred-job escape with a best-effort drain and named what it could not do: a fragment outrunning the job budget leaves work behind. Every granted wrapper is now bound to its fragment and every gate refuses it to anyone else, so a leftover continuation runs and **obtains nothing**. `cap.owner` is the first denial code naming a structural invariant rather than a policy. | **Turns a named residual into a checked property, and the check's own negative case is forced rather than argued** — the probe deliberately outruns the budget so the leftover path is exercised, including for the COM facet, whose check would otherwise be code no control can break. What remains of G6.16's gap is accounting (a stranger's deadline and clock), not authority. |

| **THE AUDIT-MODE RESIDUAL, CLOSED.** G6.17 shipped with `cap.owner` logged-and-allowed in audit like every other gate, justified as consistency. It is now unconditional. The line is not "structural vs policy" — the reach gates are structural too — but **whether an operator has anything to observe and then enable**: every other code answers "may this fragment do this?" (a question about the grant, which is their lever), and this one answers "is this even this fragment's capability?", which no grant can change. | **Removes a stated residual rather than re-describing it, and the test asserts the DISTINCTION** — a closed window allowed and a foreign capability denied, under the same audit mode in the same request, so it cannot pass on a build where audit stopped working. The exception is in the machinery, not the gates. `cap.owner` is now the one code that does not follow `comcon.mode()`; a second would force a list. |

| **THE LOWERING CEILING IS MEASURED, and it reframes M5.** `t/tools/lowering-ceiling.t` + `nginx.bench`: untyped lowering is **8.3× off hand-written C on arithmetic and 17× on a byte scan**, and `--jit-dump-c` shows why — every operation boxes a `JSValue`, tag-checks both operands and writes a type-feedback byte, with the accumulator living in a `double`. A typed-SHAPE arm, gas check kept, is **at parity**. The zero-copy `ArrayBuffer` view over nginx memory **works and buys nothing** (the access path dominates; one 16 KB copy is 0.2 µs against 12.7 ns/byte to scan it). | **Turns the M5 commitment from a judgement into a measurement, and corrects two beliefs of our own** — that a host call per element is the thing to avoid (it costs about the same as a compiled typed-array read), and that `policy-compute-split.t`'s 13× control demonstrates lowering quality (it demonstrates loop elimination). Adds no assurance claim: this is decision evidence for a milestone, not a confinement property. |

| **"CAGES NEST FOR FREE" MEASURED — G7.9 — and half of it is not built.** SHOWCASE17 §8's reseller scenario has ACME caging its own customers by calling the kernel operators; measured, `comcon` reads `undefined` in a fragment, declaring it changes nothing, and granting it is `E_CAP_GRANT`. **Attenuation nests without limit; authoring does not nest at all.** Also: enumeration **check [8]** now enforces that a NOT-BUILT list cannot name a word the code ships — the rot that let the ROADMAP's POSITION block call four shipped words unbuilt for three days. | **Adds one leaf and one checker, and corrects a scenario in place rather than leaving it to be discovered.** The security half was already asserted by the S6 gate (a fragment reaching `comcon` is an escape); what was missing was saying that this *also* means the reseller story is unbuilt. Nothing signed becomes untrue — the boundary moved in the docs, not in the code. |

| **F14 FOUND AND CLOSED, F15 FOUND AND OPEN — G7.10, G6.19** — every confined invocation walked the whole shared heap to read one counter: measured 22.0% of stock throughput idle and **0.2%** while a peer retained 200,000 objects; now 68.0% / 66.6%, gated as a ratio (1.01×, control 173.57×). Out of memory now reads as out of memory rather than `null`, and an exception during `include()` reaches the host with its value instead of none. The investigation found F15 — the top-level expression is evaluated before admission, outside the compartment scope, unmetered at config phase — and records it rather than fixing it in passing. | **Closes a cross-tenant channel the signature did not know about, and opens a finding it did not know about.** Neither changes what was attested; both are recorded so the signature is not credited with either. |

| **F15 PHASE 1 CLOSED — G7.11, and a §15-SIGNED CLAIM WAS INCOMPLETE.** Investigating F15's original finding (an unmetered top-level eval) turned up a worse one: G7.6 — signed 2026-09-12, same date as §15 — probed eight shared surfaces for cross-fragment channels and found none, but every probe mutated a VALUE (`JSON.__chan = 'x'`); none tried REASSIGNING a binding (`JSON = evil`). That ninth operation was wide open: an admitted fragment declaring `imports: ['Promise']` for READ could overwrite `Promise` for every co-resident fragment, and an UN-ADMITTED fragment (`{}`, no admission at all) could do the same to any intrinsic. Closed by freezing every binding on the compartment's globalThis once, at creation, independent of admission. | **A claim §15 attested (G7.6's "separated by ... an admission gate") was TRUE of every operation its own battery tried and INCOMPLETE as a description of the boundary — not forged, not fabricated, but narrower than the prose read.** §15's invalidation list (end of that section) does not name this case; recorded here on the same model as F14's row, and G7.6 itself now carries the correction in place, parallel to the `Symbol.for` erratum it already carries. F15 stays open for its original finding plus a wrapper-breakout defect found alongside (phases 2–3). |

**A signature is not re-earned by a change that removes a gap**, and it is not invalidated
by one either. What would invalidate it is listed at the end of §15; a finding *closed with
evidence and recorded here* is the opposite of that.