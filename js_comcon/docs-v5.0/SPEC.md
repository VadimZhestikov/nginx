# COMCON — The Specification

*The clean normative read of the design: current truth, stated once, no revision
archaeology. The rationale for every decision, and the sequence of reviews that produced
it, lives in the companion documents (`FOUNDATION.md` for the argued form, `SEMANTICS.md`
for the formal kernel, `POM.md`, `HARDENING.md`, `VERIFICATION.md`, `THREATS.md`,
`PERFORMANCE.md`, `ROADMAP.md`) and in the design-record appendix (§14). Where this
document and a companion disagree, this document is normative for **what**; the companion
is authoritative for **why**. Implementation status: §13 — the COMCON-lite core
(increment A through the request path) is built and tested; the rest is design.*

---

## 1. What COMCON is, in one screen

COMCON runs code from parties that do not trust each other — tenants, sub-tenants,
vendors, AI generators — inside one server, each fragment confined by a **policy** the
host attaches. It is a capability-secure system with a tiny logical core:

- **One axiom.** *No operation mints authority.* Capabilities are unforgeable; the only
  way to hold one is to be granted it; the only guarded operation is granting, and you
  may grant only what you hold. Everything else is free. (Monotonicity follows *by
  construction*, not by a runtime check.)
- **Two resource kinds.** Host **capabilities** (COM nodes, tables, sockets, clock, RNG,
  I/O…) and **POM handles** (subtrees of the program tree). Both unforgeable.
- **Four operators.** `grant`, `mediate`, `bind`, `admit` (§4).
- **One tree.** Every governed artifact — a program, a config, a validation pattern, a
  foreign module — is an instance of one pattern: (grammar, tree, schema, lowering),
  governed at two hinges (admission: surface→tree; operation: tree mutation).

Everything else in this document is a consequence, a representation, or an engineering
concretion of those four bullets.

The payoff is a performance **gradient**, measured at its endpoints: the same policy runs
at ~28% of stock nginx interpreted and ~96% compiled to C — and the confinement is
*identical* on both, because soundness is stage-independent (staging changes cost, never
what is allowed).

---

## 2. Vocabulary (normative terms — used consistently everywhere)

- **fragment** = **node**: a governed subtree of a program/config tree. (The two words
  are synonyms; "fragment" is the security framing, "node" the tree framing.)
- **instance**: the *language* a node is written in — JavaScript, config, `pattern`,
  WASM. One pattern, N instances.
- **capability**: an unforgeable value authorizing a fixed operation-set on a resource.
- **facet**: a capability wrapping narrower behavior around authority it hides (the
  library "transaction" pattern; a `mediate` result whose interceptor closes over caps
  the receiver never holds).
- **environment**: the finite map `name → capability` a node may use — its *entire*
  world. Its typed shape (the names + types it declares/requires, without the caps) is
  the **environment signature**, a.k.a. the fragment's **free-name manifest**. (These
  are one concept; the older term *import_list* is retired.)
- **quotation**: an unbound node — a *description* of code/policy carrying zero
  authority.
- **closure** (of a policy value): a policy that *carries* capabilities, bounded by its
  producer. Contrast a quotation, which describes and is bounded by its realizer.
- **stone**: a deep-frozen plain-data value (primitives + frozen records/arrays; no
  getters, no proxies, no mutability), safe to share and to splice.
- **anchor**: an inert source marker naming a policy attachment site.
- **pin**: a content hash a policy is locked to.
- **epoch**: one version of a node's *binding* — the rollback/rewrite unit.
- **generation**: a per-fragment counter tracking *baked-in authority* invalidation
  (distinct from epoch; §7).
- **profile**: **restrictive** (a policy that only denies/attenuates) or **adaptive**
  (a policy that transforms behavior). *The shipped core is restrictive-only.*
- **contract**: (environment signature, natural-language spec, tests) — the admission
  gate for a fragment.
- **tier**: T1 (interpreted bytecode) or T2 (compiled C); a fragment may be hybrid.

---

## 3. The three axes (do not conflate them)

Three orthogonal classifications, each on a different object:

| Axis | On what | Values |
|---|---|---|
| **profile** | a *policy* | restrictive / adaptive |
| **mutation safety class** | an *operation on the POM* | R (read) / L (local-init) / F (fan-out-required) / X (irreversible-guarded) |
| **tier** | a *fragment's compiled state* | T1 / T2 / hybrid |

A restrictive policy, an F-class rewrite, and a T2 fragment are three unrelated
properties of three different things.

---

## 4. The kernel

Two unforgeable resource kinds (§1). POM access is a *capability kind*, not an operator:
the host holds the root POM handle and distributes attenuations via `grant`/`mediate`.

1. **`grant(env, name, cap)`** — place a *held* capability into an environment under a
   name. The only guarded operation — and even its guard is not a runtime check: any
   value passable to `grant` was necessarily evaluated from the caller's own
   environment. Possession is a metatheorem of evaluation plus unforgeability.
2. **`mediate(cap, interceptor) → cap′`** — a membrane over every operation of the
   capability: `deny` / `attenuate` / `transform` / `meter` / `revoke` / `redact`
   (redaction of POM handles — interfaces visible, bodies hidden — is the
   source-visibility control). An interceptor that closes over other caps yields a
   **facet**. `A(cap′) ⊆ A(cap) ∪ A*(interceptor)` — always within the creator's
   authority.
3. **`bind(env, node)`** — attach a *frozen* environment to a node. Inside it, names
   resolve *only* through the environment: no ambient globals, no free imports, no
   ambient `eval`. If the node is already bound, the new binding **meets** the old
   (intersection of authority; associative, commutative, idempotent). Consequences:
   plain `bind` can only *narrow* — even a self-rebind; *widening* is a distinguished
   **administrative rebind** (a new epoch) requiring an `admin`-op handle. Failure modes
   compose by strictness (`reject > deny > attenuate > audit`).
4. **`admit(node, contract) → node | reject`** — the gate: typecheck against the
   contract's schema; syntactic predicates (`no reflect`, grammar subset); the
   contract's tests run *inside the sandbox with clock/RNG/I/O denied* (reproducible;
   zero blast radius). Instance-generic: the grammar and schema are supplied per
   instance, so the one rule gates JS, config, patterns, and WASM.

**Free intrinsic constructors** (not operators, not caps, not library — the same class):
`env()` (empty environment), `quote` (a tagged template producing an unbound node),
`stone()` (deep-freeze plain data). `stone` is a kernel intrinsic because the `quote`
rule depends on it (§6).

**Derived, not primitive**: `attenuate` / `revoke` / `meter` / `audit` = `mediate`
flavors; `include(src, policy, contract)` = `parse ∘ admit ∘ bind`; `realize` (§6).
Richness lives in **libraries**, themselves governed (bound under environments that
withhold the raw operators, exporting only vetted combinators).

**The No-Amplification theorem** (proved in `SEMANTICS.md`): every evaluation produces
only authority its evaluator held; host effects are confined to `A*(ρ)`; the binding
store is modified only at handle-covered nodes and only downward; a fragment cannot
alter any outer policy. Holds *given* an unforgeable TCB — which is the M-SES milestone,
not an assumption to wish away.

---

## 5. The Program Object Model (POM)

The tree the kernel operates on. Source ↔ concrete syntax tree is 1:1; AST → bytecode →
C → `.so` are projections *with provenance* (policy rides the provenance links; below
source level, enforcement = compiler faithfulness, the M8 obligation).

A program never touches "the POM" — only a **handle-scoped view** (`reach(h)` bounds
visibility, `ops(h)` bounds capability). The node view exposes: `kind` · `id` · `hash` ·
`span` · `parent`/`children`/`anchors` · reads (`text`/`quote`/`query`) that **return
quotations** (cap-free by the same rule as `quote`) · `describe()` (per-kind op registry
+ safety class; the *describe ⊇ mutable* invariant holds) · `binding` (redacted by
default) · `lowered` (provenance + dirty flag) · mutations (`replace`/`insert*`/`remove`/
`revive`, admitted quotations only, classified) · `bind`/`rebind`.

**Identity**: `id` is **creation-ordered** (a monotonic id assigned once at first
admission from a *persisted counter, recorded in the canonical config tree*), never
positional, loaded across restart/reload — never re-derived. `hash` is content-stable.
Policies *target* by id/selector, *pin* by hash.

**Two lifecycle invariants**: executing an unbound node is an explicit error (never an
implicit ∅-run); a bound node is **never unbound** (a pin/hash mismatch refuses the *new
epoch* while the old one keeps serving). Between them, no reachable state runs
formerly-governed code ungoverned.

**Mutation safety classes**: **R** read (always safe) · **L** local/init (stage-0 /
pre-fork / not-yet-bound) · **F** fan-out-required (live rewrite in a multi-worker
server: broadcast → per-worker dirty → bytecode fallback → re-AOT → coherent epoch
switch; never stop-the-world) · **X** irreversible/guarded (snapshot-first / confirm /
reject).

**Born-bound**: query-bound policies are *live* — match-sets are recomputed at every
admission within the covered reach, so newly admitted matching code enters already
governed. Selector v1 is five registered deterministic combinators (`module`,
`callsites`, `exports`, `anchors`, `within`); the general query grammar is a later
upgrade.

---

## 6. Policy values: closure vs quotation

A policy is a first-class value in exactly two forms, distinguished by one
machine-checkable bit:

- **Closure** carries capabilities → bounded by its producer (delegation; intersection).
- **Quotation** describes a policy → inert, zero authority → bounded by its **realizer**
  (specification). `quote` accepts only **stone** splices (a structural "no caps now"
  check is TOCTOU-unsound against getters/mutation); splices enter at *data positions*,
  never as text, so quotations are injection-immune. Opaque values carry authority and
  are therefore unspliceable — secrets cannot leave through quotations or proposals.

There is **no policy language other than policy-JS plus the governed library**. A
quotation quotes policy-JS; "declarative" is a `syntax_allowed` profile of it; the
diffable descriptor table is its admission-time *normal form*. `realize(q)` is the kernel
verbatim: `admit(q, K)` (contract *mandatory*), then `bind(ρ_R ↾ q.manifest, q)` — the
realizer's grants **restricted to the quotation's free-name manifest** (least-authority
realization; a proposal cannot trojan the realizer's session), then execute. Two-phase
binding: `${…}` splices are early-bound producer data (stone); free names are late-bound
*mentions* resolved under the realizer.

---

## 7. Enforcement: how, when, and the two clocks

Policy is attached by **query** (govern code you cannot edit) or by inert **anchor**
(authored code); the target program always remains valid, runnable JavaScript.
Enforcement resolves at the earliest stage that can decide it — the **four moments**:
static (name resolution + typing) · admission (contract) · residual runtime (membranes /
hybrid) · compile-through (typed static policy → C linking only allowed stubs, capability
constants baked in, *no runtime policy interpreter*; static mediations partially
evaluated to inline checks).

**Extend by granting, never by syntax.** Every COMCON operation is a granted name — `pom`
is a capability object, the kernel operators are environment values, `quote` is a tagged
template — JavaScript gains vocabulary, never grammar. So the NAME rule is the entire
access-control story; withholding a name *is* the gate.

**The typed profile** is a *discipline over* policy-JS, not a language: types arrive from
the schema (the API), inference covers locals (the profile is a small Misty-like core —
no `this`/classes/coercion), and the residue is written as **erasure-sound JSDoc-style
comment annotations**. Numbers are IEEE doubles in both tiers (JS semantics normative);
the typed integer is `int` (a safe-integer refinement, |x| ≤ 2⁵³−1); `i32`/`u32` via the
asm.js coercion idiom (`x|0`, `x>>>0`) are erasure-sound by construction and unbox for
free. No host op may expose a numeric domain past the safe range (ms not ns timestamps;
strings/opaque for 64-bit ids).

**The two clocks** (distinct, co-triggering re-compilation): **epoch** = a binding
version (changes when a node's policy/content changes; the rollback unit). **generation**
= a per-fragment invalidation counter for *baked-in authority* — bumped when a capability
the fragment compiled in is revoked, even if the fragment's binding never changed (a
facet derived from a now-revoked library cap). Revocation fans out at revoke-time through
the provenance/grant-chain registry; the hot path stays one load + one branch at entry.

**Adaptive is deferred.** The shipped core is restrictive-only, so composition is
unconditionally ACI (meet) throughout. Adaptive profiles (transforms; mirror-as-policy)
arrive at M9 as an isolated addition, with the already-specified one-per-node rule
(`E_BIND_ADAPTIVE_CONFLICT`) then active.

---

## 8. The fragment artifact and the two tiers

Not two sequential stages — **two permanent tiers**. Bytecode is load-bearing forever
(the F-class fallback runs it; the `any` residue executes on it; admission and
`comconctl dev` run interpreted; maxim-less deployments are the correct-but-28% tier).
maxim's IR *is* QuickJS bytecode, so the architecture is **"fat bytecode"**:

```
typed policy-JS ──M3/M4──▶ FRAGMENT ARTIFACT ──(iff maxim)──▶ C → .so (phase-34 hybrid:
   = bytecode (T1 executable, by type ERASURE)                {C fn, bytecode}; prefer C)
   + type/capability side-table
   + environment signature (= the free-name manifest)
   + content hash + schema hash + admission certificate
```

**Erasure soundness**: a typed program run interpreted with types ignored behaves
identically to compiled — types only *reject* (at admission) and *accelerate* (at T2).
So M8's obligation is "T2 refines T1", checkable per fragment by differential testing.
The artifact may be content-addressed by `H(source ∥ schema-version)`, folding pin-check
and schema-check into one identity match; either way it verifies **both** content drift
(pin) and schema drift (the schema hash) at every load, per instance.

Compiled-tier safety costs the 96% ceiling did not include: the generation check at
entry, back-edge gas in compiled loops (**on all C entering the funnel — maxim- and
wasm2c-emitted**), and write guards where a lower tier stores into a declared-typed
shared slot (the type boundary — `Number.isSafeInteger` for `int` slots). Low
single-digit %; M7 measures.

---

## 9. WASM: the border crossing, never the interior

**Substrate follows provenance, not language.** Our-born JS runs T1/T2 (the
type-checker + compiler is the trust root); JS→WASM is a category error (a second
sandbox around an already-safe language). Foreign-born code enters as WASM through the
`wasm` facet, where the mapping is exact: **validation = `admit`, the import object = an
environment, fuel = a budget mediation** — a module has no ambient authority and is
*born-bound by construction*, so the substrate enforces the possession axiom for free.
Two lanes: an embedded runtime for cold modules; **wasm2c ingestion** for hot ones
(WASM → C with SFI checks preserved, into the same funnel, gas/revocation included;
wasm2c joins the TCB). One export lane: maxim → WASM carries an admitted fragment onto
foreign hosts, admission guarantees travelling while authority discipline degrades to
the foreign ABI (reported). WASM's built-in meterability/revocability are exactly what
R3/R4 engineered into the native tier — "spent for you."

---

## 10. Administration, config, and the enumerations

**There is no management plane.** `comconctl` is a shell: every verb is a `std.ops`
library program run as an admitted stage-0 episode in an operator session; "administrative"
is a property of the session's *environment*, never of a tool. Administration inherits
every mechanism (audited like tenants; office-hours/cosign as mediations). A session's
identity → environment mapping is a host-integration deliverable (must exist before the
first operator session).

**Config is an instance.** A config fragment is a sentence of a restricted config
grammar, admitted like code (grammar + the dual-role schema's types + tests + pin +
audit-first rollout); JSON snapshots are quotations of config subtrees; the
propose→realize flow is closure-vs-quotation for the config instance. *Data is code
bound to the empty environment* — one tree, an environment-richness gradient, not a
data/code dichotomy.

**The closed enumerations** (all *generated*, never maintained; completeness is what
makes safety claims checkable): per-instance grammar enumerations (JS p_symbols, config
productions, the WASM validated format), the global **compile portals** list, and the
global **ops-resource capabilities** (denial log, binding/epoch store, provenance
registry, class-F broadcast channel, snapshot store, signing key, learning-recorder
switch).

---

## 11. The layered core (COMCON is not monolithic)

The design is strata, each usable without the ones above it:

| Layer | Adds | Needs |
|---|---|---|
| **COMCON-lite** | deny-by-default environments, registry allow/deny, denial log, audit→enforce | S1+S2 only — **no compiler, no tiers, no quotations, no adaptive, no POM-rewrite, no config-instance** |
| **+ onboarding** | learning-mode harvest, generated docs, dependency workflow | — |
| **+ typed/compiled** | the typed profile, the fragment artifact, T1/T2 | M-UNIFY + the front-end/binder/lowering |
| **+ live ops** | queries, POM rewrite, epochs | — |
| **+ config instance** | M-CFG (parallel; needs none of the above) | — |
| **+ adaptive** | transforms, mirror-as-policy | (M9) |

The whole first row — multi-tenant confinement — is the highest-demand capability and
needs no compiler at all. **Compatibility principle**: pilgrim without COMCON stays
fully supported; COMCON attaches per-fragment; there is no flag-day.

---

## 12. What is assumed, deferred, or open (the honest edges)

- **Assumed (and therefore a milestone, not a hope)**: TCB unforgeability (M-SES);
  compiler faithfulness (M8). Named residuals accepted rather than closed (`THREATS.md`):
  engine memory safety, information-flow/side channels between co-resident tenants (the
  post-M9 IFC track), availability-within-reach for controllers.
- **Deferred, not dropped**: adaptive profiles (M9); opaque values + COW domains (the
  engine-substrate track, gated on the first-slice COW/IC microbenchmark); IFC/taint
  (post-M9).
- **Open**: the nginx integration contract specifics (§9.4 — the increment-A reality
  check); default-root out-of-box contents; budget-unit semantics; the grammar-version
  compatibility window; cluster-edge (cross-process) policing.

---

## 13. Status

Design-complete after five review passes (correctness R, verifiability V, engineering E,
consistency C, threat model); the M1 perf spike measured the 28%/96% endpoints
(`t_performance/maxim_m1/`). **Construction has begun and the COMCON-lite core is real**
(`src/js/ngx_js_compartment.*` + integration; build log in `INCREMENT_A.md` §6): the
compartment identity, the deny-by-default tenant environment, the host→tenant grant
primitive, the reach-cycle registry gates, and a **confined tenant serving live HTTP
requests** (headers as data both ways) — each behind a `t/comcon_*` test, and
**accepted end-to-end by a dogfood demo** (`js_com_demos/COMCON_dogfood/`: a caged
mirror tenant on real multi-worker traffic). **Increment A is complete.** Everything else in this SPEC (typed profile, tiers, quotations, POM
rewrite, config instance, adaptive) remains design. Delivery follows the increment
re-cut (`ROADMAP.md` §13), confinement-first. This SPEC is normative for *what*; the
design record (§14) holds *why* and *how we got here*.

---

## 14. Design-record appendix

The versioned, annotated companions are the design record — read them for rationale,
worked examples, proofs, and the review history:

- `FOUNDATION.md` — the argued architecture + the full vN.M delta log.
- `SEMANTICS.md` — the formal kernel, the No-Amplification theorem + proof sketch, the
  authority-traced worked examples.
- `POM.md` — the node interface, the COM→POM mirror, the mutation classes, lifecycle.
- `HARDENING.md` — M-SES (S1–S6), the escape-probe gate.
- `PERFORMANCE.md` — the cost model, the measured endpoints, the lane costs.
- `VERIFICATION.md` — the V1–V15 verification track.
- `THREATS.md` — the adversary × asset × mitigation completeness ledger.
- `ROADMAP.md` — milestones, the increment re-cut, the review-finding tables (R/E/C).
- `SHOWCASE*.md` — 50 look-and-feel scenarios.
- `MANUAL.md` — the working-backwards user's manual.

Review-finding indices (all in `ROADMAP.md` unless noted): **R1–R12** §11 (correctness),
**V1–V15** §12 + `VERIFICATION.md` (verifiability), **E1–E12** §13 (engineering),
**C1–C13** the v5.3 delta (consistency), **TM-1/TM-2** `THREATS.md` (threat model).
