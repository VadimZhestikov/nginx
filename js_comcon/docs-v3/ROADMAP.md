# COMCON — Roadmap & Measured Results (v3)

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
  and the facet/reach rule per op (HARDENING §S4).

- **M2.5 — POM + kernel spec.** Consolidate `SEMANTICS.md` + `POM.md` into the
  normative spec: resources, the four operators, possession metatheorem,
  closure/quotation, profiles, node interface, R/L/F/X classes. After M2.5, M3's
  capability check is simply "does this reference resolve in the bound environment."
  *Expanded per showcase lessons (§5):* also deliver (a) the **selector/target
  language grammar** (promoted from open questions — the showcases used it constantly
  and invented syntax ad hoc), and (b) the **denial/explain schema** (promoted — it is
  the operator UX, the deny-suite assertion language, the learning record, and the LSP
  diagnostic; design it with the descriptors, not after).

- **M-LIB — standard policy library (new; showcase lesson §5.1).** The user-facing
  surface is not the kernel but the combinators: `std.profiles.*` (tenant,
  pure_library, forensics/REL, marketplace, config_builder…) and the mediation
  vocabulary (`routes/allowHosts/uses/ttl/window/cosign/readOnly/redact/protocol/
  opaque.*`). Authored *in* the policy language once M3 exists; governed by its own
  policy (raw operators withheld); interceptors that close over capabilities need
  **certification criteria** (TCB-adjacent). Seed set specified at M2.5; grows with
  M5/M6.

- **M-SES — engine hardening.** Phases S1–S6 and the gate as specified in
  `HARDENING.md`. Does not block M2–M5 (trusted code); **gates M6/M7-with-tenants**;
  required even pure-AOT (hybrid fallback re-enters the interpreter). Start S1 early.

- **M3 — COMCON front-end.** Restricted-subset parser + capability declarations +
  compile-time rejection of out-of-environment references. Typed AST out, no codegen.

- **M4 — Type binding.** Bind the AST against the M2 schema → fully-typed IR; `any`
  forced to the hybrid path. Typed locals inferred; mismatches rejected.

- **M5 — maxim lowering (one handler).** Lower typed request-header handlers to
  unboxed C against the typed stubs (the M1 hand-written ABI). Deliverable: a
  compiler-produced `.so` for the M1 example; behavior byte-identical to the
  interpreted rule; the C ideally ≈ M1's hand-written C. *Scope note (showcase
  lesson §5.5):* lowering must **partially evaluate static mediations** — a
  constant-predicate membrane (e.g. an allowHosts prefix guard) becomes an inline
  check in the generated C, not a call. Scenario 43's "policy that vanished" is
  this feature; without it the compile-through performance story is untrue for any
  mediated capability.

- **M6 — Dispatch + AOT wiring.** The event dispatcher calls the compiled C function
  pointer (per-tenant `.so`, phase-35 precompile); interpreted fallback for the
  non-lowerable. End-to-end compiled policy serving in a worker. *Gated by M-SES.*
  The POM's class-F live-rewrite machinery (epoch broadcast + dirty→fallback→re-AOT)
  lands here, reusing `describe()` and the cfgbus transport.

- **M7 — Benchmark the real pipeline.** Compiled-by-COMCON policy vs interpreted vs
  stock, same method as M1. The number that confirms (or corrects) M1 at
  full-pipeline scale — expected between M1's 28% and 96% endpoints, near the top if
  the lowering is direct.

- **M8 — Safety hardening + audit. GATE for any multitenant use.** The compiler-
  faithfulness obligation, now precise (SEMANTICS §3 assumption F): *the lowered C
  must refine the mediated semantics along the provenance links — simulating every
  MEDIATE/NAME gate it erased.* Plus per-tenant `.so` isolation, table-key
  namespacing, worker resource guards, threat-model doc.

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
