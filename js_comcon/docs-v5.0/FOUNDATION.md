# COMCON — Foundation & Architecture (v5.0)

*Supersedes `docs-v0/FOUNDATION.md` (v2, 2026-07-06/07). v3 integrates the architecture-
refinement work of 2026-08-18: the possession kernel, the Program Object Model, the
closure/quotation distinction, the formal semantics (see `SEMANTICS.md`), the live-
mutation safety classes (see `POM.md`), the hardening scope (see `HARDENING.md`), and
the first measured performance gate (M1, see `ROADMAP.md`). Material from v2 that
remains load-bearing is carried forward here in condensed form; §12 lists every
terminology change.*

*v4 adds the **symmetry correction** (rev 3.1, 2026-08-20, user-spotted): COM and POM
are no longer described as two mirrored special trees but as two instances of **one
governed-language pattern** (§2a); the data/algorithm dichotomy dissolves into the
**empty-environment principle**; and COM gains the admission hinge it was missing —
a typed, admissible config surface (work item M-CFG). Nothing in the kernel changes.*

---

## 0. One-sentence thesis

> **COMCON is a capability-controlled reflective tower: applications, policies, and the
> compiler all operate on one Program Object Model (POM) under one possession rule, with
> every enforcement pushed to the earliest stage that can decide it — down to policy
> compiled away entirely into typed C.**

The v2 thesis ("a policy is a program driving a low-level engine API, and every inner
policy is strictly no more permissive than its outer one") survives intact — v3 grounds
it: the "low-level engine API" shrank to **four operators**, the narrowing lattice became
a **theorem with a proof sketch**, and the program tree the policies operate on became a
first-class, capability-gated object (the POM).

v4 sharpens the house analogy into the general statement it was pointing at: COM and
POM are **two instances of one governed-language pattern** (§2a) — every governed
artifact is (grammar, tree, schema, lowering) with the same two hinges, and "data vs
algorithm" is not a dichotomy but a position on the environment-richness gradient:
**data is code bound to the empty environment.**

---

## 1. Primary target & motivation *(carried from v2, updated)*

**Primary deployment:** the nginx-family projects — js_com / pilgrim / mirror — where
JavaScript from **third parties that do not trust each other** (tenants, sub-tenants,
vendors, AI generators) runs inside one server. COMCON provides mutual protection
between tenants and between tenants and the host.

**Generality requirement (unchanged):** nothing in the core may be nginx-specific.
nginx specificity lives in policy libraries and the host integration layer, never in the
engine mechanism.

**Compatibility principle *(v5.1 — E12)*:** *pilgrim without COMCON remains fully
supported; COMCON attaches per-fragment; there is no flag-day.* Existing `js_source`
deployments never break; adoption is one fragment at a time (external binding makes
even that possible on code nobody edits). Delivery follows the increment re-cut
(ROADMAP §13): confinement first — the interpreted-tier multi-tenant cage ships and
pays rent before any compiler exists.

**What is new since v2 — the payoff is now measured.** The M1 perf spike (2026-08-16)
established the endpoints of the performance gradient on real hardware: a policy
hand-lowered to C (what a compiled typed COMCON policy becomes) runs at **96% of stock
nginx**, while the same policy interpreted runs at **28%** — and a directive-expressible
control shows the entire gap is the interpreter. The "restriction is optimization fuel"
argument of v2 §8.2 is no longer only argued. (Numbers and method: `ROADMAP.md` §M1.)

---

## 2. First principles *(v2 §2, tightened)*

1. **Trust is layered and directional.** Authority flows downward as explicitly granted
   capabilities, never assumed upward (POLA).
2. **Protection lives inside the language.** The compiler and engine are the enforcers.
3. **Make wrong results impossible, not discouraged.** Remove the capability.
4. **Protection is mutual and symmetric.** Exporter and importer both declare policy.
5. **Restriction is monotonic — a narrowing lattice.** v3 upgrade: this is now the
   **No-Amplification theorem** (SEMANTICS.md §3), a consequence of one axiom rather
   than an invariant to be policed.
6. **Policies are programs (reflexive).** A policy runs under its own governing policy.
7. **Tame Turing-completeness.** Provably-safe sublanguages; the safest profiles compile
   to typed C (maxim).
8. *(new in v3)* **Soundness is stage-independent; staging is purely a performance
   property.** Moving a check earlier never changes *what* is allowed, only what it
   costs. The security argument and the performance gradient are formally decoupled.
9. *(new in v4)* **Data is code bound to the empty environment.** There is one kind of
   governed tree, not two; a JSON snapshot is a node bound to ∅, a full program is a
   node bound to a rich environment, and everything (config, patterns, typed policy)
   sits between. Monotonicity, meet, quotations, and epochs apply uniformly across the
   gradient.
   *Caveat — this does NOT say data is harmless.* It says data's danger is exactly the
   danger of authority-free code, which leaves three residual channels, each governed
   by its own mechanism, none by the theorem: **(a) cost** — ∅-bound evaluation can
   still burn CPU/stack/memory (billion-laughs nesting, runaway generators) → budgets
   meter every episode including admission, and bounded-by-construction grammars
   (patterns) remove runaway classes outright; *the theorem answers "what can it
   touch?", budgets answer "how long can it try?"*; **(b) persuasion** — data acts
   through its *consumer's* environment (SQL injection, Log4Shell — a log string
   implicitly bound to an interpreter holding network authority; `__proto__` keys) →
   admit data before any authority-holding interpreter touches it, and keep the
   consumer's own cage narrow; the principle is the *diagnosis* of this attack family:
   there is no passively-safe data, only data whose eventual binding you did or didn't
   control — COMCON makes that binding explicit, checked, and ∅ by default;
   **(c) semantically hostile values** — a perfectly-typed `listen 22` is wrong, not
   unauthorized → admission types + the quotation/realizer split (a proposal can be a
   lie; it cannot be a self-executing lie). Covert *content* (stego in output by a
   fragment that legitimately read something) remains the named IFC gap (§13.4).
10. *(new in v4.1)* **There is no management plane.** Administration is not a
   privileged API: every operator action is an admitted stage-0 episode in a session,
   and every tool verb is a library program over the kernel plus granted capabilities.
   "Administrative" is a property of a session's *environment*, never of a tool (§8a).
11. *(new in v4.2)* **Extend by granting, never by syntax.** Every COMCON operation
   arrives as a granted name — `pom` is a capability object, the kernel operators are
   environment values, `quote` is a plain tagged template — JavaScript gains
   vocabulary, never grammar. Load-bearing three ways: external hardening must govern
   code we cannot edit (no keywords needed in it); withholding a name *is* the access
   gate (the NAME rule stays the entire story); and the POM's 1:1 source mapping plus
   all JS tooling survive. Corollary: the typed profile is a *discipline over*
   policy-JS (schema + inference + erasure-sound comment annotations), never a
   separate language (ROADMAP §M3).

**The axiom under all of it (v3):**

> **Possession — no operation mints authority.** Capabilities are unforgeable values;
> the only way to have one is to be granted it; the only guarded operation is granting
> (you must hold what you grant). Everything else is free. Corollaries: a fragment can
> always self-restrict (weakening needs no permission) and can never alter an outer
> policy (its capabilities were never granted inward).

---

## 2a. One pattern, N instances — and the config instance *(new in v4)*

v3 still described two special trees ("COM : config :: POM : program, same design,
second instance"). v4 recognizes the minimal model underneath:

> **A governed artifact is a language instance:** (grammar, tree, type schema,
> lowering) — governed at exactly two hinges. The **admission hinge** (surface → tree):
> grammar restriction + typing + contract tests = `admit`. The **operation hinge**
> (tree mutations at runtime): capabilities + safety classes = grant/mediate/bind.

COM, POM, Misty-style patterns, the SQL facet, iRule Tcl, and the policy language
itself are all instances. The kernel needs **zero new operators** — `admit` is already
instance-parameterized (the policed include's language argument), and the ∅-environment
case of `bind` is simply data (Principle 9).

**The asymmetry v3 left open — and the concrete gap it exposed.** For the *program*
instance both hinges existed (the typed-JS surface admits down to the POM; handles +
R/L/F/X govern the tree). For the *config* instance only the operation hinge existed
(describe()/safety classes/capabilities on COM) — the surface (nginx.conf, JS-over-COM,
JSON snapshots) was ungoverned and untyped. **COM was missing its admission hinge:**

> A tenant's config fragment should be a *sentence of a restricted config grammar*,
> admitted like code: `syntax_allowed` over config productions ("may write `server{}`
> blocks; `listen` only in your port range; no `proxy_pass` outside your namespace"),
> **typed against the same schema M2 produces** (the typed nginx-API schema does double
> duty: types of granted capabilities *and* type system of the config surface), with
> contract tests, pin-by-hash, profiles, and audit-first rollout — all existing
> machinery. This is work item **M-CFG** (`ROADMAP.md`), a parallel track that does not
> block the compiler milestones.

**Retro-evidence that this is the right correction** (mechanisms we built before naming
them): the config snapshot/rollback JSON files are **quotations of COM subtrees** —
cap-free descriptions realized by an operator; and the operator-reconfig UX plan
("tenant proposes, host applies") is **closure-vs-quotation for the config instance**.
With M-CFG the symmetry completes in both directions: POM had borrowed COM's safety
classes; COM borrows back POM's typed, admissible surface. Both instances then have all
three layers governed: surface + types (admission), caps + classes (operation),
lowering + provenance + epochs (execution).

One semantics clarification comes with the generalization (detailed in `SEMANTICS.md`):
for data-like instances, **meet composes mutation *rights*, never values** — there is
no "intersection of two listen ports"; conflicting values are an ordinary admission
error, while the right-to-set composes by intersection like any authority.

---

## 3. The Program Object Model (POM)

*(v3 terminology: POM = **Program** Object Model — the reflective program tree. v2 used
"POM" for the Policy Object Model, the policy-facing API surface; that API collapsed
into the four-operator kernel of §4, freeing the acronym. See §12.)*

The unit of protection is the **fragment** — v2 defined it as a grammar production
symbol; v3 identifies it with a **POM node**: a handle on a program subtree
(module / class / function / block / statement / expression), where

- source ↔ concrete syntax tree is **1:1** (with trivia);
- AST → bytecode → safe-C → compiled `.so` are **projections with provenance**, not
  1:1 — policy rides the provenance links, and below the source level enforcement is
  the *compiler's faithfulness* (a stated proof obligation, ROADMAP §M8);
- nodes have **path-stable ids** and **content hashes** — policies *target* by
  id/selector but *pin* by hash, so a policy written for `F@hash₁` refuses a silently
  edited `F@hash₂` (fragment identity / supply chain);
- fragment names form a tree, policy inheritance follows it (v2 §3.1 unchanged);
- the running program may itself hold (attenuated) POM handles — the **base level and
  the meta level are the same kind of thing**, which is what makes "a running program
  is indistinguishable from a policy" operational rather than philosophical.

**Scope rule (unchanged from v2, now with the reason):** policies attach to
**nodes/productions, never lexeme ranges**. Lexeme ranges are not closed under the
operations that matter — they can split a scope or a construct, so nothing is provable
about them. Region-style sugar desugars to nodes; anchor pairs must coincide with
production boundaries (parser-enforced).

The v2 requirement to **enumerate all p_symbols** (stable, versioned naming of grammar
productions) carries forward verbatim as the enumeration of POM node kinds.

The node interface, its lifecycle, and the mutation safety classes are specified in
`POM.md`.

---

## 4. The kernel — two resource kinds, four operators

**Resources (unforgeable):** host capabilities (COM nodes, tables, sockets, clock,
RNG, IO…) and POM handles (program subtrees). POM access is **not an operator** — it
is a capability kind; the host holds the root handle and distributes attenuations.

**Operators:**

1. `grant(env, name, cap)` — place a **held** capability into an environment. The one
   guarded operation — and even its guard is not a runtime check: any value you can pass
   to `grant` was necessarily evaluated from your own environment. **Possession is a
   metatheorem of evaluation plus unforgeability** (SEMANTICS.md §2), which is why the
   hardening milestone (`HARDENING.md`) is the literal enforcement mechanism of the
   axiom, not hygiene.
2. `mediate(cap, interceptor) → cap′` — a membrane over every operation of the
   capability: deny / attenuate / transform / meter / **revoke** / **redact**
   (redaction of POM handles — show interfaces, hide bodies — *is* the source-visibility
   control). An interceptor closing over other caps makes `cap′` a **facet** — the
   pattern by which libraries expose safe "transactions" over raw authority they never
   leak.
3. `bind(env, node)` — attach a **frozen** environment to a POM subtree. Inside the
   subtree, names resolve **only** through it: no ambient globals, no free imports, no
   ambient `eval`. If the node is already bound, the new binding **meets** the old
   (intersection of authority — associative, commutative, idempotent ⇒ multi-policy
   composition is order-independent and confluent *for restrictive mediations*; v5.0 —
   transforms do not commute, so **at most one adaptive-profile policy per node**,
   SEMANTICS R1). Consequently **even a self-rebind
   can only narrow**; *widening* a live binding is inexpressible inside the algebra —
   it is a distinguished administrative rebind (a new binding **epoch**) requiring an
   admin-class handle, and in a multi-worker server it is a fan-out-required mutation
   (POM.md §3).
4. `admit(node, contract) → node | reject` — the gate: typecheck against the declared
   environment signature, syntactic predicates, and the contract's **tests run inside
   the sandbox with determinism caps (clock/RNG/IO) denied** — so admission is
   reproducible and has **zero blast radius** (tests run against capability doubles).

**Everything else derives.** `attenuate` = mediate with a pure filter. `revoke` =
narrowing to zero via a mediation flag (v2 §5.4's "the check point is the indirection"
holds — no proxies, no identity split). `include(src|spec, policy, contract)` =
parse ∘ admit ∘ bind — v2's policed include (`include <interpreter> <symbol>
<restrictions>`) is this composition with a language argument (§8). `eval`/`Function`
remain **compile portals** exactly as v2 §3.6 analyzed them — pre-packaged slivers of
control, withheld by default, and now each portal routes through `admit`. v2's
`engine_control` is simply **a bind-capable POM handle**; v2's
`sub_policies_allowed`/`what_policies_allowed` are the rules for granting such handles
downward. v2's acyclicity invariant (no fragment controls its ancestor path) is kept as
an explicit grant discipline: never grant a fragment a bind-capable handle covering its
own ancestors.

**Kernel stays at four.** Richness lives in **libraries, themselves governed**: a policy
library is a node bound under an environment that withholds the raw operators and
exports only vetted combinators — so policy authors can only assemble complete
"transactions" (v2's expressiveness-ladder discipline, now enforced by the same
mechanism it disciplines).

---

## 5. Two relations, one tree *(v2 §3.4, restated in kernel terms)*

| | **Control edges** | **Communication edges** |
|---|---|---|
| Mechanism | holding a bind-capable POM handle over a subtree | granted capabilities (possibly mediated/faceted) |
| Shape | asymmetric, acyclic (grant discipline) | arbitrary graph, both sides consent |
| Invariant | narrowing lattice (meet on rebind) | attenuation-only delegation (§9) |
| Story | tenants, sub-tenants, host | mutual protection between peers |

"A controls B" must never fall out of "A talks to B": no communication grant may confer
control (never smuggle a bind-capable handle inside a data structure — the deep cap
check of §6 polices exactly this), and control does not implicitly open communication.

---

## 6. Policy values: closure vs quotation *(new in v3 — the delegation/specification split)*

A policy is a first-class value; there are exactly **two kinds**, distinguished by one
machine-checkable bit — *does the value contain capabilities?*

- **Closure** — the value **carries** capabilities. It can only carry what its producer
  held, so it is bounded by its producer: if A governs B and B builds a policy P for
  target T, then **T ≤ P ≤ B ≤ A** (the intersection, "A+B" case).
- **Quotation** — the value **describes** a policy: inert syntax, literally an unbound
  POM node, zero authority. A **realizer** R turns it into force —
  `realize(Q) = admit(Q, contract)` then evaluate Q's environment clause under **R's**
  environment, then `bind`. The target is bounded by the realizer, **not** by the
  quotation's producer: B (governed by A) may *describe* a policy stronger than
  anything B holds; A's control over the outcome is exercised through what A granted
  into B's environment (B cannot even develop against names it cannot resolve) and
  through the admission contract A authors at realization.

**The cap-free rule (load-bearing; strengthened in v5.0 — R2):** the quotation
constructor accepts only **stone** splices — deep-frozen plain data (primitives +
frozen records/arrays; no getters, no proxies, no mutability). A merely structural
"no capabilities right now" check is TOCTOU-unsound: a mutable splice could acquire a
capability after checking, a getter could produce one lazily. Stone makes cap-freeness
*stable*. A capability spliced into a "description" would smuggle a closure inside a
quotation; with the rule, the closure/quotation distinction is checkable, not
conventional — and **opaque values, carrying authority, are unspliceable**: secrets
structurally cannot leave through proposals (SEMANTICS.md §2, rule QUOTE). Pleasing corollary: **POM reads return quotations** —
program text read out of the tree is cap-free data by the same rule; one constructor,
one invariant.

**Honesty note:** you cannot prevent a fragment from *writing* arbitrary descriptions —
data construction is free, as it must be. You prevent descriptions from *taking effect*
(`admit`) and from being developed against real authority (environment vocabulary).
You cannot stop anyone writing a config; you stop it being applied.

Worked examples of both modes, with authority traces, are in `SEMANTICS.md` §4.

---

## 7. Attachment: how policy meets program text

**Rule: the target program always remains valid, runnable JavaScript.** Two independent
reasons: (i) the POM's 1:1 source mapping and every existing JS tool depend on an
unforked grammar; (ii) hardening third-party code *requires* external binding to be
complete on its own — which demotes inline syntax to ergonomics.

Two ways to *name a target*, one policy language:

- **External binding — queries.** The target sub-language is a selector language over
  the POM, both *extensional* (this node, this anchor) and **intensional** ("all call
  sites of `fetch` in module M", "every export of fragment F"). Intensional targeting
  is required for the hardening scenario: you cannot annotate code you do not own.
  This is v2's representation **B (sidecar)** and **C (canonical config tree)** —
  C remains canonical, exactly as v2 §9.1 ruled.
- **Inline binding — anchors, not policy text** *(v3 change from v2 §3.2)*. The inline
  carrier is an **inert marker that names a site** — `"use comcon: checkout";`
  (directive-prologue string, a no-op statement in plain JS) or `/*@comcon checkout*/`.
  The policy itself lives in a separate unit referencing the anchor. v2's
  nested-backtick form — policy *programs* embedded in the source — is **retired**:
  it trapped policy text inside string literals, made policy unswappable without
  touching code, and violated the sidecar discipline v2 itself recommended for tenants.
  What v2's nesting expressed (the scope stack) is expressed by node nesting itself.
  **BUILT in increment D5b-2 (2026-09-12)**, in the directive-prologue spelling only:
  anchors are node attributes, selectable as `anchors(glob)` over a `cst()` view
  (`t/comcon_pom_anchors.t`). The `/*@comcon checkout*/` comment spelling is **not**
  built — it needs comment trivia threaded through the parse and attached to the
  following node, and a half-built anchor form that silently fails to register is worse
  than one that does not exist. Recognition matches the **raw** directive text, so the
  name a reviewer reads is the name that binds.

**Two policy profiles** *(new in v3)*:

- **Restrictive** — deny/attenuate only; behavior under policy ⊆ behavior without it.
  The program remains a correct standalone JS program. Hardening existing code **must**
  stay in this profile.
- **Adaptive** — mediation *transforms* behavior (rewrites, injected protocols —
  mirror-as-policy lands here). The program then depends on COMCON semantics; the
  policy must **declare** the profile, making "runnable without COMCON" a checkable
  claim instead of a hope. *(v5.4 — deferred off the critical path:)* **the shipped
  core is restrictive-only**; adaptive is an isolated late addition (≈M9, with
  mirror-as-policy and scenario 22). Nothing before M9 needs transforms, so the entire
  early system enjoys unconditionally-ACI composition — the R1 caveat, the one-per-node
  rule, and `E_BIND_ADAPTIVE_CONFLICT` stay specified but dormant until then. Deferred,
  not dropped.

**Failure semantics, declared per rule:** `reject-at-compile` / `deny-at-runtime` /
`attenuate-silently` / `audit-only`. Hardening brownfield code starts `audit-only` and
tightens — this is v2's learning mode (§11) meeting the binding surface.

---

## 8. Enforcement: three sub-languages, four moments

The **enforcement language** is not embedded in JavaScript as syntax; it is three
sub-languages, **declarative at their normal form** *(reworded v5.3 — C7, after rev
3.3: the authority language is written as policy-JS programs; what is data — hence
compilable, analyzable, diffable — is its admission-time normal form, the descriptor
tables, plus the value-level target selectors and contracts)*:

1. **Target (WHERE):** POM selectors (§7).
2. **Authority (WHAT):** environments built by grant/mediate — subsuming v2's
   `import_list` / `export_list` / `internal_list` descriptors (an env *is* the
   import list; export policing is mediation on what crosses outward).
3. **Contract (MUST):** types + pre/post predicates + tests + natural-language spec
   (one artifact, three consumers: human intent, AI generator, machine verifier).

There is deliberately **no composed standalone syntax** for these (v4.2): policies —
including quoted ones — are written in policy-JS plus the governed library; the
"declarative" form realizers demand is a `syntax_allowed` *profile* of policy-JS,
normalized at admission into diffable descriptor tables. Target selectors and patterns
remain value-level DSLs interpreted by library functions under capabilities. Two-phase
name binding (splices early-bound as ∅-data, free names late-bound under the realizer)
and the `realize = admit → bind → exec` collapse are specified in `SEMANTICS.md` §4.4.

One policy is enforced at **four moments** — same policy, different stages:

1. **Static (stage 0):** every free name must resolve in the bound environment;
   out-of-caps references are compile errors; type-binding against the typed host-API
   schema (ROADMAP §M2).
2. **Admission:** `admit` contracts — types + caps + in-sandbox tests before linking.
3. **Residual runtime:** what is not statically decidable (dynamic keys, `any`) runs
   behind membranes / the hybrid interpreted path.
4. **Compile-through:** typed static policy → maxim emits C linking **only the allowed
   stubs**, scoped state, capability constants (tenant key prefixes) baked in —
   confinement with **no runtime policy interpreter**.

v2's three delivery stages (declarative → compile-time hooks → run-time hooks) map onto
this as the *build order*; its tri-state descriptors (`allow / deny / allow-with-check`)
survive as the static/residual split, and its hook invariants (pinned authority,
conjunctive-only, confinement, budgets, fault mapping) are now properties of `mediate`
interceptors — pinning = the interceptor's closure environment, conjunctive-only =
monotonicity, budgets = metering mediations. Principle 8 gives the guarantee v2 argued
for: adding a later stage cannot change what is allowed, only what it costs — **the
performance gradient is the staging gradient**, with M1 as its measured endpoints.

---

## 8a. Administration: there is no management plane *(new in v4.1 — user-spotted)*

`comconctl` (the operations CLI that the manual and showcases lean on) is **a shell,
not a tool**. Every invocation opens an **admitted stage-0 episode** in an operator
REPL/REL session under an operator environment; every verb is an ordinary **library
program** (`std.ops`, governed like any library) written over the four kernel operators
plus granted capabilities. There is no API a verb can reach that a tenant fragment
granted the same capabilities could not — v2 §9.3's "the tooling never needs (or gets)
a backdoor" is thereby *derived*, not asserted.

The verb set decomposes completely: reads (`denials`/`docs`/`diff`/`trust-report`) are
pure queries over log/describe/provenance caps; `learn`/`propose` = audit-mode bind +
log + heuristics emitting a **quotation**; `shadow`/`enforce` = rebind;
`admit-config`/`propose-config`/`realize` = admit/quote/realize verbatim;
`snapshot`/`rollback` = quote / rebind-to-epoch; `revoke` = mediation flags walked
along grant chains; `rewrite` = reflect.rewrite + admit + class-F broadcast; `attach`
*is* a session. Irreducible residue: bootstrap and transport (host integration — in
pilgrim, the existing admin-shell/`nginx.repl` machinery).

**The third closed enumeration.** For the verbs to be pure library code, the host
resources previously implicit in "the tool" must be first-class capabilities: the
denial/observation log · the binding/epoch store · the provenance/grant-chain registry
· the class-F broadcast channel · the snapshot store · the signing key · the
learning-recorder switch · **the audit/enforce/learn mode switch** *(added v5.53: the
rollout verbs decompose over it, so it is a resource like the others; it was missing
from this list while `std.ops` shipped it, which is the drift V7's checker now
catches)* · **the session registry** *(added v5.65: the identity→attenuation table of
§8b. It is a resource like the others precisely so that the thing which HANDS OUT
authority is itself held, not ambient — a session without it has no `grant`/`revoke`
verb at all. It stores descriptors, never environments, so holding it is the right to
map, not the right to grant.)*. *(Realization note: the snapshot store is the binding store's quotation
record — "snapshot = quote" — rather than a separate host object.)* This joins the per-instance grammar enumerations and the
compile portals as the third *kind* of closed enumeration (§13.2) whose *completeness*
makes a safety claim checkable — here, "no backdoor."

Consequences: administration inherits every mechanism for free (office-hours/cosign
mediations govern admin verbs; `trust-report` runs over operator sessions themselves);
CI, dashboards, and AI operators use the identical gate — comconctl is merely one shell
among many, rebuildable by anyone from their granted slice; and M-SES loses an entire
attack-surface class, since there is no separate management API to harden
(`HARDENING.md`).

---

## 8b. The session boundary: identity → environment *(new in v5.65 — closes TM-2)*

Everything in §8a assumes a session **has** an environment. THREATS.md's **TM-2** named the
hole: *how an authenticated principal (human, CI job, AI agent) becomes a granted
environment — who authenticates, where the identity→env table lives, and how that table is
itself governed — was host-integration work that no document owned.* It was the last
unowned finding in the threat model, and it had a deadline: it must exist before the first
real operator session.

**COMCON DOES NOT AUTHENTICATE, AND THAT IS THE WHOLE TRUST TRANSFER.** The host asserts a
principal — an mTLS client certificate's subject, a JWT the host verified, a unix socket's
peer credentials, an SSH-authenticated shell. COMCON never validates that string and cannot:
it has no notion of a credential. Everything below is conditional on the host asserting the
principal honestly, and any integration that passes a *client-supplied* identifier here has
handed the client the session. This paragraph is the boundary; it is stated this loudly
because a blurred version of it is how identity systems are actually broken.

**The registry holds DESCRIPTORS, not environments.** A session grant records an
*attenuation* — a cap-free descriptor table (`{imports, routes, ttl}`) — never a capability
and never an env. Three things follow, and they are the reason the design is this shape:

1. **Stealing the table yields nothing.** It contains no authority to steal. Compare the
   obvious design (store an env per principal), where the table IS the keys to the building.
2. **It can live in `nginx.shared`,** because data crosses a process boundary and
   capabilities do not — so the registry is **fleet-wide by construction**. That is not a
   nicety: the audit/enforce mode switch shipped as per-process and put a four-worker fleet
   in mixed modes (v5.56). A session table with that bug would authenticate on one worker
   and not on the next.
3. **Leases are the shared store's TTL**, so an expired grant is reclaimed when it is
   probed, by machinery that already exists and is tested (v5.63). A lease that nobody
   sweeps still expires.

**Resolution attenuates the RESOLVER's own environment.** `resolve(principal, env)` takes
the env to narrow as an argument — no ambient authority, the same rule `std.ops` follows —
and returns `env` restricted to the descriptor. The registry therefore cannot hand out
authority the resolving side did not already hold: a session env is **≤ the env of whoever
resolved it**, which is the kernel's monotonicity property applied at the identity boundary
rather than a second rule to trust. A descriptor naming something the base env does not
grant is **refused**, not silently dropped, because a mapping that quietly grants less than
it says is a mapping nobody can audit.

**Deny by default.** An unknown principal resolves to the empty environment — the same
answer as an undeclared free name. Not an error, not a default role: nothing.

**Where the chain is rooted.** The first environment is not granted by the registry; it is
held by **host JS at configuration time**, which is the root that already exists. There is
no new privilege and no bootstrap ceremony: an operator with `js_source` has the root env
because they have the config file, and everything a session ever holds is a narrowing of
that. A registry that could mint authority would be a management plane (§8a says there is
none); a registry that can only narrow is a *lookup table for attenuations*.

**Revocation needs no chase.** Resolution happens per use, not once at login, so `revoke`
removes a row and the next resolve returns the empty env. There is no token to hunt, no
cache to invalidate, and no session that outlives its grant.

**The ninth ops-resource.** The session registry joins §8a's enumeration as `sessions`, so a
session without that capability has no `grant`/`revoke` verbs *at all* (absent from
`Object.keys()`, not present-and-throwing) — the no-backdoor property, extended to the thing
that hands out authority.

**What this deliberately does NOT provide**, recorded so the gap is visible rather than
assumed: authentication (above); a principal *naming* policy (the host's namespace, not
ours); transport and the login flow (the P19 admin-shell substrate); and any notion of a
*role* — there are only attenuations, because a role is just a descriptor someone named.

---

## 9. Run-time object model *(v2 §5, carried forward)*

The engine-level mechanisms of v2 remain the run-time substrate; v3 changes their
justification, not their design:

- **COW domains** (v2 §5.1): per-fragment *views of the same object identity* — why
  `Proxy.revocable` was rejected stands. COW views are how communication-edge sharing
  is implemented under the hood of granted caps.
- **Opaque values** (v2 §5.2): hold/pass/return but not inspect; conversion is a
  capability. Known caveat (leaks via allowed operations, timing) still stands.
- **Handles** (v2 §5.3): opaque cross-boundary references, dying with the connection.
- **Delegation & revocation** (v2 §5.4): delegability is part of the grant
  (`no | once | to | attenuated_only`), attenuation-only, grant chains for provenance
  and cascade revocation; **revocation = narrowing to zero** through the very check
  points that make references revocable without proxies. The confused-deputy honesty
  note stands verbatim. In kernel terms all of §5.4 is `mediate` flavors — the
  scenarios (offboarding, CVE response, one-shot grants, leases, AI-safe grant profile)
  are unchanged and still normative.

---

## 10. Multi-language & the policed include *(v2 §6, carried forward)*

A policy defines its fragment's **language**: unrestricted JS, restricted JS down to
JSON-equivalent subsets (the reduction principle: `JSON.parse(s)` ≡ `eval(s,
json_policy)`), typed-JS profiles closed enough for maxim/safe-C, or other languages
entirely (iRule Tcl via per-language compile hooks — mirror as a policy). The uniform
mechanism is the **policed include**: `include(source|spec, policy, contract)` names
what parses it, the grammar symbol it must produce, and its limits — in v3 this is
literally `parse ∘ admit ∘ bind`. No arbitrary text inclusion, ever. The SQL-injection
flagship stands: admit only the parameterized-query production and concatenated SQL
becomes *inexpressible*.

---

## 11. Contracts, AI, and learning mode *(v2 §9.5–9.6, sharpened)*

A controlled fragment's **contract** = (capability environment, natural-language spec,
tests). `include(spec, policy, tests)` for AI generation: generate → admit (types +
caps + tests in-sandbox) → bind → link; after admission the fragment compiles to typed
C and its tests become CI for the compiled artifact.

**The blast-radius property (the strongest argument for the design in an AI world):**
authority is deny-by-default, so a generated fragment cannot exceed its environment
even if the spec was misunderstood or the tests are weak — **capabilities bound damage;
tests only bound correctness.** v2's asymmetric failure property is its policy-side
twin: an AI-written sub-policy can deny too much, never grant too much — a liveness
risk, never a breach.

v2's **dual test surface** (allow-suite / deny-suite, denial structure as the assertion
language), **trust-inverse-to-the-lattice** table, **methodology** rules 1–9, and
**learning mode** (static/dynamic harvest, shadow mode, the intent gate — *auto-deploy
of learned policies is forbidden*) carry forward unchanged; learning mode's
`audit-only` phase is now a declared failure-semantics mode of the binding surface
(§7). Source visibility to fragment creators/generators = presence/redaction of a POM
read handle — withheld, a generator cannot see the surrounding program at all.

---

## 12. Terminology & design changes from v2 (the honest delta)

| v2 | v3 | why |
|---|---|---|
| **POM = Policy Object Model** (`pom.*`, `engine_control`, `add_policy({...})`) | **kernel of 4 operators** (grant/mediate/bind/admit); acronym **POM reassigned = Program Object Model** | the policy API collapsed; the program tree became the first-class object |
| nested-backtick policy literals (policy text inline) | **anchors** (inert markers) + external policy units | policy text must not be trapped in strings; JS stays pure; sidecar discipline v2 already preferred |
| narrowing lattice as invariant to enforce | **No-Amplification theorem** from the possession axiom | proved (sketch), not policed; possession is a metatheorem — grant needs no guard |
| `engine_control` capability | bind-capable POM handle | one capability kind, no special object |
| `sub_policies_allowed` / `what_policies_allowed` | rules for granting bind-capable handles downward | same meaning, kernel vocabulary |
| policy = descriptors + hooks | env (grants) + `mediate` interceptors; tri-state = static/residual split | hook invariants become properties of mediation |
| fragments = grammar productions (names/paths only) | POM nodes + **intensional queries** + pin-by-hash | hardening 3rd-party code needs queries; supply chain needs hashes |
| — | **closure vs quotation** policy values; cap-free quotation rule | resolves delegation vs specification in one bit |
| — | **restrictive vs adaptive** profiles | makes "runnable without COMCON" checkable |
| — | mutation **safety classes R/L/F/X** on live program rewrite; epochs; hybrid fallback | reuses js_com's config-mutation machinery for code (POM.md) |
| TCB notes scattered | **M-SES hardening milestone**, scoped, gating first untrusted execution | HARDENING.md |
| performance model argued | M1 endpoints **measured** (96% vs 28% of stock) | ROADMAP.md |

Everything in v2 not named above (COW domains, opaque values, delegation/revocation
scenarios, multi-language, bootstrap chain, LSP/authoring, threat validation, policy
engineering, learning mode, compile portals, p_symbol enumeration) **carries forward**.

**v3 → v4 delta (the symmetry correction):** "two mirrored trees" → **one
governed-language pattern, N instances** (§2a); Principle 9 added (*data is code bound
to ∅*); COM gains its admission hinge (typed config surface, work item **M-CFG**); the
M2 schema is declared dual-role (cap types + config-surface types); meet clarified for
data instances (rights, not values); snapshots recognized as quotations of COM
subtrees. Kernel, theorem, M-SES, and all measured results unchanged.

**v4.1 (in place, the comconctl closure):** Principle 10 (*there is no management
plane*) + §8a — administration = admitted episodes; tool verbs = `std.ops` library
programs over kernel + caps; the **third closed enumeration** (ops-resource
capabilities) added; M-SES surface shrinks (no separate admin API).

**v4.2 (in place):** the no-second-language correction (policy-unit grammar withdrawn,
SEMANTICS §4.4); compilation tiers + the fragment artifact (ROADMAP §10); Principle 11
(*extend by granting, never by syntax*) with the typed profile fixed as a discipline
over policy-JS (schema + inference + JSDoc-style erasure-sound annotations, ROADMAP
§M3). **Explicit retirement:** v2 §6's allowance for "extended JS" (new typed syntax)
is withdrawn — v3 §7 implied it, v4.2 states it: grammar is never extended, only
vocabulary.

**v5.1 (in place — the engineering review, E1–E12, ROADMAP §13):** increment re-cut
(confinement-first delivery: COMCON-lite → onboarding → typed+compiled → live ops →
config; dogfood at increment A); M-UNIFY (maxim merges into the vendored engine tree —
one bytecode definition, one hardening surface); docs-v5.0 frozen as the single
normative spec (in-place revisions only); compatibility principle (§1: no flag-day);
dependency workflow (E1), tier-transparent stack traces (E2), selector staging (E9),
one-generator-two-outputs (E10), stage-1-needs-no-membranes (E11).

**v5.131 (in place — G-21 closed: a live epoch compiled in the master and adopted by every
worker, without a reload):** (1) The protocol: a worker's request-time include keeps the wrapper
text it was admitted from and sends it to the master (`NGX_CMD_JS_COMCON_AOT`, one message of
at most 64 KB); the master spawns ONE detached helper — a fork of itself with the compartment
and the cache directory — and does nothing else (no thread, no blocking, its signal loop
untouched); the helper compiles the text compile-only (nothing runs, not an IIFE source, not a
statement), enqueues every function under the wrapper on a gcc thread it starts for itself,
drains it, writes an index atomically and exits; on every invocation, at most once a second,
the worker looks for the index and installs the artifacts it names, re-asking after five
seconds up to six times, then `unavailable`. `aotStatus()` gains `pending`, `via`
(`config`/`master`/null), `tries`, `unavailable`. (2) Identity across processes is by SOURCE KEY
(the function's own text plus its shape), never by the atom-bearing bytecode hash the cache is
keyed by; the index maps source key to artifact hash and the worker installs by explicit hash
under the cache-hit path's own checks (version symbol, no process-bound direct calls, the atom
table rebound to its runtime). (3) Portable codegen: the helper emits no symbol-named direct
JIT-to-JIT calls (the inline-cache call, resolved where it runs, is taken instead), the mode is
folded into the hash, and an artifact so compiled carries no direct-call marker — the first
cut was refused two of every three functions by exactly that marker. (4) **Found by the first
test, pre-existing:** a worker that created its runtime after the fork — the compartment at a
first request-time include, a SharedWorker's runtime — started a gcc thread of its own through
the engine's runtime init: it compiled synchronously inside a request, N times across N
workers, and under the master's minimal environment (no PATH) every job failed and wrote a skip
marker that the helper then honoured. Workers now forbid the thread at process init
(`js_jit_forbid`); the helper allows it for itself and sets a PATH when the environment has
none; it also resets `SIGCHLD` so nginx's handler cannot reap the gcc children the compile
thread waits for. "The JIT is inert in workers" is now enforced, where it had been assumed.
(5) The helper is detached: nginx does not count it as live at shutdown and does not signal
it. (6) The demo harness passed its `NGINX` variable into nginx's environment, where nginx
reads a variable of that name as its inherited listening sockets, refuses to daemonize and
blocks the shell; `demo_start` now unsets it for the process it starts, so the README's
`NGINX=/path bash test.sh` form works as written. (7) `t/comcon_aot_epoch.t`, which pinned
"a live epoch stays interpreted", now pins the instant of admission (`compiled: 0, pending:
true`) under a private artifact cache — on a warm cache the master's index answers before the
epoch is asked for, which is the feature, and which its old shape read as a failure. (8) **What
the warm cache found.** The second gate run, with the previous run's index answering at
include time, ran every request-time fragment of the suite NATIVE inside the request that
admitted it — a pass the suite had never had, since request-time fragments were interpreted
for good. Three files failed. **F22, found and fixed:** the compiled tier's store to a global
tested only for an uninitialised cell and otherwise wrote the variable cell directly; the
interpreter also takes its slow path when the reference is CONST, which is how a non-writable
global property — the compartment's frozen binding (F15 phase 1) — is represented. Compiled
fragment code could reassign `Promise` for every co-resident fragment where interpreted code
was refused. Fixed in the engine: the generated store reads the const bit at the store and
takes the interpreter's whole branch (two runtime entries, appended to the vtable; codegen
version 20 so no artifact with the bypass is ever loaded); an SR-2 row pins both tiers, with
a control. **F23, open:** compiled code keeps no program counter, so a failure raised from
the native tier carries no line — `t/comcon_pom_origin.t` now says which tier answered
rather than pin a line the tier cannot give; the fix is per-operation `cur_pc` maintenance in
codegen, on the compiler track. **F24, an instrument:** the mutants corpus's spin probe (`t += i` over 4e7
iterations) finished inside the 100 ms meter on the native tier, so `meter=off` changed
nothing and survived — and no count mends an integer loop, which gcc folds or runs eighteen
times faster than the interpreter; the probe now reads a property per iteration, which goes
through the runtime on both tiers (measured within a factor of two), and 5e7 of them outlast
the meter on both. (9) So the warm pass is a stage of the gate (2c) and of the
pack, and every run has its own artifact cache (`QJS_JIT_CACHE`) — the accident is now the
instrument. Engine debt to the fork.
`t/comcon_aot_master.t` (17, compiled tier), control
`aot-master-inert.patch`, OPERATOR_API §8n, ASSURANCE G7.27, demo `L_Live_Ops/L4`. Gate green
on both binaries.

**v5.130 (in place — G-05 closed: the allow-suite, a cage derived from observed behaviour and a
candidate admitted against it):** (1) `comcon.std.suite`: `record(f, {max})` keeps every call's
(input, output) as the JSON text the boundary marshals, in the include result's own callable
(one property test per call when off); `cases(f)` reads the stable cases apart from the
unstable ones (an input answered two ways pins nothing) and counts what a full recorder
dropped; `tests(cases)` emits a contract `tests` quotation — one function expression that
replays every case inside the compartment and throws on the first divergence, naming it;
`check(candidate, cases)` is the same replay on the host; `coverage(f)` is which of the
fragment's functions the window entered, which never were (by name and line), and which gates
fired. (2) `h.record/suite/coverage/guard` on `bindAt` and `bindShared`, `ops.record/suite/
coverage/guard` over a name: `guard` pins the suite into the binding's own contract copy, so a
rebind that answers a recorded case differently is refused with `E_ADMIT_TEST` and the epoch
does not move; the policy diff reads a removed pin as widening. (3) One mechanism under
coverage: `comcon_call_count`, a per-function entry counter the engine keeps in every build
(one increment at the interpreter's call entry), read by `js_comcon_call_counts()` through the
POM's own bytecode walk; on the compiled tier a lowered function's direct calls bypass that
entry, so the report carries `tier` and `exact: false` rather than a number it cannot defend.
Engine debt to the fork. (4) Two things the suite's own test found: a shared value is at most
511 bytes, so `bindShared.guard` carries the suite in fixed chunks under sibling keys with the
count and length on the record; and a shared `replace()` published the record BEFORE realizing
the candidate — a refused one would have broken every worker's next reconcile — it now
realizes first, as `bindAt` always did. (5) A thrown answer is recorded as `threw` without its
text: an exception's message crosses the boundary prefixed, and a case must compare the same
on both sides. (6) The pack on the first commit reported the three `library-programs-absent`
rows INCONCLUSIVE again — this change rewrote both lines that patch anchors on (the include
callable, the freeze of `std`) — so it is regenerated on this tree, as the script's rule
requires. `t/comcon_std_suite.t` (27), control `suite-guard-inert.patch`, OPERATOR_API §8m,
ASSURANCE G7.26, demo `O_Operators/O4`; `std.ops` now twenty-four verbs. Gate green on both
binaries.

**v5.129 (in place — G-01 closed: a grant can be withdrawn while the fragment runs, and the
withdrawal follows every delegation):** (1) `comcon.withdraw(f, name?)` and
`comcon.withdrawn(f)`; `h.withdraw`/`h.withdrawn` on `bindAt` and `bindShared` handles;
`ops.withdraw(name, grant?, {confirm})` (class X, the confirmation naming the binding) and
`ops.withdrawn`; the manual (`ops.docs`) and the trust report mark a withdrawn grant. (2) The
mechanism: every granted wrapper — socket, outbound, COM facet, author — holds a refcounted
*grant record*; the authoring tier's copy-then-narrow gives the copy a record whose parent is
the original's; the gate walks the chain right after `cap.owner`, at the same six sites; the
fragment's stats slot keeps its records under the contract's names, which is the host's one
handle on a grant after the wrappers have vanished into the closure. (3) `cap.revoked`, the
second unconditional denial code after `cap.owner` (the operator who withdrew is not
observing a policy), frozen in the golden corpus with a `revokeBefore` row shape and named in
SPEC. (4) A binding's revocation sticks: `bindAt` re-applies it to every epoch a replace
realizes or a rollback restores; `bindShared` carries it on the shared record with a `rev`
counter beside the epoch, so a fan-out is not a new epoch. Irreversible: nothing un-withdraws,
because restoring authority is a widening and every widening is a new admission. (5) The verb
is `withdraw` because `comcon.revoke()` is already the grant-time flavour — one name for two
acts would let a mistaken call return a descriptor where a revocation was meant; both leave
a revoked capability, which is what the code names. (6) `asContract` reads a binding's
contract as `realize()` does (grants derived from `env` + `imports`), so the docs and the diff
of a binding see the grants it holds. (7) **Found by the first gate run, under ASAN:** the
grant loop first held its records by a *borrowed* pointer on the theory that the wrapper stays
alive until publish — false for a grant the fragment's closure never captures (the mutants
corpus grants names its probes do not use): the wrapper dies when the closure is applied, and
the pointer dangled at publish (heap-use-after-free in `ngx_js_grant_ref`, a worker dead with
"corrupted double-linked list" on the compiled tier). Both entrances now take a reference of
their own per record, publish *transfers* it into the table, and one outer function per
entrance releases what was not transferred — dozens of refusal paths, none of which has to
know. Clean under ASAN with leak detection on. (8) The pack on the first cut reported the three
`08934b766` commit-revert rows INCONCLUSIVE — this change rewrote lines that commit touched —
so they are re-based as one maintained patch, `t/tools/controls/library-programs-absent.patch`
(the three library programs and the include result's contract removed; 12, 13 and 11
assertions fail), as the script's own rule requires. `t/comcon_revoke.t` (31), the V12 row, control
`revoke-not-checked.patch` (16 assertions fail with the walk blinded; the read-back ones
pass), OPERATOR_API §8l, ASSURANCE G7.25, demo `S_Security_Teams/S5`; `std.ops` now twenty
verbs. Gate green on both binaries.

**v5.128 (in place — the shutdown flake instrumented so its next appearance explains itself;
one latent master spin removed):** (1) Three reads of the master's shutdown path, each a
mechanism the third face (v5.127 item 6) could have been, each ruled out on the code: a
stolen `SIGCHLD` (the SharedWorker manager thread blocks every signal with `sigfillset`
before it is created; `/proc/<pid>/task/*/status` agrees); the master channel reader
blocking the loop (`js_com_proxy_cache.t` sends no master message, and the header read is
`MSG_DONTWAIT`); a blocking `exit` hook (the hook's line is logged last, after the three
reaps). 80 start/QUIT cycles of the same fixture under CPU load: 0 hangs. The signature the
log tail gave — every child reaped, the master idle, then `kill(reaped, 9) failed` — says the
master's process table still held a slot it counted live. (2) So the table is now printed at
the moment that matters: when a `TERM` arrives while a `QUIT` is in progress (only a harness
or an operator who gave up waiting sends that), the master logs one alert line,
`terminate after quit with live=N reap=R: [slot pid name e= x= d= r= j=]…` — every slot with
the five flags `ngx_reap_children()` reads. It sits inside the 40-line tail the "no alerts"
failure prints, so the fourth face, if it comes, arrives with its own explanation. (3) Every
"signal N (SIGx) received" line now carries `tid=`, the kernel thread id of the handler, so a
signal delivered to a helper thread would be visible as such. (4) One latent defect found by
the reading and fixed: the master channel payload loop assumed a blocking socket (`recv()`
"blocks since we are in the sigsuspend loop"), but the channel fds are `O_NONBLOCK`, so on a
torn message it spun on `EAGAIN` with every signal masked — a loop the master could not be
signalled out of. It now waits at most ~200 ms in 1 ms steps, then drops the message with a
log line naming the slot and the byte count. No test observes the change (no test tears a
message); the gate is green on both binaries. The flake stays open, with a better instrument.

**v5.127 (in place — three library-kind gaps closed: the whole appetite in one read, the
policy diff and the would-deny list, the manual as a query):** (1) `comcon.std.evaluate(fn |
source, {declares})` — G-13 — every free name from the admission collector, classified, with
call sites and lines from the bytecode, the dynamic-code flag, the undocumented remainder and
the `imports` line a contract would need; a source is accepted only as one function
expression and nothing runs. (2) `comcon.std.policy.diff(before, after)` — G-05's diff half —
verdicts `narrowing` (auto-safe), `widening`, `unchanged`, `incomparable`, every change with
its direction, read through the grant translation `include()` itself feeds the C side
(factored out as one function so a report cannot drift from the cage); include results and
`bindAt` handles carry their contract. (3) `comcon.denials(f)` / `ops.wouldDeny(f)` — G-05's
would-deny half — the one mechanism of the three: the invoke sets a pointer to the running
fragment's own per-code counters (nested for a sub-fragment), the compartment's one counting
site increments both, so the gates that fired are attributed to the binding they fired in;
under an audit posture the rows are what enforce would have refused. Negative control
`denials-not-attributed.patch`, verified. (4) `comcon.std.docs.model/render` and
`ops.docs(name)` — G-16's docs half — the tenant's manual as a projection of what its binding
holds, with the live epoch. (5) `std.describe()` told the truth again: its `absent` list had
named six shipped mediation words and `std.ops` as missing; it now lists the canonical NOT
BUILT set and `onViolation`/`profile` are in `enforced`. Four tests (48 assertions), three
demos (`O_Operators/O3`, `A_Auditors/A2`, `A_Auditors/A3`), OPERATOR_API §8k, ASSURANCE
G7.24, `t/comcon_std_lib.t` made robust to port remapping. Gate green. (6) **The
broadcast-fuzz flake showed its third face, with the instrument's tail this time**, in the
pack on `686253e1b`, in `t/js_com_proxy_cache.t` — a file with no SharedWorker and no
fragment: QUIT at 20:43:33, the worker, cache manager and cache loader all exited with code
0 within the same second and the master logged each SIGCHLD, then nothing for 93 s until the
harness sent TERM, after which the master ran its TERM→KILL escalation against the cache
manager it had already reaped (`kill(151110, 9) failed`). So the master stayed in its loop
believing a child was live after reaping every child. The master's one helper thread blocks
SIGCHLD (checked: `SigBlk` in `/proc/<pid>/task/*/status`), so a stolen signal is not the
mechanism; 80 start/QUIT cycles of the same fixture under CPU load did not reproduce it. Open,
with a better signature: a reaped child still counted live in the master's table.

**v5.126 (in place — the showcases annotated with real code; the gaps collected; four demos
for two new audiences):** (1) Every one of the 53 scenario headings in `SHOWCASE*.md` now
opens with a `REAL CODE (v5.125)` block: what the shipped tree does for that scenario, in its
real spelling, the tests that pin it, and a gap id where the sample and the tree differ. The
samples stay as written (illustrative, hypothetical). (2) `SHOWCASE-gaps.md` is the register:
a scorecard (17 scenarios run as written, 4 in a different spelling, 16 in part, 16 not
built) and 26 gaps, each classified — decision, substrate, library, design, honest limit —
with the earliest home ROADMAP §5 names. Four of the sixteen not-built scenarios are one
substrate decision (`opaque.*`/COW), two are one design increment (multi-language), one is a
posture nobody has decided; three gaps are honest limits the samples contradict (no re-AOT of
a live epoch in a worker; a deadline abort is uncatchable by the fragment; learn mode installs
at the first include). (3) Four scenarios had shipped code, no demo, and an audience the first
twelve demos did not address: `js_comcon_demos/L_Live_Ops/` (L1 epochs and rollback, L2
fleet fan-out on four workers, L3 pinned by hash with the pins computed outside the tree) and
`js_comcon_demos/A_Auditors/` (A1 the trust report, the call-site query, the reviewable
rewrite). Sixteen demos, 168 checks, all green.

**v5.125 (in place — M5.1c built and measured: class B's method calls typed by identity, and
the rule now says NO-GO at 2.3×, so the compiler track closes at M5.1c; F20 and F21 found by
a differential fuzz and closed; the flake hunt clean):** (1) M5.1c, the cut §2f.2 pointed at,
of M5.1a's kind: `charCodeAt` on a string receiver with an int index, and `Math.imul` on two
ints, are emitted inline when the callee IS the engine's own C function — identity by
function pointer at run time, never by name, so a tenant's `charCodeAt` is still a call —
and the half-typed bit ops take a double operand through ToInt32 in place instead of the
runtime. `JIT_CODEGEN_VERSION` 19; three SR-2 rows with the spec's values written out
(surrogate halves, out of bounds, `NaN`, a fake receiver, int32 overflow of `imul`, the ToInt32
edge doubles). **Measured:** class B lowered 52.5 → 34.38 ns/char against the C bound 15.00,
**2.3×, NO-GO by the rule stated before the numbers**; class A unchanged at 2.2×. M5.1c
collected the part of the class B prize that the spec fixes the type of; what remains is the
engine's per-character read and boxing, which no lowering of this kind can remove. **The M5
track closes at M5.1c** (PERFORMANCE §2f.3). (2) The three SR-2 rows failed on their first
run for a reason that was not M5.1c: **F20**, five kinds of place where the typed lowering read a
value from the wrong slot, and an inference that typed a local wrongly — a NUMBER local stored from an int slot
read the double register; a branch on an int condition read the double register (the 32-bit
and 8-bit branch forms alike); a branch on an untyped condition, and a fused compare-and-branch
on untyped operands, left the typed slots below the condition unboxed for the join label to
read stale; and the type inference walked the bytecode linearly, so a value arriving at a
join over a jump edge (`b = c ? 1.5 : 0`) never reached the store's type and the local stayed
INT (the compiled tier stored 1). Every shape was reachable before M5.1a with `var` locals
holding a double and an int in turn. (3) To find the rest of F20's class the tree now has a
**differential fuzz with a delta reducer**, `t/tools/jit-diff-fuzz.py`: random small
functions over int and double locals, run interpreted and with every function compiled,
the interpreter as oracle; a divergence is reduced to a minimal function automatically. Its
first twelve seeds diverged on seven; the first run also aborted — **F21:** the compiled
tier's helper for bitwise NOT called the unary-arithmetic slow path with an opcode it has no
case for, so `~x` on a double, a boolean, a string or `null` called `abort()`: a fragment
holding `~1.5` took the worker down. Fixed by taking the interpreter's own path. After the
fixes: 30 seeds, 1,800 functions, zero divergences; the engine's own test files under
`--jit-compile-all` fail exactly where the committed engine failed (pre-existing, recorded).
The fuzz is a gate stage (`gate.sh` 2b) and a pack check. The pack on `c8e4e6d04` had one row
INCONCLUSIVE — M5.1a's control patch, whose lines M5.1c moved; re-based and verified, 34 rows.
(4) The broadcast-fuzz flake hunt:
three full runs of the compiled-tier corpus, no recurrence; the instrumented harness stays
armed. ASSURANCE G7.23, F20, F21, §16; SPEC §8 (what T2 lowers with a type); THREATS T11.

**v5.124 (in place — the record made readable; class B re-measured against a C bound, and the
rule now says GO; the last sweep gap closed; the flake's text captured):** (1) README's
80-line history — one line of it 83 KB, every entry already in this delta log — is replaced by
a five-line state, and ROADMAP's POSITION opens with where the work is, in five lines, above
its log. (2) The benchmark's class B "typed" arm is a C kernel, `nginx.bench.fnvEngine`: the
same FNV with every character read through the engine, which no codegen change can move. On it
class B measures typed 15.16 ns/char against lowered 52.5 — **3.5×, GO by the rule stated
before the numbers**, where the v5.114 NO-GO stood on a lowered-JS denominator that was not a
bound. Where the 52.5 goes is visible: a `charCodeAt` method call and a `Math.imul` call per
character, both with results the language fixes the type of — the shape of an M5.1c of
M5.1a's kind (no speculation), recorded as a decision with its number (PERFORMANCE §2f.2).
(3) The residue sweep's include-inside-a-full-parent shape names `checkRequest`, so the
admission request-field check runs under the allowance too; every named place is swept.
(4) The broadcast-fuzz flake's first face carried text at last, in the pack on `de081c548`:
`kill(worker, 9) failed (No such process)` — the master in its TERMINATE escalation at
shutdown, which means a terminate-class signal reached it; standalone, three kept logs show
QUIT only. Who sent it is not known; `t/lib/Test/Nginx.pm` now prints the error log's last 40
lines on any "no alerts" failure, which is what would have said. ASSURANCE G7.21, §16.

**v5.123 (in place — the gate in the tree; G7.22's limit measured; the stream surface swept;
build artifacts untracked):** five items of one plan. (1) `t/tools/gate.sh` is what "gate
green" in a commit message means — the sanitizer builddirs rebuilt first, `t/` on the
interpreter build, the confinement corpus on the compiled build, the stress suites, ASAN and
UBSAN with leaks on, exit code as verdict — and `--configure` produces the four builddirs from
the recipe the record used; until now the script lived in a session's temp directory, so the
claim was the author's word (REVIEW.md §1). (2) G7.22's named under-count, measured
(`t/comcon_retained_backstop.t`): a 3 MB leaker beside a sibling making ~45 KB of cycles per
call keeps 100% of its exact count after 250 of the sibling's calls and after 200 interleaved
ones — the engine's own collector gets there first and credits the call it runs in, never the
leaker's in three provoked configurations; a ≥ 50% bound stands as the regression guard and the
correction is unchanged, because the measurement gave nothing to tighten. (3) The residue sweep
has twelve shapes: a stream server's handler receiving an uncaught out-of-memory, one alignment
per TCP connection, the session finalized every time, 32 host failures per arm, compiled on the
compiled arm (86 assertions). (4) `objs_jit/` — 212 objects, a binary and the configure outputs
— is no longer tracked; `.gitignore` covers every builddir. (5) The warm-speculation finding
could not be filed upstream: the maxim repository has issues disabled; the text is delivered
to the author. **And the mechanism of v5.120 did what it promised on the pack for `3d0e11d0e`:**
three rows INCONCLUSIVE — two patches whose lines F2 moved (re-based, both hold) and one false
SKIP, an assertion whose prose said "not silently skipped:" matching a detector that now reads
only prove's own skip line — with the kept evidence saying which. ASSURANCE G7.21, G7.22, §16.

**v5.122 (in place — F2's leak half CLOSED: what a fragment retains across calls is its own,
capped, refused past the cap):** user decision 2026-09-15 on the design of the same day, with
its four recommendations taken (refusal not denial; 8 MB default; a sub-fragment charges its
own slot under the parent's cap; a host reader). Every invocation charges its fragment with the
compartment's malloc delta around the call — exact for what refcounting frees at once, which is
nearly everything a call makes and does not keep. Cycles are corrected twice over, both at the
outermost invocation only so ordinary work stays O(1) (F14 is not reopened; the
heap-independence instrument reads 0.98×): a call that grew the runtime by 64 KB or more pays
a collection BEFORE its delta is taken, and once usage has grown 4 MB since the last mark a
collection establishes the ground truth and scales any excess out of the counts in
proportion. A parent's window contains its sub-fragments' charges, so they are taken out of
the parent's delta — the first version charged the parent twice, and the nested test row said
so. New: `meter({retainedBytes})` (narrowing only), refusal `E_MEM_RETAINED` (decided before
anything is pushed: the fragment does not run), `comcon.memStatus(f)`. `t/comcon_retained_memory.t`
(19, both binaries), the V12 golden corpus row, the enumeration checks. **What it cannot
attribute is named, not hidden:** at the backstop a real leaker beside a sibling that makes
many small cycles is under-counted (never over-counted), and the runtime cap stays the
backstop for that. ASSURANCE G7.22, G6.6's GAP closed, F2's row, §16; AUDIT_M-SES §3's first
row closed; THREATS T11; SPEC; OPERATOR_API.

**v5.121 (in place — evidence from a failed control; warm speculation off; the sweep
widened):** three items of the same plan. (1) `verify-negative-controls.sh` keeps every run's
`prove -v` output, and the test's own directory when a row fails to hold or cannot run, under
`$VNC_EVIDENCE` (the pack sets it inside its output); a row that holds cleans up. The broadcast
fuzz asserts that at least one worker RECEIVED a broadcast — the flake that spoiled two
signable pack runs (the UBSAN misaligned-read control "not holding" because no worker received
anything) is now a failing assertion carrying the per-worker counts. (2) Every warm-recompile
value speculation in the vendored engine is off behind `JIT_WARM_VALUE_SPECULATION 0`, after
reading each site: P45b `get_field` and P46 `get_array_el` substitute 0 on a miss (P45b's IC
hit reads with no tag check), P49 `get_var_ref` and P51 `add` read a value with no tag check,
P52 `put_var_ref` skips freeing the old value; P48 and P50 are sound (the codegen's own stack
types). The codegen is handed no hints and no warm job is queued. Unreachable here twice over;
carried to the fork. (3) `t/comcon_oom_sweep.t` has eleven shapes: F18's at a 16-byte step
over 64 alignments, include inside a parent that filled its allowance (mostly `null` — the
error itself unbuildable), an admission `tests` that fills memory (refused, coded), a rejected
promise's reaction filling memory in the settle loop (the fragment's `queued` returns); 80
assertions, both tiers, no worker died. Not swept: the stream surface. ASSURANCE G7.21, §16.

**v5.120 (in place — the negative-control debt paid; every row automated):** twenty rows both
signatures accepted as manual — inverse patches that no longer applied because later commits
rewrote the lines, controls that were never a commit revert, and fixes in `quickjs/` outside
what the script reverted — are now MAINTAINED reverse patches under `t/tools/controls/`, each
the smallest change that brings its defect back (a generation check dropped, a range check
skipped, a freeze made a no-op, the drain of leftovers commented out, the heap walk restored,
the bounds' `min()` removed, `author.include` refused, the marshal letting an object cross,
the JIT's catch dispatch ignoring the flag, the compartment teardown skipped, the COM classes
unregistered in the compartment, the backtrace annotation holding no reference, the half-typed
xor emitted as or). `verify-negative-controls.sh` applies and verifies them exactly like the
commit rows, rebuilds the engine for a `quickjs/` patch, runs a leak row under `objs_asan`
looking for the named frame, treats a skipped test as INCONCLUSIVE, and fails the run when a
patch no longer applies — so the debt cannot accumulate silently again. F19's row is a commit
revert over both binaries. The reviewer pack prints INCONCLUSIVE rows instead of a MANUAL list.
**30 rows, 30 verified** on the first full run (three re-crafted on the way: a patch that
commented a call out and hit `-Werror=unused-function`; F18's, which needs BOTH halves of the
fix absent — the `stack` guard alone keeps the freed object untouched; and copy-vs-rewrap,
whose by-hand row COULD NOT FAIL: the `/stale` arm never reused the closed socket's slot, so a
re-wrap and a copy both threw — the arm now hands the slot to a new socket and a re-wrap reads
the stranger).
Also, from step 4 of the same plan: maxim's warm element hint, which substituted 0 for the
value on a type miss, is switched off in the vendored engine — unreachable here twice over
(include-time compile, no compile thread in a worker), so hygiene, not a fix, and no test can
reach it; carried to the fork. REVIEW.md §3, AUDIT_M-SES §5, ASSURANCE G7.19–G7.21's GAPs, §16.

**v5.119 (in place — the residue sweep as a battery, and F19):** F18's class lives in a byte
window a sanitizer moves, so no sanitizer corpus covers it; `t/comcon_oom_sweep.t` makes the
sweep a standing instrument on both tiers — seven places the allowance can bite (inside
try/catch, through finally, inside a generator, inside a microtask after await, inside a
sub-fragment with the parent catching, inside the host's marshal of the result, inside the
catch handler's own allocation), 32 alignments each, every fragment authored at config phase so
the compiled arm lowers it (the first version authored them per request, ran interpreted, and
its own tier assertion said so), 54 assertions: no worker died anywhere. **First run found F19:**
the host's ToString of a fragment's error ran out of memory itself, reported `error` for an
out-of-memory it could have named, and left ToString's exception pending on the compartment;
fixed in `ngx_js_comcon_exc_text`. ASSURANCE G7.21, F19's row, §16; AUDIT_M-SES §2b.

**v5.118 (in place — M5.1b PARKED with its numbers; the M5 track closes at M5.1a):** user
decision 2026-09-15 on the analysis in PERFORMANCE §2f.1. (B) declared parameter shapes at the
boundary: class A's remaining gap is 2.1×, below the rule's 3×, and the word would add a second
source of truth for a value's type (an entry guard, a refusal, the closed-set machinery).
(A) a host typed view of request bytes: the request crosses as JSON, so it needs a new channel
and an opt-in word; its gain is 12.5 ns/byte over a `charCodeAt` scan — 2.5 µs for a uri,
noise against the 1.1 µs request boundary, 200 µs for a 16 KB body — and no measured policy
scans bodies in JS (E0: the real ones are header/uri/shared bound, 0.92–1.36×). A product
question; the shape is fixed in §2f.1 should it appear (one capability word, copy-backed,
never a zero-copy view). ROADMAP POSITION, ASSURANCE §16.

**v5.117 (in place — M5.1a, the narrow compiler's first cut: class A's lowered scan 11.72 →
1.26 ns/byte, nothing assumed):** two codegen changes in the engine, `JIT_CODEGEN_VERSION` 18.
(1) A bit op (`& | ^ << >>`) with ONE provably-numeric operand yields a typed int32 — by the
spec, not by speculation: ToInt32 applies to both sides, the only non-int32 result needs both
operands to be BigInt, and Number-with-BigInt throws; the inline path runs only when both are
int-tagged at run time, otherwise the same runtime as the boxed path is called and its int32
unboxed. An int accumulator therefore survives `h ^ u8[i]`, which is where the whole 19× was
lost. (2) An in-bounds element of an integer typed array is read in place, bounds-checked
against the count the engine keeps current (0 when detached). Rejected: loop versioning (an
out-of-bounds read is `undefined`, needing a deoptimisation) and feedback speculation (maxim's
warm hint substitutes 0 on a miss). Recommended first as versioning; the closer look found the
spec already gives the accumulator its type. Four SR-2 cases with the spec's values written out
(65 mixed-operand results, nine typed-array kinds, the BigInt throw, the class A shape), the
run validated by breaking both paths (2 of 53 fail). G7.18's ten probes and F16's probe
unchanged. **A correction to §2e's record:** the class B "typed bound" was lowered JS and moved
with this change (20.16 → 13.75); the fragment's own number did not (50.0), so class B stays
re-parked. ASSURANCE G7.20, §16; PERFORMANCE §2f; SPEC §8; a MANUAL negative-control row. The
patch is carried to the engine fork.

**v5.116 (in place — a second signature on `4a86d2a62`):** the reviewer pack run in full,
twice, on the tree that carries M5.0 and F18. Run 1 failed one automated negative control
(`d3a438051`, the broadcast misaligned header read) on the known broadcast-fuzz flake — the
fleet's receive path was not reached, so the reverted read went unreported; the row re-run alone
holds. Run 2 passed every gate: 355 files on both binaries, sanitizers 0 in `src/js`, negative
controls 7 verified / 0 failed / 2 inconclusive. Both transcripts are committed under
`reviews/`. A second signer, Dick Hardman, accepted the evidence and the residuals: `REVIEW.md`
§4 (the row's "I ran it myself" clause struck, because the pack was executed by the authoring
session at the signer's request), ASSURANCE §15's second table and F11's row, AUDIT_M-SES §5's
third row, ASSURANCE §16. **F11's reproduction half stays open**; what closed is that two names
now accept the same residuals on a transcript of the whole evidence.

**v5.115 (in place — F18: an out-of-memory inside the engine's own backtrace annotation freed
the pending exception; a fragment-reachable worker SIGSEGV at the allowance, closed in the
engine):** found by the gate for M5.0's commit — `t/comcon_author_basic.t` `/nestmemory` killed
the worker 3/3 on this layout, never under ASAN or valgrind (a sanitizer moves where the
allowance bites). The engine defers an error's backtrace when the throw happens in bytecode and
adds it at the interpreter's exception label by `build_backtrace(ctx, rt->current_exception, …)`,
holding no reference; the annotation allocates, at the allowance an allocation throws
out-of-memory, `JS_Throw` releases the error being annotated, and the annotation defines `stack`
on a freed object. On the same path an uncatchable deadline abort would have lost its flag.
Fixed with `build_backtrace_pending`: a reference held across the annotation and, if the attempt
threw, the original error and its flag put back minus `stack`; the parser's two sites use it
too; `build_backtrace` stores no exception-tagged `stack`. `t/comcon_oom_backtrace.t` sweeps the
allowance across 32 alignments of a 1 KB fill so the window is hit on any layout (28 of 32 hand
the catch the error), and was validated against the unfixed engine. ASSURANCE G7.19, F18's
ledger row, §16; AUDIT_M-SES §2b; THREATS T13's line; a MANUAL negative-control row. The same
patch is carried to the engine fork.

**v5.114 (in place — M5.0, the go/no-go benchmark: class A GO at 19×, class B NO-GO at 2.5× —
M5.1's target is the byte-scan shape, not policies):** the measurement the M5 order was built
to reach. `t/tools/m5-go-nogo.t` (against `objs_jit`, in-process, §2b's controls) measures the
two classes SR-2 already holds, four arms each, with the rule stated before the numbers:
`lowered / typed ≥ 3 → GO`, where `typed` is the ACHIEVABLE typed-shape bound — a gas-checked C
pointer walk for a byte scan, one engine read per character for string code — and never the
floor. Class A, byte-scan validation: typed 0.61 ns/byte, lowered 11.72, **19.2×, GO**. Class B,
the token check: typed 20.16 ns/char, lowered 50.00, **2.5×, NO-GO** — string code reads its
characters through the engine whatever its types, and the allocations around the loop are
engine work no lowering removes. Two things the numbers settle: the gas check costs nothing
(0.61 vs 0.57), so G7.18's gate is not the price; and untyped lowering buys the data plane only
1.4× over the interpreter, so the prize is entirely in the typed shape. **M5.1 is therefore a
narrow compiler** — typed-array element access and int32 accumulators, the byte load and the
int op with the gas check kept — under the SR-2 differential for every case it lowers and under
G7.18 for every gate it could erase. The "compile the policy" reading of M5 is re-parked with
its number, as the record said it would be. One control fired on the way: a JS `*` is a double
multiply, so class B's FNV was not the C kernel's until `Math.imul` — fixed in the benchmark
and in the SR-2 case alike. PERFORMANCE §2e; two `nginx.bench` kernels (`scanTyped`, `fnv`).

**v5.113 (in place — M5 step 3: M8's harness is the SR-2 differential, and it gains M5.0's two
fragment classes — and a hole):** "T2 refines T1" (SEMANTICS §3 (F), SPEC §8) already had its
instrument: `t/comcon_include_faithfulness.t`, SR-2 for include — every representative fragment
run on both binaries from one file, compiled response == interpreted, compiled denials ==
interpreted, and the NATIVE line asserted so the compiled arm is really compiled. M8's harness is
that file; the typed cases arrive with M5.1. What arrives now is M5.0's two candidate fragment
classes — a byte-scan validation loop over the request (class A) and a string-heavy token check
(class B) — so the fragments the go/no-go benchmark will measure are under the differential
FIRST: whatever the compiler later does to them must keep producing the interpreter's bytes.
Both lower today (`NATIVE (1 of 1)`), both agree.

THE HOLE, found on the way. The first version of the two cases had newlines inside the fragment
source, which the harness embeds in a single-quoted JS literal: a syntax error at include, on
BOTH tiers, identically — and 27 of 28 assertions passed, because "compiled response ==
interpreted response" is true of two arms that fail the same way. Only the non-vacuity gate
(the NATIVE line) noticed. The harness now asserts, per case, what a correct response LOOKS LIKE
(`expect_re`, or at least a non-empty body without the include error's signature): equal is not
enough. Recorded as an instrument fix, because a differential that can pass on two identical
failures is the dead-probe class this tree has been burned by before (V14/V9, the dead-probe
sweep). ASSURANCE G4.1 addendum.

Steps 1–3 of the M5 order are done; next is M5.0 — the benchmark that decides go/no-go.

**v5.112 (in place — M5 unparked: the ground is prepared first — a second signer's pack, and
the compiled tier's resource gates as a standing battery):** M5 (typed lowering to maxim C) is
unparked on the user's decision, and its order is deliberately evidence-first: (1) a second
signer for SR-4, (2) a battery for what a fragment can REFUSE TO STOP DOING on native code —
F16's class, which the S6 battery cannot see — (3) M8's "T2 refines T1" as a differential
harness over typed fragments, (4) M5.0, a benchmark that decides go/no-go (the record says
compiling a host-call-dominated policy buys ~1.0×; only a typed-shape arm reaches parity),
(5) M5.1, the codegen, only if M5.0 says yes.

STEP 1. `t/tools/reviewer-pack.sh` ran in full on the tree as of v5.111: every gate green
(`t/` 353 files on `objs`, `t_stress`, three checkers, sanitizers 0 in `src/js`, negative
controls `verified 7 / failed 0 / skipped 2`, 18 MANUAL rows printed) — except two rows the
authoring session caused itself by writing an unfinished test into `t/` while the pack was
running, after its dirty-tree check had passed: `check-assurance` saw the file uncited and
the JIT suite ran it unfinished. That run is not a signer artifact. The pack is re-run on the
committed tree, and its transcript is what a second signer attaches (REVIEW.md §4). Lesson,
recorded: nothing is written into `src/js`, `quickjs`, `t` or `js_comcon` while the pack runs.

STEP 2. `t/comcon_compiled_resource_gates.t` (36, two arms from one file): the deadline through
`try/catch`, catch-and-spin, `finally`, a compiled generator, async before and after its first
`await`, and a compiled sub-fragment inside a compiled parent's `try/catch`; a `uses` budget, a
redacted field and the memory allowance in compiled loops. Every probe agrees with the
interpreter, verdict and denial counters; every deadline probe runs to its 200 ms meter; the
compiled arm is proven compiled. Nothing widened. One fact surfaced: **async fragments are not
lowered at include time** — `compiled == 0` on the JIT arm although the codegen handles async
bodies — so an async policy runs interpreted on the compiled tier. Recorded as the battery's
expectation and as a thing M5.0's benchmark must know. ASSURANCE G7.18; §16.

**v5.111 (in place — `subFragments` becomes a LIVE count: a dropped sub-fragment releases its
slot):** the plan's D8 deferred sub-fragment slot release with `subFragments` as a lifetime
count; the user chose the live count. The callable a sub-fragment is returned as is now an
object of its own class — a `call` handler, so `typeof` says "function" and `f()` works, and a
FINALIZER, which is the reason: when the parent drops its last reference, the fragment's slot is
freed and the count refunded, immediately (QuickJS is reference-counted; no collection is
waited for). The callable holds a reference to its author capability, so the refund always lands
on live memory; the slot release checks the compartment is still there, because at worker exit
`ngx_js_comcon_teardown()` empties the frags array before the runtime's finalizers run. A parent
that authors per request and drops the callable spends nothing lasting; one that caches eight
holds eight; `author.used` reads what is held now. Every test that relied on the lifetime
semantics had to change — which is the right kind of evidence that the word changed: the budget
arm holds its callables in an array to exhaust the count and then drops the array to watch it
refund; the golden `E_AUTHOR_LIMIT` probe must HOLD its first callable to be refused a second;
the audit file's witness that a leftover admitted nothing moved from the counter (now trivially
still) to the include's own log line. SPEC §8a, OPERATOR_API §8j, MANUAL §3.8, SHOWCASE17 §8,
THREATS T13, ASSURANCE G7.14 re-worded; §16 row.

**v5.110 (in place — F17, found by turning leak detection on: a worker never freed the
compartment, and the compartment's COM node classes had no finalizer — the sanitizer corpus
now detects leaks):** step 2 of the remaining items was run once under ASAN with
`detect_leaks=1` — the corpus runs `detect_leaks=0` — and reported 4 KB the sanitizer gate had
never seen. Pulling that thread found two defects and a blind spot.

(a) `ngx_js_exit_process` freed the tenant runtime and the host runtime and never the
compartment: every worker that had ever included a fragment exited holding it. One
`ngx_js_comcon_teardown()` now serves the three places a process lets go of its copy — reload,
master exit, worker exit — where before there were two identical blocks and one missing. And
freeing the compartment where fragments actually RAN makes `JS_FreeRuntime`'s own assertion a
leak check of every invocation path: over 84 files it held. No wrapper, job, result, re-grant or
sub-fragment leaves a live JS reference behind.

(b) With the corpus leak-detecting, one file still reported: `comcon_v12_denial_codes.t`, whose
audit-mode row calls `listener.serverByName()` inside a fragment. `ngx_js_com_register_classes()`
allocated every COM class ID for the compartment runtime and `ngx_js_com_install_protos()` gave
each a prototype, but `JS_NewClass()` for the node classes — `NginxServer`, the per-module nodes,
the upstream classes — happened only in the host's `ngx_js_com_init()`. A class ID with a
prototype and no definition still mints objects, and they are freed with NO FINALIZER: opaque
and 4 KB dynamic-location pool leaked per call, unboundedly, from a fragment, in audit mode
(T11). The registration now happens in `ngx_js_com_register_classes()` for every runtime it sets
up; the host's separate call is gone.

THE BLIND SPOT. The corpus ran leaks-off for a real reason: one by-construction allocation — the
SharedWorker manager thread's per-iteration `pollfd` array, live because the thread is parked in
`poll()` at exit — made every report noise, and G7.10's narrow leak stage was the only leak
instrument. That reason is now a one-line suppression with its reason beside it
(`t/tools/lsan.supp`), the corpus runs `detect_leaks=1`, and a leak block is classified like any
other report: a `src/js` frame makes it ours. The narrow stage stays for the specific claim it
makes. What the upgraded instrument reports today: zero `src/js` blocks over 84 files; one file's
single-process exit allocations in nginx core, reported and not failed. ASSURANCE G7.17, F17's
ledger row, §16.

**v5.109 (in place — the remaining items, one at a time: the engine debt paid to the fork;
the author capability's gates probed in both postures):** two of the open items the close-out
listed, done.

THE ENGINE DEBT. The four engine files the vendored `quickjs/` had accumulated since the last
subtree pull — `JS_GetMallocSize`, `JS_GetOutOfMemoryCount`, `js_comcon_is_single_toplevel_
closure`, `JS_IsUncatchableException`, V14's reproducible translation-unit name, F16's guard,
`JIT_CODEGEN_VERSION` 17 — are carried to the pilgrim-quickjs fork's `pilgrim` branch, at byte
parity, under its own self-gate (`make CONFIG_JIT=y test`). `run-test262.c` was NOT carried:
the fork's copy is newer (its glibc backtrace fix) and flows the other way, on the next pull.
The fork commit is local; nothing was pushed.

THE GATES IN BOTH POSTURES. G7.14's gap said the author capability's foreign-owner gate was
not probed under audit, because nothing can carry the capability to another fragment. It can
be reached, through the one path that runs a fragment's code as someone else: a LEFTOVER,
drained as nobody by the next invocation (G6.16). `t/comcon_author_audit.t` (15) arranges ten
leftover `author.include()` calls and finds what v5.96 already decided for every wrapper:
`cap.owner` is UNCONDITIONAL — fired in enforce and in audit alike, admitted in neither,
`unconditional=1` on the record. The capability's other first question, `cap.expired`, is
ordinary — a 1-second capability granted at request time (the clock starts when it crosses,
which is why granting it at config phase measured nothing) denies after 2 s in enforce and is
logged-and-allowed in audit, three times, once per gated operation. Two behaviours, each the
one the other wrappers have.

**v5.108 (in place — the authoring tier, phase 4: the close-out — the theorem's nested step,
the nested cost, the words, the controls):** nothing new ships; what shipped in v5.105–v5.107
is now stated where a reader looks for it, and every claim names its control.

THE THEOREM. SEMANTICS §3's proof sketch gains the (AUTHOR) case: `ρ_sub` is the parent's
environment restricted to its grants and narrowed per name by copy-then-narrow, so (MEDIATE)
per name, (ADMIT) and (EXEC) bound a sub-fragment by `A*(ρ_parent)`; values cross as text, so
the runtime-passing clause of (EXEC) is vacuous by construction. Assumption (F) — compiler
faithfulness — now names F16's class: the lowered C must simulate the RESOURCE gates (the
interrupt, the allowance) as well as the authority gates, and the S6 battery, which asks what
a fragment can reach, cannot see a failure of the first kind.

THE COST. PERFORMANCE §2d: one nested invocation is ≈ 0.59 µs on top of the outer boundary's
0.72 µs (`t/tools/confined-invoke-cost.t`, extended) — less than the host boundary, as its
shape predicts: JSON and the push/pop, no compartment enter/leave, no posture, no settle loop,
no drain.

THE WORDS. SPEC §8a is the normative statement (the closed set of eight sub-fragment contract
words, the three attenuation words, the refusals); OPERATOR_API §8j and MANUAL §3.8 are the
host's and the reseller's views; THREATS T13 is the adversary who holds an author capability;
SHOWCASE17 §8's reseller scenario is rewritten around what was actually built (with its two
honest limits: a facet's route glob has no meet, and depth is two); INCREMENT_MLIB §4's "raw
operators withheld" records that the second tier arrived, and not as `comcon.std.*`.

THE CONTROLS. Phase 1's `min()` on the deadline and the allowance is pinned where nesting
exists (`t/comcon_author_basic.t` `/nestdeadline`, `/nestmemory`: a sub-fragment asking for
3 s inside a 500 ms parent is aborted at 500 ms; one asking for 16 MB inside 2 MB runs out at
~109 × 16 KB); a sub-fragment's queued job is pinned running in the host's drain after the
parent returns (`/jobs`). `t/tools/verify-negative-controls.sh` gains one automated row —
`37b3c2057`, phase 3: revert its `src/js` half and `t/comcon_author_regrant.t` fails, verified
— and five MANUAL rows with exact instructions: phases 1 and 2 (later phases rewrote their
lines), copy-vs-rewrap, the nested marshal, and F16 (in `quickjs/`, outside what the script
reverts). AUDIT_M-SES §2b carries F16 and the tier's controls. ASSURANCE G7.14–G7.16's gaps are
updated, and §16 has the row.

The five-phase plan is complete. What remains open is listed, not implied: `uses`, `window`,
`cosign`, `protocol` cannot be written by a sub-fragment contract; a facet cannot be
narrowed from inside; depth is two; the audit-mode probing of the author's foreign-owner
gate; releasing a sub-fragment's slot.

**v5.107 (in place — the authoring tier, phase 3: re-granting, by copy and never by
re-wrapping):** a sub-fragment contract may now carry `grants: {name: cap}` and `attenuate:
{name: {allow|redact: [fields], ttlSeconds}}`. The parent passes the wrapper objects IT was
granted; each becomes the sub-fragment's by COPY-THEN-NARROW — the parent's opaque is
duplicated, generation, budget, window, cosignature and glob included, the owner becomes the
sub-fragment's, and only the mask (AND, `allow` asserted to be a subset of what the parent
holds) and the expiry (min) can move, downward. A(sub) ⊆ A(parent) is therefore a property of
the copy, not of a check.

WHY A COPY. Phase 0 found that `ngx_js_socket_wrap_bounded()` reads `gen` from the live
registry, so a child minted from the parent's HANDLE would be fresh and valid even when the
parent's wrapper is stale — its socket closed and the slot reissued. That is a laundering path,
and the copy closes it structurally: a stale parent yields a stale child, and a re-grant made
FROM a stale parent is stale too (measured: after the host closes the socket, the parent, its
cached sub-fragment and a fresh re-grant all answer the same way). Everything the copy inherits
verbatim is inherited for a reason: the budget is the same fleet-wide counter, so nothing is
spent twice; the cosignature is the same principal, so one wrapper is still one vote. A
session-typed wrapper is refused outright — its cursor is one conversation, and a copy would
be a second at the same position, a one-shot operation performed once per copy.

THE WORDS ARE DATA. A fragment has no `comcon.*` producers, so it writes `allow`, `redact` and
`ttlSeconds` as plain values; any other word is `E_CAP_FLAVOR`, for the reason the host refuses
an unknown word — an unrecognized attenuation once meant FULL authority. A field mask on an
outbound capability or a facet, a ttl on a facet, a plain object, an author capability, a
wrapper that is not the parent's own: each refused, none defaulted. Two of my first readings
were wrong and the evidence said so: `redact` is not `allow` (it only removes, so redacting a
field the parent never had asks for nothing, and asserting the subset for it refused a
legitimate narrowing), and "no mask word" means UNCHANGED, not ALL (asserting ALL ⊆ parent
refused every re-grant that only set a ttl).

BOTH ARMS AGREE: the host attenuating `allow(['address'])` and a parent re-granting
`allow: ['address']` from address+port give the identical view; a control arm that keeps `port`
differs from it in exactly that field. `t/comcon_author_regrant.t` (34). ASSURANCE G7.16. Phase
4 (docs, SEMANTICS §3's induction step, PERFORMANCE, negative-control rows) is next.

**v5.106 (in place — the authoring tier, phase 2: a fragment can author fragments — and F16,
found by it: compiled code could catch its own deadline):** `comcon.author({subFragments: N,
ttlSeconds?})` is a capability like any other that `include()` grants; inside the fragment it is
a NginxComconAuthor whose one operation, `author.include(source, {imports, intrinsics?,
checkRequest?, tests?, timeoutMs?, memoryBytes?})`, returns a callable SUB-fragment. The
sub-fragment is a real fragment — its own handle and identity, its own deadline and allowance
nested inside the parent's (v5.105 is what made that a push), the parent's posture — and it is
admitted by the SAME pipeline the host uses.

ONE PIPELINE, TWO ENTRANCES. `comcon.include()` was split into four stages that operate on
compartment values only — compile the wrapper without running it and check its shape (F15 phase
2), call it under a deadline (F15 phase 3), run the contract's tests, lower and publish with the
prediction check — and `author.include()` is a second entrance to those stages from inside the
compartment. There is no second copy of admission to drift. What a sub-fragment may NOT do is
refused rather than ignored: admission is mandatory (no contract, no `imports` — the author is
less trusted than the host); `grants` (phase 3), `deps`, `identity`, `onViolation`/`profile`
(the posture is the parent's), `meter` are each `E_ADMIT_CONTRACT`; a sub-fragment cannot even
NAME its parent's grant (`E_ADMIT_FREENAME`). WHAT CROSSES THE NESTED BOUNDARY IS TEXT — the
argument in, the result out, an exception as a fresh error carrying message and string `code`
only — because an object is a channel: a returned or thrown closure called by the parent would
run sub-fragment code under the parent's identity. A nested invocation is SYNCHRONOUS and drains
nothing (a promise is `E_INVOKE_PENDING`): the job FIFO is shared. A sub-fragment past its
DEADLINE aborts the whole invocation (the interrupt is uncatchable); one past its ALLOWANCE
raises an ordinary exception, as at the host boundary. `E_AUTHOR_LIMIT` names the budget and the
depth cap (2), and a string `code` raised inside a fragment now survives to the host as
`e.code`.

THE STOP CONDITION HELD. The S6 battery, one definition now composed two ways (`globalThis` is a
denied name no manifest re-admits, so an admitted fragment at either depth cannot carry that one
row), answers at depth 2 exactly as at depth 1, probe by probe, and the full battery is refused
identically by both entrances.

F16, FOUND ON THE WAY. On the JIT build the parent CAUGHT its sub-fragment's deadline abort.
Probed directly: a fragment lowered to native C could catch its OWN deadline — the interrupt is
thrown uncatchable, the interpreter's exception path honours the flag, and maxim's generated
catch dispatch never asked, so `try { for(;;){} } catch(e){}` swallowed the interrupt and ran on
(and the hostile form would loop forever). Fixed in the engine with a public getter,
`JS_IsUncatchableException()`, one guard in the generated dispatch, and `JIT_CODEGEN_VERSION`
16→17. The F5 battery could not see it: its runaway probe has no `try/catch`. Three of my own
mistakes were found by the evidence rather than by me — `%uD` is an nginx format, not the
engine's; a grant and the invocation argument sharing one name is the argument shadowing the
grant, which looked like a post-fork prototype bug for an hour; and a class ID allocated
lazily (with the first compartment) sat at 0 in the describe() registry, where 0 is also what a
primitive answers, so `nginx.describe('a string')` returned the author's table — the full
suite caught it, the registry now refuses an unallocated ID, and the ID is allocated with
every other.

`t/comcon_author_basic.t` (43), `t/comcon_author_depth2_gate.t` (13),
`t/comcon_jit_uncatchable.t` (5, both builds), the `E_AUTHOR_LIMIT` golden row. ASSURANCE G7.14,
G7.15, F16's ledger row. Phase 3 (re-granting by copy-then-narrow) is next.

**v5.105 (in place — nesting readiness: a confined invocation's bounds are a STACK, not a
constant — phase 1 of the authoring tier):** the second compartment tier (a fragment that
includes and invokes SUB-fragments through a granted `author` capability; the five-phase plan
is in ROADMAP.md's POSITION block) needs one thing the invocation path did not have: a nested
invocation must run inside its parent's bounds and give them BACK afterwards. This delta makes
the bounds behave as a stack before any nesting exists, so the property is the push's rather
than a promise every future caller has to keep.

THREE THINGS WERE CONSTANTS THAT HAD TO BECOME VALUES. (1) The memory allowance was restored to
a literal `64 * 1024 * 1024` at three sites (compartment creation, the leftover drain, the
invocation exit) — correct while nothing nested, wrong the moment something did: the first
nested pop would hand the ENCLOSING call the whole runtime back for the rest of its own run.
The engine has a setter and no getter, so the limit in force is now mirrored in
`jcf->comcon_mem_limit` and moved by a `ngx_js_comcon_mem_push()`/`_pop()` pair that restores
what it FOUND, and can only narrow it: a push asks for min(the limit in force, in-use-now +
allowance). (2) `ngx_js_comcon_deadline_push()` min'd a fresh deadline against the worker's
request deadline (G7.13) but not against the deadline ALREADY in force on the compartment —
never asked while the previous value was always 0, and the rule "a sub-fragment runs inside its
parent's remaining time" the moment it is not. It now mins against both. (3) A depth counter,
`jcf->comcon_depth` (`NGX_JS_COMCON_MAX_DEPTH`, 2), goes up with the fragment identity and
down with it; the settle loop and the trailing drain run at depth 1 ONLY, and the leftover
drain refuses to run at any depth, because the job queue is one FIFO for every fragment and a
nested frame that drained it would run the ENCLOSING fragment's jobs under the inner one's
identity — the deferred-job escape (G6.16) with the roles reversed.

NOTHING REACHES A NESTED INVOCATION IN THIS TREE, so nothing observable changes: every push
still finds 0 / the runtime limit, every pop still restores them, and the existing suite is the
evidence (the drain runs BEFORE the invocation's own push, so no site was nested even by
accident — checked before touching it). The tests that pin the stacked behaviour arrive with the
nesting that exercises them (phase 2). What this delta pins is the SHAPE: the only literal
runtime limit left in `ngx_js_module.c` is the host runtime's (`jcf->rt`), and the two
compartment bounds share one discipline — push what you found, restore what you found.

**v5.104 (in place — F15, phase 3: the compartment meters itself, whether or not a worker
exists — F15 CLOSED, all three parts):** phases 1 and 2 closed what the wrapper's shape allowed
to bypass; this closes F15's ORIGINAL finding, the one that opened the investigation into the
other two.

comcon_rt's interrupt handler used to require a worker before it was installed at all, because
it read `w->request_deadline_ms` — the same field the host runtime's handler reads. A worker
does not exist at CONFIG PHASE (`js_source` evaluation, including `nginx -t`), so anything
reaching comcon_rt there ran with NO interrupt handler whatsoever. MEASURED, each hanging until
killed: the wrapper's own body (its whole job is `"use strict";return(source)`, so a looping
source runs during `include()` itself, before the fragment is even admitted); a confined
invocation of an already-admitted fragment; and an admission test that calls the fragment it is
testing, since `tests` exists precisely to invoke it. Request-time paths were not unbounded — a
worker's own ambient deadline already covered them — but only as a side effect of sharing that
field, not by a budget of their own.

THE FIX gives comcon_rt a deadline that belongs to the COMPARTMENT, not the worker: a new field,
`jcf->comcon_deadline_ms`, checked by a new interrupt handler keyed on `jcf` rather than `w`.
`jcf` is a stable pointer across `fork()` — the same property that already lets `jcf->worker` be
set post-fork into a struct that existed before it — so the handler installs ONCE, at
compartment creation, with no post-fork re-wiring needed (the old worker-gated handler needed
exactly that, and the code for it is gone). A push/pop pair tightens and restores the deadline
around each risky call, min'd against the ambient `w->request_deadline_ms` when a worker exists
(so a fragment still cannot outlive its enclosing request) and defaulting to the fragment's own
timeout alone when it does not.

A PUSH AROUND THE WRONG CALL WAS FOUND AND CORRECTED THE SAME SESSION, BY TESTING THE CLAIM
RATHER THAN TRUSTING IT. The first attempt wrapped `JS_EvalFunction()` on the compiled wrapper —
but that call only MATERIALIZES the wrapper's closure (its whole job, per the three-opcode shape
phase 2 verifies, is "make one closure and return it"); it does not CALL it, so no
fragment-adjacent code runs there. The wrapper's body — where the looping IIFE that named this
finding actually loops — runs at the LATER call that invokes the materialized wrapper with its
grant arguments. Debug logging added to check the claim showed `JS_EvalFunction()` returning in
milliseconds with no exception every time, which is what said the push was on the wrong
statement rather than merely too generous.

`t/comcon_deadline_without_worker.t` (9). The config-phase case is driven by a DIRECT `nginx -t`
subprocess, deliberately outside Test::Nginx's own `run()`: that harness waits up to 5 seconds
for nginx's pid file, written only after `js_source` finishes, so a single 5-second config-phase
timeout already sits at that budget's edge. The three request-time cases run one per request for
the same reason in the other direction (`http()` carries an 8-second alarm). ASSURANCE G7.13 ·
F15's ledger row now closed, all three parts.

**v5.103 (in place — F15, phase 2: a fragment's own text cannot escape the wrapper it is
compiled inside):** phase 1 closed the corruption; this closes what let it bypass admission
entirely.

`comcon.include()` builds `(function(g0,...){"use strict";return(` + source + `)})` and
compiles the whole buffer as one script. Admission only ever inspected the RESULT of that
compile — the returned function — never the rest of the script that produced it. So a source
whose own text closed the wrapper early (an unbalanced `)}` inside what looks like a string,
comment or template literal) and supplied more script-level code afterward ran that code with
NO ADMISSION GATE APPLIED AT ALL, no matter how strict the contract asked to be: MEASURED,
`imports: []` — the strictest an operator can write — admitted a fragment whose escaped text
read another fragment's declared free names and reassigned a shared intrinsic for every
fragment, before phase 1's freeze existed to catch that. `{}` (no admission at all) fared no
better, which was already the documented behaviour for that path.

THE FIX COMPILES FIRST AND RUNS ONLY IF THE SHAPE IS RIGHT. `JS_EVAL_FLAG_COMPILE_ONLY`
compiles without executing anything, so a breakout's injected code cannot run before — or
instead of — being refused. A new engine helper,
`js_comcon_is_single_toplevel_closure()`, then checks the compiled unit's OWN bytecode: the
legitimate shape — one parenthesized function expression, nothing else at the script's top
level — always compiles to exactly three opcodes (`fclosure8`; `set_loc0`; `return`: create
the one closure, store it as the completion value, return it), verified against the
compiler's ACTUAL output for every shape that matters (zero params, several params, a
free-variable reference, an IIFE as the body) rather than assumed or re-derived with a second
parser. Only if that check passes does `JS_EvalFunction()` actually run it, producing the
identical result a non-breakout fragment always got.

A FIRST VERSION OF THE CHECK WAS INCOMPLETE, AND THE GAP WAS FOUND BEFORE SHIPPING BY TESTING
THE CLAIM RATHER THAN TRUSTING IT. Counting nested closures in the constant pool (must be
exactly one) is sufficient for the FRAGMENT wrapper — it is itself function-shaped, so
breaking out of it always consumes that closure and needs a replacement to keep the result
callable. It is NOT sufficient for `contract.tests`, wrapped in bare parens with no function
shape to consume: a comma expression can smuggle in a side effect —
`(1), (globalThis.__x = 1), (function(fragment){ return true; })` — with only ONE function
anywhere in it. Disabling the opcode check and keeping only the closure count reproduced
exactly that one gap and nothing else, which is how the final check came to verify the root's
own OPCODE SEQUENCE rather than stopping at a count that happened to work for the wrapper it
was first tried against.

The same protection is applied to `contract.tests` AS A REFUSAL, not a silent skip: that field
already had one silent-skip path (a string that fails to compile at all), and turning a
breakout into a second one would make a test that looks like it validates something quietly
not run — worse than a loud refusal for a phase whose entire point is verifying behaviour.

WHAT KEEPS WORKING, unaffected because it is entirely nested inside the one wrapper closure
regardless of its own internal complexity: an IIFE as the fragment body, a called IIFE with no
grants at all, many grant parameters, and a real `contract.tests` function.

`t/comcon_wrapper_breakout.t` (13, two controls: the whole mechanism reverted, and the opcode
check alone disabled) · ASSURANCE G7.12 (new leaf) · F15's ledger row now two parts of three
closed · enumeration check [2]'s PORTALS row updated (x2 → x4 call sites in the one function).

**v5.102 (in place — F15, phase 1: a fragment cannot reassign a shared global for every
OTHER fragment):** measuring F15's original finding (an unmetered top-level eval) before fixing it
turned up something worse.

M-SES-1 freezes intrinsic VALUES (`Object.prototype`, `Array.prototype`, ...) so a tenant cannot
pollute a shared prototype, and it deliberately never freezes globalThis itself — recorded, at the
time, as leaving room to add capabilities afterwards. What that also left was every BINDING
writable and configurable: the name `Promise` pointing at `Promise`, not `Promise`'s own
properties.

MEASURED: an ordinary, PROPERLY ADMITTED fragment body — `imports: ['Promise']`, no wrapper
tricks — did `Promise = function(){ return 'EVIL'; }`, and every OTHER fragment reading `Promise`
afterwards got the attacker's function. `imports` governs whether a name may be REFERENCED at all;
nothing asked whether the reference was a read or a write, and admission's own INTRINSIC category
is spelled "a value to compute with, not authority it acts through" — true of READING Math or
JSON, false of REASSIGNING them for every co-resident tenant. The same failure reached UN-ADMITTED
fragments too: `comcon.include(src, {})` skips admission entirely by design, so `JSON = {...}`
needed no declaration at all.

**AND A CLAIM §15 SIGNED WAS INCOMPLETE, NOT FALSE.** G7.6's cross-identity battery — evidenced
the same day as the signature — probed eight shared surfaces for cross-fragment channels, every
one a VALUE mutation (`JSON.__chan = 'x'`). None tried a BINDING reassignment (`JSON = evil`),
which is the ninth operation and the one that was open. Recorded in ASSURANCE §16 and in G7.6
itself, on the same model as the `Symbol.for` erratum it already carries: a battery measuring a
real mechanism against an incomplete set of operations.

THE FIX is a runtime, value-level protection: every binding present on the compartment's
globalThis is frozen (non-writable, non-configurable) once, at compartment creation, before any
dependency or fragment has ever run — closing both the admitted and the un-admitted path with one
mechanism, orthogonal to whether admission ran. globalThis stays EXTENSIBLE, deliberately:
dependency loading declares its own names via a plain global-code eval, repeated on every
`include()` call that names it for as long as the worker lives (no caching), and none of those
names exist yet at freeze time so nothing about that path changes — measured, five repeated loads
of the same dependency keep working identically. WHAT THIS DOES NOT CLOSE: a fragment that
explicitly imports `globalThis` and plants a brand-new name as a rendezvous — but `globalThis` is
already denied by admission even when listed in `imports`, so this residual only reaches
UN-ADMITTED fragments, which have no free-name gate of any kind by design; closing it needs a
private scope per fragment, which belongs with the runtime-per-tenant question, not this patch.

`t/comcon_global_binding_freeze.t` (8, one control) · ASSURANCE G7.11, G7.6 corrected in place,
F15 re-scoped and part closed · enumeration check [2] gained one PORTALS row for the fixed,
host-authored freeze script.

**v5.101 (in place — a confined invocation no longer costs what the rest of the compartment holds):**
step 1 of the proposal written after v5.100, and the first item in a while that was found by
reading code for a plan rather than by a test.

F14 — EVERY CONFINED INVOCATION WALKED THE WHOLE SHARED HEAP. It narrows the compartment's limit
to "allocated now + this call's allowance", and learned "allocated now" from
`JS_ComputeMemoryUsage()`: the right number, reached by walking every context, module and live GC
object — twice per call, on a heap every fragment shares. Measured on one worker, a handler making
one trivial confined invocation: **22.0% of stock throughput with nothing retained, and 0.2%
(476 req/s) while a different fragment held 200,000 objects.** After reading the same counter in
O(1) (`JS_GetMallocSize`, added to the vendored engine): 68.0% and 66.6%. In-process, 10.8 µs →
0.78 µs idle and 2,030 µs → 0.78 µs loaded.

WHY IT MATTERS MORE THAN ITS SIZE: IT WAS A CHANNEL. F8 was accepted on a measurement of CPU a
sender burns while the receiver waits, which the execution deadline caps. This medium is memory a
sender merely HOLDS — it persists across requests with the sender idle, and no deadline touches it,
because the walk ran inside the receiver's own call. Nothing had measured invocation cost against
heap size, and PERFORMANCE.md had no number for a confined invocation at all; §2c now has one, and
it is the baseline every future tier must beat.

TWO REPORTING DEFECTS, found chasing why a memory probe printed `undefined` (G6.19). At a hard limit
the engine cannot allocate the InternalError it means to throw and throws `null` instead, so a
fragment that exhausted its allowance reported `comcon: fragment: null` — identical to `throw null`.
The engine now counts out-of-memory throws and the host names them. And `include()`'s failure path
was `return fn`: `JS_EXCEPTION` from the COMPARTMENT context handed to the HOST, where nothing was
pending, so any top-level throw arrived as `typeof "unknown"` with no message at all.

F15 — FOUND, AND DELIBERATELY NOT FIXED IN PASSING. The fragment's top-level expression is evaluated
before admission, outside the tenant compartment scope, and unmetered: a looping expression hung
`nginx -t` until killed. No authority leaked — a grant read there is `undefined` — but because
`cap.owner` happens to refuse it (the grant is bound to a handle that does not exist yet), not
because the reach gate that should holds. Fixing it changes what a config-phase include may do,
so it is recorded as OPEN for its own decision.

`t/comcon_invoke_heap_independence.t` (3, gated as a ratio: 1.01×, control 173.57×) ·
`t/comcon_fragment_error_report.t` (7, two controls) · `t/tools/confined-invoke-cost.t` ·
ASSURANCE G7.10, G6.19, F14, F15 · PERFORMANCE §2c · THREATS T9.

**v5.100 (in place — a fragment's leftovers are charged to nobody):** the last named residual of
G6.16, and the smallest of the three items in the saved plan. It was also the one where the
INSTRUMENT was written from a model instead of a measurement, twice.

WHAT WAS OWED. Every invocation drains the compartment to quiescence before returning, so a
fragment's continuations are charged to the fragment that created them. That drain is best-effort: a
fragment which outruns the 10,000-job budget leaves work queued, and nothing can un-queue ordinary
JS. G6.17 closed the AUTHORITY half — `cap.owner` means a leftover runs and obtains nothing. The
ACCOUNTING half was still open: a stranger paid.

AND THE QUEUE ORDER IS WHAT MAKES IT A CHANNEL RATHER THAN AN ANNOYANCE. Jobs are FIFO
(`list_add_tail` to enqueue, `job_list.next` to dequeue), so leftovers run FIRST. Measured: with
12,000 jobs queued ahead of it, a fragment's ENTIRE 10,000-job allowance goes on a stranger's work
and its own continuations never run at all. That is the original escape's shape with the arrow
reversed — instead of one fragment reaching into the next invocation, one fragment SPENDS the next
invocation. They also ran on that fragment's deadline, and because the unhandled-rejection counter is
reset per invocation, a leftover that rejected was logged as *"this fragment's queued jobs"* against
a fragment that had never seen it.

MEASURING IT FOUND SOMETHING THE BACKLOG HAD NOT NAMED. `cap.owner` compares the capability's owner
against the fragment NOW RUNNING, so the same leftover was DENIED when a different fragment was
drained into and ALLOWED when its own fragment happened to be invoked again — with `ttl` and `window`
then evaluated at that later moment, under that invocation's posture. **Whether unfinished work kept
its authority was decided by traffic order**, which is worse than either answer held consistently.

THE FIX: drain leftovers at the START of an invocation, before it arms its deadline, narrows its
allowance, pushes its posture or claims its identity — under a job budget and a 50 ms deadline OF
THEIR OWN, the fleet posture, and nobody's identity. So an invocation's work belongs to that
invocation, and the authority answer stops depending on who is running. It runs INSIDE the
compartment, which is not optional: these jobs are fragment code, and running them between
compartment scopes would run them as HOST_ROOT with the A1 reach gate off — an accounting fix
becoming the escape it is tidying up after. And it runs as nobody BY CONSTRUCTION rather than by
assignment: `cur_frag` is zero outside any invocation and the nested-invoke guard is what establishes
that, so there is no `frag_set()` to get wrong.

THE INSTRUMENT WAS WRITTEN FROM A MODEL TWICE, AND THE MODEL WAS WRONG BOTH TIMES. The first probe
used one invocation queueing 20,100 jobs and asserted 10,100 would be left behind; its control did
not fire, because the fragment's own 16 MB allowance caps it at ~19,400 queued, so only 9,416 were
left — under the next fragment's 10,000 budget, which is exactly the condition needed. The second
used three invocations and left 9,000: still under. It takes FOUR, and that is now written into the
test as the reason it is the only place the defect is reachable. *A number asserted from an argument
is a number nobody measured* — and both times the control failing to fire is what said so.

`t/comcon_leftover_accounting.t` (9) · ASSURANCE G6.18 · G6.16's GAP closed.

**v5.99 (in place — the L4 filter window was not a wait state, and that was two defects):**
a flake hunt that found the flake, and found something worse on the way to it.

THE FLAKE. `t/js_pilgrim_p17_server_accept.t` failed about 2.8% of the time (7 in 250) with
`[alert] *10 open socket #3 left in connection 2` and `aborting` at graceful shutdown. Two wrong
theories were paid for first: that it was the COMCON work (it is not — the test uses no `comcon.*`
at all and every new gate no-ops when `owner == 0`), and that it was the L4 filters as such (an A/B
gave 2/250 without them against 7/250 with, which is not a refutation and was written down as one:
n=250 cannot separate 0.8% from 2.8%).

THE MECHANISM, and it is nginx's own design read correctly. Between `ngx_event_accept()` and
`ngx_http_init_connection()` a connection with an L4 filter armed belongs to `src/js`:
`c->read->handler` is `ngx_js_l4_read_handler` and the http module has never seen it. Stock nginx
never trips that shutdown alert for a connection awaiting its first request because such a connection
holds a NON-CANCELABLE `client_header_timeout`, and `ngx_event_no_timers_left()` makes the worker
DECLINE TO EXIT while one exists. The L4 window armed no timer, so nothing held the worker and a FIN
still in flight lost the race to SIGQUIT. `ngx_close_idle_connections()` is not the mechanism —
it only touches `c->idle`, which a waiting connection is not.

AND THE SAME MISSING TIMER IS AN UNBOUNDED HOLD. With `client_header_timeout 1s` and one
pass-through filter armed, a client that connects and sends NOTHING is still connected 5 s later;
remove the filter and nginx gives up in 1 s. So an unauthenticated peer pinned a connection slot and
its pool until reload by sending zero bytes — slow-loris with no loris, no header to dribble. That is
the more serious half, and it was invisible because the flake is what complained.

THE FIX IS TO STOP INVENTING A LIFECYCLE: arm the timeout nginx's own wait state arms, answer the
same two give-up signals it answers (`rev->timedout`, `c->close`), and mark the connection reusable
so `ngx_drain_connections()` can reclaim it. ONE deadline for the whole window, not one per read, so
a peer cannot extend a never-finishing filter by dribbling bytes. Result: **0 failures in 250 runs**,
and the deliberate version of the flake — park one connection and stop nginx — went from 100% to 0.

A THIRD DEFECT, FOUND BY THE ASAN LEAK STAGE WRITTEN FOR THE FIRST. Thirteen call sites in the two
pre-http windows closed with `ngx_close_connection()`, which does not destroy `c->pool` —
`ngx_event_accept()` creates it, so nothing else owns it there. Measured: 199 rejected connections
leaked 101,888 bytes, 512 each. The general rule now stated in `t/run_sanitizers.sh`: **code of ours
that closes a connection before `ngx_http_init_connection()` must use
`ngx_http_close_connection()`.** Reject is the worst possible place for it — an operator reaches for
it under attack, so the leak rate is the attacker's to choose.

THE INSTRUMENT WAS WRONG TWICE, both times caught only by a control that failed to fire, which is by
now the recurring shape of this work rather than an anecdote. An RSS-based leak test moved 0 KB over
3000 rejects (1.5 MB of 512-byte chunks comes out of already-mapped heap) and was deleted as a dead
probe. Its ASAN replacement then reported CLEAN with the fix reverted, because it grepped one line
either side of the `ngx_event_accept` frame while `in N object(s)` is the block header four frames
above — it summed nothing. And the L4 half of the widened stage did not fire either, because the new
test never reached the EOF path; 50 connect-and-vanish clients were added so the corpus reaches it.
Every control now fires: 300 pools on the reject path, 49 on the L4 EOF path, 1 on the timeout path.

Tests: `t/js_pilgrim_p17_l4_window.t` (8, with the control arm BUILT IN — `:8080` has no filter, so
it is stock nginx's own wait state and cannot be broken by a change to `src/js`; the assertion is
that the filtered arm AGREES), `t/js_pilgrim_p17_reject_leak.t` (7), and a leak stage in
`t/run_sanitizers.sh`.

**v5.98 (in place — a NOT-BUILT list is now enforced, and "cages nest for free" is measured):**
two halves of one habit: a doc set that IS the normative spec must not describe what is absent from
memory.

CHECK [8]: A NOT-BUILT LIST MAY NOT NAME A WORD THE CODE SHIPS. The rot it catches happened and
nothing caught it for three days -- the ROADMAP's POSITION block, the most-read paragraph here, went
on saying the posture vocabulary and `allowHosts`/`ttl`/`window` "need C-side enforcement" and that
`cosign` "needs an approval-recording protocol" after all five had shipped. Check [7] already fails
when SPEC.md is MISSING a word the code has; this is the same V7 rule from the other side.

IT PARSES ONE CANONICAL LINE PER DOCUMENT, NOT PROSE, and the first version did the opposite. That
version scanned the whole "what is not built" region for backticked words and asked whether each
line also said "shipped". It found the real drift -- and it also flagged a DATED bullet that
correctly recorded "seven of ten words ship" as of v5.85. That is history, and A CHECKER THAT ARGUES
WITH HISTORY TEACHES PEOPLE TO DISABLE IT. The POSITION block genuinely mixes current status with
dated records inside single bullets, so no rule over that prose can be exact. So the DOCUMENTS were
changed instead -- each carries one machine-readable line and the prose around it is free, because
it is no longer the list. That is the V7 move: a derived comparison, not a heuristic. (An earlier
version also swallowed the paragraph after the line, because a blockquote's continuation ends at the
next BULLET rather than at a blank line, and reported six drifts that were not there. A span that
grows silently is worse than no span.)

AND SHOWCASE17 §8's "CAGES NEST FOR FREE" IS HALF TRUE. Its reseller scenario has ACME caging its
own customers by calling env/grant/mediate/include from inside its own code. Measured
(`t/comcon_nesting.t`): `comcon` and `nginx` both read `undefined` inside a fragment -- with NO
contract, so a real read of the compartment global and not a free-name refusal -- declaring `comcon`
in `imports` does not conjure it, and granting it is refused with E_CAP_GRANT, because what may
cross into a compartment is exactly what the host can WRAP and the operator table is not a C-backed
capability.

It is in fact the OPPOSITE of something already asserted: a fragment reaching `comcon` is an escape,
and the S6 gate requires that probe CLOSED. So the boundary is:

    ATTENUATION NESTS, WITHOUT LIMIT.   AUTHORING DOES NOT NEST AT ALL.

A host can build cages as deep as it likes -- measured three levels, each only narrowing -- and hand
the innermost to a fragment. A fragment can build none, attenuate nothing it holds, and pass nothing
to anybody. The positive control matters as much as the refusals: without it they would read as
"nothing works here" rather than as a precise line. Until a second compartment tier exists (the
"raw operators withheld" increment), a platform must write a reseller's policy on their behalf --
the cages are real and nest properly, only the authorship is centralised.

**v5.97 (in place — the LOWERING CEILING is measured, and it reframes M5):** a full proxy makes a
new question askable: re-implement part of a hot path nginx implements in C -- a header filter, a
body step -- in JS, purely to get extra functionality into it. Affordable if M5 shipped with types?

`t/tools/policy-compute-split.t` answers the OLD question (are real policies compute-bound -- no)
and does not answer this one, because a hot path is not policy code: IT MOVES DATA. So
`nginx.bench` + `t/tools/lowering-ceiling.t`, in-process on objs_jit, with C offered at both -O and
-O2 because nginx builds at -O while maxim compiles its output at -O2/-O3 -- an asymmetry that
would have FLATTERED JS. Numbers in PERFORMANCE.md §2b.

UNTYPED LOWERING IS 8.3x OFF HAND-WRITTEN C ON ARITHMETIC AND 17x ON A BYTE SCAN. And
`--jit-dump-c` says why, which is the part that matters: the accumulator lives in a DOUBLE, and
every operation materialises two JSValues, tag-checks both operands at run time, keeps a
runtime-dispatch fallback, re-boxes, and writes a byte into a global type-feedback array. There is
no type information, so every op is a tagged-value op. THE GAP IS BOXING, NOT CODE GENERATION.

The test of that claim is a TYPED-SHAPE arm: the same algorithm with raw int32 locals and no
boxing, still inside maxim's framing and still paying the BACK-EDGE GAS CHECK that R4 needs. It
runs AT PARITY with hand-written C. So parity is reachable in principle and the entire prize is
whether inference can drop the boxing -- which is the M5 risk, now precisely stated instead of
assumed. The arm is a hand-written STAND-IN for typed output: it shows the framing costs nothing,
not that inference can prove `h : int32`.

THREE BELIEFS OF OUR OWN WERE CORRECTED BY MEASUREMENT.

The zero-copy `ArrayBuffer` view over nginx memory WORKS -- and BUYS NOTHING. It measures the same
as a copy, because the access path dominates and not the backing. One 16 KB copy is 0.2 us, i.e.
0.012 ns/byte, against 12.65 ns/byte to scan it: THE COPY WAS NEVER THE PROBLEM, contrary to an
estimate made from first principles before measuring.

A HOST CALL PER BYTE COSTS ABOUT THE SAME as a compiled typed-array read (20.4 vs 12.7 ns/byte).
An assertion written the other way round -- "a call must be several times a read, so the
representation is the decision" -- was refuted by its own data. In this engine a per-element read
is already priced like a crossing: the lever is not crossing less, it is not being boxed.

AND `policy-compute-split.t`'s KNOWN-POSITIVE CONTROL IS LOOP ELIMINATION. Its `s + i*3` shape
runs at 0.2 ns/iter in C and 0.4 lowered -- neither compiler runs the loop. That control still
does its job (it proves the harness can see a win, so a 1.0x elsewhere is a property of the
policy), but it must not be read as "compute-bearing code gets 13x from lowering". *An instrument
whose control is stronger than the effect it certifies will make every negative result look like a
property of the subject.* THE SHAPE MAXIM WINS BIGGEST ON IS THE SHAPE GCC DELETES.

The engineering consequence, quantified: for one 16 KB buffer, C scan 12 us, JS scan 207 us, one
host call 0.04 us. Letting C scan and handing JS the answer is ~17x cheaper than letting JS walk
the bytes -- the same propose-don't-hold pattern `allowHosts` and `std.config` already use. Today
that is not a stylistic preference, it is the only affordable shape.

**v5.96 (in place — `cap.owner` denies in every mode: the audit-mode residual v5.95 stated):**
v5.95 shipped the owner gate through the ordinary denial machinery, so in audit mode a foreign
capability was logged and ALLOWED like every other gate, justified as consistency with
`sock.mutate`'s ownership check. That residual is now closed rather than re-described.

THE LINE IS NOT "STRUCTURAL VERSUS POLICY". The A1 reach gates are structural too, and they are
audit-able on purpose: the onboarding story is that an operator switches to audit, watches the full
would-be-denied reach, and then enforces. The test is narrower:

    IS THERE ANYTHING HERE FOR AN OPERATOR TO OBSERVE AND THEN ENABLE?

Every other code in the set answers "MAY THIS FRAGMENT DO THIS?" -- a question about the GRANT. The
grant is the operator's lever, so watching a denial and then narrowing or widening the grant is a
real workflow. `sock.listener` and `out.drain` are both of that kind: the fragment reached outside
what its grant covers, and the operator can change the grant.

`cap.owner` answers a different question: IS THIS EVEN THIS FRAGMENT'S CAPABILITY? No grant can
change that answer. The only ways to trip it are a leftover continuation spending another fragment's
capability, or a bug in the binding, and neither is something an operator tunes. Allowing it in audit
hands out authority that no configuration asked for -- which is not observation, it is a different
policy, silently.

So it is counted and logged like every other denial and DENIES in every mode, and the LOG SAYS SO:
`mode=audit ... unconditional=1`. A line that reported the mode and not the action would tell an
operator the opposite of what happened.

The exception lives with the denial machinery rather than at the gates. One place says which codes
are unconditional, so a reader of `ngx_js_compartment_denial()` does not have to go looking for gates
that quietly ignore its return value -- and a future gate that needs the same treatment has somewhere
to say so.

THE TEST ASSERTS THE DISTINCTION, NOT THE BEHAVIOUR. Both halves run under the same audit mode in the
same request: a closed WINDOW (a policy) is logged and allowed, and a foreign capability is denied
anyway. Asserting only the second would pass on a build where audit mode had stopped working
altogether -- which is the shape of test this project has been caught by before.

`cap.owner` is now the one code whose behaviour does not follow `comcon.mode()`. That is a special
case, however well argued, and it is recorded as one: a second such code should force a list rather
than another `if`.

**v5.95 (in place — A GRANTED CAPABILITY BELONGS TO ONE FRAGMENT: the structural half v5.93 owed):**
v5.93 closed the deferred-job escape by draining every invocation's queued jobs, and said plainly
what that drain cannot do. It is BEST-EFFORT: a fragment which outruns the job budget leaves work
behind, and no bounded loop can fix that, because the leftover jobs are ordinary JS and nothing can
un-queue them.

SO THE QUESTION CHANGES. Not *can we stop the code running* -- we cannot, in general -- but *can we
stop it having authority*, which is answerable cheaply. Every granted wrapper (socket, outbound, COM
facet) now records the fragment it was granted to, and every gate asks that first. A leftover
continuation runs and OBTAINS NOTHING. THE DRAIN DECIDES WHO IS CHARGED; THIS DECIDES WHO CAN SPEND.

`cap.owner` is the first denial code in the set that names a STRUCTURAL INVARIANT rather than a
policy the operator wrote. Nobody configures it and nothing legitimate trips it: the host's own
wrappers are unbound, and a fragment's own wrappers match while it is running. It is checked BEFORE
the mask and before every other gate, because a capability that is not yours is not yours redacted,
budgeted or scheduled -- it is not yours at all, and that is the only question here whose answer does
not depend on what anyone configured.

THE BINDING IS A PREDICTION, AND THEREFORE ASSERTED. The wrappers are built in the grant loop while
the fragment's handle is not assigned until the push at the end -- by which time the wrapper values
have been released and live only inside the closure. So the handle is predicted, and then CHECKED
against the real one: a mismatch kills the fragment instead of publishing it. A wrapper bound to the
WRONG fragment would be worse than one bound to none, because the gate would then be enforcing an
invariant nobody holds while looking like it was working.

THE PROBE FORCES THE RESIDUAL RATHER THAN ARGUING ABOUT IT. A fragment queues 10,100 deferred
requests; the budget lets 10,000 through, attributed to it (32 recorded and 9,968 dropped, which the
outbound queue counts exactly); the remainder run inside the next fragment and add NOTHING while
counting as `cap.owner`. The same shape is run for a COM facet, because a facet is only ever holdable
by a foreign fragment through a leftover job -- so without that arm the facet's check would be code
no control can break, in a change whose whole subject is that.

And the other half of the control, which matters as much: the host reading its own socket, the host
spending its own outbound capability, ONE mediated capability granted to TWO fragments working for
both (each include builds its own wrapper), and a facet -- with `cap.owner` firing exactly zero
times. *A gate that fires on correct use is not a gate, it is an outage.*

AND THE BOUNDARY WAS IN THE WRONG PLACE, which a test from another increment caught. The identity
was restored when the CALL returned -- but SR-1 deliberately materializes the result INSIDE the tenant
compartment, so a getter on the returned object is fragment code running during JS_JSONStringify.
With the identity already restored, such a getter held capabilities that were no longer "its own",
and a fragment could not read its own grant from its own return value.
`t/comcon_include_sr1.t` found it because its escape probe distinguishes `null` (the reach gate
denying) from `undefined` (something else denying), and it suddenly got the wrong one. The identity,
the posture and the per-invocation allowance now all end where the COMPARTMENT does. *A boundary that
is in three places is a boundary you have to be reminded of by a test.*

What remains of v5.93's gap is ACCOUNTING, not authority: leftover work is still charged to a
stranger's deadline and budget, and still runs at a stranger's wall-clock time. And in AUDIT mode a
foreign capability is logged and allowed, like every other gate -- deliberate consistency with
`sock.mutate`'s ownership check rather than an oversight, but it does mean the one code here that is
not a policy still bends to a policy switch.

**v5.94 (in place — V10: the epoch machinery model-checked, and the defect it found):** the last
V-track item that did not depend on the parked compiler track. The mode fan-out is a fleet-wide
protocol over shared memory with concurrent writers, worker respawn and master reload; the formal
semantics is single-threaded, so MONOTONE ROLLOUT WAS A SLOGAN WITH NOTHING BEHIND IT.

Built as an exhaustive interleaving checker (`t/tools/check-epoch-model.py`) over the protocol READ
OFF THE JS BOOTSTRAP rather than an idealisation of it, and run from the suite like every other
standing checker, so drift is a build failure. Not a TLA+ or Spin spec: a model in a language nobody
here runs in CI is a model that stops being true.

THE MODEL FOUND A DEFECT. The epoch bump was three separate operations from JS -- `shared.get`, `+1`,
`shared.set` -- so two operators switching concurrently both read epoch N and both wrote N+1 WITH
THEIR OWN MODE. The second write won the cell; the first worker kept N+1 locally with the mode nobody
else had. That would have been a transient lost update HAD THE RECONCILER NOT EARLY-RETURNED ON EPOCH
EQUALITY. It did, so the worker and the cell agreed on the only thing the reader compares, and the
divergence was PERMANENT AND SILENT: a fleet moved to `enforce` could leave one worker in `audit` for
the rest of its life, unshielded, with nothing anywhere to say so.

AND THE FIRST FIX WAS NOT ENOUGH -- THE MODEL SAID SO BEFORE ANY CODE WAS WRITTEN. Relaxing the early
return from `==` to `<=` left the identical 168 violations, because no reading rule can repair a state
where the worker and the cell hold the same epoch. The publish had to become ATOMIC:
`ngx_js_shared_mode_publish()` does the read, the increment and the write in ONE critical section
under the shared store's own lock, exactly as the budget charge and the consent record already did.
The reconciler's change from "adopt on DIFFERENT" to "adopt on GREATER" is what makes the rollout
monotone -- a real property, and a different one: it stops a stale publish from walking the fleet
backwards.

THE MODEL KEEPS ALL THREE ARMS -- pre-fix, reconciler-only, shipped -- so the claim "the atomic half
is the necessary one" is CHECKED rather than asserted, and a model that reported no violation for a
protocol nobody had changed would fail its own run. That is the M-SES battery's unconfined arm,
applied to a model.

The live half matters as much as the model: a model nobody compared against the code is a paper
exercise. `t/comcon_v10_epoch_model.t` asserts the cell's SHAPE (`<epoch>:<mode>`, which the JS write
path would not produce, so the model cannot quietly start describing a protocol the code no longer
runs), and it plants an OLDER cell and a NEWER one -- the same probe both ways, so "an older cell is
ignored" cannot pass because nothing is ever adopted.

Still unmodelled, and said rather than implied: two-phase epoch groups (R10) and rollback, because
neither ships.

**v5.93 (in place — AN INVOCATION LEAVES THE COMPARTMENT QUIESCENT, closing a hole v5.92 opened
one day earlier):** a fragment can queue a job and return without awaiting it --

    function(a){ Promise.resolve().then(function(){
                     out.request('https://a.example.com/LATE'); });
                 return 'returned'; }

-- and nothing else in this process drains the compartment runtime, because the host's drains are a
different runtime. So that job sat pending until some LATER, UNRELATED invocation returned a promise,
and then ran inside it. Measured: the capability was untouched when the fragment returned and
exercised during the next fragment's settle loop.

EVERYTHING AN INVOCATION BOUNDS WAS THEREFORE THE WRONG INVOCATION'S. The deferred use ran on a
stranger's DEADLINE and MEMORY ALLOWANCE. It was gated at a stranger's wall-clock time, so `ttl` and
`window` were evaluated at the wrong moment. And it ran under a stranger's `onViolation` POSTURE --
so a shadowed fragment's deferred work could execute under an enforcing binding, or an enforced
fragment's under audit, which makes it a security property and not an accounting one. It is also a
channel: the first fragment spends the second one's job budget.

It was unreachable until v5.92 admitted async fragments, because a fragment that cannot name
`Promise` cannot queue a job. Widening admission opened it, so closing it belongs with that change
rather than in a backlog -- and it was found by probing the increment, not by a report.

So every invocation now drains inside the compartment scope, before the posture and the memory limit
are restored. THE FIRST VERSION CARRIED NO JOB CAP, on the argument that an invariant with a cap is
not an invariant -- and the argument is correct while the consequence was not affordable. An uncapped
drain over a self-queueing chain runs until a bound the operator set, and measurement says which one
is reached first: a `.then` chain exhausts the 16 MB per-invocation MEMORY allowance after 354,885
promises, long before a 300 ms deadline, while an `await` chain allocates slowly enough to reach the
REQUEST deadline instead -- turning a fragment that was refused in milliseconds into a ten-second
request the client abandoned. That was caught by the full suite rather than by the test written for
the change.

So QUIESCENCE HERE IS BEST-EFFORT, stated rather than implied. The drain shares the settle loop's job
budget, which is large enough to attribute every fragment whose continuations are bounded -- every
fragment that is not deliberately pathological -- and one that outruns it is REPORTED, loudly, with
its leftover jobs still able to run inside a later invocation. THE STRUCTURAL FIX IS OWED AND NAMED:
bind each granted capability wrapper to the fragment it was granted to, and have the gates refuse
when the fragment being invoked is not that one. Then a leftover job cannot use authority no matter
when it runs, and this loop is about attribution only -- which is all a best-effort loop can honestly
promise.

TWO OF THE FIX'S FIRST ATTEMPTS WERE WRONG, AND THEIR CONTROLS SAID SO.

The first logged a "job that threw", which cannot happen: every job in this compartment is a promise
reaction, and a promise reaction that throws does NOT fail -- the machinery catches it and rejects the
derived promise, so the job SUCCEEDS and the failure becomes an unhandled rejection. The assertion
written against the job's return value never fired. The report now comes from a rejection tracker,
and installing one revealed that NOTHING in this process had installed one on either runtime: every
unhandled rejection anywhere was silent.

The second restored the posture before the drain, and the posture assertion passed anyway -- because
the binding asked for `deny` while the fleet was in `enforce`, and restoring `enforce` over a `deny`
binding changes nothing. The two postures have to DISAGREE for the assertion to mean anything. A
control that cannot tell the fixed code from the broken code is not a control.

AND A v5.92 CLAIM WAS WRONG. It said the deadline is the real bound on the drain and the job cap the
belt. Measurement: a self-queueing promise chain ran 354,885 jobs and was stopped by the F2
per-invocation MEMORY ALLOWANCE, long before a 300 ms deadline could elapse. All three bounds are
real; the order was written down without being measured. *A bound nobody measured is a bound nobody
knows the order of.*

The S6 escape battery gains an ASYNC arm for the same reason the fix does: admission accepted no async
function before v5.92, so the gate had never seen the shape it now admits. The assertion is not that
nothing is open but that the async arm is IDENTICAL to the synchronous one, probe by probe -- the
question is whether the SHAPE changes what the cage allows, and only a comparison answers it.

**v5.92 (in place — ASYNC FRAGMENTS, and the blocker was not where the roadmap said it was):**
ROADMAP has carried "async fragment invocation" as the prerequisite for a real `fetch`, with the
blocker recorded as the SYNCHRONOUS INVOKE: JS_Call, then JSON-stringify, with no promise detection
and no job drain. That is true, and it is not where an async fragment stopped. IT NEVER REACHED THE
INVOKE:

    comcon.include: admission refused: admit: arg0 not a bytecode function

WHICH IS NOT TRUE OF THE THING IN FRONT OF IT. An async function, a generator and an async generator
are ALL bytecode functions -- the engine says so, in `js_class_has_bytecode()` -- they simply carry a
different class id. Six COMCON analysis entry points tested one id instead of asking the engine, so
the whole C3 analysis (free names, dynamic code, the request-field seal) plus the POM surface
REFUSED TO LOOK at an entire class of functions, and reported that refusal as a property of the
function. A fragment the analysis CANNOT read must be refused. A fragment it WILL NOT read is a
different and worse thing, because the message sends the operator to rewrite code that was never the
problem. One defect, six sites, one engine helper.

With that fixed the invoke half becomes reachable. An async fragment returns a promise, and a promise
has to be settled before anything can be marshalled out of it, so the invoke drains THE
COMPARTMENT'S OWN pending jobs. The compartment has its own runtime, which is the reason this is safe
to do at all: the loop runs the fragment's microtasks and cannot schedule a host job or another
tenant's continuation. The same loop over the host runtime would be a very different thing.

IT DRAINS MICROTASKS, NOT THE WORLD. A promise that only a timer or an outbound response could settle
stays pending however long the loop runs, and is REPORTED rather than waited on -- there is nothing
to wait for. E_INVOKE_PENDING, the one code on the refusal axis raised AFTER the fragment ran, and
deliberately so: a denial names a gate that refused authority the fragment reached for, and nothing
was refused here; what the tenant must change is in their fragment, which is what the refusal axis
names. The alternative was JSON.stringify on a pending promise, which is "{}" -- a
plausible-looking empty object, and the worst of the three available answers.

TWO BOUNDS, AND THEY ARE DIFFERENT BOUNDS. *(v5.93 — corrected by measurement:* this paragraph said
the DEADLINE is the real bound and the job cap the belt. For a promise-chain runaway it is the other
way round — 354,885 promises exhaust the per-invocation MEMORY allowance long before a 300 ms
deadline elapses. The cap is what makes the settle loop terminate promptly with a usable message; the
allowance is what stops an uncapped drain; the deadline is the outer bound on all of it. *A bound
nobody measured is a bound nobody knows the order of.)* The JOB CAP makes the loop's termination
obvious without reasoning about where the interrupt fires -- and it changes the MESSAGE, which is the
part that matters to an operator: the same runaway loop under a 30-second meter stops in under a
second and is told its promise never settled, rather than being told, thirty seconds later, that it
timed out.

SO THIS IS NOT `fetch`, and it is now clear exactly why not. Nothing in a compartment can settle an
await on real I/O; making one possible means suspending the nginx request handler across a fragment
call, which touches the F6/F12 deadline and the F2 per-invocation allowance on the most
safety-critical path here. The blocker has moved from "the invoke is synchronous" to "the host cannot
yet suspend", which is a smaller and much better-specified problem than the one the roadmap recorded.

**v5.91 (in place — the POSTURE words: a posture belongs to the BINDING, not the fleet):**
MANUAL has written `{profile:'restrictive', onViolation:'audit'}` since v5.0 and nothing read either
word. INCREMENT_MLIB §4 withheld them for a reason worth repeating: A POSTURE ASSEMBLED FROM IGNORED
KEYS WOULD READ LIKE A POLICY AND DO NOTHING, WHICH IS WORSE THAN ITS ABSENCE -- it would be
believed, and by exactly the reader least able to check.

What changed is that there is now something to be a posture OF. Ten mediation words enforce, and the
audit/enforce switch exists -- but only FLEET-WIDE, and that is the wrong granularity for the rollout
MANUAL describes: SHADOWING ONE TENANT'S NEW POLICY BY PUTTING THE FLEET IN AUDIT ALSO STOPS
ENFORCING EVERY OTHER TENANT'S, which is a strictly worse posture than the one the operator is
carefully trying to reach. Observe-first has to be a property of the BINDING, and it now is: one
request can hold a shadowed binding and an enforced binding at the same time, in either direction
relative to the fleet.

It can WEAKEN as well as strengthen, and that is acceptable only because THE CONTRACT IS WRITTEN ON
THE TRUSTED SIDE -- the fragment's source is untrusted, the contract around it is the operator's own
configuration. The same argument `cosign`'s `as` rests on, and the same warning attaches: do not
build a contract out of tenant-supplied data. The override is applied in C for one invocation and
restored afterwards INCLUDING ON THE EXCEPTION PATH, because otherwise one fragment that throws
would quietly unshield every later request in that worker.

`profile` IS READ BY BEING REFUSED where it cannot be honoured. 'restrictive' is what every
mediation here already is -- the vocabulary attenuates and none of it transforms -- so it means what
it says. 'adaptive' is refused rather than ignored: the transforming half has no implementation, and
accepting the word would make "this program runs standalone without COMCON" unfalsifiable for
exactly the fragments where that claim matters. E_ADMIT_CONTRACT, the code for a contract field that
is present but unusable; no new code was invented for it.

`std.postures.*` stays absent, with a sharper reason than §4's original. It is no longer that
nothing enforces. It is that WHAT `lockdown` SHOULD NARROW TO IS A DECISION NOBODY HAS MADE: MANUAL
says "writes: deny, exports: freeze", which needs a per-member mutating/reading split across a whole
environment rather than over one capability. Assembling it from the words that do exist would be
inventing policy and calling it a bundle.

**v5.90 (in place — M-LIB `protocol`, and THE MEDIATION VOCABULARY IS COMPLETE):** the tenth and
last of MANUAL's mediation words. A session type over a capability's own operations: `allow` says
which operations exist, `uses` how often, `ttl`/`window` when, `cosign` by whom, and this one IN
WHAT ORDER. `comcon.protocol('address','port*','fd')`; denial code `cap.protocol`.

Two consequences are not obvious and are therefore written down. `protocol('fd')` IS A ONE-SHOT
CAPABILITY -- once the last step is consumed the conversation is over -- and that is an attenuation
`uses(1)` cannot express: a budget is fleet-wide and resets with its window, a protocol is
per-wrapper and never resets. And the cursor is deliberately NOT fleet-wide the way a cosign record
is: a session type describes ONE conversation, and two holders sharing a cursor would interleave
into nonsense.

IT ENFORCES ORDER, NOT COMPLETION, and that is a limit rather than an oversight. "You cannot take
the fd before you have looked at the address" is checkable at the moment of the call; "you must
eventually close" is not, because a fragment can simply return and there is no event at which the
host could notice. A session type that silently enforced half of what session types usually mean
would be worse than one that says which half.

THE GATE'S POSITION IS THE DESIGN, and it is the first gate here that has to SEPARATE ITS DECISION
FROM ITS EFFECT. Every other gate's decision is also its effect: a budget charge happens when it is
decided, and a cosign consent IS the decision. So each of those can only ever be last. A protocol's
effect -- advancing the cursor -- can be deferred, and must be: an operation that a later gate still
refuses did not happen and must not move the conversation on. Checking after cosign would record a
signature for an operation about to be refused for being out of order; committing before the budget
would advance a conversation whose operation was never performed. So the transition is CHECKED
before cosign and COMMITTED after the budget. *A gate that mutates state must separate its decision
from its effect, or it can only ever be last.*

AND ITS TEST FOUND A DEFECT IN `cosign`. The first probe for the check/commit split could not see
it -- two principals meant two WRAPPERS with two cursors, and a control that moved the commit up to
the check passed. Rewriting it so ONE wrapper attempts twice, with the cosignature arriving in
between, exposed that an already-consenting principal was judged by its POSITION in the record
rather than the record's LENGTH: once "alice,bob" reached a quorum of two, ALICE RETRYING WAS STILL
DENIED, because she is first. That breaks the sequence an operations room actually performs --
alice tries, is told to find a cosigner, bob cosigns, ALICE RETRIES -- and the word was only usable
if the second person happened to be the one who performed the operation. Every cosign assertion had
the second principal perform it, which is exactly the shape that passes with the bug present. Found
by the test for a different word.

The mediation vocabulary is now closed and complete: ten of ten. `opaque.*` remains, and is the one
name that was never a mediation -- making a value usable-but-unreadable is an engine-substrate
question, not an attenuation of authority.

**v5.89 (in place — three defects found by asking v5.87's question one axis over):** probing each
mediation word ALONE, rather than only in the composition it normally arrives in, found a `window`
gap at v5.87. Asking the same question of each word ON EACH CAPABILITY KIND found three more.

ONE: `uses`, `ttl` and `cosign` normalize to an allow-everything MASK — they attenuate how many
times, how long and by whom, never WHAT — and a mask is the socket shape. So a bare one of those
over an OUTBOUND capability arrived at include()'s translation as kind 0 and was refused with
"grant is not a NginxSocket or NginxServer" for a grant that was a perfectly good outbound
capability. Fail closed, so nothing was ever widened, but the operator was told their capability
was the wrong type when the real answer is THE WORD CARRIES NO TYPE AT ALL. The kind belongs to the
CAPABILITY and is now read back from it. The promotion is scoped to grants that carry a mediation:
an unmediated outbound grant stays refused, because accepting one would be a widening smuggled in
under a bug fix, and the promoted wrapper gets the glob "*" (a mediated wrapper whose destination
set is everything) rather than the empty glob, which the wrapper reads as "this is the host's own
unmediated capability" and which would take the gates off the path.

TWO, and the material one: the outbound path did NOT namespace its budget key the way the socket
path did, so `uses('k')` on a socket and `uses('k')` on an outbound capability were TWO COUNTERS.
That contradicts a documented property — two capabilities share a budget exactly when the operator
names the same counter — so a budget an operator believed was one limit of 10 was two limits of 10.

THREE: `JS_ToCStringLen` on a MISSING property does not return NULL, it returns the nine-character
string "undefined". So a descriptor with no glob was wrapped with the literal host glob `undefined`,
matching only a host of that name — fail-closed by accident — while the refusal a few lines below
claimed to be what caught an absent glob and in fact never ran for that case. A guard that cannot
fire is indistinguishable from one that works until something needs it to.

The lesson is about the SHAPE of the question, not any of the three: *try the feature in the
position nobody writes it in.* v5.87 asked "alone instead of composed" and found one. v5.89 asked
"alone, on each kind" and found three.

**v5.88 (in place — M-LIB `cosign`, the two-person rule):** the ninth of the ten vocabulary
words, and the first that bounds WHO rather than when or how often — the only mediation a holder
cannot satisfy alone. `cosign({key, quorum, within, as})`; denial code `cap.cosign`.

THE HARD PART IS NOT THE COUNTER, IT IS WHO IS COUNTING. COMCON does not authenticate (§8b): the
host asserts the principal and COMCON maps it. So `as` is written by the operator's own
configuration on the trusted side, and there is NO PATH FROM INSIDE A COMPARTMENT THAT SETS IT. A
fragment therefore holds exactly one identity per invocation and can cast exactly one vote —
DISTINCTNESS IS STRUCTURAL, not checked. Had `as` been a string a fragment could write, the word
would have been theatre: one fragment voting twice under two names.

Which is why the quorum assembles ACROSS INVOCATIONS rather than within one: alice runs the policy
and is denied pending a cosignature, bob runs the same policy and it executes. That is what a
two-person rule looks like in an operations room, and it is why there is no `approve()` verb — THE
ATTEMPT IS THE CONSENT. The corollary has to be said out loud because it is the one surprising
thing here: a `cap.cosign` DENIAL IS NOT "NOTHING HAPPENED". The denied attempt recorded a
signature. It is the only denial in the set that is a WAITING STATE rather than a verdict, and the
only one with a SIDE EFFECT.

The record is the SET of consenting principals, never a count of attempts: one operator pressing
the button twice is still one signature, and a rule that counted attempts would be a one-person
rule with extra steps. It lives in `nginx.shared`, so it is FLEET-WIDE — two operators land on
whichever workers accept their connections, and a per-worker record would make the quorum
unreachable except by luck (the per-process mode switch of v5.56, repeated). `within` is FIXED and
ANCHORED AT THE FIRST SIGNATURE, the same shape and the same disclosure the `uses` budget makes
about its window.

NO `of:[...]` ALLOW-LIST, DELIBERATELY: HOLDING THE CAPABILITY IS THE MEMBERSHIP. Only a principal
the operator chose to hand a cosigned capability to can attempt at all, so a list inside the
descriptor would re-state in a weaker place what the grant already decided — and it would invent an
unsatisfiable-meet case (quorum greater than the intersected set) for nothing.

A THIRD LATTICE SHAPE. Masks meet by AND and lifetimes by MIN; `quorum` meets by MAX — needing MORE
signatures is the narrower authority — while `within` meets by MIN. Two narrowing directions in one
word, which is why they are written out rather than routed through a shared helper. The `key` and
the acting principal must be IDENTICAL or the meet is refused: merging two keys would let consent
given for one decision authorize another, and a capability with two acting principals would have to
vote as somebody. `E_CAP_PRINCIPAL` is the 14th refusal code and deliberately not folded into the
other two — a `cosign` with no `as` spelled the flavour correctly and composed nothing; it is
simply incoherent policy, and a tenant's CI wants to tell that from a typo.

GATE ORDER IS A DECISION: expiry, window, destination, cosignature, budget. Consent is never
recorded for an operation another gate would refuse — otherwise signatures could be gathered at
02:00, or against a destination this capability can never reach, and spent where it can.

AND A CONTROL CAUGHT A WRONG INSTRUMENT AGAIN. The expiry probe read `req.args.as`, but `req.args`
is the RAW QUERY STRING, not a parsed object — so both requests voted as the same principal, and
the probe passed identically whether the `within` meet took the shorter window or the longer one.
Five controls were run against this word; four failed as designed and the fifth passed, which is
the only reason the defect was found. *The measurement was fine. The thing doing the measuring was
not.*

**v5.87 (in place — M-LIB `window`, and the defect its probe found in the INVOKE):** `ttl`
bounds a capability by a countdown. THREATS.md wants the signing key bounded by a SCHEDULE, which
is a different shape: usable in hours, not for an hour. `window({days:'Mon-Fri', from:'09:00',
to:'17:00'})` is that, with its own denial code — `cap.window`, not `cap.expired`, because "has run
out" and "is outside its hours" are different operational facts and an operator paged at 02:00
needs to know which one they are reading. **Eight of the ten vocabulary words now ship.**

**UTC is a decision, not an oversight.** "Office hours" is a local-time idea, and the temptation is
to read the host's TZ. A gate that does cannot be tested identically on two machines and shifts
under a daylight-saving transition with nothing edited. So the operator converts once, where they
can see what they are doing, and the docs say so in those words.

**The two shapes a schedule gate gets wrong are the ones tested hardest.** `from > to` WRAPS
midnight — a 22:00–02:00 shift is real, and a naive `from <= now < to` makes it permanently closed.
`from === to` means the WHOLE of an allowed day, not "never", because an operator writing
00:00–00:00 means all day and a never-open capability is spelled by granting nothing. The day mask
is tested **by its complement**: a window naming every day except today, with hours open right now,
must still deny — otherwise a passing test could be ignoring the day list entirely. Every window in
the test is computed from the current time, so the file asserts the same thing at any hour.

**Probing each flavour ALONE found a gap that composition hid.** `mediate(cap, window(spec))` with
no mask beside it fell through to the unknown-flavour refusal — the feature refusing its own
simplest use — because the include translation only had branches for the shapes that arrive in
composition. The normal way to reach `window` is composed with `allowHosts`, which worked from the
start.

**AND THE PROBE FOUND A DEFECT IN THE INVOKE, which is the better half of this entry.** The window
probe returned a denied call directly — the most natural thing to write — and got
`SyntaxError: unexpected token: 'undefined' at <result>:1:1`. The invoke marshals a result by
stringifying it in the compartment and re-parsing it in the host, and `JSON.stringify(undefined)`
is `undefined`: not the string, not JSON text at all. That value went to the parser.

**`undefined` is not an exotic return value here — it is what EVERY DENIED GATE produces.** A policy
whose last statement reads a redacted field, or calls an operation its mediation refuses, returns
undefined by construction. So the one path an operator is most likely to hit *while tightening a
policy* was the path that reported an internal parse failure against a pseudo-file they had never
heard of. It survived because every existing probe happened to wrap its result: the V12 rows return
the string `'denied'`, the outbound probes return an object. `null` still crosses as null, which is
the distinction the fix must not erase.

**A feature's own test found a bug in the machinery beneath it, because the test was written the
way a user would write it rather than the way the machinery prefers.**

**v5.86 (in place — the outbound ROUND TRIP, a scheme that can be pinned, and two properties
that were documented before they were true):** v5.85 shipped the outbound capability and tested
its mediation exhaustively — the glob admits and denies, the reach gate holds, composition works.
**It never once tested the round trip.** Every assertion could have held while a queued intent was
unperformable, because nothing carried an intent through to a response.

`std.outbound.perform(cap, req)` is the host half. It takes a REQUEST because `fetch` lives on the
request object, so draining belongs inside a handler rather than at config time. Errors are
RESULTS, not exceptions: one unreachable destination must not abandon the others, and a policy that
asked for three things is owed three answers.

**The test is the deliverable.** A policy asks for three destinations — one of them
`169.254.169.254`, the link-local metadata address a confined policy most wants and least should
have, denied by the glob. The host performs the permitted two against a backend in the same nginx,
one answering 200 and one 503. The **same policy is then invoked again with the responses and
computes a verdict from them.** That is the loop the capability exists for, and two invocations is
not a workaround — it is the shape the synchronous invoke imposes.

**`protocol` is not what I assumed, and checking beat guessing.** The plan called for
"`protocol` — refuse non-TLS destinations". MANUAL defines `protocol("handshake", "frames*",
"close")` as enforced operation ORDER — a session type over a capability's methods, still unbuilt
and a much larger feature. The name was taken. Restricting the destination's scheme is an
attenuation of the DESTINATION, so it went into `allowHosts` as a scheme-qualified glob rather than
becoming an undocumented eighth vocabulary word. The scheme is matched **exactly**: `http*://`
admits neither http nor https, because a wildcard scheme accepting TLS and plaintext alike is the
opposite of what writing a scheme asks for.

**TWO OF THE FOUR CONTROLS DID NOT FIRE, and that is the finding.** Not a near miss — two
documented properties were unbacked, and one of them was **false**:

- `perform()` said it cleared "only what it performed, so an intent appended during the awaits is
  not dropped". **`clear()` took no count at all.** The claim was invented in the comment. Fixed by
  implementing `clear(n)`, which shifts the remainder down, so another request sharing the
  capability can append while this one awaits I/O.
- The scheme was said to be matched exactly rather than globbed, but every existing case refused
  `http` under either rule, so exactness was **unmeasured**. Now asserted with a wildcard-scheme
  glob that must match nothing.

**A property nobody can break is a property nobody has checked.** Running a control and finding it
inert is the same information as a failing test, arriving in a less obvious form — and this is the
second time today (after the `Symbol` facet) that the useful result was a control refusing to fire.

**v5.85 (in place — M-LIB `allowHosts`: a fragment's reach OUTWARD becomes a capability, and
the reason it is not a `fetch` is a finding):** `allowHosts` was the last vocabulary word blocked
on a missing MECHANISM rather than on enforcement. The roadmap said it "needs an outbound
capability to mediate — there is none yet", which was true and understated the blocker.

**A confined fragment is invoked SYNCHRONOUSLY** — `JS_Call`, then JSON-stringify the result. No
promise detection, no pending-job drain. So a capability that performs network I/O cannot be
handed to a fragment at all without making fragment invocation asynchronous, and that would touch
the F6/F12 deadline and the F2 per-invocation memory allowance on the most safety-critical path
in the system. That is its own increment, at least the size of F12 and F2 together, and pretending
otherwise would have meant shipping a blocking connect inside an nginx worker.

**So the capability RECORDS INTENT and the host performs the I/O** — M-CFG's pattern exactly:
*the tenant proposes what it cannot apply.* `request(url)` is synchronous, checks the destination
against the glob **in the compartment**, and appends a descriptor the host reads afterwards. The
mediation therefore bites where the capability is exercised, which is what makes `allowHosts` an
attenuation of authority rather than a filter over data — the distinction the kernel axiom turns on.

**Two gates, two codes.** `out.host` is the glob refusing a destination; the refused request never
reaches the queue, so the host cannot perform what the glob denied. `out.drain` is the A1 reach
gate on `pending()`/`clear()`: those are the HOST's half, and a fragment able to drain the queue
would read what a **sibling fragment sharing the same capability** had recorded — a channel
between tenants, not an outbound request. Both frozen in the V12 corpus with their own probes.

**Host globs wildcard on the LEFT where route globs wildcard on the right**, so one matcher in C
knows both shapes. Two matchers would be two places for the same rule to be wrong — and this is
the third time that argument has decided a design question here.

**A glob and a budget compose; two globs do not.** Two host globs have no computable meet, so
re-mediating with a different one is REFUSED, the `routes` rule for the identical reason. But
`allowHosts` with `uses` or `ttl` attenuates orthogonal axes, and "only these hosts, at most N
times an hour" is the composition an operator actually wants — so those compose. **The first
version refused every mixed pair, which made the budget and lifetime plumbing on the outbound
wrapper unreachable: code no control could break.** The test asking for that combination is what
surfaced it.

**A URL with credentials is refused outright rather than parsed around.** In an allowHosts world
`https://api.example.com@evil.net/x` is an invitation to smuggle a host past a glob, and a parser
that merely searches for `//` reads the wrong half as the host.

**AND MY BUILD CHECK WAS BROKEN THE WHOLE TIME, which is the lesson of the day.** Classifying the
new capability in `describe()` failed to compile — the class id was undeclared in that
translation unit — and I did not notice, because I had been verifying builds with
`grep -E " error |warnings being"`. GCC writes `: error:`, so a pattern requiring a space after
"error" matched nothing, `make` reported failure only in its exit code, and the tests kept passing
**against the last good binary.** Three earlier stale-artifact incidents this session were about
trees I forgot to rebuild; this one was about a build that failed while I was watching. Verify by
EXIT CODE, never by grepping for a pattern that can miss. Seven of the ten vocabulary words now
ship.

**v5.84 (in place — the reviewer pack: F11 is not closed, but it is no longer expensive):**
F11 is the one finding in the ledger that no engineering closes. `AUDIT_M-SES.md` §5 and
`ASSURANCE.md` §15 each carry a single signer, and both rows say so themselves — *"the §4
commands were executed by the authoring session, not independently re-run by the signer"*. What
exists is acceptance of reproducible evidence, not a reproduction, and A2 accepts that as
residual risk.

**The obstacle was never missing evidence. It was work.** Reproducing §4 meant reading about
eleven hundred lines across two documents, extracting commands by hand, knowing which build
directories are stale, and deciding for yourself which numbers matter. `reviewer-pack.sh` does
that once: one entry point, a verdict table, and a transcript to attach to a signature. F11 moves
from "nobody has reproduced this" to "reproducing this costs an afternoon" — **which is a change
in cost, not in status, and the ledger says so.**

**Three design choices carry the value, and each is a lesson already paid for here.**

**It refuses on a dirty tree.** A signature has to name a commit; evidence gathered from a
modified tree names nothing. And it writes its transcript OUTSIDE the repository, because a tool
that insists the tree be clean must not be the thing that dirties it — the first version wrote
four build logs into the working copy, which would have made its own second invocation refuse.

**It rebuilds every builddir and checks each binary is newer than the newest source.** The
`objs*/` trees are tracked in git, so a clone or a `reset --hard` hands over a *committed* binary
rather than one built from the code under audit. That exact trap produced a FAIL on `objs_jit`
during the audit's own assembly, and the reverse case is worse: a pass nobody measured. This
session hit the same shape three more times — a stale `qjs.o` made `--jit-aot` look like an
unknown option, a day-old `libquickjs.a` would have gated nginx against the old engine, and stale
sanitizer trees made a fixed test look build-dependent. So freshness is now asserted, not trusted.

**GATE and REPORTED are separated.** Gates are objectively pass/fail and decide the exit code.
Counts — files, tests, leaves, the delta-log version — are printed and deliberately **not
judged**: they drift legitimately as tests are added, so gating on them would fail for the wrong
reason, and pinning them in the script would create a second copy of every number outside the
documents that own them. That is the same reasoning that kept the refusal codes out of SPEC.md.

**And it deliberately does not summarise the residuals.** `REVIEW.md` is a procedure that points
INTO §15, §16, §14, the findings ledger and the audit's §3 and §6; it restates none of them.
§15's own words are the argument: a signature applied without reading the ledger *"converts 'we
know these holes exist' into 'someone looked and found nothing', which is worth less than no
signature at all."* A signature on a summary would be exactly that.

**What a signature there would and would not buy, stated in the block itself:** it closes the
REPRODUCTION half of F11. It does not make the case two attestations of the DESIGN — that needs a
reviewer who disagrees with the argument and says where, which is a larger exercise. Recording the
narrower claim honestly is worth more than implying the broader one.

**v5.83 (in place — the flake hunt: the gate's one unexplained failure was mine, and my
stability check had been too small to see it):** two intermittents had gone unattributed. A
failure with no name poisons every later result, because any future red gate can be waved away as
"probably that one". So: **25 full-suite runs, every run's full output kept, recording each failure
by name** — the reason the earlier one went unidentified is that the gate piped prove through
`tail -4` and the line naming the file fell outside the window.

**24 passed, one failed, and the file was `t/js_com_propagation.t` — written the same day.** Its
test 13 looked the *sweeping worker* up in the post-sweep fan-out tally. Nothing makes that worker
serve any of the 24 follow-up requests, and with four workers it often does not: measured directly,
**the writer was absent from the fan-out in 4 of 40 runs under load.** Absent means the lookup
returns undef, `undef → 0`, and the threshold fails.

**The fix removes the dependency instead of retrying around it.** The sweep request now reports
its own count, in the request that did the writing, so "the sweeping worker sees its own writes" is
true by construction. Phase 1 of that same file was already immune for exactly this reason — its
writer-specific facts come back in the write request's own response — and phase 2 reached for a
tally instead. The A/B under load is the control: **fixed 0/40, pre-fix 3/40.**

**MY STABILITY CHECK WAS WORTHLESS AT THAT SCALE, and this is the transferable part.** I ran the
file three times, saw three passes, and reported it stable. Three runs cannot distinguish "stable"
from "fails 4% of the time". Worse: **sixty standalone runs of the PRE-FIX code also passed** — the
condition needs full-suite load, so single-file repetition at any count would never have found it.
A denominator is only meaningful against the conditions the failure needs.

**A second, latent flake of the same family fell out of the measurement.** Test 12 required ≥2
distinct workers in the fan-out; under load the fewest seen was exactly 2. But the writer need not
appear, so a fan-out reaching one NON-writer worker satisfies the leak check perfectly and would
have failed that assertion. The precondition the check actually needs is "≥1 worker other than the
writer", and it now asserts that, over the same set the leak check uses so the two cannot disagree.
Found by measuring the distribution rather than assuming it.

**What is NOT resolved, stated with its denominator.** The earlier unexplained gate failure
("failed test 8" on `objs`) did not recur in 25 runs; `js_sw_accept_control.t` never failed, which
supports reading its one appearance as contamination from a concurrent rebuild of mine rather than
a product flake. Neither is closed. The propagation flake is a plausible explanation for the
original one — same file, same window — but the test index does not match, so it is not claimed.

**v5.82 (in place — V9: describe ⊇ mutable reaches the program instance, and seven ops were
undescribed):** `t/js_com_describe.t` has held the COM tree to one discipline since the
beginning — every mutable member appears in `describe()` with a class. The **program instance**
has exactly the same shape and had no check at all. Four surfaces carry a `describe()` listing
their ops with a rights class: the bytecode-backed NodeView, the CST-backed NodeView, the
`bindAt` epoch handle and its class-F sibling `bindShared`. **Each list is hand-written inches
from the members it describes.** Add `h.freeze` without a row and nothing fails.

**Seven ops were undescribed.** `cst` on the bytecode view and `origin` on the CST view — the
crossings between the two, both added later than the lists. `describe`, `epoch` and `tombstoned`
on `bindAt`. `describe` and `epoch` on `bindShared`. **`describe` was itself a classified row on
both NodeViews and absent on both handles**: the same op classified on one surface and not on its
sibling, which no reader would spot and a checker finds immediately.

**Auditing the fourth surface is what found the last two, and that is the lesson.** Three of the
four were easy to construct; `bindShared` needed a shared key and an `onRequest` hook. Stopping
at three would have produced a coverage claim that reads as complete — and the sibling with its
own copy of a hand-written list is precisely where the same drift lands twice. It did.

**Checked in BOTH directions**, because the two failures are different: ⊇ catches a new op
nobody classified, ⊆ catches a row left behind by a rename, describing something imaginary. The
`op` and `cls` vocabularies are **restated in the test** rather than imported from the
implementation — a test that imports the vocabulary it is checking agrees by construction and
checks nothing.

**Two of the auditor's own rules were wrong first, and both were caught by reading its output
rather than by trusting it.** Requiring a described member to be CALLABLE reported `children`
and `parent` on both NodeViews — which are real ops that materialize **lazily as data** on
access, i.e. the correct implementation of a lazy view. And requiring the two NodeView variants
to have identical op sets would have required them to be the same object; they differ by one
crossing each, legitimately, so what is checked is that they classify every op they SHARE
identically. *A rule that reports the correct implementation of the thing it checks gets switched
off within a week.*

**Stated limit:** it checks that a class is FROM the vocabulary, never that it is the RIGHT one.
A `replace` row marked `R` instead of `F` would pass. The rights semantics are not written
anywhere a checker can read, so only presence and vocabulary are mechanical here.

**With V9 built, F9 drops to four unbuilt V-items — and all four sit on the parked compiler
track. The reachable verification backlog is empty.**

**v5.81 (in place — V14: the same fragment did NOT compile to the same bytes, and half the
first fix was inert):** the compile→sign→cache story assumes deterministic compilation. Nothing
had ever checked it, and the premise was already suspect: `bc_hash` folds in a
`__DATE__ __TIME__` build stamp. Two cold compiles of one fragment, diff the `.so`.

**It failed, in exactly six bytes, at the same offset every time.** The generated C was
byte-identical; GCC records the translation unit's filename as an `STT_FILE` symbol, and that
name came from `mkstemps` — `qjs_jit_2m6d6L.c` one run, `qjs_jit_Y1siAd.c` the next. Six
characters of randomness, straight into the artifact.

**What that cost was the meaning of a signature.** Signed bytes attested WHICH COMPILE produced
an artifact rather than WHAT IS IN IT: two honest compiles of one fragment disagreed, so
signature equality could not be used to decide that a cached `.so` matches a fragment. The fix
is one name — `jit_write_repro()` gives the file a basename derived from the bytecode hash, the
same identity the cache is already keyed by, inside a private directory. The directory stays
unique deliberately: a deterministic PATH would put two processes on one file, and nothing makes
a partial write or an unlink-during-read safe.

**HALF THE FIRST FIX DID NOTHING, and the control is the only reason that is known.** It also
`chdir`'d the compiler into the job directory and passed bare filenames, on the theory that an
absolute path would be recorded. Reverting just that half changed nothing — **GCC records only
the basename** — so the probe still passed with the chdir gone. A control that confirms a fix
also tells you which part of it was load-bearing, and here it removed a third of the diff.
*Shipping the inert half would have been shipping a belief.*

**And the instrument's own first version hid a broken tree behind a SKIP.** When a deliberate
compile error was introduced, it reported "could not build … SKIP" and exited 0 — a failing
engine reading as "nothing to test". A skip is for a toolchain that is ABSENT, never for one
that is present and failing; it refuses now. Same family as the stale-object trap that bit
twice more today: `qjs.o` reused from a non-JIT build made `--jit-aot` look like an unknown
option, and `libquickjs.a` was a day old, so the nginx binaries would have been gated against
the *old* engine.

Three controls: the random basename restored (the defect reproduces, same six bytes at the same
offset), a compile error (refuses), and a run producing no artifact (refuses rather than diffing
two absences). V14 is hand-run — it needs a JIT-capable `qjs`, which the ordinary build does not
produce — so it constrains nothing per-commit, and one fragment on one compiler on one host says
nothing about reproducibility across toolchains. Trusting-trust stays accepted as residual.

**F9 drops to five unbuilt V-items, and only V9 is reachable.**

**v5.80 (in place — F13 closed: the request is in the registry, and every row says it is
request-scoped):** every config-phase node had a declared type and a safety class. The object a
tenant actually touches had neither — `nginx.describe(req)` returned **zero rows**. `remoteAddr`,
`uri`, `method`, `headers`, `body`: no type, no class, nothing for the typed tier to reason with
and nothing for S4 to follow, even though `req.location` is a live COM handle from a request
into the config tree.

**All 54 rows are classified: 28 getters, 25 methods, one settable (`statusCode`).** They are
**TABLE rows, not entries in the read-only name map**, and that choice is the substance. The map
is keyed by BARE MEMBER NAME across every class, so a request `headers` and a location `headers`
would have to agree on one type — and `requestScoped` would come from the discovery path's
hardcoded `false`. A table row is per-class and carries its own flags, so `RQS` is a fact about
this member on this class instead of a guess that happens to be right.

**Every type was read off its getter, and none was wrong on the first run.** The name→magic map
in `ngx_js_request_proto_funcs[]`, then the `JS_New*` each `case` actually returns. `startTime`
is a number, not a Date. `connection` is an object, not a number. `location` is a handle, not a
path string. `body` is a string that is null until the body is read. Set that against the
read-only map, where one row in 24 hand-written entries was wrong (`names`): **reading the
implementation and inferring from the name are not the same activity**, and the difference shows
up as a defect rate.

**The hardcoded `requestScoped: false` is no longer latent-false, and it is CHECKED rather than
argued.** It was correct only because the seventeen plainly request-scoped getters were never
emitted. Now the request's members are table rows with their own `RQS`, and the conformance test
asserts that **every** request row says true — so a getter added without a table row is emitted
by the discovery pass with `false` and fails the suite the day it lands. That is the assertion
V8's pin at zero existed to force, and the pin is now the real count.

**One judgment, stated rather than hidden.** The classes for METHODS are a reading, not a
measurement: reads are `readonly`; response writes are `safe` and NOT reversible, because bytes
already sent cannot be recalled — "irreversible for the lifetime of the process" scaled down to
one request; and the five that change *where the request goes* — `pass`, `redirect`,
`subrequest`, `fetch`, `hijack` — are `guarded`, matching `proxy.pass` on the config surface for
the same reason, rather than being demoted to safe because they happen to live on a request.
Nothing mechanically checks that mapping. It is written in the table so it can be argued with.

**F13 was the last OPEN finding in the ledger.** What remains is two ACCEPTED residuals — F8's
timing channels and F11's single signer — and F9's six unbuilt V-items, four of which sit on the
parked compiler track.

**v5.79 (in place — the TESTS are gated now: 39 assertions claimed something and checked
nothing):** V11 mutation-tests the policies. Nothing tested the tests — and this arc found
**five assertions that could not fail, every one by accident while doing something else**: the
`realize` probe that forged a 4th argument to a 3-argument operator, the `cap.expired` corpus
row that reported "alive" forever with nothing pinning it, the `Symbol.for` arm comparing a
value with itself, that file's coverage tally scraped from the whole payload (a battery of
seven reporting 8-of-8), and the F9 ledger row that counted five unbuilt V-items where the
table had always shown six.

**A dead probe does not fail. It reassures.** Every "closed" in the findings ledger rests on an
instrument, so `check-dead-probes.py` now gates the four shapes above, each cheap to detect
statically and each expensive to find by hand.

**First run: 39 assertions that claim something and verify nothing.** 27 were padding —
"nginx started without crash", "all checks passed", and hand-written duplicates of the
harness's own no-alerts checks — deleted, with every plan decremented to match. Twelve named a
specific behaviour: **eight restated a claim the assertion directly above already proves**
(deleting a duplicate claim loses no coverage; keeping it teaches the reader that a message
implies a check), and **four were genuinely untested** — Content-Length suppression under a
body filter, a chained filter running after an empty intermediate body, non-string filter
returns passing through, and header-filter ordering surviving a restart.

**All four of those claims turned out to be TRUE, which is exactly why they survived.** A
false claim gets noticed the first time someone relies on it; a true claim with no check
behind it is indistinguishable from a verified one until the behaviour changes. The Content-
Length assertion got a control of its own — a location with no body filter, which DOES send
the header — because `unlike(..., /^Content-Length:/)` also passes when the pattern is simply
wrong.

**And the checker was twice its own best test case.** Its first version reported the *prose*
that documents a dead probe as a dead probe — in the very file whose comment explains the bug
it was written to find — and then did it again in a second check after being fixed in the
first. A checker that reads source as text must be told where the code is, once per check, and
forgetting it in one place produces findings that look exactly like the real thing. Its
`f(x) === f(x)` rule also had to learn the difference between a dead discriminator and the
legitimate **determinism** assertion two fuzz generators here use (`f(77) === f(77) &&
f(77) !== f(78)`): the rule now fires only when the comparison decides between two labels.
Five controls, one per check.

**Stated limit, so nothing is over-credited:** this is a STATIC reader. It cannot tell whether
an assertion's subject is reachable, whether a control fires, or whether a corpus row is
compared with its expectation at run time. Clean means "not dead in the four known ways", never
"every assertion is live". The dynamic half remains `verify-negative-controls.sh` (six rows
automated, six manual) and the inline controls in the `comcon_*` suites.

**v5.78 (in place — F3's `Symbol.for` residual is WITHDRAWN: it was a probe comparing a
value with itself, and the facet that would have "fixed" it breaks erasure):** the audit's
F3 row has said since 2026-09-12 that an operator who declares `Symbol` for two tenants
hands them `Symbol.for` as a rendezvous. The arm that measured it read

    Symbol.for(k) === Symbol.for(k) ? 'SHARED' : 'clean'

— **two calls in the same fragment, compared with each other.** That is true of any
registry, private or shared; it never looked next door and it could not come out `clean`. Its
plant returned a `typeof` that nothing consumed. **A probe whose read cannot be false is not
a probe** — the third dead probe this arc has found, after the forged-4th-argument `realize`
probe and the `cap.expired` corpus row that reported "alive" forever.

**The real question is not the key, it is the store.** A shared registry gives two fragments
the same KEY. A key opens nothing on its own: a channel needs a STORE both can reach. So the
probe now takes the symbol and uses it as a property key on every surface both fragments
touch — and comes back `proto:refused,json:refused,array:refused`, read `clean`. Two controls
make that a measurement rather than a hope: the same text **unconfined** reads the mark back,
which proves the key matched across two separate calls (so the registry really is
runtime-wide, now demonstrated instead of asserted), and with the **M-SES-1 freeze disabled**
the confined arm becomes a live channel and four assertions fail. The mechanism is named by
removal: it is the freeze, not the absence of a shared name.

**A per-fragment `Symbol` facet was built, worked, and was REJECTED.** Giving each fragment
its own `for`/`keyFor` table is the obvious capability-secure move and it is the wrong one
here, for a reason worth stating: **it breaks erasure (G11.7).** Two fragments of one program
that both call `Symbol.for('k')` and expect one symbol would get two under COMCON and one in
plain node — an annotation changing what the code *computes*, which is precisely what erasure
forbids and what SR-2's faithfulness argument rests on. Shipping it would have traded a
signed invariant for a channel no probe can show is open. **The reject is the result**; the
code is not in the tree.

**And the accounting gets an ERRATUM, not a rewrite.** `AUDIT_M-SES.md` §3 is inside a signed
attestation, so its text stands as signed, with a marker pointing at §6 where the withdrawal
is recorded. A signed document that quietly acquires corrected facts is worth less than one
whose errors are visible: a reader who cannot tell which claims moved cannot rely on any of
them.

**v5.77 (in place — V8's effect-class half: the propagation column is true, and proving it
took two arms that disagree):** `propagation` is the COW-trap axis — `worker-local`,
`zoned-shared`, `auto-shared` — and **306 rows make the claim while nothing had ever checked
it.** It cannot be checked in one process: inside a single worker, "my memory" and "the
fleet" are the same observation.

It is also the registry's **only conditional claim.** `describe()` does not report the
table's value for upstream peers; a refine hook resolves it from `peers->shpool != NULL`. So
one fixture carries a zone-backed upstream and a plain one, the same `weight` member is
classified two ways, and **the run asserts the two arms DISAGREE.** That is the load-bearing
part. "No other worker saw the write" also passes when the write did nothing, when the
fan-out reached one worker, and when propagation never worked at all — the negative arm is
only evidence if the positive arm fires beside it.

**Result: the claim holds.** `w1 zoned=77 plain=77` and `w2/w3/w4 zoned=77 plain=1`. Then a
sweep generated per registry row — 67 eligible `worker-local` members stamped in one worker —
and **not one leaked.** This is a leaf that says a registry column is TRUE; after an arc in
which nearly every instrument found a defect, it is worth noticing that one did not.

**No record crosses processes, and that was forced.** The obvious design has the writer
stash what it wrote in `nginx.shared` for the readers — but a shared value is capped at 512
bytes, so the record would have been **silently truncated** and the sweep would have checked
a fraction of what it reported. Instead the writer stamps a sentinel and every worker
independently reports anything holding one. Nothing is communicated, so nothing can be cut.

**Two instrument bugs, both mine, both about reading my own output.** The restore fan-out
never reached the worker that swept — only that worker holds the old values, and a request
cannot be addressed to a worker — so "the restore left 67 members changed" was really "the
restore never ran"; the fix fans out until it lands, and **two separate assertions now
distinguish "it ran" from "it worked."** And I read a diagnostic that prints a sample capped
at six as though it were the total, so 67 unrestored members looked like 6. *A truncated
report and a small number are indistinguishable unless the report says which it is.*

**Coverage is stated, not implied:** 67 of the 154 settable `safe`+`reversible`+`worker-local`
rows the walk reaches. **52 booleans cannot carry a distinguishable sentinel** — `true` is
also a natural value — so a boolean leak is invisible to this method; 35 non-number rows are
excluded because a sentinel written into a routing member can stop the worker under test from
matching the next request, and a worker that cannot serve cannot report. And **`auto-shared`
has ZERO rows in the registry**: a declared enumeration value with no instances, untestable
by construction rather than untested.

Four controls, two of them at the CONFIG level — remove the zone, add a zone — so they
exercise the real mechanism instead of mutating the test; one runs `worker_processes 1` and
confirms the precondition FAILS rather than passing vacuously. Clean under ASAN and UBSAN —
after rebuilding those trees, which were stale and made the *other* new test look as though
it behaved differently under a sanitizer.

**v5.76 (in place — M2.5 re-stated: the spec says what the system does, and a check keeps
it that way):** `SPEC.md`'s own header calls it "the clean normative read of the design:
current truth, stated once, no revision archaeology." A document with that job is the one
most exposed to silent decay, because nothing fails when it rots.

It had **zero mentions of `routes`, `ttl`, the refusal codes, `cap.expired` or the session
registry** — every one of them shipped. Its §10 still described a session's
identity→environment mapping as "a host-integration deliverable (must exist before the first
operator session)"; that deliverable is `std.sessions`, built weeks ago. Its §13 was stamped
v5.35 against a delta log at v5.75 and still listed increment D as design.

§2 now carries the closed mediation vocabulary and the **meet rule** — masks AND, lifetimes
take the minimum, globs and rate budgets are REFUSED — stated as a property of each flavor's
ORDER rather than as a convenience. §10 carries the session registry with the load-bearing
part in the open (it stores descriptors, never environments, which is exactly why it carries
no authority and can be fleet-wide; COMCON does not authenticate, and the host's assertion of
the principal is the entire trust transfer), the two code axes, why `E_BUDGET_*` is empty **by
placement**, and the ops-resource capabilities named by the identifier an operator passes.
§13 separates a dated current-truth block from how that truth was reached.

**The durable half is a checker, not an edit.** Check [7]: every member of a set the spec
calls closed must appear in the spec. Its first run found `bindings` described only as
"binding/epoch store" — the concept, not the key a caller types — which is why it matches a
**backticked identifier** rather than a bare word: `log` and `mode` are ordinary English, and
a bare-word search would pass on any prose at all *while reporting green*. An inert check
that looks green is worse than no check.

**What it deliberately does NOT check, and why.** The fifteen refusal codes are not required
by name. Copying them into the spec would duplicate MANUAL §3.2, and **a spec that copies a
table acquires a second place for that table to be wrong** — so the spec must name
`comcon.refusalCodes()` and send the reader to the one authority. Demanding the list would
trade one staleness for another. And the whole check is a **presence** check: it can tell that
the spec names `ttl`, never that it describes `ttl` correctly. Stated in the tool, in G11.9,
and here, because a check whose limits are not written down gets credited with more than it
does.

Four controls, one per branch. And the standing test that runs the checker already argued the
right thing — "no drift" from a checker that bailed out early is a vacuous pass — so [7] is
pinned there as having RUN, along with [3], which that list had been missing.

**v5.75 (in place — V8: the registry's read-only half is held to account, and the
request turns out not to be in the registry at all):** the typed tier and the config-review
path reason from what `describe()` says. The SETTABLE half has had an instrument since the
setter fuzz — which found four rows misdeclaring their type. The READ-ONLY half never did,
and that is the half where the REACH paths live: `proxy`, `ssl`, `upstream`, `sockets` are
getters that hand back a handle reaching further into the tree.

The classification tables deliberately omit read-only members ("they carry no mutation
safety class"), so a read-only row is assembled at runtime from a prototype discovery pass
plus a static name→type map — **24 entries standing in front of 126 getter-only members.**

`t/js_com_schema_conformance.t` generates the check from the live walk and found three
things. **One real misdeclaration:** `names` declared `object[]` and returns `string[]`,
while the adjacent `serverNames` row had it right all along — what an inconsistency looks
like from the inside is two rows for the same kind of thing disagreeing. **Twenty read-only
rows the walk reaches had no declared type at all** — honest (`"getter"` is not a lie) but
not an answer, and the typed tier cannot reason with it; all twenty are now classified
against their implementations, which is also how `pid` got recorded as the pid FILE PATH
rather than a process id. **And `nginx.describe(req)` returns ZERO rows.**

**That third one is the finding.** The request — `remoteAddr`, `uri`, `method`, `headers`,
`body` — is the tenant-facing surface, and it carries no declared type and no class. The
read-only descriptor hardcodes `requestScoped: false` for every row, which looked like a
false claim about seventeen plainly request-scoped getters until the walk settled it: those
rows are never emitted, so the field is **unfalsifiable rather than wrong**. Latent, not
live — a distinction worth making, and worth pinning: the test asserts the request row count
is zero, so whoever adds those rows must fix that field in the same change. Finding F13.

**Pinning the negative space is the method here.** The corpus is generated, so it cannot go
stale; what is hand-written is the *inventory* — the unclassified set (pinned at empty) and
the five map entries the walk cannot reach (named, and explicitly NOT counted as verified).
A coverage number that moves silently is not coverage, and "24 of 24 rows" would have meant
nineteen.

**The volatility control, and an assumption of mine that was wrong.** A pure-read check
compares snapshots before and after reading everything — but live counters (`conns`,
`fails`, `connections`) move on their own, and without a control the server's own traffic
reads as a mutation caused by the read. So it takes two snapshots back to back with no reads
between and excludes whatever already moved. I then asserted that this exclusion set must be
non-empty, on the theory that a live tree always has moving counters. **It is empty** — an
idle fixture has none — and that is the *best* case, not a failure: nothing excluded means
the pure-read result is unqualified. Demanding the instrument find noise is not a control;
the control is C4, which proves the mechanism fires on a value that really moves.

**v5.74 (in place — M-LIB `ttl`: a capability with a lifetime, and the lease that finally
bites):** TM-2 gave session grants a lease; `include()` binds grants as closure parameters at
ADMISSION. So an operator who resolved a session once and bound a fragment had handed it
capabilities that outlive the lease indefinitely — **the mapping expired, the authority did
not.** Neither feature was wrong alone; the hole existed only in their composition, which is
where nearly everything in this arc has been found.

`mediate(cap, ttl(seconds))` closes it, and `std.sessions.resolve()` can now stamp a lease's
remaining seconds onto what it hands out. **The clock starts when the capability crosses into
the compartment**, not when `mediate()` built the descriptor: the descriptor carries a
duration, so there is one clock (nginx's) instead of two that could disagree.

**LIFETIMES COMPOSE WHERE BUDGETS REFUSE, and the contrast is the point.** Two budgets have
no computable meet — 10/min and 100/hour are not ordered, so `uses` refuses to re-mediate
with a different one rather than guess and widen. Two lifetimes ARE ordered: the shorter is
strictly narrower than both, so `ttl` takes the `min`, in either composition order. Same rule
(never widen), opposite outcome, because the lattice differs. Expiry is checked BEFORE the
budget is charged — spending budget on an operation that cannot happen would make the audit
read as though the tenant were still working.

**Two instrument defects, both about where a clock starts.** The V12 corpus row for
`cap.expired` first reported `alive` forever and **the suite passed**, because the row
included its fragment *after* the sleep (a fresh lifetime) and nothing asserted the row had
fired — a corpus row that never fires is decoration, so it is now pinned explicitly. And the
dedicated test minted its short-lived capability at CONFIG time, racing nginx's own startup:
the first run failed because the request arrived after the capability had already expired.
Both are the same lesson from opposite ends: a lifetime test must control when the clock
starts.

Three controls: the expiry never checked (four assertions fail), the meet taking the LONGER
lifetime (exactly one — the meet assertion), and the wrong denial code recorded (the V12 pin
plus the undeclared check). `t/comcon_cap_ttl.t` (11).

**Also in: the two negative-control rows that went inconclusive are now MANUAL, with
reasons** — their inverse patches no longer apply because this session's own commits rewrote
those lines. `git apply -R -3` was tried and is recorded as a TRAP: it applied one file,
failed the other, and left the partial revert in the tree. A control that half-reverts a fix
and walks away is how a tree quietly stops being the one you tested; an explicit "revert this
by hand" is worth more than an automated maybe.

**v5.73 (in place — F4 closed, F9 reduced, F8 measured: the three that "were not mine to
close"):**

**F4 — the guarded class, fuzzed one process at a time.** The setter fuzz excluded `guarded`
because writing to those members rewires live dispatch and degrades the server under test,
so every probe after the write measures wreckage. **That objection was about SHARED STATE,
not about the members** — so each one now gets its own nginx: enumerate the guarded members
from the live registry, start a fresh instance per member, fuzz that one alone. Three
members are reached by the same walk the setter fuzz uses; all three take a hostile battery
without a crash and keep serving. `irreversible` stays untested because the walk reaches
**none** — a fact about the walk, recorded as such rather than as coverage.

**What the control taught, and it changed the file:** with the handler setter patched to
dereference NULL, the "does the process still serve?" probe **still passes**, because nginx's
master respawns the dead worker before the next request. Only the log check fails. The
liveness probe is not a crash detector; a file with only that probe would have called a
segfaulting setter clean.

**F9 — V13 is built, so six V-items remain rather than seven.** `t/comcon_v13_erasure.t`
runs one corpus twice: admitted and confined inside COMCON, and in plain **node** with no
annotations at all. **A different ENGINE is the point** — an in-process comparison shares
the runtime whose behaviour is in question and would agree with itself. Seven rows chosen
where erasure could plausibly break (V1's numeric boundaries 2⁵³/−0/NaN, string and JSON
round-trips, RegExp state, sort and enumeration order, try/catch/finally ordering): all
byte-identical. One row had to be rewritten before it counted — `1/-0` serializes to JSON
`null` on both sides, so the row would have agreed no matter what the engines did.

**F8 — not closed, but no longer unquantified.** T9 is accepted by design; an accepted risk
of unknown magnitude is worth less than one of measured magnitude. A fragment cannot read a
clock (`Date` is deliberately not an intrinsic), so the receiving tenant times nothing: the
observer is the client and the medium is contention. The worker is single-threaded, so while
one tenant burns CPU the other **does not run at all** — a peer's latency goes from
**0.3 ms to 347 ms (1227× idle, ~2.9 bits/s)**. Under a 50 ms execution deadline the
separation falls to 49.8 ms. **The deadline (F6/F12) is the only mitigation for T9 in the
tree, and it bounds the per-event leak to exactly its own value** — narrowing the channel,
never closing it, which is what "accepted residual" has to mean.

**v5.72 (in place — F12 and F2: the bounds reach the places execution actually resumes):**
two resource findings from the ledger, and both tests were WRONG FIRST in ways only their
controls could show.

**F12 — an `await` used to reset the protection.** F6 defaulted the host deadline on, but it
was armed in exactly one place (the content handler) and cleared whenever a handler
suspended; everything else that runs request JS — a header or body filter, the body-read
completion and the microtask drain after it, the access phase — ran unbounded. I first
counted 19 `JS_Call` sites and called it out of scope. The real chokepoint is
`w->current_request`: it is set exactly when JS is about to run on behalf of a request,
which is exactly when a request deadline applies — **eight sites, one helper**. Nested
entries INHERIT rather than re-arm, because a filter that re-armed inside a handler would
hand a runaway a fresh budget every time it crossed a layer. Worker-level JS (broadcast acks,
listener callbacks) is deliberately NOT bounded by a knob named `workerRequestTimeout`.

**F2 — a fragment gets a per-INVOCATION memory allowance.** `JS_SetMemoryLimit` is per
runtime and every fragment shares one, so 64 MB bounded the compartment as a whole and one
fragment could exhaust everyone's budget. The mechanism is that same limit, narrowed to
(current usage + allowance) for the duration of one call and restored after: the invoke is
single-threaded, so growth in that window IS this fragment's. 16 MB default;
`contract.meter.memoryBytes` may only NARROW, enforced in C so calling `__invokeConfined`
directly cannot buy a bigger budget. **It bounds a BURST, not a leak** — a fragment retaining
a little on every call still walks the shared cap upward, which stays the runtime limit's job
and stays open as the remainder of F2. A per-call allowance reads like per-fragment
accounting and is not, so the difference is pinned by a test rather than left to be assumed.

**Both probes passed for the wrong reason, and the controls are what said so.** The F12 probe
used a GET — no body, so `readBody()` settled synchronously, the continuation never left the
original entry, and removing the arm changed nothing. The F2 probe allocated 64 MB, which
hits the pre-existing runtime cap, so it also passed with the new allowance disabled. Fixed
by sending the body LATE (a real suspension) and by sizing the allocation to sit above the
16 MB allowance and below the 64 MB cap. **Neither mistake was visible in a green run; both
were visible the moment the control failed to fire.**

`t/js_host_request_deadline.t` (9), `t/comcon_fragment_memory.t` (6), one control each.

**v5.71 (in place — F6: host JS is bounded by default, and the narrower gap that closing it
exposed):** AUDIT_M-SES §3 carried this as OPEN *and as a deliberate scope choice* — "bind
the guard to the confined path only, so host JS behaviour does not change." The cost of that
choice was a worker hung until SIGKILL on one accidental `while(true)` in a
`location.handler`, taking every other client on it down, while
`nginx.workerRequestTimeout` — the knob that would have prevented it — defaulted to 0.
**A guard that is off by default protects only the operators who already knew they needed
it.**

**Ten seconds, not the tenant's one.** Host JS is trusted and may legitimately spend real
SYNCHRONOUS time in a request (a COM tree walk, a large parse); nothing legitimate
approaches ten seconds, and a worker wedged for ten is still enormously better than one
wedged forever. `0` is an explicit opt-out, a malformed or missing value reads as the
default (the same fail-closed direction as every other malformed-contract decision here),
and the property now READS as its own default so the knob documents itself.

**Three measurements shaped the test, and each corrected an assumption.** (1) Assigning the
knob inside a handler affects the NEXT request, because it is read once before the handler
runs — the first probe was measuring the default while claiming to measure 300 ms. (2) The
CLIENT gives up before a 10 s server bound does (Test::Nginx waits 8 s), so the default's
evidence is the error log, not the response — which is also the symptom an operator meets:
a runaway looks like a client timeout. (3) An abort is not catchable in the handler; the
interrupt unwinds the whole call, so a handler cannot report its own execution and the test
must not ask it to.

**Closing F6 exposed a narrower gap, now named F12 rather than implied away:** the deadline
bounds one SYNCHRONOUS ENTRY and is cleared when a handler suspends, so a continuation
re-entered from an event callback runs unbounded — **an `await` resets the protection**.
Arming every re-entry means touching 19 `JS_Call` sites; a time-gap heuristic was considered
and REJECTED because under sustained load the worker never idles, so a cumulative clock
would abort every request — a cure far worse than the disease. It is also not probeable
today: a content handler suspended on a `setTimeout`-resolved promise does not resume at all
(measured: 500 with no deadline involved), so that path cannot be exercised that way.

Two controls: the default reverted to 0 (six assertions fail, including the headline) and the
interrupt handler made inert (five fail). `t/js_host_request_deadline.t` (7). Recorded in
ASSURANCE §16 — F6 as signed is no longer true, and F12 did not exist as a row when §15 was
signed.

**v5.70 (in place — [TBD-2] fully resolved: the capability layer's own refusals get codes):**
the second and last tranche. `E_CAP_FLAVOR` — a mediation flavor outside the closed
vocabulary, which is the refusal that closed a **fail-open** where a typo (`redcat` for
`redact`) once meant FULL authority. `E_CAP_ESCALATE` — **a composition that cannot be
SHOWN to narrow**: a routes glob or a budget with no computable meet, a mask meet that
widened, a realization env that is not a sub-map of the realizer's. Four raising sites, one
code, because they are one rule.

**Thrown by a JS `capRefuse()` that mirrors `ngx_js_comcon_refuse()` in C** — `.code` on the
error, the code bracketed at the END of the message — so the two halves of the platform
cannot drift in the shape of a refusal.

**Two of E_CAP_ESCALATE's four sites cannot be provoked through the public API**, and that
is recorded rather than papered over: `realize()` builds the restricted map from the
realizer's own env so it cannot disagree with it, and the mask meet is an AND of two masks.
They are defence in depth against a future logic bug. The first attempt DID write a probe
for the realize case — it passed, silently, because `realize()` takes three arguments and
the forged fourth was ignored. **A probe that cannot fail is dead code pretending to be a
test**; it was deleted and the reason written where the next reader will look. The two
reachable sites are pinned instead (`comcon_v4_monotonicity.t`, `comcon_budget_uses.t`).

**[TBD-2] is now fully resolved, and the two EMPTY families are answers rather than
omissions:** `E_BUDGET_*` because budget exhaustion is a **denial** (`budget.uses`) at a
run-time gate, not an admission refusal; and the deadline abort because it is the engine's
interrupt, with no refusal of ours at that point to label. What keeps only message text is
the argument checking of library calls — on purpose, since the fix there is to fix the call.
Three controls: the refusal thrown without `.code`, the code dropped from the message, and
the wrong code on the budget refusal. Recorded in ASSURANCE §16 as a change after the
signature; F10 closes.

**v5.69 (in place — F5: the escape battery, against code that is actually native):** the
audit's PARTIAL row, and the one a security reviewer reaches for first. **Running the gate
on a JIT-capable BINARY was never the same claim as running it against COMPILED CODE**, and
the difference is not academic: server-AOT happens in the master pre-fork, and
`js_comcon_aot_compile()` returns 0 for any bytecode function — "eligible", never
"compiled" — which is why the include site logged success on that 0 for weeks (D4c). A gate
that never checks whether lowering happened reports a green compiled tier while measuring an
interpreted one.

**So the precondition is asserted before anything else.** The compiled arm must report
`compiled >= 1` from `aotStatus()` — measured **20 natively-lowered functions** — and the
interpreted arm must report `0`: the same fragment, the same battery, two demonstrably
different tiers, read with a probe that never compiles anything itself. Then the battery:
nothing open on either tier, and **the two agree probe by probe** — the claim SR-2 makes for
the confinement surface, now made for the escape battery specifically.

**One definition, two consumers.** The battery moved to `t/tools/mses-probes.js`, read by
both the standing S6 gate and this one, because two copies of an escape battery is how one
of them quietly stops testing what the other still does.

**Two controls, each aimed at a different way of being wrong.** Both arms on a
non-compiling binary: the precondition assertion refuses, so the file cannot be fooled into
being a second interpreted run wearing a JIT binary. The intrinsic freeze disabled on the
**compiled build only**: probes come open *on native code* and tier agreement breaks — so
the battery demonstrably bites on lowered code, not just on bytecode.

Recorded in ASSURANCE.md **§16, changes after the signature**: F5 moves from accepted
residual to closed. A signature is not re-earned by a change that removes a gap, and not
invalidated by one either — but it is never credited with work it did not see.

**v5.68 (in place — SR-4 SIGNED: the last standing gate is closed, on one signature and an
accepted list of residuals):** `ASSURANCE.md` §15. The gate has stood open since SR-3 passed
on 2026-09-01; it closes not because everything is proven but because the tree, the evidence
and the gaps are all written down and re-runnable, and someone has accepted them by name.

**The re-run is the signature's whole content.** Four builddirs rebuilt and verified to
carry the newest change; `t/` on both builds (317 files, 4221/4233); `t_stress` on both
(18/90); ASAN and UBSAN over the 55-file COMCON corpus with **0 findings in `src/js`** and
the positive control firing, so a clean run is not an inert one; the six enumerations; the
case checker (55 leaves, 69 artifacts, no orphan test, every T1–T12 and V1–V15 addressed);
the standing instruments.

**Two things the re-run found, which is why one re-runs.** (1) The M5 split instrument
measures only on `objs_jit` — on `objs` every arm reads ~1.0× with `installed:0`, because
that build has no server-AOT call. It did not return a plausible number: its own guards (the
control must exceed 5×, the arms must sit on different tiers) failed the run and caught the
operator. (2) **Automated falsifiability fell from 8/8 to 6/8**: two inverse patches no
longer apply because *this session's own commits rewrote the lines they target* (the
grant-wrapping path for budgets, the include contract path for refusal codes). The tests
still pass; what is lost is the automated proof that they can tell the difference — accepted
as maintenance debt, and named.

**What the signature does NOT say.** One signer, and the commands were run by the authoring
session, so this attests **acceptance of reproducible evidence, not independent
reproduction** — the same caveat `AUDIT_M-SES.md` §5 carries, and the honest reading is that
a second pair of eyes is what is still missing, not an artifact. Eleven findings are
**accepted as residual risk rather than closed** (per-fragment memory attribution, the
unfuzzed COM classes, the compiled tier under the escape battery, host JS unbounded by
default, IFC/timing, seven unbuilt V-items, two uncoded capability refusals, and the
single-signer audit). §14 states what the case does not establish — memory safety,
confidentiality against timing, host JS, and the sufficiency of any test's assertions — and
that statement is part of what was signed. §15 ends with what would invalidate it.

**v5.67 (in place — M-LIB step 3: `uses`, the first mediation that attenuates RATE):** the
vocabulary shipped four words (`revoke`/`redact`/`allow`/`routes`) while the documents
promised ten, and M-LIB's remainder was blocked on exactly that — *"shipping them as
descriptors would be shipping policy that does nothing."* `comcon.uses(key, limit, window)`
is the enforcement.

**A use is any gated operation**, a read of a mediated field as much as a method call:
charging only calls would make `s.address` free and let a tenant spend the interesting part
of a capability without touching its budget. A **redacted read is not charged** — a field
the membrane hides was never an exercise of the capability.

**THE COUNTER IS FLEET-WIDE, and that is the property that decides whether the feature is
real.** It lives in `nginx.shared`, so the operator who wrote `limit: 10` gets ten — not ten
per worker, which is forty on this box and is not the number they wrote. The test spends a
budget across concurrent connections on four workers and asserts the total: **workers=4,
spent=10, denied=14**, with the worker count asserted from `nginx.shared.incr()` because a
single-worker fixture cannot see this class of bug at all (v5.56's mode switch shipped with
exactly it).

**Measured `+0.037 µs` per charged use** (0.060 vs 0.023 for an unbudgeted read, 300k
iterations) — one hash probe under the store's spinlock. Affordable *because* HOST-PERF
fixed the store first: the same charge would have cost up to 0.42 µs on a miss a day
earlier, which is the difference between a feature and a tax.

**Refusals, not defaults.** A missing key, limit or window is refused — a budget with no
limit is a mistake, not "unlimited", and the one direction a mediation may never take is
toward more authority. **Re-mediating with a DIFFERENT budget is refused** on exactly the
routes-glob grounds: 10/min and 100/hour are not ordered, so a meet would have to guess and
the guess would widen one of them. An identical budget composes; a field mask composes
freely, and the mask still attenuates.

**The window is FIXED, not sliding, and the code says so** rather than leaving it to be
discovered: the counter carries a TTL, so a caller may spend `limit` at the end of one
window and `limit` at the start of the next. A sliding window costs per-use timestamps in
shared memory.

**It also settles where `E_BUDGET_*` belongs.** [TBD-2] left that family empty for want of
anything to refuse; building the mediation showed the answer is that exhaustion is a
**denial** — a gate refusing an operation at run time (`budget.uses`) — not an admission
refusal. So the family stays empty **by design** rather than by omission, and the denial
enumeration grows by one, with its V12 corpus row and check [5] demanding it before it could
ship. Audit mode needed no new code at all: the charge goes through
`ngx_js_compartment_denial()`, so a budget is logged-and-allowed in audit exactly like every
other gate. `t/comcon_budget_uses.t` (16) + 4 controls.

**v5.66 (in place — F3: cross-compartment identity, probed at last, and the claim turned
out to be two claims):** AUDIT_M-SES §3 had it as **NOT EVIDENCED** with the note that
*"cannot by construction" is an argument, not a test*. Probing it split it:

**Host <-> fragment is STRUCTURAL.** The compartment is its own `JS_NewRuntime()`, so the two
sides share no heap and no JSValue can cross — the invoke's JSON marshalling is not a policy
but the only thing that *can* happen. Patching the invoke to pass the argument by reference,
as a control, produces an empty response rather than a leak: using a value across runtimes is
undefined behaviour. **An isolation whose control is "the worker dies" is structural, and
saying so beats pretending there is a check to toggle.**

**Fragment <-> fragment shares EVERYTHING.** One runtime, and one *context* — `comcon_ctx` is
created once and reused — so two tenants share a global, an intrinsic graph and a prototype
chain. Nothing structural separates them. Eight shared surfaces are now probed as CHANNELS
(plant in fragment A, read in fragment B): `Object.prototype`, a frozen constructor, `JSON`,
an array index, `Error`, `String`, the function prototype, and a granted capability plus its
class prototype. **None is a channel — and removing the M-SES-1 freeze opens five of seven**,
which converts "the intrinsics are frozen" from a description into a load-bearing claim with
a demonstration.

**Two findings from the probing itself.** (1) A fragment CAN write an own property onto its
granted capability wrapper — only the class *prototype* is frozen. It reaches nobody (each
include gets its own wrapper over the same C object; the host's carries no properties at
all), so what makes it safe is per-include wrapping, not a refusal — a different claim, and
now the one under test. The first version of the file asserted the refusal and PASSED, on an
unanchored regex that matched an earlier probe's record. (2) **An operator who DECLARES
`Symbol` for two tenants hands them `Symbol.for` as a rendezvous** — recorded as F3's
residual. **WITHDRAWN at v5.78: this was not measured, it was an artefact.** The read compared
`Symbol.for(k) === Symbol.for(k)` inside ONE fragment, which is true of any registry. The
registry IS shared — the rewritten probe's unconfined control proves the key matches across
two calls — but a shared key is not a channel without a store, and every store the two
fragments share is frozen.

**And one instrument defect worth keeping.** The unconfined control arm deliberately pollutes
the HOST's `Object.prototype` with the mark it plants — so a later host-side read of the same
mark inherits it, and the test reported that the host could see what a fragment wrote. It
could not; the test could see its own control. Marks must be DISJOINT, not merely different,
and the control now cleans up after itself.

**v5.65 (in place — TM-2 owned and built: a principal becomes an environment by
ATTENUATION, never by minting):** the last unowned finding in the threat model, and the one
with a deadline (*before the first real operator session*). Spec in **§8b**; mechanism in
`comcon.std.sessions`; `sessions` joins §8a as the **ninth ops-resource**, so a session
without it has no `grant`/`revoke` verb at all.

**Three consequences follow from one decision — the registry stores DESCRIPTORS, never
environments.** (1) Stealing the whole table yields no authority; compare the obvious
design, where the table IS the keys to the building. (2) It can live in `nginx.shared`,
because data crosses a process boundary and capabilities do not — so it is **fleet-wide by
construction**, which is not a nicety: the mode switch shipped per-process and put four
workers in mixed modes (v5.56), and a session table with that bug authenticates on one
worker and not the next. (3) Leases are the shared store's TTL, so an expired grant is
reclaimed when probed, by machinery that already exists and was just made fast (v5.63).

**Resolution attenuates the CALLER's own env.** `resolve(principal, env)` narrows the env
it is handed, so a session is **≤ the env of whoever resolved it** — monotonicity at the
identity boundary, inherited from the kernel rather than argued again. An unknown principal
or an expired lease resolves to the EMPTY env (the answer an undeclared free name gets). A
descriptor naming something the base env does not grant is **refused, not trimmed**: a
mapping that quietly grants less than it says is one nobody can audit. A function in a
descriptor is refused where the caller can see it, rather than dropped silently by
`JSON.stringify`.

**COMCON DOES NOT AUTHENTICATE, and §8b says so at the top.** The host asserts the
principal — mTLS subject, a JWT it verified, peer credentials — and that assertion is the
entire trust transfer; a deployment passing a *client-supplied* identifier has handed the
client the session, and nothing here can detect it. The principal namespace and the login
transport stay the host's too. Recorded as the residual of ASSURANCE.md **F7**, which moves
from OPEN to SPECIFIED + BUILT.

`t/comcon_std_sessions.t` (20) + 5 controls, each aimed at a property that would be
dangerous to lose: resolution that stops narrowing, an unknown principal falling back to
the caller's env, a greedy mapping trimmed instead of refused, a capability accepted into a
descriptor, and the verbs appearing without the capability. **`t/comcon_std_ops.t` caught
the change by itself** — it pins the resource count at 8, and the ninth broke it, which is
the closed enumeration working in the direction nobody plans for.

**v5.64 (in place — V15 / SR-4: the assurance case exists, is machine-checked, and its
first finding was that a quarter of the evidence was dead):** `ASSURANCE.md` — G0
decomposed into 53 leaves over G1–G11, six named assumptions, a findings ledger of eleven,
and a rename table. It is the artifact a security reviewer asks for, and it closes the one
gate that has stood open since SR-3 passed.

**Checked, not asserted.** `t/tools/check-assurance.py` (run by `t/comcon_assurance.t`)
enforces six rules: every `EV:` names an artifact that EXISTS; every leaf has evidence or a
GAP with a `home:`; **no orphan evidence** — every `t/comcon_*.t` must be cited by some
claim, so a test proving something nobody claims is a finding in the other direction; every
adversary T1–T12 and V-item V1–V15 appears; **no document in the set cites a test file that
does not exist**; and a gap cannot point at a finding nobody wrote down. Seven controls,
one per rule.

**F1 — the finding that arrived before the document did.** Sweeping the doc set for cited
test files: **20 of 83 citations pointed at files that do not exist**, every one a
pre-CONVERGENCE name deleted in P6a/P6b when its `comcon_include_*` sibling took over. A
reviewer following THREATS.md's T11 citation to `t/comcon_gas.t` found nothing. Live claims
now cite the successor; §12's rename table redirects the historical passages, and the
checker validates the redirect targets too, so the fix cannot rot the way the citations did.

**And a hole in itself.** The first tree covered T1–T12 except **T9** — side channels were
in the assumptions and claimed by no leaf, which check [4] refused. G7.7 now states the
position plainly: the direct readout is closed, the indirect one is neither mitigated nor
probed, and with T4 it is the deferred confidentiality axis of the whole design.

**The ledger is the deliverable, and the test asserts it is not empty** (an assurance case
that claims everything is evidenced has been written to reassure). Eleven findings: memory
attribution per fragment, cross-compartment identity, guarded/irreversible COM members, the
compiled tier under the escape battery, host JS unbounded by default, TM-2's unowned
session→env mapping, the IFC/timing residual, seven unbuilt V-items, the two empty code
families, and the single-signer audit. **The case is NOT SIGNED** — the tree is an artifact,
attesting to it is an act, and §14 says what it does not establish.

**v5.63 (in place — HOST-PERF: the two measured host costs are fixed, and the M5
precondition is discharged):** not a COMCON feature — the two costs the M5 evidence run
turned up in September, fixed on their own terms and re-measured **A/B back to back on one
box** (comparing against numbers from an earlier session is how this project has been wrong
before). `shared.incr` miss **0.420 → 0.084 µs (5.0×)**, hit 0.077 → 0.062;
`req.headers['x-tenant']` **0.130 → 0.043 µs (3.0×)**, against 0.029 for a hoisted local.

**The store is now an open-addressed hash table, and deletion SHIFTS THE CLUSTER BACK.**
Tombstones would have been less code and would have failed under exactly the workload this
fixes: a rate limiter churning keys fills the table with dead markers, reports "store full"
while holding almost nothing, and recovering needs a 166 KB compaction under the spinlock
every worker shares. Backward shift (Knuth 6.4R) keeps the table dense and self-maintaining,
paying a bounded memmove on the cold path (delete/expiry) instead of on the hot one. The two
properties the old full scan provided incidentally are kept deliberately: an expired entry
is reclaimed **when it is probed**, and a freed slot is reusable immediately.

**`req.headers` is materialized once per request**, held on the request's opaque. One
consequence is now documented rather than discovered: a write to `req.headers` is visible to
a later read of it — before, it went to a throwaway object and vanished silently, which is
the worse of the two behaviours.

**Both fixes buy a new way to be catastrophically wrong, so both tests are built around
that rather than around the speed-up.** A hash table can hold a key it cannot FIND (a
deletion that breaks the probe chain reaching it) — so `t/js_shared_hash_table.t` runs 4000
mixed operations against a JS **model** and compares key by key, plus capacity, slot reuse
and expiry inside a cluster. A per-request cache can OUTLIVE its request — so
`t/js_request_headers_cache.t` checks isolation across requests, including two on one
keepalive connection, with distinct `Authorization` headers: the control (a cache made
static) fails exactly those assertions. Three controls total, all red where intended.

**Found while writing the expiry test:** `ngx_time()` is nginx's CACHED time, refreshed by
the event loop, so a handler that busy-waits blocks the very loop that would advance it — no
key can be seen to expire from inside one request. The first version spun 1100 ms and
watched the ttl stay at 1. The sleep has to happen where nginx can run, i.e. between
requests.

**What it says about M5:** re-measured, a host call costs 0.062 µs where a typed stub on a
resolved slot costs 0.032, so the gap a typed ABI could close is **~0.03 µs per call**, not
the 0.38 µs the scan was contributing — and the rest is JS→C dispatch and the spinlock,
neither of which lowering JS removes. One named residual, recorded rather than smuggled in:
`incr` still stores its counter as a STRING and does atoi + snprintf per call (~0.024 µs).

**v5.62 (in place — [TBD-2] resolved: the admission refusals have codes, and writing
them found a contract field that did nothing):** V12 dated the gap; this closes it.
**Two axes, deliberately not merged.** A DENIAL code names the gate that fired while a
fragment was RUNNING (`nginx.tenantDenials().byOp`); a REFUSAL code names why a fragment
was never admitted at all. Thirteen refusal codes — `E_ADMIT_{ARG, NOTBYTECODE, SOURCE,
DYNCODE, FREENAME, INTRINSIC, SCHEMA, TEST, CONTRACT, DEP}`, `E_CAP_GRANT`,
`E_PIN_IDENTITY`, `E_EPOCH_STALE` — closed and frozen in `ngx_js_compartment.h`, each
carried three ways: `.code` on the thrown Error (the contract), bracketed at the END of
the message (so an error log is greppable, and so no existing reader or test that
matched the prose stops matching), and `code` on the verdict `admit()` returns. A
CERTIFIED verdict carries no code at all — an empty code on success is a value someone
eventually compares against. `comcon.refusalCodes()` enumerates the set from the same
table the refusals are thrown from (V7: generated, never maintained), which is what lets
the V12 corpus check completeness in both directions; check [6] of the enumeration
checker does the same against the C table.

**WHERE THE LINE IS DRAWN — and it is the whole design decision.** A code is warranted
where the refusal is a POLICY OUTCOME about a fragment, something a deploy pipeline
should assert on. It is NOT warranted for a malformed call into a library function
(`query: empty selector`, `std.config.apply: arg0 must be a plan`): the fix there is to
fix the call, and coding it would invite CI to pin to our argument checks. Of 48 JS-side
throws, most are the second kind. Naming everything would make the taxonomy mean nothing.
Two families are named-but-empty on purpose, the `host:null` discipline again:
`E_BUDGET_*` (the deadline abort is the ENGINE's interrupt — there is no refusal of ours
at that point to label) and `E_CAP_FLAVOR`/`E_CAP_ESCALATE` (the JS capability layer's
own refusals — policy outcomes that DO deserve codes, and are the next tranche).

**THE DEFECT IT FOUND, in the writing of its own probe:** `contract.tests` was read with
a bare `JS_IsString()` and **silently ignored otherwise**, so `tests: [fn]` — the
spelling the plural key invites — was ADMITTED with the behavioural gate never run. A
contract asking to be checked, admitted unchecked, saying nothing. Now refused
(`E_ADMIT_CONTRACT`), the same direction the `intrinsics` narrowing already takes: a
contract that looks stricter than it is, is worse than an absent one. It was found
because the corpus row was written in the plausible-but-wrong spelling and the test
reported `accepted:true` — the probe was wrong AND the code was wrong, and only the
instrument could tell which. Five controls, each red on its own assertion: rename a code
in C (both enumeration directions + three assertions), drop `.code` from the throw (only
the own-code assertion), drop the bracketed code from the message (only the in-message
assertion), revert the fail-closed `tests` check (the defect returns, loudly), drop a
corpus row. `t/comcon_v12_denial_codes.t` now 14; MANUAL §3.2 rewritten around the two
axes, with the aspirational denial-record block marked as design rather than output.

**v5.61 (in place — V12: the denial codes are now a frozen contract, and the admission
side is shown to have none):** MANUAL §3.2 tells tenants "codes are stable across releases —
pin your CI to codes, not to message text." Nothing could keep or break that promise on
purpose. `t/tools/golden-denials.js` freezes each compartment code with a probe and the edge
it guards; `t/comcon_v12_denial_codes.t` (9) runs them and diffs
`nginx.tenantDenials().byOp` per probe, so a rename breaks here rather than in every tenant's
CI at once. Each probe must fire **its own code and nothing undeclared** — collateral (a
listener probe necessarily crosses `sock.listener` first) is declared in `also`, never hidden;
a code with no reachable path carries `unreachable` **with the reason**, which `enum.sockets`
does. Check [5] of the generated enumerations (V7) compares the corpus to
`ngx_js_denial_names[]` in both directions, so a code cannot be added without a probe or a
written reason it cannot have one.

**The finding is the ADMISSION side: it has no codes to pin to.** Undeclared free name,
dynamic code, a request field outside the sealed schema — all *message text*. The tenant told
not to pin to message text has nothing else available for admission; §3.2's taxonomy
(`E_CAP_*`, `E_ADMIT_*`, …) remains **[TBD-2]**. The corpus records today's prefixes as
`PROVISIONAL` so the gap is dated and visible, and the test states in its own assertion text
that they are not a contract. Five controls, the premise one included: renaming a code in
`ngx_js_compartment.c` and rebuilding failed three assertions plus check [5]; the fifth
guards the guard — `t/comcon_enumerations.t` now asserts the LAST check printed its banner,
so a checker that exits cleanly before an appended check is caught. Writing it also
caught a row that froze the wrong refusal — the dynamic-code probe *referenced* `eval` rather
than calling it, so the deny list refused it as a free name and the row would have passed
forever while attesting to a gate it never reached.

**v5.60 (in place — M-CFG's config instance: one tenant subtree onboarded through admit, end
to end):** the last named deliverable of increment E, and the first thing to compose D5b-1's
sound rejecter, D3's quotations, M4's typed registry and `std.ops`. `comcon.std.config`:
`review(source, policy)` → a typed, hash-pinned plan · `diff(plan, target)` → what would
change, **audit-first** · `apply(plan, target, {confirm})` → all-or-nothing, snapshotting ·
`rollback(result, target)`. `t/comcon_config_instance.t` (20), six negative controls.

**THE PROPOSAL NEVER EXECUTES**, and that is the mechanism rather than a precaution: a COM
capability cannot cross into a compartment, so a config fragment could not be handed the tree
even if that were wanted. The sound rejecter reduces the source to a **descriptor table** —
inert, diffable — and the operator applies the table with its own authority. FOUNDATION §2a's
*"tenant proposes what it cannot apply; the operator realizes"* is thereby a property of the
design rather than a convention someone must honour.

**REFUSAL IS BY SAFETY CLASS, NOT BY A BLOCKLIST.** The gate is derived from
`describe()`/`describeType()`: `safe` applies, `guarded` is admitted but demoted to
needs-confirmation and must be NAMED at apply time (POM.md §3 class-X semantics), `read-only`
is refused — so a COM member added next year is classified the day it is added. Review is a
pure function of (source, policy, registry), needing no live object, so a proposal can be
reviewed before anything is touched.

**Three things the composition taught that the parts could not.** (1) **`apply()` was not
atomic:** `root` was written, `proxy.pass` was refused by the COM setter, and the throw
discarded the snapshot the caller needed to undo the first write — a live subtree left
half-configured with no way back. The registry cannot prevent it, because whether an upstream
EXISTS is not something a type system knows; `apply()` now checks every gate before writing
anything and, on a setter refusal, restores what it wrote and names the failing op.
(2) **`proxy.pass` re-targets an existing `proxy_pass`** and cannot create one — a tenant may
be given a proxy location to re-point, not an arbitrary location to convert. (3) **A
`function`-typed member cannot be expressed at all** (21 of them on a location): a declarative
sentence carries only literals, which is stronger than refusing them by class.

**v5.59 (in place — M5 evidence, part 2: the payoff is not where the thesis puts it):**
part 1 (v5.58) showed that compiling a policy's JS buys ~1.0×, so M5's value, if any, is in
the typed stub ABI. Part 2 decomposes the host call itself (`t/tools/host-call-cost.t`,
2 M iterations, identical on both tiers), and the answer is that **the typed ABI is not the
expensive part**.

| per call | µs | |
|---|---|---|
| a plain JS function call | 0.034 | for scale |
| JS→C dispatch, boxed args | 0.039 | **the ABI floor — about the price of a JS call** |
| + `JS_ToCString` of the key | 0.033 | **not measurable: string marshalling is free** |
| typed stub, slot resolved | 0.034 | what M1's slab atomic looked like |
| + the spinlock | 0.041 | |
| **`shared.incr` today** (near-front hit) | **0.079** | +0.045 = the LINEAR SCAN |
| **`shared.incr` on a miss** (217 keys) | **0.417** | +0.38 = the scan |
| `req.headers['x-tenant']` | 0.130 | |
| …with `req.headers` hoisted once | 0.030 | **+0.10 = materializing the surface** |

The costs a typed stub ABI **uniquely** removes — dispatch, boxing, marshalling — come to
about **0.035 µs, roughly one JS call**. What dominates instead is a **linear scan over 256
slots comparing 128-byte keys under a spinlock** (2.3× on a hit, 12× on a miss) and
**re-materializing `req.headers` on every access** (0.10 µs, versus 0.030 µs when the surface
is hoisted). Both are host-side and fixable **today, with no compiler**: an index instead of a
scan, a per-request cached headers surface.

**This reframes the M1 gate.** M1's hand-written C had neither cost — a slab atomic on a
resolved slot, direct request access — so a substantial part of its **3.44×** is a
data-structure result that has been read as a compilation result. The honest next step before
any M5 commitment is to fix those two host costs and re-measure the gap; what survives is the
compiler's actual prize.

> *(Done at **v5.63**, same day: both are fixed — miss 0.420 → 0.084 µs, `req.headers`
> 0.130 → 0.043 — and the gap a typed ABI could still close is **~0.03 µs per call**. The
> numbers in the table above are the BEFORE column; read v5.63 for the after.)*

`nginx.__benchStub(mode, arg)` is the decomposition instrument (documented in
`src/js/ngx_js_com.c` as an instrument, not an API: it writes a slot by index with no key,
which no policy should be able to do).

**v5.58 (in place — M5 decision evidence: two measurements, and one belief corrected):**
the commitment question for the compiler track rested on two unmeasured beliefs.
`t/tools/policy-compute-split.t` measures both, **in-process** — a throughput benchmark on
this box runs through WSL2's mirrored-mode firewall, which adds a large fixed per-request cost
outside the thing under test and compresses every ratio toward 1.0, i.e. it would manufacture
the answer being looked for.

**Is a real policy compute-bound? No.** Interpreted vs `jitCompile()`d, same source, same
request, three runs: count_tag **0.93–1.07×**, ratelimit **1.02–1.28×**, jwtish
**0.91–0.96×**, routing **0.78–0.93×** — against a known-positive control at
**20.6–24.5×**. The control is the shape AOT-A measured at 5.79× end-to-end, so the harness
can see a speed-up and these policies simply do not have one. **The `jwtish` case is the
informative one:** a realistic 200-character token, a split, and a hash loop over the payload
— real work, still 0.92×, because the time is in the RUNTIME's string machinery (`split`,
`charCodeAt`, indexing) rather than arithmetic the compiler can unbox. So "compute-bearing"
in the AOT-A sense means **arithmetic in JS**, and string-heavy policies do not benefit either.

**How big is the minimum stub set? Single digits.** Extracted from the same policy sources
with the D5b-2 CST: `nginx.shared`, `req.headers`, `req.uri` — **3 members**, against a
classified COM surface of **339 members across 55 rows**. A stub ABI for policies of this
shape is roughly `shared.incr`, a header lookup and a uri read — not "type the COM surface".
That reframes M5's cost from a track to a milestone, *provided* the payoff is taken from the
stubs, which is where these numbers say it lives.

**A documented precondition retires.** R4 warns that generated C is not covered by
`JS_SetInterruptHandler`, so "until back-edge gas lands, the tier-2-eligible profile is
loop-free". Measured: a natively-lowered fragment (`compiled:1`) running an 8-billion-iteration
loop under a 150 ms meter is **interrupted at 150 ms**, exactly like the interpreted one.

**`nginx.jitStatus(fn)`** ships with it — D4c's `aotStatus`, for host functions, read-only.
It exists because the first A/B compared **compiled with compiled** and nothing could say so:
`jitCompile()` cannot answer "is this compiled", since calling it to find out changes the
answer, and its `skipped` field means "already compiled, ineligible, **or failed before**" —
at request time, where the gcc thread is dead, `skipped:1` meant "not eligible here", which
read as "already compiled". With `jitStatus` the arms report 0/1 compiled functions before and
after the warm-up, so the comparison is what it claims to be.

**v5.57 (in place — V11: policy mutation testing, the deny-suite's own verifier):** built
early (it is listed under M7/M8 but needs nothing from the compiler) because it is the
systematic form of the discipline this project applies by hand. A negative control asks *does
this test fail when I break the code*; mutation testing asks **does the suite notice when the
POLICY gets weaker** — which is the question that matters for a capability system, where a
regression looks like a permit nobody asked for rather than a crash.

`t/tools/policy-mutants.js` emits widen-one-permit variants of a base policy: one more field
through the membrane, the membrane removed, one more name in the manifest, the intrinsics
narrowing relaxed or dropped, `checkRequest` off, the meter off. The test records the
deny-suite's outcomes under the base policy and re-runs them per mutant — **killed if any
outcome differs; a survivor is the finding.** 12 mutants, all killed, none survived.
Equivalent mutants are **declared, not discovered**, and asserted in the other direction:
`imports+eval` must SURVIVE, because the deny list refuses `eval` whatever a manifest says, and
a suite that killed it would be reporting authority that does not exist.

**It corrected a belief on its first run**, which is the argument for having it: it killed
`imports+JSON`, labelled equivalent on the assumption that `intrinsics: []` excludes `JSON`
outright. It does not — `intrinsics` removes the **no-declaration free pass**, not the ability
to DECLARE a name, so `{intrinsics: [], imports: ['JSON']}` permits `JSON`. Coherent, and not
what "the narrowing excludes JSON" sounds like; the run said so about a policy written two
commits earlier (v5.55).

Three controls, all mutating the HARNESS rather than the engine, since the suite is the
subject: drop a probe and the matching mutant survives; make every probe report one outcome
and everything survives; declare a real widening equivalent and the two-sided check fires.

**v5.56 (in place — the audit/enforce mode switch goes FLEET-WIDE):** `std.ops` shipped the
rollout verbs with a doc note saying the switch was "per process". That was not a limitation
but a hole, and measuring it settled the question: on four workers, one `shadow()` call
followed by 24 requests gave **16 audit and 8 enforce** — the fleet in **mixed modes**,
nondeterministically. The dangerous direction is the common one: an operator calls
`enforce()`, gets `"enforce"` back, and some workers keep **auditing** — still allowing what
they believe they have begun denying. It is the same shape as the inert-`comcon.mode()` defect
fixed hours earlier (a verb reporting success while the system does not change), now
distributed.

The fix rides **D4b's transport**, not a new one: the mode lives in `nginx.shared` as
`{epoch, mode}`, and each worker **reconciles lazily** — one shared read before a fragment
runs, and before the mode is reported. Lazy pull, no broadcast, no stop-the-world; a worker
busy during the switch picks it up on its next fragment. A reconcile applies the mode
**locally and publishes nothing**, or every worker would bump the epoch and the fleet would
chase its own tail (asserted: 24 invocations across the fleet leave the epoch unchanged).
`nginx.shared` does not exist at config-eval time, so every access is guarded — a config-time
`comcon.mode()` sets the local mode and publishes nothing, which is right, since every worker
inherits it across `fork()`.

**Measured cost: +0.10 µs per fragment invocation** (a `nginx.shared.get`) against a 0.64 µs
do-nothing fragment — noise for anything that computes, ~16% of the degenerate case. The C-side
alternative (a plain shared integer read on the invoke path) is there if it ever shows.

**WHY IT SURVIVED REVIEW, which is the transferable part.** `t/comcon_std_ops.t` runs with the
default `worker_processes 1`, and a single-worker fixture cannot see a fan-out bug. The new
test's instrument then took three tries, each version green while measuring nothing: "several
workers" inferred from *repeated* per-worker counters (repeats are coincidences); a per-process
id from `Math.random()` (the runtime is created pre-fork, so every worker returned the *same*
"identity" and four workers looked like one); and finally `nginx.shared.incr()`, which is
genuinely per-process. The requests also had to become **concurrent** — under light sequential
load one worker wins nearly every accept. Until all of that was true, the invoke-path control
PASSED: the fix looked unnecessary because the test could not see the workers it was about.

**v5.55 (in place — `contract.intrinsics`: the allowance narrows, so a policy can suppress
it):** v5.54 made the intrinsics a floor that nothing could lower, and that **removed an
expressible policy**: before it, `{imports: []}` meant *no free names at all*; afterwards the
strictest expressible contract was "intrinsics and nothing else". Asked whether a policy could
suppress the intrinsics, the answer was no — a regression the allowance introduced, now closed.

`contract.intrinsics` takes a list, and the effective allowance is the static list **MEET**
that list: `{intrinsics: []}` refuses every free name including language values (the old
strictest setting, back), `{intrinsics: ['JSON']}` permits exactly that one.

**`imports` DECLARES, `intrinsics` NARROWS** — two knobs pointing one direction each, so
nothing here widens authority and the monotonicity story (V4) survives intact. Naming a
non-intrinsic in `intrinsics` is **refused**, with a message saying to use `imports`, rather
than ignored: a contract word that reads like policy and does nothing is the failure this
project keeps meeting. A narrowing switches admission **on** by itself (inert otherwise), it
survives `realize()` — the path `bindAt` and `std.ops.rebind` take, where dropping it would
silently un-narrow a live rewrite — and a **present-but-malformed** narrowing reads as the
strictest setting, the same fail-closed direction a malformed `imports` takes.

`std.profiles.tenant` and `pure_library` pass `opts.intrinsics` through, and **neither
defaults to it**: tightening a shipped profile silently would break fragments already
computing with `JSON`. `pure_library({intrinsics: []})` is the strictest contract expressible.

The V3 oracle models the narrowing (corpus now 14 × 9 = 126 cases), and five negative controls
cover it. Two of them had to be *added* rather than merely run: the first attempt at
"realize() carries the narrowing" and "malformed reads as strictest" both PASSED, because the
code was right and nothing tested it — a control that cannot fail is telling you about the
tests, not about the code. Recorded in `AUDIT_M-SES.md` §6 alongside the allowance itself.

**v5.54 (in place — the C3 intrinsics allowance: a third free-name category, decided not
inherited):** the V3 oracle's divergence (v5.53) is resolved by decision (user, 2026-09-12).
A free name now falls in one of THREE categories rather than two:

- **DENIED** — `eval`, `Function`, `globalThis`, `global`, `self`. Ambient-authority reach;
  no manifest re-admits them.
- **INTRINSIC** — a short list of language values a fragment computes WITH: `undefined`/
  `NaN`/`Infinity`, `Object`/`Array`/`String`/`Number`/`Boolean`/`BigInt`/`JSON`/`RegExp`,
  `Map`/`Set`/`WeakMap`/`WeakSet`, the `Error` types, `parseInt`/`parseFloat`/`isNaN`/
  `isFinite`, the URI helpers. **Admitted without declaration.**
- **DECLARABLE** — everything else, including every host name: must appear in `imports`,
  which is what makes a manifest mean anything.

**`Date` and `Math` are deliberately NOT intrinsic** — clock and RNG are the side channels
§3 of the audit leaves open — nor are `Promise` (scheduling past the invocation the deadline
measures), `Symbol` (`Symbol.for` is a runtime-wide registry: a channel between fragments,
not a value), `Proxy`/`Reflect` (object-graph tampering over held values, capabilities
included), or the `ArrayBuffer` family (`SharedArrayBuffer` is a channel). Each remains
usable by declaring it. The list is short on purpose: the cost of omitting a name is one
word in a manifest; the cost of wrongly including one is a silently wider gate.

**This widens a declaration requirement, not a reach.** Every intrinsic was already
reachable inside the compartment — a fragment ran with the standard intrinsics either way,
and one with admission OFF used them freely — so the gate had been *stricter than the
boundary it guards*, while refusing ordinary JS: `x !== undefined` needed `undefined`
declared, a name nothing is granted for, and `std.profiles.pure_library` (`imports: []`)
could not use `JSON` or `Object`. It can now.

Recorded in **`AUDIT_M-SES.md` §6 — changes to the audited surface after the signature**,
a new section that adds information without altering an attested claim: row (a) of §1 rests
on "no ambient HOST globals", which is unchanged, and the `t/comcon_mses_gate.t` probes
behind it are untouched. Nothing is re-signed.

Two things keep the decision from decaying: the V3 oracle models the three categories, so
the differential test fails if the engine stops matching; and **`check-enumerations.py`
check 4 compares the engine's list against the model's copy and fails the suite if `Date` or
`Math` is ever added to either** — a decision nothing checks is a decision a convenient edit
undoes.

**v5.53 (in place — the verification track's now-due column: V3 executable reference
semantics, V4 monotonicity-as-an-assertion, V7 generated enumerations):** three obligations
that §12's own table listed as due *now* and that had never been built. Each found something.

**V3 — a kernel ORACLE** (`t/tools/kernel-oracle.js`), written from the RULES rather than the
implementation, because an oracle derived from the code it checks agrees by construction and
detects nothing. It shares no code with `src/js` and never calls `comcon`; it models R-ENV,
R-ADMIT, R-MEDIATE, R-MEET and R-ZERO, and `t/comcon_v3_oracle.t` runs a **generated** corpus
(14 mediation chains × 5 admission settings) through model and engine and compares field by
field — after checking that the corpus is large and that the model's predictions
*discriminate*, since an oracle predicting one answer agrees with an engine doing anything.

**It diverged on the first run, and the divergence is a policy question rather than a bug:**
the C3 gate has a deny list (`eval`, `Function`, `globalThis`, `global`, `self`) and **no
intrinsics allowance**, so `undefined`, `JSON`, `Object`, `Math` are free globals that must
appear in `imports` — an ordinary `x !== undefined` is refused unless the operator declares
`undefined`, a name nothing is granted for. Consequences worth deciding rather than
inheriting: `std.profiles.pure_library` (`imports: []`) cannot use `JSON` or `Object`, and the
gate refuses to let a fragment *declare* intrinsics that the compartment *provides* (and that
a fragment with admission OFF uses freely). **Deliberately not widened** — the audit is
signed, and widening an admission gate is a decision, not a cleanup. What did change is the
diagnostic: *"free name not declared in imports"* instead of *"not granted"*, since the old
wording sent a reader hunting a missing capability.

**V4 — the lattice inclusion asserted**, in the two places authority could grow downward.
Re-mediation now computes the **attenuation meet** (field masks are a lattice; the meet is an
AND) and asserts `A(cap'') ⊆ A(cap')`; before, re-mediating an already-mediated capability
failed with *"grant is not a NginxSocket"* — fail-closed by accident, with a message about the
wrong thing. A `routes` glob has no computable meet, so it is re-mediated only by an identical
glob and otherwise refused; guessing would be the widening the check exists to prevent. And
`realize()` asserts the restricted env is a **sub-map** of the realizer's — true by
construction, which is why it is checked: a substituted cap is the shape a TCB bug takes, and
it would otherwise confer authority nobody granted.

**V7 — the three closed enumerations, checked from source** by
`t/tools/check-enumerations.py`, run by `t/comcon_enumerations.t` so drift breaks the suite:
p_symbol kinds (C enum vs POM.md vs the JS selector layer — where a `FunctionDeclaration` once
reported *stmt* at one tier and *function* at the other), **compile portals** (every place
`src/js` turns text into code — 13 functions, 20 sites, the operator REPL included, since an
operator session IS a compile portal), and the ops resources. **It found drift immediately:**
the audit/enforce/learn mode switch was in the code and absent from FOUNDATION §8a's list, so
§8a now enumerates it, and the snapshot store it named is enumerated in the code as the
binding store's quotation record. Two honest limits: the p_symbol list is *checked* against
three sources rather than *emitted* from one, and the portal list is an allow-list rather than
generated — both make drift loud, which is the property, with generation proper belonging to
the M2.5 registry walk.

Also fixed while here: my own instrument counted a **comment** mentioning `JS_EvalFunction`
as a compile portal, and a test probe built by `String.replace()` on a pattern that did not
occur was identical to the probe it was supposed to differ from — so the corpus passed while
covering nothing. Both are now asserted rather than assumed.

**v5.52 (in place — increment M-LIB step 2: `std.ops`, administration as library code; and a
runtime mode switch that was silently inert):** FOUNDATION §8a says there is no management
plane — comconctl is a shell, not a tool, and every verb is an ordinary library program over the
kernel plus the **ops-resource capabilities**, which is what makes v2 §9.3's "the tooling never
needs a backdoor" **derived** rather than asserted. `comcon.std.ops(resources)` is that
derivation made executable: **a session takes its resources as arguments and reaches for no
ambient authority**, and a verb whose resource was not passed is **absent from the session**
rather than present-and-throwing. So "what can this session do" is answered by `Object.keys()`;
a session given nothing has only `describe()`.

**The third closed enumeration is now checkable.** All seven §8a resources are enumerated,
including the two with **no host spelling** (`provenance` — grant-chain registry; `signing` —
signing key): `host:null` is what makes a gap visible instead of absent, and the verbs needing
them (`revoke`, `cosign`) are reported as *withheld, with the reason*. `describe()` walks the
same table the session is built from, so the enumeration cannot drift from reality (ROADMAP §12
**V7**: generated, never maintained).

Fifteen verbs ship, each decomposing over something real: `denials`/`learn` (the report caps),
`shadow`/`enforce`/`learnMode` (the audit-first rollout = `comcon.mode`), the binding-epoch
store over `bindAt` handles where **snapshot = quote**, `rewrite` (SHOWCASE §38 in one verb:
`harden` + rebind), and `trustReport`. **`remove` is class X** and is guarded as that class
demands — snapshot-first plus a confirmation that NAMES the binding, so a `{confirm:true}`
pasted from another call cannot remove the wrong one.

**THE DEFECT STEP 2 FOUND.** `comcon.mode()` wrote `jcf->tenant_mode`, but the mode that
*gates* is a static set once by `ngx_js_compartment_policy_init()` at the end of config load.
So `mode()` worked during the host eval and was **silently inert at request time** — precisely
when an operator runs it. An operator calling `enforce()` on a running server got "ok" and kept
**auditing**: still allowing what they believed they had begun denying. It surfaced because
`std.ops` reads the mode back through the denial report, and the two disagreed.
`ngx_js_compartment_mode_set()` now switches the effective mode **without resetting the
counters** (switching audit → enforce must not destroy the evidence that justified it), and
`comcon.mode()` returns the effective mode so a caller can verify rather than trust. **Per
process:** there is no fleet-wide mode fan-out, and `std.ops` reports the scope.

`t/comcon_std_ops.t` (26) asserts the rollout as ENFORCEMENT, not as a label — the same A1
reach probe is denied under `enforce` and allowed under `shadow`, switched at request time —
plus six negative controls. Scope in `INCREMENT_MLIB.md` §6.

**v5.51 (in place — increment M-LIB step 1: the standard policy library, and the fail-open
it found first):** increment D finished the kernel, and finished is not usable: there were
**20 kernel operators and no `std.*` at all**, while MANUAL.md was written as-if-shipped
against `std.profiles.tenant(acme)`. A correct `include()` needs four contract fields to agree
with an environment built by three other operators, and the agreement fails in two opposite
directions — omit a granted name from `imports` and admission refuses the fragment
(fail-closed but baffling); list an ungranted one and the fragment sees `undefined` at
runtime. **`comcon.std.profiles.*` makes that agreement once**, with `imports` DERIVED from
the env so the manifest cannot drift from the grants, and tenants **bounded by default**
(the audit's §3 list names "host JS unbounded by default" as accepted residual risk — a
tenant profile inheriting only the 5s ceiling would repeat it deliberately).

**The one rule for a profile: only fields the kernel ENFORCES.** MANUAL's
`{profile:"restrictive", onViolation:"audit"}` and `std.postures.*` are NOT shipped, because
nothing reads them — `realize()` knows only `profile:'declarative'`. A posture assembled from
ignored keys would read like a policy and do nothing, which is worse than its absence: it
would be believed, by exactly the reader least able to check. `std.describe()` states, per
contract field, **what enforces it**, and names the absent vocabulary.

**The fail-open step 1 closed first.** A library generates mediation descriptors
mechanically, so their failure mode is the library's. `include()`'s flavor translation fell
through to its default `{kind:0, mask:FULL}`: a descriptor the enforcement layer does not
implement (`allowHosts`) or a one-letter typo (`redcat` for `redact`) **granted the capability
in full** — a misspelling that WIDENED authority, measured (`s.address` read as a string
through both, where `redact()` hid it). The vocabulary is now closed
(`revoke`/`redact`/`allow`/`routes`) and refused at `mediate()`, at the producer; and
`mediate()` **snapshots** the descriptor, because checking at `mediate()` and reading at
`include()` is a time-of-check/time-of-use gap that reopened the hole from the other end
(`var it = redact([...]); mediate(sock, it); it.flavor = 'redcat'`).

`t/comcon_std_lib.t` (19), seven negative controls, scope in `INCREMENT_MLIB.md`. Absent by
decision, each with its reason recorded there: `std.ops`, postures, the host/ttl/window
vocabulary (needs C-side enforcement), the "raw operators withheld" governance half, and
interceptor certification criteria — which do not apply yet, because in this implementation
interceptors are inert descriptors interpreted in C, never functions that close over caps.

**v5.50 (in place — increment D4c: the compiled tier under a live epoch switch;
INCREMENT D IS COMPLETE):** scoped by measurement rather than by the plan's wording, because
the measurement changed the answer.

**The safety half holds, and is what was tested first:** a live rewrite of a fragment that WAS
lowered to native C is answered by the new epoch — never by the old `.so` — and rollback
restores the retained epoch. A stale `.so` still serving requests after a rewrite is the worst
failure class F exists to prevent.

**The re-AOT half is not a worker's to do.** A live epoch switch is built in a worker,
post-fork, and the JIT's gcc thread does not survive `fork()`, so the new epoch runs the
**bytecode fallback**: correct, coherent, interpreted. POM.md §4's `re-AOT ──▶ live(e+1)` arrow
is the one a worker cannot walk. Reaching it needs a compiler-bearing process (the master keeps
its thread) to build the `.so` and workers to pick it up from the hash-keyed JIT cache — new
IPC, a separate increment, and not needed for correctness.

**What was actually missing was the ability to TELL**, and that was a defect, not a gap.
`js_comcon_aot_compile()` returns 0 for any bytecode function — "eligible", never "compiled",
as its own header says — and the include site logged *"include fragment lowered to native C"*
on that 0. On every include. Including every request-time epoch switch, in a process with no
compiler. `comcon.aotStatus(fragment)` → `{jit, functions, compiled}` now answers it
(read-only: asking never compiles; `compiled` is the only field that means native code exists),
and the notice says `NATIVE (n of m functions)` or `BYTECODE (m functions, nothing lowered: no
compiler in this process)` — two messages sharing no substring, so a log reader grepping for
one can never match the other.

`t/comcon_aot_epoch.t` asserts the asymmetry directly: the same include path logs NATIVE at
load (master, pre-fork) and BYTECODE at request time, on the same fragment.

**v5.49 (in place — increment D5b-4: cross-file provenance; increment D's D5b line is
complete):** **a span now says which base it counts in.** This closed a real defect, not a
formality: `pom(fn).line0` was **18** and `pom(fn).cst().line0` was **1** for the same
function, with no file named anywhere — the same field meaning two things at two tiers, so a
denial record built from one and read as the other points at the wrong place and looks right.
Bytecode-tier spans now carry `base:'file'`, `file` and `col0` (`col0` was already computed in
`comcon_pom_fill` and discarded); `cst()` spans carry `base:'node'` and no file, so they cannot
be misread as absolute; and `node.origin()` converts node-local → absolute against the origin a
view inherited from the bytecode node it came from, or an explicit
`comcon.cst(source, {file, line0, col0, offset})` for text you hold rather than a live
function (the §38 shape). The column shift applies to **line 1 only** — later lines start at
their own column 0.

**`origin()` returns null when the origin is unknown, and an absolute `range` only when a byte
offset was supplied** (the bytecode tier has none: `pc2line` maps lines, not offsets).
Inventing a plausible location is the source-map lie — a denial record naming the wrong
file:line is worse than one that admits it does not know.

**The include hop (POM.md §6 Q3).** A fragment's synthetic file origin is `<comcon-fragment>`
(`NGX_JS_COMCON_FRAGMENT_ORIGIN`, now one constant shared by the eval site and the error path),
and a fragment failure reports `... at <comcon-fragment>:LINE:COL` — only that token is copied
out of the inner stack, never the rest, which also names host frames and host paths. So
MANUAL §7.4's denial-record `where` is fillable at the tier where it matters most. The line is
the AUTHOR's line because `include()`'s wrapper preamble contains no newline: that is a
**contract**, pinned by `t/comcon_pom_origin.t`, because adding one shifts every line in every
fragment error, frame and span by one, silently, at every tier at once.

And `pom()` now **refuses** a confined fragment's bound wrapper. It used to describe the
wrapper — a two-line closure in `<comcon-bootstrap>` — and answer every query about it: a wrong
answer shaped exactly like a right one. It names what to use instead.

Found while writing the negative control for the preamble contract: the wrapper's buffer size
was computed from a **separate literal** of the same text, so editing the wrapper without
editing the `sizeof` overflowed the allocation by the difference. The three pieces now derive
from one definition each (`NGX_JS_COMCON_WRAP_HEAD/MID/TAIL`).

**v5.48 (in place — increment D5b-3: source-rewrite hardening):**
`comcon.harden(node, query, wrapper)` replaces every site a query matches with the wrapper
quotation, `$$` standing for the site's own source, and returns a report whose `quotation`
installs through D4 (`bindAt`/`replace`). **The rewrite produces TEXT, never an install** —
so a hardening pass is reviewable, diffable and admissible before it runs, and the authority
to change a live site stays where D4a put it. `comcon.cst(source)` arrived with it: the same
CST view over plain TEXT rather than a live function, which is the §38 shape (you have vendor
source you may not edit), and what lets a rewrite be checked structurally instead of by string
compare.

**What it is for, against what already existed.** For a FREE name the capability kernel is
strictly better — `grant(env,"fetch",mediate(cap,guard))` needs no parser and cannot be
evaded by spelling. harden() exists for the residual the kernel cannot *name* and D5a's
bytecode audit cannot *see*: a locally-bound callee, a method call, one site at a position.
`t/comcon_pom_harden.t` hardens `var g = real; g('a')` and asserts the guard STOPS the call
(the wrapped function's log is empty), with the unhardened fragment running in the same
request as the control.

**Three limits, recorded because they are easy to overread.** (1) The wrapper must be ONE
`ExpressionStatement` — that is what refuses `__guard($$); evil()`, which splices to three
*valid* statements and so cannot be caught by re-parsing; it is NOT a general injection
defence, since a comma sequence is also one expression and a wrapper is code. **What bounds a
wrapper is the env it is realized under, never its syntax.** (2) Overlapping matches are
refused rather than half-rewritten (whichever is spliced second would discard the first).
(3) A **host function cannot be granted** into a compartment — only C-wrapped COM
capabilities cross — so a wrapper on the confined tier must carry its own logic or call a
granted capability; the test asserts the refused install leaves the live site untouched.

Found by composing the two: D4a's `replace()` pushed history *before* realizing, so a
replacement that failed admission consumed a rollback slot and the next `rollback()` restored
the epoch already live. It now realizes first — all-or-nothing.

**v5.47 (in place — increment D5b-2: full CST, finer selectors, and the anchors model):**
acorn 8.14.0 is vendored as a TCB artifact (`src/js/vendor/`, provenance + sha256 + a generator
with a `--check` mode) and exposed fail-closed as `comcon.__parse`; `node.cst()` maps ESTree onto
the kinds D0 reserved at the substrate (block=3, stmt=4, expr=5) with **node-local** spans, so the
tree descends below function granularity and sees what a bytecode scan cannot — a locally-bound
callee (`var g = fetch; g()`) or a method call. `query` gains `block`/`stmt`/`expr`/`call(glob)`/
`type(glob)`/`anchors(glob)`/`line(N)`/`line(N-M)`.

**The anchors half is §9's inline binding, finally built** (and ROADMAP §4 item 1, the last
unbuilt item of the minimal first slice): `"use comcon: <name>";` is an INERT MARKER NAMING A
SITE — a directive-prologue string, a no-op statement in plain JS — exposed as a node ATTRIBUTE
and selectable by name, while the policy stays in a separate unit referencing it. So policy text
is never trapped in a string literal, and a policy targets a site that **survives edits above
it**. That is asserted rather than argued: two fragments identical but for two inserted lines,
where the anchor selector finds the site in both and the `line()` selector finds it in exactly
one. Anchor recognition reuses acorn's own `directive` determination (a hand-rolled prologue scan
accepted a parenthesized string, which the language does not) and matches the **raw** spelling, so
an escaped name that decodes to `checkout` binds nothing — the name a reviewer reads is the name
that binds. `anchors()` on the bytecode tier **throws** rather than reporting no sites.
`t/comcon_parser_vendor.t`, `t/comcon_pom_cst.t`, `t/comcon_pom_anchors.t` (five negative controls,
each named in the test header with the test it breaks).

**v5.46 (in place — increment D5b-1: the declarative-profile checker):**
`comcon.reviewDeclarative(source)` implements the `syntax_allowed` declarative profile (§8 /
SEMANTICS §4.4): a small pure-JS recursive-descent parser that accepts only a straight-line sequence
of fluent call-chains over dotted name paths (literal / nested-chain / free-name-ref / obj / arr
args) — no loops, conditionals, operators, assignments, computed access, or functions — and returns
diffable **descriptor tables**. It is a **sound rejecter**: what it accepts is exactly what reduces
to the tables, so an untrusted config/policy proposal becomes *soundly reviewable*, not merely
runtime-validated. `realize(q, {profile:'declarative'})` refuses a non-declarative proposal at
admission. This is the one platform hook the config-language pattern needs
([[PATTERN_config_language.md]] / config-DSL is otherwise userland); the full CST + source-rewrite
(D5b-2+) stays a separately-gated milestone. `t/comcon_declarative.t`.

**v5.45 (in place — increment D5a: call-site enumeration / audit):** `node.references(name)` /
`node.callsites(name)` enumerate every reference to a free name or method `name` in a fragment (with
line numbers), and the subset that are actual call sites — the intensional **audit READ side** of
SHOWCASE §38 ("where is `fetch` called?"). Built from a **bytecode scan, no parser**
(`js_comcon_pom_callsites`, quickjs.c): the callee↔call correlation is exact because the operand
stack is tracked, so a nested-argument call `fetch(helper(2))` is still attributed to `fetch`.
Records `{name, line, method, call}`; free names via `OP_get_var`/`get_var_ref` →
`closure_var[].var_name`, methods via `OP_get_field` atoms; locally-bound callees need the CST (D5b).
Reframing that made this cheap: §38's **enforcement** is already the capability kernel's — a
fragment's `fetch(...)` resolves `fetch` as a free name bound through the manifest, so "harden every
use of `fetch`" is `grant(env,"fetch",mediate(cap,guard))`. D5a supplies the *audit* half; the two
compose into "audit + enforce" with no parser. `t/comcon_pom_callsites.t`. Full-CST source-rewrite
hardening (D5b) stays a separately-gated milestone.

**v5.44 (in place — increment D4b: class-F multi-worker fan-out):** `comcon.bindShared(key,
quotation, contract, onRequest)` is the multi-worker spelling of `bindAt` — the class-F ("fan-out
required") mutation class made real (POM.md §3). The current `{epoch, source}` is the single source
of truth in `nginx.shared` (lock-free, instantly visible to every worker); each worker's handler
**reconciles lazily** on each request — reads the shared epoch and, if newer, recompiles the shared
source **in its own compartment** and swaps (rebuild-on-write per worker), freeing the old fragment.
So a `replace()` in any one worker fans out to all of them coherently: no worker serves a torn state,
and only the source string crosses realms (never a JSValue). Lazy-pull (shared KV as truth +
per-request reconcile) was chosen over eager push — strictly coherent and simpler. Reuses
`nginx.shared` as the transport (no new broadcast mechanism — [[pilgrim-shell-fundament-principle]]).
`t/comcon_pom_fanout.t` (4 workers: all-v1 before, all-v2 at epoch 1 after one replace). Compiled-tier
live re-AOT (D4c) and the full-CST front-end (D5) remain.

**v5.43 (in place — increment D4a: POM mutation = rebuild-on-write + epochs):** `comcon.bindAt(site,
quotation, contract)` installs an admitted quotation at a live binding **site** and returns a frozen
**epoch handle**. POM mutation is **rebuild-on-write** (POM.md §4): `replace(q)` recompiles the
admitted quotation (via `realize`) into a *new bound fragment* and swaps the site (new epoch,
retaining the prior for `rollback`) — not an in-place bytecode edit, which QuickJS forbids. The site
is an `install(callable, epoch)` fn the caller wires to the existing COM setter (`loc.handler = …`),
so there is **no parallel install path** ([[pilgrim-shell-fundament-principle]]). `remove()`
tombstones (class X), `revive()` restores; `describe()` classifies each op R/L/F/X (the COM safety
taxonomy generalized to code, POM.md §3). Rollback history is bounded and superseded fragments are
freed (`__freeConfined`), so live rewrite does not accumulate. `t/comcon_pom_mutate.t`. (Reuse
finding: install / tombstone+revive / fan-out / snapshot-rollback already exist in js_com — D4 is a
thin epoch layer over `realize` + the COM setters. A pre-existing pool-lifetime bug that request-time
`replace` exposed — `comcon_frags` grown on a stale config-eval pool — was fixed with a dedicated
long-lived pool.) Multi-worker class-F fan-out is D4b; compiled-tier live re-AOT is D4c.

**v5.42 (in place — increment D3: POM-node quotations + stone splices):** `comcon.quote(source,
splices?)` now accepts producer **splices** — the data half of §6's closure-vs-quotation and §4.4's
two-phase binding. Each splice is deep-checked **stone** (cap-free: no functions, capabilities, or
accessors — a getter could mint a cap lazily, TOCTOU-unsound), a stage-0 error at the **producer**;
this is the strengthened cap-free rule (R2) that keeps the closure/quotation distinction *checkable*.
`realize` binds each splice as a **JSON literal in an enclosing IIFE var**, so the quoted code
resolves the splice name to escaped *data*, never text — a spliced string cannot smuggle a `bind()`
call (JSON.stringify escaping is the parameterized-SQL defense, scenario 3), and the names become
bound closure vars invisible to the admit free-name gate. A POM node's own `quote()` (D1) is now
**realizable** — `realize(node.quote(), K, env)` compiles + runs a real subtree, joining the
reflective tree to the operator kernel. Pure-JS, no engine change. `t/comcon_pom_splice.t`.
Structured POM-node splices (into a *parsed subtree*, preserving sub-node handles) and `includeAt`
anchor-**site** insertion (a tree mutation) await D4/D5.

**v5.41 (in place — increment D2: the POM selector language):** `node.query(sel)` — the target
sub-language (POM.md §6 Q2) over the NodeView subtree, at coarse granularity: `selector := term
('within' term)*`, `term := factor+` (AND), `factor := module | function | * | name(glob)`, glob
= exact/prefix/suffix/contains. `A within B` selects nodes matching A that have an ancestor matching
B — the intensional composition ("functions within module X") that hardening requires. It is a
value-level DSL interpreted under the handle (no new host grammar — §7's "one policy language"),
results are NodeViews (a query result is itself quote()-able), and selection is **born-bound (R9)**:
`query` is a pure function recomputed live on every call, never a snapshot, so when D4 calls it at
admission a newly admitted node that matches enters already governed. Pure-JS, no engine change.
`t/comcon_pom_query.t`. Finer selectors (`callsites(fetch)`, span/anchor predicates) extend the
grammar at D5 when statement/expression nodes exist.

**v5.40 (in place — increment D1: the lazy NodeView surface):** `comcon.pom(fragment)` returns a
reflective `NodeView` tree over a compiled fragment (module/function granularity) — the read half
of `POM.md`'s node interface. Fields `kind/id/hash/span/childCount/name`, lazy `children`/`parent`
getters, methods `text()/quote()/describe()`, a redacted `binding`; each node is **frozen**. The
load-bearing property: **reads return quotations** — `text()`/`quote()` hand back v5.38
`comcon.quote()` values (inert, frozen, cap-free), never raw source, so reflection cannot leak
authority (SEMANTICS REFLECT); source visibility is exactly the presence of a read-capable handle.
`describe()` lists the read ops with safety class `R` (describe ⊇ mutable — writes land in D4).
Identity: creation-ordered `id`s (stable within a process, keyed by content hash + path) and
content `hash` for pin-by-hash (R7). Backed by one GC-safe C accessor
(`js_comcon_pom_node_at` — no held pointers; the root fragment is kept alive by the JS closure), so
navigation is flat (`t_stress/com_pom_navigate.t`). `t/comcon_pom_nodeview.t`. Cross-restart id
persistence, expression-granularity laziness, and `reach/ops` handle attenuation are deferred to
their natural later phases (INCREMENT_D.md).

**v5.39 (in place — increment D0: the POM substrate):** the reflective Program Object Model
(§3, `POM.md`) gets its foundation. QuickJS keeps no AST, but each compiled fragment's
`JSFunctionBytecode` already carries its own source slice, a pc→line table, and its nested
functions as cpool constants — so the **granularity floor** (module + function) is materialized
directly from the bytecode tree with **no parser** (`js_comcon_pom_inspect`, `quickjs.c`, beside
the existing free-globals/dynamic-code helpers). The `p_symbol` enumeration is fixed as schema
**`comcon-pom-1`** (`module=1, function=2, block=3, stmt=4, expr=5`; never renumber, only append);
block is an intra-function scope and stmt/expr need the full CST, so 3–5 are enumerated but
synthesized in later phases (D5). Content identity is FNV-1a over the source slice (R7 pin-by-hash).
A diagnostic bridge `comcon.__pomInspect` reflects a fragment as a node tree
{kind,name,span,hash,children} — the lazy `NodeView` JS surface (`text/quote/describe/query`) is
D1. `t/comcon_pom_substrate.t`. Scope + phases in `INCREMENT_D.md`.

**v5.38 (in place — `realize` + `quote` resolved: the quotation half made real):** the
closure-vs-quotation split (§6) now has both sides shipped. `comcon.quote(source)` is an inert,
frozen, **cap-free description** (zero authority — a source string carries no capability, so the
stone rule holds trivially); `comcon.realize(q, contract, realizerEnv)` gives it force under the
**realizer's** authority — the operator-realizes-a-tenant-proposal path (showcases 46–47), the
inverse of `bind`/`include` (which use the *producer's* env, closure discipline). Least-authority
realization (R6) is enforced: the contract is **mandatory**, and the realization environment is
`ρ_R ↾ manifest` — the realizer's grants **restricted to the quotation's declared free-name
manifest** (`contract.imports`) — so a proposal reviewed as "needs a,b,c" can never reach anything
else the operator's session holds (the confused-deputy fix). `realize` **refuses a closure** as
arg0 (a bound `include()` result), making the closure/quotation *bit* machine-real; the existing
`admit` gate enforces free-names ⊆ manifest, charging refusal at the realizer. `t/comcon_realize.t`.
**Deferred with the reuse-the-primitive fundament: `includeAt` is NOT built** — over a concrete COM
node it is exactly `loc.handler = realize/include(src, K)` (already works), so a standalone operator
would duplicate a js_com primitive; its only non-redundant form is query/selector targeting, which
awaits the POM tree. **Structured `${…}` splices and POM-node quotations** (quote of a *parsed
subtree*) likewise await POM nodes (increment D).

**v5.37 (in place — `bind` resolved: the env-first spelling of `include`):** `bind` was a
misnamed metered-closure wrapper (it did not confine — a host closure has already captured its
env). It is now the real kernel bind: `bind(env, source, opts)` COMPILES the source in the
confined compartment under the env (delegating to `include` with `grants` from
`grant(env(), …)` + `imports`/`meter`/`tests`/`identity` from `opts`), so free names resolve only
through the env's grants + intrinsics — `bind(grant(env(),"x",cap), src, {meter})` ≡
`include(src, {grants:{x:cap}, meter})`. This makes the `env`/`grant`/`bind` capability layer
coherent and actually enforcing, and unifies it with `include` (the contract-first spelling). The
dead `__runMetered` helper is removed. `t/comcon_operators.t` (bind compiles + threads args,
meters a runaway loop, AND confines — the bound fragment cannot reach the host). A standalone
`bind` over a *parsed POM node* awaits POM nodes (increment D). *(v5.38: `realize`/`quote`
now ship the quotation counterpart of this closure-first `bind` — see above.)*

**v5.36 (in place — `admit`'s test-phase: behavioral admission):** `admit` gains its third phase
(SPEC §4 `(Γ, φ, T)`): `include(source, {tests})` runs the contract's `tests` — a
`function(fragment){…}` — against the compiled fragment IN the confined compartment, under the
TENANT reach gate, and REFUSES admission if any test throws. Zero blast radius: the tests run with
no host authority in scope and IO denied, so a host can admit an untrusted / AI-generated fragment
by verifying its BEHAVIOR before granting it authority ("tests bound correctness, capabilities
bound damage" — Principle 3, threat T3). `t/comcon_admit_tests.t` (pass admits; a failing/throwing
test refuses with a reproducible reason; a test that reaches for the host is itself refused).
Remaining refinement: swapping the clock/RNG for fixed doubles during the run (the reproducibility
half of "determinism caps"; the security half — host authority + IO denied — already holds).

**v5.35 (in place — the CONVERGENCE is complete; ONE confined mechanism):** the whole
directive-driven "tenant" path is retired. The five `js_tenant_*` directives
(source/mode/dependency/artifact/handler) and the tenant compartment subsystem
(`eval_tenant_sources`, `onRequest`/`tenant_request_handler`, `js_tenant_handler`'s content
handler, `grantToTenant`, the tenant `_ctx`/`_rt`) are DELETED. Every confined fragment —
request handler or config fragment — is now `comcon.include(source, contract)` bound through
the EXISTING `location.handler` (or another COM setter). This is **Principle 11 realized**:
"extend by granting, never by syntax" — the confined mechanism is a *granted operator* over
the one COM, not a directive. Shared functions (`ngx_js_tenant_context_new`,
`ngx_js_tenant_lockdown`, `ngx_js_learn_seed`, the recorder, `ngx_js_com_install_protos`,
`ngx_js_compartment_*`, `comcon.mode`, `jcf->tenant_mode`, `nginx.tenantDenials/tenantLearning`)
are kept — the include compartment uses them. Full track in INCREMENT_CONVERGE.md (P1–P6).
comcon 21/159 (include-only), t/ 266/3412, t_stress 16/80, both builds.

**v5.34 (in place — the compiled tier follows include; SR-2 holds for include):** one call
(`js_comcon_aot_compile(comcon_ctx, fn)`, `#ifdef CONFIG_JIT`) lowers an include fragment to
native C; the invoke's `JS_Call` dispatches to the compiled `jit_func`. The open risk — does
maxim's AOT lower the CAP-CLOSURE shape (a fragment closed over granted-socket/dep params)? —
resolved POSITIVELY. `t/comcon_include_faithfulness.t` runs interpreter-vs-AOT over the
confinement surface and asserts identical responses AND denial counters (T2 refines T1 for
include), `all_compiled` non-vacuous. The compiled tier no longer depends on the tenant
`onRequest` handler — which is what unblocked v5.35's removal.

**v5.33 (in place — include reaches interpreted parity with the tenant path):** `include`
composes the C3 gate — factored into `ngx_js_comcon_admit_check` shared with the `admit`
operator — when `contract.imports` is present (opt-in), plus an optional identity pin
`H(H(source)‖schema)` via `contract.identity`; `contract.deps=[{name,path,sha256}]` loads
pinned pure libraries as per-fragment CLOSURE PARAMS (`ngx_js_comcon_eval_dep`, hash-verified,
bare-global eval); learn-mode recorder seeding added to the include compartment (keyed on
`jcf->tenant_mode`, since the compartment is built during host eval *before* `policy_init`
applies the mode). The request/response "serve" helper was scoped and then **resolved as a
NON-GAP** — `location.handler` + `req.respond`/`req.json` already suffice, so no kernel
`comcon.serve` (reuse the js_com primitive; the recursive-inclusion fundament). The tenant
scenario tests migrated to `comcon_include_*` siblings.

**v5.32 (in place — `mediate()` is an enforced membrane, on sockets and COM nodes):**
`mediate(cap, interceptor)` realizes `A(cap′)⊆A(cap)` in C, no cross-realm object. A socket
carries a per-wrapper FIELD MASK (`comcon.revoke/redact/allow` → bit per getter magic;
`ngx_js_socket_wrap_masked`; a masked field reads `undefined`). A COM node is mediated by a
`NginxComFacet` (`comcon.routes(glob)`): a thin, STATELESS cap that *borrows* the ONE canonical
server op and routes `paths()`/`allowed()`/gated `addLocation`/`removeLocation` through a route
glob. The facet is required because a COM server node is STATEFUL (per-wrapper
`prefix_locs`/`dyn_pool`/`tree_pool`); re-wrapping it into the separate compartment runtime
(the socket pattern) would diverge the live location tree and UAF at teardown — so it is never
re-wrapped, only borrowed.

**v5.31 (in place — live-cap grants + M-SES-1b + three response-path security fixes):**
`include(src,{grants:{name:sock}})` re-wraps a granted socket compartment-native (a fresh
wrapper around the same C handle, reach-gated) and binds it as a closure param; the invoke runs
under `ngx_js_compartment_enter(NGX_JS_COMPARTMENT_TENANT)` so the A1 gate confines it.
**M-SES-1b** makes the grantable cap prototypes non-extensible in the confined compartment
(`ngx_js_comcon_harden_cap_protos`) — the flagged prerequisite of the grant model (a live grant
makes the cap proto reachable via `getPrototypeOf`). **Three vulnerabilities were surfaced by
routing confined handlers through the shared `req.respond`/invoke paths — and fixed there, so
they now protect EVERY js_com handler (host and confined):** (1) response-header **CRLF
injection** (`ngx_js_header_has_crlf` drop); (2) response-**framing smuggling** — a handler-set
`content-length`/`transfer-encoding`/`connection` emitted a duplicate framing header
(`ngx_js_header_is_framing` drop); (3) **HIGH — a reach attempt hidden in a return-value getter
ran as HOST_ROOT** because the include invoke JSON-materialized the result *after*
`compartment_leave`; fixed by materializing under TENANT. (SR-1's HIGH-1/MEDIUM-2 were fixed for
the tenant path earlier; they had not been ported to the general response path — this closes
that.) TM-1 denial-log quotas+sampling is IMPLEMENTED (`comcon_include_denial_log.t`).

**v5.30 (in place — `comcon.include`: scope-isolated confined fragments):** a fragment is
compiled in its OWN runtime compartment (`jcf->comcon_rt`, mirroring the tenant compartment:
curated intrinsics + `ngx_js_tenant_lockdown` + the COM protos), held C-side
(`jcf->comcon_frags`), and invoked IN the compartment with arg/result JSON-marshaled — only
strings cross, so no JSValue crosses the realm/runtime. Own runtime after two reverted attempts
(a cross-realm crash, then a shared-runtime leak); `jcf` is cached in a module-static because
`ngx_cycle` is not the current cycle during `init_conf`. Gives authority isolation
(`typeof nginx → "undefined"`), a metered abort, and clean own-runtime teardown.

**v5.29 (in place — the M-CFG kernel operators + the shell fundament):** FOUNDATION §4's four
operators are realized as GRANTED NAMES on the host `comcon` object —
`comcon.{env,grant,mediate,bind,admit,include,meter}` — with `include = parse∘admit∘bind`.
`admit` reuses the C3 gate; `bind`/`meter` tighten the worker gas deadline around a call. The
governing principle, recorded from user direction: **pilgrim/js_com is a non-invasive SHELL
around an EXISTING nginx; its role is DYNAMIC configuration** (modify/shrink/extend the *live*
config via the COM); the operator's `nginx.conf` FILE is untouched except the single `js_source`
directive; **NEVER add nginx directives for confinement** — the `js_tenant_*` directives violate
this and are retired for host-JS operators; a per-request timeout is `meter` mediation, not a
directive. OPERATOR_API §8 decisions resolved (imported `comcon` module; root cap set; meter
units timeoutMs-now/gas-later; closure default; L-rights init-time scope). Full track in
INCREMENT_MCFG.md.

**v5.28 (in place — SR-3 escape-completeness audit PASSED, one freeze gap fixed):** the
scheduled adversarial pentest of the hardened tenant context (curated intrinsics +
M-SES-0 taming + M-SES-1 freeze + gas) found **no sandbox escape** — every dynamic-code
route stays tamed (`(new Error()).constructor.constructor`, bound-fn, `Symbol.species`
all throw), strict `this` is `undefined`, the core + iterator + shared generator
prototypes are frozen, unbounded recursion is caught. Two completeness findings, neither
an authority escape: **SR3-1 (MEDIUM, FIXED)** — the M-SES-1 harden walks property
*values*, so it missed the sibling iterator instance-prototypes reachable only by
*calling* a method (`%String|Map|Set|RegExpStringIteratorPrototype%`); a tenant could
pollute them across requests. Fixed by adding those as explicit harden roots (the
generator function's own `.prototype` is per-function/isolated and left alone; the shared
`%GeneratorPrototype%` was already frozen). **SR3-2 (CONTAINED)** — a Promise microtask
loop is bounded by the existing per-request gas (~1 s, worker recovers), no new
mechanism. Re-audited clean on both tiers; `t/comcon_freeze.t` +2 SR-3 cases (comcon
22/187 both builds). The confined tier's confinement is now adversarially validated;
full-test262-under-AOT untrusted-native remains gated on maxim finalization only.

**v5.27 (in place — gas: per-request execution budget, both tiers):** a confined tenant's
CPU time is now bounded (memory already was, via `JS_SetMemoryLimit`). Previously a
`while(true){}` handler hung the worker — deny-by-default caps + frozen intrinsics stop
*authority* abuse, not *resource* abuse. Interpreted tier: the interrupt handler is wired
onto the tenant runtime (it is a separate runtime and had none) with a host-imposed
per-request deadline (`NGX_JS_TENANT_TIMEOUT_MS`, default 1 s) set around the tenant
`JS_Call` and cleared at every compartment-leave (covering handler + microtasks + response
getters). Compiled tier: **back-edge gas** — maxim polled interrupts only at call sites, so
a compiled loop with no calls ran uninterruptibly; now a poll is emitted on backward gotos,
gated by an inline per-function down-counter so the per-iteration cost is a decrement, not a
call (the C6 compute throughput is unchanged, ~75K req/s). Verified: interpreted *and*
AOT-compiled infinite loops are both interrupted at ~1 s, normal handlers unaffected
(`t/comcon_gas.t`; comcon 22/184 both builds). This is the T11 availability control; the
fuller metered budget model (per-op/per-fragment, fine memory attribution) + a configurable
`js_tenant_timeout` directive remain S5. The back-edge-gas codegen change should be
upstreamed to the maxim repo.

**v5.26 (in place — M-SES-1: intrinsic freezing):** the tenant lockdown now transitively
**hardens (Object.freeze) the intrinsic graph** — every constructor/prototype/method reachable
off `globalThis` (plus the generator/async + array-iterator prototypes), leaving `globalThis`
itself extensible so caps still install. This closes a real, demonstrated **cross-request
state leak**: the tenant runtime is long-lived, so `Object.prototype.x = …` in one request
persisted into the next (and would leak across tenants under a shared runtime). Now such a
write throws (frozen + strict) and never takes effect; ordinary JS — creating/mutating one's
own objects, calling built-in methods — is unaffected. This is the S1 "frozen intrinsics"
control (`ngx_js_tenant_lockdown`, `t/comcon_freeze.t`; comcon 21/180 on both builds). It
complements M-SES-0 (which closed *dynamic code*, not *prototype tampering*). Remaining M-SES:
M-SES-2 = the SR-3 escape-completeness pentest (the wider untrusted-production gate, with full
maxim finalization). Small follow-up: freeze the COM/Socket protos too (installed after the
lockdown; their mutators are C-gated regardless).

**v5.25 (in place — C7 / M8 / SR-2: the compiler-faithfulness gate PASSES, profile-scoped):**
the compiled tier is now certified faithful — **T2 refines T1** — across the confinement
surface, not just one fragment. `t/comcon_faithfulness.t` runs a suite (report / Request
reads / Response shapes / compute / granted-socket reads / **the A1 gated reach `.listener`
and gated mutator `close()`**) interpreted vs AOT-compiled and asserts byte-identical
responses AND identical denial totals — the gates fire the *same* number of times in both
tiers, so **confinement provably survives compilation and the compiled code leaks no
authority** — plus that every fragment actually compiled (non-vacuous). 22/22. This is the
security capstone of the compiled tier: erasure (C5.0) + performance (C6) + faithfulness
(C7). **Scope is the COMCON tenant profile** (strict-module confined fragments — the maxim
surface that is in-profile-clean); the *increment-C* gate is met, while **untrusted-native
production** additionally waits on M-SES + full maxim test262 finalization (INCREMENT_C.md
§3). Deferred still: back-edge gas + two-clocks revocation (interpreted-first), and the C5.1
inline-gate perf optimization (narrow value per the C6 data).

**v5.24 (in place — C6 benchmark: the compiled tier's value, measured):** interpreted vs
AOT-compiled, the same confined handler under load (`t_performance/comcon_c6/`): a
**~13.5× speedup where JS compute is the bottleneck** (a hot typed-int loop: ~5.3K → ~72K
req/s) and **neutral on I/O/builtin-bound handlers** (~186K both — JS is a small slice of a
thin routing handler; the compiled tier accelerates *that* slice, not nginx/HTTP/builtin
time). This is exactly the tier's design profile, honestly bounded: the compiled tier is a
large win for compute-heavy confined tenants and a no-op for thin ones (which are already
fast). Confinement is identical in both tiers (erasure) — the speedup costs no A1-gate or
denial fidelity. Implication for the roadmap: C5.1 (partial-eval inline gate checks) targets
the *reach-gated hot path* specifically, not the raw compute measured here. Results +
interpretation: `t_performance/comcon_c6/RESULTS.md`.

**v5.23 (in place — C5.0: the compiled confined tier is real):** a confined tenant handler
now runs as **native C**. After the C3/C4 admission gate, `js_comcon_aot_compile` lowers the
handler via maxim's synchronous server-AOT (`js_jit_compile_all → drain → install_results`,
CONFIG_JIT `objs_jit` build); workers inherit the compiled function via fork/COW. The
**confinement is preserved by construction** — the compiled code calls the same gated host
functions under the same host-set compartment — proven by the C5.0 differential test
(`t/comcon_lowering.t`): the same fragment run interpreted vs AOT-compiled yields
**byte-identical responses AND an identical denial-counter total** (erasure soundness on a
real fragment, the seed of the M8/SR-2 gate), with the test guarding non-vacuity (the
handler actually compiled) and clean shutdown. Full comcon suite green on both the
interpreter and JIT builds (18 files / 148), AOT active for every tenant on the JIT build.
This rests on the maxim in-profile JIT correctness gate (v5.21). Deferred per the scope:
back-edge gas + two-clocks revocation (no budget/epoch machinery at either tier yet);
partial-eval inline gate checks (perf, C5.1). Also fixed a pre-existing single-process-mode
teardown crash (both builds; exit_process freed a runtime that exit_master then re-freed via
the aliased `jcf->rt` — `t/comcon_teardown.t`), surfaced while validating C5.0-b. Scope +
status: `INCREMENT_C5.md`.

**v5.19 (in place — M-SES-0: dynamic-code lockdown):** the tenant context is no longer a
full-intrinsic `JS_NewContext`. It is now `JS_NewContextRaw` + a **curated intrinsic set**
(`ngx_js_tenant_context_new` — everything except `Proxy`, which nothing in the tenant path
needs) followed by an **SES-style lockdown** (`ngx_js_tenant_lockdown`, run as a module):
the Function / generator / async / async-generator constructors are neutralized (their
prototypes' `constructor` redefined to a frozen throwing stub) and the `eval` / `Function`
/ `Reflect` globals are deleted. This **closes front-end audit finding A1**: dynamic code
is now genuinely unreachable (`[].constructor.constructor(...)` throws), so C3-rest's "no
dynamic code" guarantee is *sound* and C4's free-name manifest is a *complete* over-
approximation of reached capabilities — the prerequisite the audit flagged for C5 erasure
soundness. The Eval intrinsic must stay (it installs the module compiler `JS_Eval` needs);
only the reflective `eval` *global* is removed. Standard tenant JS (Object/Array/JSON/Math/
Date/RegExp/Map/Set/Promise/TypedArray) is unaffected. This is M-SES-0 only — cross-tenant
intrinsic *freezing* (M-SES-1) and the full escape-completeness pentest (SR-3) remain.
Tests: `t/comcon_mses.t`, `t/comcon_frontend_audit.t` (A1 line now asserts the route
throws). Scope + implementation notes: `INCREMENT_MSES.md`.

**v5.18 (in place — the front-end soundness audit):** an adversarial audit of the
increment-C admission front-end (C3.0/C3-rest/C3-types/C4), run empirically before
building the compiled tier on it. **Confinement held — no capability escaped in any
vector** (dynamic code and `globalThis[…]` reach only the deny-by-default tenant global;
every ungranted host name reads `undefined`), and the free-name walk proved robust across
default initializers, computed keys, class `extends`/fields, destructuring defaults and
nested arrows. But two soundness *claims* were overstated (details in VERIFICATION.md
"Front-end soundness audit" + THREATS LOW-6): **C3-rest does not actually eliminate
dynamic code** — the Function constructor is reachable via `[].constructor.constructor`,
the async/generator constructors, and `Reflect.construct`, none of which the name-deny-
list or opcode scan catch — so "no dynamic code ⇒ the analysis is sound" is false; the
manifest's completeness rests on the global being deny-by-default, not on the absence of
dynamic code. The audit's key strategic result: **M-SES (curated intrinsics) is promoted
from optional hardening to a hard prerequisite for C5 erasure soundness and C4 env-
signature completeness** — the compiled tier may not treat the free-name manifest as a
complete capability set until the reflective intrinsics that defeat it are removed. Fix
landed: reflective global aliases (`globalThis`/`global`/`self`) are refused
(defense-in-depth, not a soundness fix). Minor type-check-completeness gaps (destructured
Request param, computed/aliased access, rest-param arity) folded into the C5 erasure
remainder. Regressions: `t/comcon_frontend_audit.t` (pins the containment guarantee + the
fix).

**v5.17 (in place — C4: the fragment artifact):** the admitted fragment is no longer a
transient — after the C3 checks pass it is sealed into an **artifact** (SPEC §8's "fat
bytecode", minus the not-yet-built lowering): a **content-addressed identity**
`H(H(source) ‖ schema-version)` plus an **admission certificate** (the env-signature size
— the free-name manifest from C3.0 — and a bit per C3 clearance: free-names resolved,
dynamic-code-free, Request sealed, onRequest signature). The identity folds the content
pin and the schema pin into one match, so the new **`js_tenant_artifact <hex>`** directive
refuses the config on *either* content drift (the fragment's bytes changed) or schema
drift (the C2 surface it was admitted against changed) — generalizing B/E1's pin-by-hash
from a single dependency file to the whole fragment + its schema. The certificate is
logged at admission (identity prefix, schema version, clearances) — the audit trail of
*what was admitted, against which schema*. Reuses the B/E1 SHA-256 machinery; the schema
identity is the C2 `version` string (`NGX_JS_C4_SCHEMA_VERSION`, drift-guarded by
`comcon_schema_conformance.t`). No lowering: this record is exactly the T1-executable
fragment C5 will lower, and the identity is what C5's compiled `.so` and M8's differential
test will be keyed to. Test: `t/comcon_artifact.t` (the Perl side recomputes the identity
independently and matches).

**v5.16 (in place — C3-types: the fragment is checked against the C2 schema):** the
typed-profile front-end stops checking only *names and constructs* and begins checking
*types* — the tenant's USE of its environment against `schema/tenant-env.schema.json`.
Two schema contracts land, chosen because each is decidable soundly at admission: (1) the
**`env.onRequest` signature** `(Request) => Response` — enforced at registration
(`ngx_js_tenant_onrequest`): the handler must be a function of at most one parameter (the
Request) and may be registered exactly once (a second registration or an over-arity
handler is refused, where before the last writer silently won); and (2) the **sealed
`types.Request`** — `js_comcon_check_request_fields` (quickjs.c) scans the handler's own
body and refuses a *direct* read of any field the Request type does not declare
(`method`/`uri`/`args`/`headers`). It is a **sound rejecter** (it fires only where the
base is provably the handler's `arg0`, never a false positive), which is exactly the
"types only reject at admission" contract; the erasure-*complete* remainder — aliased/
interprocedural value-flow, the `Response` return type, granted-`Socket` member typing,
and computed keys — is the type inference C5 needs and is named-and-deferred. Recovered
two tests that had silently skipped since C3.0 landed (`comcon_tenant_request`,
`comcon_dependency`) because their tenant fragments carried an obsolete `typeof nginx`
probe that C3.0 now refuses at load — the probes were removed (the property they checked
at runtime is now an admission-time refusal, a strictly stronger guarantee).
Test: `t/comcon_types.t`.

**v5.13–v5.15 (in place — increment C front-end lands: the typed schema + the admission
gate):** the first three built slices of the typed-profile front-end (INCREMENT_C.md §C2–
C3). **v5.13 (C2 — the typed schema):** the tenant environment is now *data* —
`schema/tenant-env.schema.json` + `SCHEMA.md`, grounded by `t/comcon_schema_conformance.t`
(every schema row probed against the running surface; drift is a test failure, the
`describe ⊇ reality` discipline). **v5.14 (C3.0 — static free-name admission):** a
fragment is refused at *load* if its handler references any name not in the bound
environment — `js_comcon_collect_free_globals` (a recursive walk of the handler's
`closure_var` globals, into nested `cpool` functions) drives an admission gate in
`ngx_js_module.c`; the runtime deny-by-default of A/B becomes an admission-time refusal
(fail fast, not a throw deep in a request). **v5.15 (C3-rest — restricted constructs):**
that free-name guarantee is only *sound* if a fragment cannot conjure invisible name
references, so **dynamic code is refused at load**: `js_comcon_uses_dynamic_code` scans
the handler's bytecode for direct `eval`/`with` (OP_eval/OP_apply_eval/OP_with_*,
recursing into nested functions), and an `eval`/`Function` *name* deny-list catches
indirect references (`var f = eval`) that the plain free-name gate would wave through as
standard globals. `with` is separately a strict-mode syntax error in the tenant module,
so it never compiles. Tests: `t/comcon_admission.t` (C3.0), `t/comcon_restricted.t`
(C3-rest). Still deferred within C3: full type-checking against the C2 schema (the
largest remaining sub-part) — the front-end today admits/rejects on *names and
constructs*, not yet on *types*.

**v5.12 (in place — increment C PLAN + the security-review cadence):** [C plan as
below]. Also **VERIFICATION.md gains the security-review cadence**: reviews at inflection
points, not by step count — four gate-reviews (SR-1 A/B conformance before C; SR-2 =
M8 faithfulness at C7; SR-3 full pentest after M-SES; SR-4 = V15 assurance case before
untrusted production), plus a per-change THREATS.md-touch discipline; within C the review
IS the per-slice differential test, not a checkpoint. SR-1 has now run (2026-09-01): deny-by-default verified sound; one HIGH (return-value getters ran under HOST_ROOT) + three MEDIUM found and FIXED (commit 66dfdb0dd, test comcon_sr1_regression.t); LOW-5/LOW-6 recorded (THREATS.md 'SR-1 result').

**v5.12-plan (in place — increment C PLAN):** `INCREMENT_C.md`, the ground-truthed build
plan for the typed/compiled maxim tier. Reality check on the real maxim tree: same
Bellard 2017-2025 base as the vendored engine (~4% delta), the compiler is a separate
`quickjs-jit.{c,h}` (type inference + bytecode content-hash already implemented), gcc+tcc
present — so M-UNIFY is additive, not a fork reconciliation. The plan is spike-first: C0
(a feasibility GATE — compile one trivial handler to a callable `.so`) → C1 M-UNIFY → C2
typed schema → C3 front-end → C4 fragment artifact → C5 lowering → C6 dispatch/benchmark
→ C7 the M8 faithfulness gate. Load-bearing invariant: erasure soundness, enforced by a
differential test on every slice (interpreted vs compiled = identical outputs AND
denials) so A/B's security is inherited, not re-implemented. M-SES gates production
compiled-untrusted tenants; C0–C6 proceed as a dev tier. C0 GATE PASSED (2026-09-01): built maxim (make CONFIG_JIT=y, clean), compiled a trivial
function to a persistent GCC `.so` that ran correctly (result=45), characterized the ABI
(JSJITFunc + the single js_jit_rt vtable + visible int-unboxing), and confirmed the .so's
only non-libc undefineds (JS_GetRuntime, js_jit_rt) + all install helpers are in
libquickjs.a (0 unresolved) — so nginx can dlopen+call a compiled fragment. Decision:
proceed to C1 (M-UNIFY). Details: INCREMENT_C.md §4.

**v5.11 (in place — increment B COMPLETE, the dependency workflow):**
`js_tenant_dependency <name> <path> <sha256>;` loads a **pure library** — evaluated in a
bare no-capability environment (before any grant), bound on the tenant global, admitted
only if its bytes match the pin. Threat-T2 supply chain, live: a hijacked update (bytes
changed) is refused at load (the last good config keeps serving); a dependency that
reaches for host authority is not admitted (truly pure). Test t/comcon_dependency.t.
Increment B (onboarding) is done: learning mode + generated grant-stub + dependency
workflow. Next: increment C (typed/compiled).

**v5.10 (in place — B1, generated grant-stub docs):** `nginx.tenantLearning()` now also
reports the already-granted names; an onboarding generator (plain library JS per
Principle 10 — `js_com_demos/COMCON_onboard/onboard.js`) turns the learning record into a
paste-ready contract stub: each wanted path classified REFUSE (omnipotent) / REVIEW
(narrow-to-a-facet), the wanted-vs-granted delta, and the enforce next-step. Fixes a
worker-side segfault (tenantLearning must read jcf via ngx_cycle, not the context
opaque). Test t/comcon_onboard.t. Remaining B: the dependency workflow.

**v5.9 (in place — increment B begins, B0 learning mode):** `js_tenant_mode learn`
(the A4 flag is now enforce|audit|learn). Learn mode seeds the tenant global with a
recorder for each withheld host name (a callable catch-all exotic object) that harvests
every access *path* the fragment walks and lets it run to completion; `nginx.tenantLearning()`
reports `{mode, wants:[{path,hits}]}` — the onboarding wishlist. A4 audit + B0 learn =
the observe→onboard→enforce loop (audit = what gated reaches happened; learn = what host
surface is wanted-but-absent). Next in B: generated grant-stub docs + the dependency
workflow. Build log: INCREMENT_A.md §7.

**v5.8 (in place — increment A COMPLETE):** A3.1 puts headers on the confined request
path as *data* (request headers in as a copy; response headers out via the return value,
CRLF/non-token dropped — showcase-4 guard), and the **dogfood demo**
(`js_com_demos/COMCON_dogfood/`) accepts increment A: a caged mirror tenant serving real
2-worker traffic, the cage proven on live requests (nginx unnameable + a granted socket's
`.listener` null cross-compartment), CRLF injection dropped, the audit→enforce loop
closed by the host reading `tenantDenials()`. COMCON-lite is now a working, tested,
demonstrated system. Next: increment B (onboarding). Honest gaps recorded (per-worker
counting, single tenant, no budgets — all deferred widenings, INCREMENT_A.md §6).

**v5.7 (in place — A4, the observability layer):** the denial log at all seven gate
sites with **TM-1 implemented to spec** (exact counters always; 100 full records then
1/100 sampling; quota-exceeded reported once — verified 250→101 records/250 counted);
`js_tenant_mode audit|enforce` (audit = log-and-allow: the observe-then-enforce loop);
`nginx.tenantDenials()` host report. Tenant runtime now carries the full COM class set
(classes per-runtime, protos per-context — no `nginx` global, so no authority) so
audit-allow can hand wrapped objects into the tenant context. Name-level denials remain
structural (silent, primary); gates are the observable layer. Increment A's core is
now complete through observability; the dogfood demo is the acceptance step.

**v5.6 (in place — the COMCON-lite core is real):** increment A built through the
request path, each unit tested: A1.0 identity seam, A1.1 reach-cycle gates, A2.0
deny-by-default tenant environment (`js_tenant_source`), A2.1 grants
(`nginx.grantToTenant`) with the cross-compartment isolation proof, A3.0 a persistent
tenant runtime serving live requests (`onRequest` + `js_tenant_handler;`) with
confinement active on the request path. Design decision recorded from code: the A3.0
request path is **zero-capability** — the tenant gets request *data* and its response
authority is its *return value*; request facets are a later widening. Pre-existing
master-abort-on-throwing-reload bug fixed en route. Build log: `INCREMENT_A.md` §6;
status: `SPEC.md` §13.

**v5.5 (in place — construction begins):** `SPEC.md` (the clean normative read, M2.5 doc
half) and `INCREMENT_A.md` (the ground-truthed build plan). The nginx integration reality
check ran against the real `src/js/`: the §9.4 contract mostly holds, the QuickJS
compartment factoring is favourable (S2 confirmed — classes per runtime, prototypes per
context), the describe registry is extensibly typed. It reordered increment A (owner-field
the global handle registries *before* context-splitting — the reach cycle is ownerless-
array-mediated, which contexts don't isolate), named the four omnipotent un-gateable
members (config.write, repl.eval/listen, use/install, Worker/SharedWorker), and found two
confinement bugs (script-writable worker limits; flat `nginx.shared`). Design survived
contact with the code.

**v5.4 (in place — convergence actions):** the two remaining different-in-kind checks
plus the accepted simplifications. **THREATS.md added** — the adversary × asset ×
mitigation completeness ledger (12 adversaries; every cell cites its mechanism; three
residuals accepted by name: engine memory safety, IFC/side channels, availability-
within-reach); it found **TM-1** (per-fragment denial-log quotas — into the M2.5 denial
schema) and **TM-2** (session identity → environment mapping unspecified — pinned to
increment A with the nginx §9.4 reality check). **The two-clocks pin** (ROADMAP §M6):
epoch = binding version; generation = per-fragment baked-authority invalidation, fanned
out at revoke time via the provenance registry — distinct clocks, co-triggering re-AOT.
**Adaptive profiles deferred to M9** (user decision): the shipped core is
restrictive-only ⇒ unconditionally-ACI composition until transforms arrive; deferred,
not dropped. **M2.5 rescoped to THE SPEC**: one clean normative read of the whole
design, archaeology to an appendix, absorbing the term-discipline sweep and the
layered-core framing; the rewrite doubles as the final consistency check.

**v5.3 (in place — the consistency pass, C1–C13):** fourth review, hunting
cross-revision drift. Substantive: **C1** node ids are recorded in the canonical
config tree with a persisted counter — never re-derived across restart/reload (POM §2;
closes R8's loop); **C2** scenario 7 removed from increment A (opaque is on the
engine-substrate track; 38-audit covers A's disclosure story); **C3** back-edge gas and
V6's CFG check apply to *all* C entering the funnel — wasm2c-emitted included.
Consistency: stone is a kernel intrinsic (C4); enumerations restated per instance +
two global lists (C5); `E_BIND_ADAPTIVE_CONFLICT` renamed — detected at the meet, not
admit (C6); "sub-languages declarative *at their normal form*" (C7); environment
signature ≡ the fragment's free-name manifest (C8); stale counts fixed (C9); first
slice = increment A's engineering seed (C10); schema-hash pinning stated per-instance
(C11); the 96% ceiling asterisked at first contact (C12); `wasm.admit` in the manual's
combinator table (C13).

**v5.2 (in place — the WASM provenance ruling):** the M-LIB `wasm` facet gains its
decision rule: **substrate follows provenance, not language** (WASM = trust tier, not
performance tier; our-born JS runs T1/T2 with the compiler as trust root, JS→WASM
ruled a category error; foreign-born code enters as WASM with the validator as trust
root); two execution lanes (embedded runtime for cold modules; **wasm2c ingestion**
into the single C funnel — two provenance front-ends, maxim JS→C and wasm2c WASM→C,
one compile/load/gas/revocation story, wasm2c joining the TCB) and one **export lane**
(maxim→WASM carries admitted fragments to foreign Proxy-Wasm-class hosts; admission
guarantees travel, authority discipline degrades to the foreign ABI and is reported).
Kernel, theorem, tiers, and measured results unchanged (ROADMAP §M-LIB). Scenario 50
("the border crossing", SHOWCASE50.md) demonstrates the inbound lane. Completed from
the source discussion: WASM natively enforces the possession axiom (modules born-bound
by construction); R3/R4 = the native tier buying back WASM's intrinsic meterability/
revocability; the third lane — tiering by heat (cold long tail = compartments with
per-compartment caps, never per-tenant `.so`s); modeled lane costs in PERFORMANCE §4.7;
the export lane's strategic reading (foreign extension ABIs become toolchain targets).

**v5.0 (this set — the pre-implementation design review, R1–R12, ROADMAP §11):**
confluence restricted to restrictive mediations, ≤1 adaptive policy per node (R1);
quote splices must be **stone** — the TOCTOU fix (R2); per-fragment generation checks
reconcile revocation with compile-through (R3); tier-2 gas via back-edge checks in
generated C (R4); write guards at typed shared-slot boundaries (R5); least-authority
realization — mandatory contract + free-name manifest (R6); unbound execution is an
error, bound nodes never unbound (R7); creation-ordered ids + binding-set drift
reports (R8); the born-bound rule for query bindings (R9); monotone rollout + two-phase
epoch groups (R10); the admission front-end named as pre-sandbox attack surface (R11);
failure-mode strictness order (R12).

---

## 13. Open questions (v3 — updates v2 §10)

Resolved since v2: formal semantics + monotonicity sketch (was §10 preamble) —
`SEMANTICS.md`; POM node interface — `POM.md`; hardening scope — `HARDENING.md`;
empirical endpoints of the cost model (v2 §10.13, partially) — M1.

Still open, in priority order:

1. **COW-domain / inline-cache cost** (v2 §10.3) — *the* performance risk; prototype
   early; design together with revocation-epoch invalidation.
2. **The closed enumerations** (v2 §10.2 + v4.1 §8a; *restated per instance-genericity,
   v5.3 — C5*) — **per-instance grammar enumerations** (the JS p_symbols, the config
   productions of M-CFG, the WASM validated format of the facet), plus two global
   lists: **compile portals** and the **ops-resource capabilities** (§8a). Completeness
   of the grammar enumerations and portals makes the sandbox-escape claim checkable;
   of the ops-resources, the no-backdoor claim. All generated, never maintained (V7).
3. ~~Selector-language grammar~~ — **promoted into M2.5** (ROADMAP §5.4): the showcase
   rework used it constantly and invented syntax ad hoc; it needs its spec now.
4. **Information flow / taint** — capabilities gate *access*, not *flow*; read-X +
   write-Y can leak X→Y. Cross-tenant confidentiality needs IFC labels layered on top.
   Named, deferred (post-M9 track).
5. **Stage-3 hook machinery details** (v2 §10.7) — confinement context, budget
   mechanics, certified-hook criteria.
6. **Multi-tenant worked example at system scale** (v2 §10.8) — still the validating
   scenario.
7. ~~Denial/explain schema~~ (v2 §10.9) — **promoted into M2.5** (ROADMAP §5.3): it is
   the operator UX, the deny-suite assertion language, the learning record, and the
   LSP diagnostic; the showcases lean on it in nearly every scenario.
8. **Config-tree ⇄ live-tree correspondence across reloads** (v2 §10.10) — now
   interacts with binding epochs.
8a. **Cluster-edge policing** (new; showcase 27) — workers-as-fragments extends the
   model across process boundaries, but the kernel semantics is single-runtime;
   cross-process communication edges (master↔worker, worker↔worker over the js_com
   SW/broadcast machinery) need their own design pass. Deferred.
9. **js_com API-factoring audit** (v2 §10.11) — now concrete: the F2 cross-reference
   getters are known level-conflating offenders; fused with M2 + hardening S4
   (HARDENING.md §S4).
10. *(carried)* directive-collision hygiene for anchors; disclosure side channels;
    static/halting limits → forbid-or-defer.
11. *(new in v4)* **The config-instance specifics (M-CFG):** the enumeration of config
    grammar productions policies may target (the config-side p_symbols); the
    value-conflict rule at admission (rights meet, values must agree — what counts as
    "agreement" for mergeable directives?); and whether third-party *config* fragments
    need their own profile split analogous to restrictive/adaptive.
