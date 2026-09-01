# COMCON documentation, v5.0

> **This set is the single normative spec** (user decision, 2026-08-23 — E6). It is
> revised **in place**: each revision adds a vN.M entry to FOUNDATION's delta log
> (current: **v5.12**, increment C PLAN (INCREMENT_C.md, spike-first C0–C7) +
> the security-review cadence in VERIFICATION.md (SR-1..SR-4 at inflection points); v5.11 INCREMENT B COMPLETE — the dependency workflow (js_tenant_dependency
> pin-by-hash + pure_library cages); v5.10 B1 generated grant-stub; v5.9 B0 learning mode; v5.8 INCREMENT A COMPLETE — A3.1 + dogfood; v5.7 A4 denial log + audit→enforce; v5.6, the COMCON-lite core BUILT + TESTED through the
> request path; build log INCREMENT_A.md §6, status SPEC.md §13; before it
> v5.5, SPEC.md + the ground-truthed INCREMENT_A.md build plan from the
> nginx reality check; before it v5.4 convergence actions — THREATS.md, two-clocks pin,
> adaptive deferred to M9; v5.3 consistency pass, v5.2 WASM ruling, v5.1 engineering
> review + increment re-cut). Full new directories happen only at genuine architectural
> reframes. Older sets (`docs-v0/`, `docs-v3/`, `docs-v4/`) are history.

*Supersedes `../docs-v3/` (and `../docs-v0/` v2). v3 integrated the 2026-08-18
architecture refinement: the possession kernel, the Program Object Model, the formal
semantics with the No-Amplification theorem, worked authority-traced examples, the
live-mutation safety classes, the M-SES hardening scope — and the first measured
performance gate (M1: hand-C policy at 96% of stock nginx vs 28% interpreted).
**v4 adds the symmetry correction** (rev 3.1, user-spotted): one governed-language
pattern with N instances instead of two mirrored trees; the empty-environment principle
("data is code bound to ∅"); COM gains its missing admission hinge — a typed,
admissible config surface (new work item M-CFG, new scenarios 46–47, new manual §3.6).
**v4.1 (in place) adds the comconctl closure**: there is no management plane —
administration is admitted episodes, tool verbs are `std.ops` library programs, and the
ops-resource capabilities become the third closed enumeration (FOUNDATION §8a,
Principle 10). **v5.0 is the pre-implementation design-review hardening** — twelve
adversarial findings (R1–R12) adopted in whole: stone-based quotation safety,
one-adaptive-per-node composition, revocation/gas/type-boundary fixes for the compiled
tier, least-authority realization, never-unbound nodes, creation-ordered ids, the
born-bound rule, monotone rollout, and the admission front-end named as attack surface
(ROADMAP §11 for the full table).*

## Reading order

| Doc | What it is |
|---|---|
| **SPEC.md** *(the normative read — start here)* | The whole design stated once, cleanly, no revision archaeology: the one axiom, four operators, the tree, the vocabulary, the three axes, tiers/artifact, WASM, administration, the layered core, the honest edges. Normative for *what*; the rest is *why*. |
| **FOUNDATION.md** | The **argued** architecture (rationale, the full vN.M delta log): thesis, principles, the POM, the 4-operator kernel, closure vs quotation, anchors & profiles, enforcement pipeline, run-time object model, multi-language, AI contracts, open questions. Read for *why*. |
| **SEMANTICS.md** | Formal companion: domains, the authority measure, evaluation rules, the No-Amplification theorem + proof sketch, three worked examples with authority traces, and what the formalization itself discovered. |
| **POM.md** | The Program Object Model: node interface, COM→POM mirror table, R/L/F/X mutation safety classes, lifecycle, implementation-reuse plan. |
| **HARDENING.md** | M-SES: the engine-hardening milestone (S1–S6), the gate, scheduling. Unforgeability is the *enforcement mechanism* of the possession axiom, not hygiene. |
| **ROADMAP.md** | Milestones M1 ✅ …M9 (+M2.5, +M-SES) with the measured M1 numbers, delivery staging, and the v3 minimal first slice. |
| **PERFORMANCE.md** | Expected performance impact: the measured gradient endpoints (96% compiled vs 28% interpreted vs stock), the cost model per enforcement moment, v3-specific costs (live-rewrite windows, meets, hashes), risks and the M7 falsification plan. |
| **SHOWCASE.md** (1–7), **SHOWCASE17.md** (8–17), **SHOWCASE37.md** (18–37) | The look & feel scenarios, reworked for v3 (envs/grants, include = parse∘admit∘bind, anchors/queries, guarantees-as-theorems). Illustrative, not normative. |
| **SHOWCASE45.md** (38–45, new) | v3-native scenarios: intensional query hardening, closure vs quotation, pin-by-hash, live-rewrite epochs, the self-auditing plugin (base≡meta), compile-through ("the policy that vanished"), contract admission for AI code, meet-composition. |
| **SHOWCASE47.md** (46–47, new in v4) | The config-instance scenarios: typed tenant config (grammar+types cage over config sentences) and propose-the-config-you-can't-apply (quotation proposals realized by operators). |
| **SHOWCASE49.md** (48–49, new in v4.2) | The typed-profile scenarios: what the 96% tier looks like to write (schema + inference + one JSDoc annotation; type errors as denials; erasure = runs anywhere) and the per-fragment `any` gradient (hybrid tier as a report, not a punishment). |
| **SHOWCASE50.md** (50, new in v5.2) | The border crossing: a foreign (Rust-built) WASM module admitted through the `wasm` facet — validation = admit, imports = grants, fuel = budgets; cold lane (embedded runtime) vs hot lane (wasm2c into the one C funnel); and why our own JS never crosses out (JS→WASM = category error). |
| **INCREMENT_C.md** (new in v5.12) | The ground-truthed PLAN for increment C (the typed/compiled maxim tier): the reality check on the real maxim tree (same Bellard base ~4% delta, compiler in a separate `quickjs-jit.c`, type-inference + bytecode-hash already present, gcc+tcc available), the erasure-soundness invariant + differential-test discipline, and the risk-ordered C0 (integration spike/gate) → C1 M-UNIFY → C2–C7 sequencing. |
| **INCREMENT_A.md** (new in v5.5) | The first construction plan, ground-truthed against `nginx/src/js/`: what the reality check confirmed (single load point, favourable QuickJS compartment factoring, extensible describe registry), the real work in leverage order (owner-field the global handle registries; the four omnipotent members; two confinement bugs), and the A0–A4 task order with file:line anchors. |
| **THREATS.md** (new in v5.4) | The threat model: 12 adversaries × assets × mitigations, every cell citing its closing mechanism; three residuals accepted by name (engine memory safety, IFC/side channels, availability-within-reach); the completeness ledger and the V15 assurance-case skeleton. Found TM-1 (denial-log quotas) and TM-2 (session identity → env mapping). |
| **VERIFICATION.md** (new in v5.0) | The verification track V1–V15: what would convince a skeptic of each claim — the numeric-model and schema-pinning decisions (V1/V2), executable reference semantics, monotonicity-as-assertion, translation validation, generated enumerations, schema conformance tests, the TLA+ epoch model, policy mutation testing, reproducible builds, and the assurance case. |
| **MANUAL.md** (draft) | The user's manual, written as-if-shipped (working-backwards artifact): the five-minute mental model, quick start, tenant / policy-author / operations handbooks (v4: §3.6 writing config, proposals), performance guide, reference (verbs, denial anatomy, glossary) — and Appendix B, the [TBD] harvest of decisions it forced into the open. |

## One paragraph

COMCON runs JavaScript from parties that do not trust each other — tenants, vendors,
AI generators — inside one server, each fragment caged by a policy the host attaches.
v3's form: one **possession axiom** (no operation mints authority), **four operators**
(grant / mediate / bind / admit) over two unforgeable resource kinds (host capabilities
and program-tree handles), policies as first-class values in exactly two modes
(**closure** = carries authority, bounded by its producer; **quotation** = describes
it, bounded by its realizer), attached by query or inert anchor so the target program
always remains plain runnable JavaScript — and the whole discipline **compiles away**:
a fully-typed static policy lowers (via maxim) to C that runs at ~96% of stock nginx
with no runtime policy interpreter, while remaining exactly as confined as the
interpreted form.

## Status

Design documentation, with **construction underway**: the COMCON-lite core is built and
tested — compartment identity, deny-by-default tenant environments, grants, the
reach-cycle gates, a confined tenant serving live requests, and the denial log with the
audit→enforce loop (`src/js/ngx_js_compartment.*` + integration; tests `t/comcon_*`;
build log `INCREMENT_A.md` §6; status `SPEC.md` §13). The M1 spike artifacts live in
`t_performance/maxim_m1/`. Everything not in the build log remains design; the showcase
scenarios (50 total) supersede `../docs-v0/SHOWCASE*.md` and their syntax remains
hypothetical by design. Design-session notes and the running decision log live in the
project memory branch (`comcon-architecture-refinement`).
