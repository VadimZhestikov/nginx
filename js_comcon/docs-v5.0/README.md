# COMCON documentation, v5.0

> **This set is the single normative spec** (user decision, 2026-08-23 — E6). It is
> revised **in place**: each revision adds a vN.M entry to FOUNDATION's delta log
> (current: **v5.124**, 2026-09-16.)
>
> **Where the work stands.** Every roadmap increment is done (A–E, live ops, the config
> instance), the confinement track is closed and the assurance case carries two signatures
> (ASSURANCE §15), the compiler track is closed at M5.1a with M5.1b parked on its numbers, and
> the mediation vocabulary is complete with `opaque.*` and the postures not built by decision.
> What is still open is named, not implied: F11's reproduction half (a signer running the pack
> themselves), the broadcast-fuzz flake (instrumented, its alert text now captured, awaiting a
> recurrence with the log tail), and the decisions listed at the top of ROADMAP's POSITION.
>
> **This file no longer repeats the history.** The delta log — every version from v5.1, each
> entry in place — is FOUNDATION's; the position and its log, newest first, are ROADMAP's;
> the evidence, the findings ledger and the signatures are ASSURANCE §15–§16; what a second
> signer does is REVIEW.md; the gate is `bash t/tools/gate.sh`.

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
| **SCHEMA.md** (new in v5.13) | C2 — the typed schema a policy is written against (input to C3's type-checker, C5's erasure oracle). The small real tenant environment (report/onRequest/Request/Response/granted Socket) with the V1 numeric discipline; machine form in `../schema/tenant-env.schema.json`, grounded by `t/comcon_schema_conformance.t`. The host COM surface (from `describe()`) is a later extension. |
| **PATTERN_config_language.md** (new 2026-09-03) | Why a config-DSL is a **userland pattern**, not a platform increment (the earlier "increment E / M-DSL" was retired on review): mint a language by granting a vocabulary + `realize`ing sentences; handle untrusted config via **propose-don't-hold** (confined sentence returns a cap-free description, a trusted host apply loop validates + applies). The one platform hook — sound declarative-profile review — folds into D5b. |
| **INCREMENT_MLIB.md** (new in v5.51) | The SCOPE + build log for M-LIB (the standard policy library): why the kernel being finished is not the same as usable, the one rule for a profile (only fields the kernel enforces), what step 1 shipped, and what is absent WITH ITS REASON — postures, `std.ops`, the host/ttl vocabulary, the governance half. Read before adding a combinator. |
| **INCREMENT_D.md** (new in v5.38) | The SCOPE + build log for increment D (POM nodes): the reflective Program Object Model over fragments — substrate/NodeView/selectors/quotations+splices/live-rewrite+epochs/call-site audit (D0–D5b-4 + D4c: **increment D is COMPLETE**, 2026-09-12). Coarse POM from bytecode (no parser); the full CST is D5b — which also enables sound declarative-profile review of config proposals (see `PATTERN_config_language.md`). |
| **INCREMENT_C.md** (new in v5.12) | The ground-truthed PLAN for increment C (the typed/compiled maxim tier): the reality check on the real maxim tree (same Bellard base ~4% delta, compiler in a separate `quickjs-jit.c`, type-inference + bytecode-hash already present, gcc+tcc available), the erasure-soundness invariant + differential-test discipline, and the risk-ordered C0 (integration spike/gate) → C1 M-UNIFY → C2–C7 sequencing. |
| **INCREMENT_A.md** (new in v5.5) | The first construction plan, ground-truthed against `nginx/src/js/`: what the reality check confirmed (single load point, favourable QuickJS compartment factoring, extensible describe registry), the real work in leverage order (owner-field the global handle registries; the four omnipotent members; two confinement bugs), and the A0–A4 task order with file:line anchors. |
| **DOCTRINE.md** (new 2026-09-03) | The security doctrine — the *why* beneath the threat model: detection is undecidable (Rice's theorem; Gödel/halting are the cousins), so COMCON **confines instead of chases** — turning the undecidable "is this code safe?" into the decidable "what authority does it hold / express?". Four pillars: confine the untrusted space (structural), harden the finite TCB (SR-2 compiler faithfulness, SR-3 pentest), meter what you can't decide (gas), name the residuals honestly. The finite-effort win. Read once, early. |
| **THREATS.md** (new in v5.4) | The threat model: 12 adversaries × assets × mitigations, every cell citing its closing mechanism; three residuals accepted by name (engine memory safety, IFC/side channels, availability-within-reach); the completeness ledger and the V15 assurance-case skeleton. Found TM-1 (denial-log quotas) and TM-2 (session identity → env mapping). |
| **ASSURANCE.md** (new in v5.64) | **The assurance case (V15 = gate SR-4).** The claim → assumption → evidence tree: G0 decomposed into 53 leaves over G1–G11, six named assumptions, a findings ledger of eleven, and the CONVERGENCE rename table. Every `EV:` names an artifact that must exist — `t/tools/check-assurance.py` (run by `t/comcon_assurance.t`) fails the suite otherwise, and also refuses orphan evidence, unowned gaps, uncovered adversaries, and any document citing a test file that is gone. **Built 2026-09-12, NOT SIGNED.** |
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

Design documentation, with **a substantial build shipped**. Increments A/B/C are done
(COMCON-lite core — compartment identity, deny-by-default environments, grants,
reach-cycle gates, denial log + audit/learn/enforce; the typed admission front-end;
the compiled tier C5–C7 with SR-2 faithfulness), the confined tier is adversarially
validated (SR-1/SR-2/SR-3 passed, M-SES-0/1/1b), and the **M-CFG operator kernel +
CONVERGENCE are complete (v5.29–v5.35)**: FOUNDATION §4's operators are realized as the
granted `comcon.{env,grant,mediate,bind,admit,include,mode}` names, and there is now
**one confined-fragment mechanism** — `comcon.include(...)` bound via `location.handler`,
on both the interpreted and AOT tiers. **The `js_tenant_*` directives and the tenant
compartment subsystem have been removed** (Principle 11); a reader should ignore the
older `js_tenant_*` / `onRequest` / `comcon_load` / `comconctl` surfaces in the
increment/manual docs — the live surface is `js_source root.js;` + the `comcon` operators
+ `location.handler` (`OPERATOR_API.md`, `INCREMENT_MCFG.md`, `INCREMENT_CONVERGE.md`).
Design-only edges remain: **POM nodes (increment D)** — parsed-subtree quotations,
structured splices, `query()`/anchor targeting, and live rewrite/epochs. (`includeAt` is
**not** a separate edge: a concrete-node `includeAt` is just `loc.handler=include(...)`, and
its only non-redundant forms — anchor-splice and query-targeting — are exactly this POM
targeting; there is nothing left to build *called* `includeAt`.) Also:
`mediate` `rateLimit`/`transform`/`audit` flavors, `admit`'s test-phase
under determinism caps, the full M2 typed schema, WASM ingestion, and adaptive profiles
(M9). Tests `t/comcon_*` (interpreter default `objs`; JIT `objs_jit`). The M1 spike lives
in `t_performance/maxim_m1/`. The showcase scenarios remain illustrative/hypothetical by
design. Running decision log = project memory `comcon-architecture-refinement`.
