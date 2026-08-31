# COMCON — Roadmap & Measured Results (v5.0)

*Merges v2 §8 (staging) and §11 (first slice) with the typed-policy→maxim milestone
plan and the M1 gate results. Ordering is de-risked: validate the payoff before
building the compiler; two hard gates (M1 performance, M8 safety) plus the hardening
gate (M-SES) before any untrusted execution.*

---

## 1. Positioning

- **COMCON is step 1.** Policies double as **compilation contracts**: a fragment whose
  environment (imports), exports, and internals are fully described is closed enough
  for AOT.
- **maxim** (QuickJS fork; JS→C via bytecode→C→GCC/TCC, typed vars, phase-35
  server-AOT, phase-34 hybrid `.so` = C fn + bytecode fallback) is **step 2**,
  consuming policy-annotated typed fragments.
- **mirror as a policy:** the mirror transpiler is a compile-time transformation — an
  *adaptive-profile* policy in v3 terms.

The pipeline: typed COMCON policy (.js, restricted+typed) → parse restricted subset →
capability check (compile-time reject) → type-bind against the typed host-API schema →
maxim lowering to unboxed C against typed nginx stubs → per-tenant `.so` (hybrid
fallback) → the event dispatcher calls the C function pointer directly.

---

## 2. Milestones

- **M1 — Perf spike (hand-written C, no compiler). ✅ GATE PASSED 2026-08-16.**
  Hand-wrote the C a compiled "count + tag" policy would become (shm-atomic counter +
  request-header read + two response headers + body) as a static nginx module;
  benchmarked against the *same policy* as an interpreted mirror rule on an identical
  config skeleton. Results (h2load, HTTP/1.1 keepalive, best of 3):

  | Config | loopback | % | cross-host 10GbE | % |
  |---|--:|--:|--:|--:|
  | stock nginx (no policy) | 391,233 | 100% | 424,882 | 100% |
  | **hand-C policy** | 350,165 | **90%** | 409,386 | **96%** |
  | interpreted mirror | 113,327 | 29% | 119,044 | 28% |

  Hand-C = **3.1–3.4×** the interpreted mirror. A *reduced-policy control* (the
  directive-expressible subset run additionally as pure nginx `map`+`add_header`
  config) showed directives ≈ hand-C ≈ stock — **the whole gap is the interpreter**,
  and the compiler's payoff is precisely the policies directives cannot express
  (shared state, counters, routing). Artifacts: `t_performance/maxim_m1/` (branch
  `js_comcon`). These are the measured endpoints of the performance gradient
  (FOUNDATION §8); v2's cost model (§8.2) is no longer only argued.

- **M2 — Typed nginx-API schema.** Machine-readable static type signatures for the
  policy-visible host surface (mirror `ev.*` first: header/cookie reads, response-header
  writes, table get/set/incr, respond/selectUpstream), each op carrying param/return
  types, required capability, effect class, and C-stub signature. **Fuse with
  hardening S4** — the same walk over the `describe()` registry yields the type row
  and the facet/reach rule per op (HARDENING §S4). *(v4)* The schema is **dual-role**:
  it types the capabilities granted to policies *and* it is the type system of the
  admissible config surface (M-CFG) — design it with value-domain constraints, not
  only function signatures, so the second role is not precluded.

- **M2.5 — POM + kernel spec.** Consolidate `SEMANTICS.md` + `POM.md` into the
  normative spec: resources, the four operators, possession metatheorem,
  closure/quotation, profiles, node interface, R/L/F/X classes. After M2.5, M3's
  capability check is simply "does this reference resolve in the bound environment."
  *Expanded per showcase lessons (§5):* also deliver (a) the **selector/target
  language grammar** (promoted from open questions — the showcases used it constantly
  and invented syntax ad hoc), and (b) the **denial/explain schema** (promoted — it is
  the operator UX, the deny-suite assertion language, the learning record, and the LSP
  diagnostic; design it with the descriptors, not after). *(v4)* Also state the
  kernel's **instance-genericity** (admit parameterized by grammar+schema; one pattern,
  N instances), the **∅-environment principle**, and the rights-not-values meet rule
  for data instances (FOUNDATION §2a, SEMANTICS v4 additions). *(v4.1)* Plus the
  **third closed enumeration**: the ops-resource capabilities (denial log,
  binding/epoch store, provenance registry, class-F broadcast channel, snapshot store,
  signing key, learning-recorder switch) — first-class caps so tool verbs are pure
  library code and "no backdoor" is checkable (FOUNDATION §8a). *(v4.2)* Explicitly
  **not** a deliverable: any standalone composed policy grammar — quotations quote
  policy-JS itself, "declarative" is a `syntax_allowed` profile, descriptor tables are
  the admission normal form (SEMANTICS §4.4); the selector grammar deliverable stands;
  the M3 front-end does double duty (tenant code *and* policy quotations).

- **M-CFG — the admissible config surface (new in v4; parallel track, does not block
  M2–M7).** Give COM its admission hinge: tenant config fragments = sentences of a
  restricted config grammar, admitted like code — `syntax_allowed` over config
  productions, typed against the dual-role M2 schema, contract tests, pin-by-hash,
  audit-first rollout. Includes quotation-based config proposals (tenant proposes what
  it cannot apply; the operator realizes — formalizing the snapshot/rollback console)
  and config learning-mode harvest. Deliverable: one tenant subtree onboarded through
  admit end-to-end. Gives the 3-layer operator-reconfig UX plan its principled
  foundation.

- **M-LIB — standard policy library (new; showcase lesson §5.1).** The user-facing
  surface is not the kernel but the combinators: `std.profiles.*` (tenant,
  pure_library, forensics/REL, marketplace, config_builder…) and the mediation
  vocabulary (`routes/allowHosts/uses/ttl/window/cosign/readOnly/redact/protocol/
  opaque.*`). Authored *in* the policy language once M3 exists; governed by its own
  policy (raw operators withheld); interceptors that close over capabilities need
  **certification criteria** (TCB-adjacent). Seed set specified at M2.5; grows with
  M5/M6. *(v4.1)* Includes **`std.ops`** — the comconctl verbs as library programs
  over the kernel + ops-resource caps (FOUNDATION §8a): there is no management plane,
  so the "tooling track" of §5.2 collapses into this library plus one thin shell
  (pilgrim substrate: the P19 admin-shell / nginx.repl machinery). *(v4.1, WASM note)*
  Also a future **`wasm` facet**: WASM slots into the one-pattern model as another
  governed language instance — its validation *is* admit, its import object *is* an
  environment (grant by another name), wasmtime fuel *is* budget mediation — giving
  polyglot (Rust/Go) or CPU-heavy leaf fragments a home as admitted, budgeted,
  mediated compute capabilities. COMCON stays the authority plane; WASM never becomes
  a second management surface. Low priority; design note in the memory branch §17.

- **M-SES — engine hardening.** Phases S1–S6 and the gate as specified in
  `HARDENING.md`. Does not block M2–M5 (trusted code); **gates M6/M7-with-tenants**;
  required even pure-AOT (hybrid fallback re-enters the interpreter). Start S1 early.

- **M3 — COMCON front-end.** Restricted-subset parser + capability declarations +
  compile-time rejection of out-of-environment references. Typed AST out, no codegen.
  *(v4.2, §20)* Deliverable added: the **typed-profile spec** — typed-JS is a *profile
  of* policy-JS, never a separate language (a grammar fork would break JS-stays-pure,
  the POM 1:1 mapping, and erasure soundness). Three type layers, none grammar:
  (i) the M2 schema types the API (types arrive from the environment); (ii) inference
  covers locals (M4 — the Misty-like core is small precisely so inference works);
  (iii) residual annotations use an **erasure-sound carrier: JSDoc-style comments**
  (`/** @type {i64} */` — valid JS by construction; checkJs/Closure precedent). The
  spec names the profile and fixes the annotation convention + inference boundary.

- **M4 — Type binding.** Bind the AST against the M2 schema → fully-typed IR; `any`
  forced to the hybrid path. Typed locals inferred; mismatches rejected. *(v4.2, §10)*
  Joint M3+M4 output = the **fragment artifact**: standard QuickJS **bytecode** (tier-1
  executable, produced by type erasure — erasure soundness, §10) + a **type/capability
  side-table** keyed by provenance/bytecode offsets + environment signature + content
  hash + admission certificate. One format serving the interpreter, maxim, the
  sign/cache pipeline, worker shipping, and M8's refinement testing.

- **M5 — maxim lowering (one handler).** Consume the **fragment artifact** (maxim's IR
  is already QuickJS bytecode — the side-table's declared types feed its existing
  typed-vars machinery, **replacing inference**; the M5 question is that mapping, not
  "build a lowering"). Emit unboxed C against the typed stubs (the M1 hand-written
  ABI). Deliverable: a compiler-produced `.so` for the M1 example; behavior
  byte-identical to the interpreted rule; the C ideally ≈ M1's hand-written C. *Scope
  note (showcase lesson §5.5):* lowering must **partially evaluate static mediations**
  — a constant-predicate membrane (e.g. an allowHosts prefix guard) becomes an inline
  check in the generated C, not a call. Scenario 43's "policy that vanished" is this
  feature; without it the compile-through performance story is untrue for any mediated
  capability. *(v5.0 — R4:)* generated C must remain **meterable**: loops emit
  **back-edge gas checks** (the interpreter's `JS_SetInterruptHandler` does not cover
  native code — without this, a compiled infinite loop hangs the worker unmetered);
  until back-edge gas lands, the tier-2-eligible profile is **loop-free** (the M1
  shape); and generated C allocates **only through metered stubs** — no raw malloc is
  ever emitted. *(v5.0 — R5:)* **shared mutable slots (flow, table) are the type
  boundary**: the compiled tier reads them unboxed on the side-table's word, so every
  **write from a lower tier into a declared-typed slot is guarded** (the
  gradual-typing boundary discipline) — otherwise a hybrid fragment writing `any` into
  a slot a compiled fragment reads as `i64` is type confusion inside native code.

- **M6 — Dispatch + AOT wiring.** The event dispatcher calls the compiled C function
  pointer (per-tenant `.so`, phase-35 precompile); interpreted fallback for the
  non-lowerable. End-to-end compiled policy serving in a worker. *Gated by M-SES.*
  The POM's class-F live-rewrite machinery (epoch broadcast + dirty→fallback→re-AOT)
  lands here, reusing `describe()` and the cfgbus transport. *(v5.0 — R3:)* every
  compiled fragment checks a **per-fragment generation counter at entry** (one
  load+branch): revoking any capability that was **statically baked** into its C bumps
  the generation → the fragment self-demotes to bytecode (where the live membranes
  are) until re-AOT. Revocation therefore has two `describe()`-visible cost classes:
  dynamic-checked (flag, instant) vs static-baked (generation bump + re-AOT).
  *(v5.0 — R10:)* multi-node changes: **partial application of a meet is itself a
  meet** — restrictive overlays are safe in any order/timing (the monotone-rollout
  property; why the lockdown button needs no transaction); **widenings** (administrative
  rebinds) use **two-phase epoch groups** — prepare on all workers, flip together.

- **M7 — Benchmark the real pipeline.** Compiled-by-COMCON policy vs interpreted vs
  stock, same method as M1. The number that confirms (or corrects) M1 at
  full-pipeline scale — expected between M1's 28% and 96% endpoints, near the top if
  the lowering is direct. *(v4.1)* Add a **WASM baseline column**: the same count+tag
  policy as a Proxy-Wasm filter (ngx_wasm/wasmtime), measuring the host-boundary
  marshaling cost against maxim's borrowed-`ngx_str_t` stubs — our claim that WASM
  pays at exactly the boundary this workload hammers is currently cost-model
  reasoning, and M7 is where it becomes (or fails to become) a measurement. *(v4.2)*
  Also a **same-artifact tier row**: one fragment artifact measured on its bytecode
  (T1) and its C (T2) — the per-fragment version of M1's 28%/96% endpoints.

- **M8 — Safety hardening + audit. GATE for any multitenant use.** The compiler-
  faithfulness obligation, now precise (SEMANTICS §3 assumption F): *the lowered C
  must refine the mediated semantics along the provenance links — simulating every
  MEDIATE/NAME gate it erased.* *(v4.2)* Practical method: **T2-refines-T1 differential
  testing** — run each fragment's admission allow-suite on both tiers of the same
  artifact and compare; the contract tests double as refinement evidence. Plus
  per-tenant `.so` isolation, table-key namespacing, worker resource guards,
  threat-model doc.

- **M9 — Breadth.** More events (response headers, L4/TLS), the borrowed/owned string
  ABI to kill refcount churn, the `any` hybrid, wider typed surface. Iterative.
  **Post-M9 track:** information-flow/taint labels (confidentiality axis —
  FOUNDATION §13.4).

**Critical path:** M1 ✅ → M2(+S4) → M2.5 → M3 → M4 → M5 → [M-SES gate] → M6 → M7 →
M8 gate → M9.

---

## 3. Delivery staging (v2 §8.1, preserved)

The three-stage delivery order stands, now with its guarantee proved rather than
argued (soundness is stage-independent — SEMANTICS §3):

1. **Stage 1 — declarative only.** Policy code executes only inside compilation
   episodes; runtime = engine consults static residue. Covers the multi-tenant core
   and is exactly the maxim-compilable profile. Stage-1 reservations (tri-state
   descriptors, check-point placement, function-tolerant field shapes) remain
   mandatory so stages 2–3 arrive without migration.
2. **Stage 2 — compile-time transforms.** Parser callbacks and source/AST rewrites —
   mirror-as-policy (adaptive profile) lands here.
3. **Stage 3 — runtime interceptors.** The resident residue: value validators, dynamic
   predicates — `mediate` interceptors on the hot path, tri-state keeping allow/deny
   fast.

---

## 4. Minimal first slice (v3 revision of v2 §11)

Exercises every pillar, updated for the kernel and the anchors model:

1. **Anchor recognition** — `"use comcon: <name>";` directive-prologue anchors parsed
   and exposed as node attributes (inert in plain JS).
2. **Minimal kernel** — `env()/grant/bind` for one enumerated node kind; a policy
   program runs at stage 0 under a hardcoded root environment.
3. **One static enforcement** — a free identifier not in the bound environment is a
   compile error; an excluded production (`WhileStatement`) rejected via `admit`'s
   syntactic predicate.
4. **One runtime enforcement** — a single mediated capability (prefix-guarded `fetch`
   or an opaque value) enforced at a check point.
5. **Monotonic nesting test** — an inner `bind` can tighten but not loosen (2)–(4);
   plus the two negative tests from the worked examples: granting a withheld name
   fails statically; splicing a capability into a quotation fails at construction.
6. **Escape-probe seed** — the first five S6 probes (constructor ladder, prototype
   pollution, indirect eval, `.stack` leak, COM upward traversal) running against the
   slice.
7. **Microbenchmarks** — shared-property access, cross-fragment call, mediated-op
   dispatch vs stock qjs; the COW/IC/revocation cost question (FOUNDATION §13.1) is
   the make-or-break implementation risk and must be measured, not argued.

> Validates: anchors + external binding, policy-as-program over the real kernel,
> static + runtime enforcement, the narrowing lattice, the cap-free-quotation rule,
> and the first hardening probes — a stage-1 artifact honoring the stage-1
> reservations.

---

## 5. Lessons from writing the showcases (2026-08-19) — plan deltas

Writing the 45 scenarios was itself a design probe. What it surfaced, and what changed:

1. **The standard library is the product surface.** Users see combinators
   (`routes/uses/ttl/window/redact/protocol/opaque.*`), almost never the raw kernel.
   → **M-LIB added** (§2), incl. certification criteria for cap-closing interceptors.
2. **Ops tooling carries half the scenarios** (learn/shadow/enforce, diff, revoke,
   epoch rollback, forensics attach, evaluate, docs, trust-report). → named as
   deliverables inside milestones rather than a monolith: deny-trace + diff with
   M2.5's schema; shadow/audit = the binding failure-semantics option (near-free);
   revoke/cascade with M-LIB mediations; epoch rewrite/rollback with M6; learning-mode
   static harvest = stage-1 dry-run (already in staging plan).
3. **Denial/explain schema promoted** from open question into M2.5 (it is the UX, the
   deny-suite assertion language, the learning record, and the LSP diagnostic).
4. **Selector language promoted** from open question into M2.5 (used constantly,
   invented ad hoc while writing — the clearest "spec missing" signal).
5. **M5 must partially evaluate static mediations** (scenario 43) — scope note added.
6. **Opaque values + COW domains have no milestone home** (scenarios 7/16/19/32
   depend on them; the M-track builds the compiler, not this engine substrate).
   → recorded as an explicit **engine-substrate track** decision point after M2.5:
   either schedule it (stage-1 declarative COMCON needs it) or mark those scenarios
   phase-2. The COW/IC microbenchmark in the first slice (§4.7) is its go/no-go input.
7. **Cluster-edge policing (scenario 27) crosses process boundaries** — the kernel
   semantics is single-runtime; workers-as-fragments is genuinely new design surface.
   → added to FOUNDATION open questions; deferred.
8. **Confirmations (no change):** determinism-caps-early paid off twice (36, 44);
   meet-composition (21, 45) and epochs (24, 28, 41) required no new mechanism — the
   kernel absorbed every scenario that wasn't an explicitly-listed gap.

### Scenario → earliest-home traceability

| Capability cluster | Scenarios | Earliest home |
|---|---|---|
| include/admit/bind pipeline, grammar-valued interfaces | 1, 3, 4, 30, 44, 45 | M2.5–M4 (+M-LIB facets) |
| Intensional queries, quotation/realize, pin-by-hash | 38, 39, 40 | M2.5 (spec) / M3 (engine) |
| Compile-through + membrane partial-eval | 43, 33(economics), epilogue | M5–M7 |
| Live epochs: revoke, fuses, overlays, rollback, rewrite | 2, 12, 21, 24, 28, 31, 34, 41 | M6 (+M-LIB mediations) |
| describe()-derived docs/audit/trust | 29, 37, 25 | M2 registry + tooling verbs |
| Learning mode / shadow | 5, 14 | bind failure-semantics (early) + stage-1 harvest |
| Frozen intrinsics, budgets, REL/forensics | 18, 10, 13, 6 | M-SES S1/S5 (+M-LIB profiles) |
| **Opaque values + COW views** | **7, 16, 19, 32** | **engine-substrate track — UNSCHEDULED (lesson 6)** |
| Multi-language includes, adaptive transforms | 11, 15, 22, 36 | M9 / stage 2 |
| Cross-process, mobile fragments, marketplace ops | 27, 17, 8, 9, 20, 23, 26, 35, 42 | mixed; 27 = new open question |

The table doubles as a demo-driven acceptance checklist: a milestone is "showcase-true"
when its scenarios run as written.

## 6. Lessons from the user's manual (2026-08-19) — second working-backwards pass

`MANUAL.md` (written as-if-shipped) surfaced a *different* class of gaps than the
showcases — the user-journey ones (full harvest: MANUAL.md Appendix B):

1. **Tenant-side SDK is missing from the plan entirely.** `comconctl dev` — run a
   fragment against **capability doubles** generated from the same environment spec the
   host binds, with the emulator and production admission being the *same* `admit`
   gate ("works locally" ≡ "admitted"). → new work item alongside M-LIB; the doubles'
   fidelity contract is its design question.
2. **The denial schema needs stable machine codes** (`E_CAP_*`, `E_ADMIT_*`,
   `E_BUDGET_*`, `E_PIN_*`…) — tenants pin CI to codes, not message text. Sharpens the
   M2.5 denial-schema deliverable.
3. **The widening workflow is a product surface:** `comconctl request` → host reviews a
   descriptor diff → approval = new epoch → generated docs self-update. The lattice
   makes self-service *narrowing* safe; the manual shows requesting-more must be a
   first-class (reviewed) flow, not an email.
4. **Terminology freeze at M2.5** — the manual had to pick user-facing words
   (cage/binding/epoch/facet/pin); its glossary should become normative before more
   docs accrete synonyms.
5. **Product decisions flushed out as [TBD]s:** default-root out-of-box contents
   (secure-vs-useful line), budget unit semantics (wall vs CPU, per-request vs
   per-episode), grammar-version compatibility window on engine upgrades, nginx
   integration knobs.
6. **Confirmation:** the grant-little → watch-denials → adjust → enforce loop is the
   product's single repeated motion across all three hats — the tooling deliverables
   of §5.2 should be sequenced to make *that loop* work end-to-end first.

## 7. Adoptions from Crockford's Misty (2026-08-20)

Reviewed mistysystem.com (same E-lineage ocap tradition as COMCON — its
authority-by-creation/construction/introduction is our possession metatheorem; its
facets/revocation are our mediate flavors — so mostly *validation*). Four targeted
adoptions, all cheap:

1. **M3 subset shape:** define the authored typed profile starting from a
   "Misty-like core" of JS — no `this`, no classes/`new`/prototype access, no
   coercion/truthiness. Cuts M3–M5 surface substantially; deletes prototype-authority
   leaks from authored code by construction. (External hardening still takes full JS.)
2. **M-LIB: Misty-style patterns** as the validation vocabulary for grammar-valued
   interfaces (named fields, composable, bounded quantifiers). Two bonuses: ReDoS is
   impossible by construction → tenant profiles can deny the regex engine and offer
   patterns instead (a removed vulnerability class, like scenario 3's SQL); and
   static patterns compile through (maxim lowers them to straight C).
3. **M-LIB: `stone()`** — deep immutability for plain *data*, paid once, zero
   per-access cost; the cheap alternative to read-only membranes on hot shared paths
   (membranes for authority, stone for data).
4. **Guest posture (M-SES S4):** actor/worker creation is a capability,
   default-denied in tenant profiles (Misty: "guests cannot create actors") —
   sharpens the existing Worker-constructor de-ambient item into a stated default.

Also: Misty's actor model (no shared memory; the address IS the capability) is the
recommended direction for open question 8a (cluster edges); and Misty + ADsafe join
the patent prior-art list — they confine by language replacement/authored subset,
COMCON confines unmodified JS via external policy over a program tree, compiled
through. NOT adopted: Misty as source language, new syntax, DEC64 (violates the
JS-stays-pure thesis).

## 8. The symmetry correction (v4, 2026-08-20 — user-spotted)

The v3 stack governed the program at two hinges (typed surface → POM; caps/classes on
the tree) but governed config at only one (caps/classes on COM) — the config *surface*
was untyped and unadmitted. The correction (FOUNDATION §2a): **one governed-language
pattern, N instances**; **data is code bound to the empty environment** (Principle 9);
and COM gains its admission hinge as work item **M-CFG**. Plan impact: M2 declared
dual-role; M2.5 gains instance-genericity + ∅-env + rights-meet; M-CFG added as a
parallel track; nothing measured or gated moves. Retro-evidence: the snapshot/rollback
JSON files were already quotations of COM subtrees, and the operator-reconfig UX was
already closure-vs-quotation for the config instance — the mechanisms existed before
the name. Patent note: "config as sentences of a restricted, typed language admitted
under a schema" is a claimable refinement between the COM claims and the safety-class
claims.

## 9. The comconctl closure (v4.1, 2026-08-20 — user-spotted)

comconctl looked like "a utility using some API" — an unclosed generalization. Closed:
**there is no management plane** (FOUNDATION Principle 10 + §8a). Every verb is a
`std.ops` library program run as an admitted episode in an operator session; all ~20
verbs decompose over the kernel + the ops-resource capabilities (the new third closed
enumeration, an M2.5 deliverable). Plan impact: the §5.2 tooling deliverables collapse
into M-LIB/std.ops + one thin shell (P19 substrate); **M-SES scope shrinks** — no
separate admin API to harden, administration rides the same admitted-episode gate as
everything else; audit closes over operators (trust-report on sessions;
office-hours/cosign mediate admin verbs natively). Residue outside the language:
bootstrap + transport (host integration).

## 10. Compilation tiers & the fragment artifact (v4.2, planning session)

The question "compile typed JS to an interpreted subset first, or directly to C?" has
a structural answer: **those are not sequential stages but two permanent tiers.** The
interpreted tier (bytecode) is load-bearing forever — (1) the phase-34 hybrid fallback
*runs* it during class-F rewrite windows; (2) the `any`/dynamic residue executes on it;
(3) admission and `comconctl dev` run interpreted (no C toolchain at admit); (4)
maxim-less deployments are the correct-but-28% tier. Direct-to-C is therefore only
ever *additive*, and maintaining a second backend that must agree with maxim doubles
the M8 burden — rejected.

**Deciding fact:** maxim's IR *is* QuickJS bytecode (its pipeline is bytecode → C →
GCC/TCC). Hence the chosen architecture — **"fat bytecode"**:

```
typed policy-JS ──M3/M4──▶ FRAGMENT ARTIFACT ──(iff maxim)──▶ C → .so
                            = bytecode                         phase-34 hybrid:
                            + type/cap side-table              {C fn, bytecode},
                            + env signature + hash + cert      prefer C, fall back
```

**Erasure-soundness principle:** a typed program run interpreted with its types
ignored behaves identically to its compiled form — types only *reject* (at admission)
and *accelerate* (at tier 2), never change semantics. So "typed → interpreted subset"
is type erasure plus the profile check, not a translation; and M8's obligation becomes
the checkable "T2 refines T1" (differential testing on the same artifact). M1's
numbers are the two tiers measured: 28% (T1) / 96% (T2 ceiling).

Parked as a post-M7 optimization: a **typed-IR entry point inside maxim** (bypassing
its bytecode-decode/type-recovery front phases for fully-typed fragments) — better
type precision, same back phases, still emits the bytecode sibling.

## 11. The v5.0 design-review hardening (R1–R12, 2026-08-22)

A full adversarial pass over v4.2 before implementation, adopted in whole (user
decisions: R1 = one-adaptive-per-node; R6 = as proposed). Summary and homes:

| # | Issue found | Fix | Home |
|---|---|---|---|
| R1 | meet/ACI claim false for adaptive transforms | confluence restricted to restrictive; **≤1 adaptive policy per node** (`E_ADMIT_ADAPTIVE_CONFLICT`) | SEMANTICS (BIND), FOUNDATION §4 |
| R2 | cap-free deep check TOCTOU-unsound (getters/proxies/mutation) | QUOTE side condition = **stone** (deep-frozen plain data); opaque values unspliceable | SEMANTICS (QUOTE), FOUNDATION §6 |
| R3 | revocation invisible to compiled fragments (membranes erased) | per-fragment **generation check at entry** → self-demote to bytecode; revocation cost classes in describe() | M6, PERFORMANCE |
| R4 | tier-2 escapes gas/memory metering (interrupt handler = interpreter-only) | maxim emits **back-edge gas**; loop-free profile until then; allocation via metered stubs only | M5, HARDENING S5 |
| R5 | type confusion via shared slots (lower-tier `any` write → unboxed read) | declared-typed slots get **write guards at the tier boundary** | M4/M5, M8 audit |
| R6 | realize = confused deputy (operator's full env) | **least-authority realization**: mandatory contract + free-name manifest ∩ realizer grants | SEMANTICS §4.4, MANUAL |
| R7 | unbound-node execution & pin-mismatch semantics undefined | EXEC requires binding (`E_UNBOUND`); **bound nodes never unbound**; pin mismatch refuses new epoch, old serves | SEMANTICS, POM |
| R8 | positional ids silently retarget after structural edits | **creation-ordered ids**; positional selectors volatile; **binding-set drift report** on admission | POM §2 |
| R9 | snapshot query bindings let new code escape policies | **born-bound rule**: match-sets recomputed at every admission in reach | POM §2 |
| R10 | mixed-epoch windows during multi-node changes | monotone-rollout property (partial meet = meet) for restrictive; **two-phase epoch groups** for widenings | M6 |
| R11 | admission front-end is pre-sandbox attack surface (parser CVEs) | fuzz the front-end; resource-limit + isolate admission parsing | HARDENING S6 |
| R12 | failure-mode composition undefined | strictness order `reject > deny > attenuate > audit`; meet takes strictest | SEMANTICS (BIND) |

Non-technical: repo confirmed **private** (company-internal) — pushed design docs are
not public disclosure; keep non-public until the patent-filing decision.
