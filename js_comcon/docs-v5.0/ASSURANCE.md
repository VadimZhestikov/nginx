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
  exception path. `profile` is read: `restrictive`/`declarative` are accepted, `adaptive` and any
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
  deadline. **home:** THREATS.md T9 · FOUNDATION §13.4 (post-M9 IFC track) · finding F8.
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
| **F8** | Information flow / timing channels between co-resident tenants | ASSUME A3, THREATS T4/T9 → G7.7 | **ACCEPTED — and now QUANTIFIED (2026-09-13):** a co-resident tenant's CPU burn moves a peer's latency from **0.3 ms to 347 ms** (1227× idle, ~2.9 bits/s) because the worker is single-threaded. Under a 50 ms execution deadline the separation falls to 49.8 ms. The deadline is the only mitigation in the tree and it narrows, never closes |
| **F9** | V-track items with no machinery yet: **V5a, V5b, V6, V10** (four — **V14 built 2026-09-13**, and it found its claim FALSE; was six — this row said five until 2026-09-13, omitting V5a, which §Placement has always listed as unbuilt; a ledger that undercounts its own backlog is the quiet kind of wrong) | VERIFICATION.md | **REDUCED TWICE 2026-09-13: V13 built** (G11.7) **and V8 built COMPLETE** — both halves: G11.8 (read-only schema conformance) and G11.10 (the propagation column, across real workers). **V14 built 2026-09-13** (G11.12 — and its claim was FALSE: the same fragment compiled to different bytes). **V9 built 2026-09-13** (G11.13 — seven undescribed ops found). **Four remain, and ALL FOUR (V5a, V5b, V6, V10) sit on the parked compiler/protocol track** — the reachable V-track backlog is empty |
| **F10** | `E_CAP_FLAVOR` / `E_CAP_ESCALATE` (the JS capability layer's own refusals) have no codes | MANUAL §3.2 [TBD-2] | **CLOSED 2026-09-12** (after the §15 signature — see §16): both ship, thrown by one `capRefuse()` that mirrors the C helper's shape. **`E_BUDGET_*` stays empty by placement** (budget exhaustion is a DENIAL) and the deadline abort has no refusal of ours to label — [TBD-2] is fully resolved |
| **F11** | The M-SES audit is one attestation with one signer; §4 not independently reproduced | ASSUME A2, G11.14 | **STILL OPEN, but no longer expensive (2026-09-13).** It needs a person, so it cannot be closed here — what has changed is the cost: `t/tools/reviewer-pack.sh` + `REVIEW.md` turn "read 1100 lines, extract the commands, know which builddirs are stale" into one command and a verdict table. **Reproduction is what a signature there buys; two attestations of the DESIGN would need a reviewer who disagrees and says where**, and the sign-off block says so rather than implying otherwise |
| **F13** | The REQUEST was outside the registry: `nginx.describe(req)` returned **zero rows**, so `remoteAddr`, `uri`, `method`, `headers` and `body` — the tenant-facing surface — carried no declared type and no class. The read-only descriptor hardcoded `requestScoped: false` for every row, unfalsifiable only *because* there were no request rows to be wrong about. | G11.8, G3.7 | **CLOSED 2026-09-13.** All **54** rows classified — 28 getters, 25 methods, one settable (`statusCode`) — as TABLE rows, which are per-class and carry their own `RQS`, rather than through the bare-name read-only map. **Every type was read off its getter, and none was wrong on the first run** (`startTime` is a number not a Date; `location` is a live handle, not a path string). The pin at zero is now the real count, plus an assertion that every request row declares `requestScoped` — so a getter added without a table row is emitted by the discovery pass with `false` and fails the day it lands. Three controls |
| **F12** | The host-JS deadline bounded one SYNCHRONOUS ENTRY — a runaway *after* an `await` was unbounded | G6.5 | **CLOSED 2026-09-13.** `w->current_request` is the chokepoint (8 entry sites, not the 19 `JS_Call`s first counted): one helper arms at each, nested entries INHERIT rather than extend, and the body-read completion — where post-`await` code actually runs — arms too. The time-gap heuristic stays rejected: under load the worker never idles |

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

**A signature is not re-earned by a change that removes a gap**, and it is not invalidated
by one either. What would invalidate it is listed at the end of §15; a finding *closed with
evidence and recorded here* is the opposite of that.