# COMCON documentation, v3

*Supersedes `../docs/` (v2, 2026-07). v3 integrates the 2026-08-18 architecture
refinement: the possession kernel, the Program Object Model, the formal semantics with
the No-Amplification theorem, worked authority-traced examples, the live-mutation
safety classes, the M-SES hardening scope — and the first measured performance gate
(M1: hand-C policy at 96% of stock nginx vs 28% interpreted).*

## Reading order

| Doc | What it is |
|---|---|
| **FOUNDATION.md** | The architecture: thesis, principles, the POM, the 4-operator kernel, closure vs quotation, anchors & profiles, enforcement pipeline, run-time object model, multi-language, AI contracts, the full v2→v3 delta table, open questions. Start here. |
| **SEMANTICS.md** | Formal companion: domains, the authority measure, evaluation rules, the No-Amplification theorem + proof sketch, three worked examples with authority traces, and what the formalization itself discovered. |
| **POM.md** | The Program Object Model: node interface, COM→POM mirror table, R/L/F/X mutation safety classes, lifecycle, implementation-reuse plan. |
| **HARDENING.md** | M-SES: the engine-hardening milestone (S1–S6), the gate, scheduling. Unforgeability is the *enforcement mechanism* of the possession axiom, not hygiene. |
| **ROADMAP.md** | Milestones M1 ✅ …M9 (+M2.5, +M-SES) with the measured M1 numbers, delivery staging, and the v3 minimal first slice. |
| **PERFORMANCE.md** | Expected performance impact: the measured gradient endpoints (96% compiled vs 28% interpreted vs stock), the cost model per enforcement moment, v3-specific costs (live-rewrite windows, meets, hashes), risks and the M7 falsification plan. |
| **SHOWCASE.md** (1–7), **SHOWCASE17.md** (8–17), **SHOWCASE37.md** (18–37) | The look & feel scenarios, reworked for v3 (envs/grants, include = parse∘admit∘bind, anchors/queries, guarantees-as-theorems). Illustrative, not normative. |
| **SHOWCASE45.md** (38–45, new) | v3-native scenarios: intensional query hardening, closure vs quotation, pin-by-hash, live-rewrite epochs, the self-auditing plugin (base≡meta), compile-through ("the policy that vanished"), contract admission for AI code, meet-composition. |
| **MANUAL.md** (draft) | The user's manual, written as-if-shipped (working-backwards artifact): the five-minute mental model, quick start, tenant / policy-author / operations handbooks, performance guide, reference (verbs, denial anatomy, glossary) — and Appendix B, the [TBD] harvest of decisions it forced into the open (lessons folded into ROADMAP §6). |

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

Design documentation — nothing here is implemented except where marked (M1 spike
artifacts in `t_performance/maxim_m1/`). The showcase scenarios (45 total) are fully
reworked for v3 and **supersede** `../docs/SHOWCASE*.md`; their syntax remains
hypothetical by design. Design-session notes and the running decision log live in the
project memory branch (`comcon-architecture-refinement`).
