# COMCON — Foundation & Architecture (v3)

*Supersedes `docs/FOUNDATION.md` (v2, 2026-07-06/07). v3 integrates the architecture-
refinement work of 2026-08-18: the possession kernel, the Program Object Model, the
closure/quotation distinction, the formal semantics (see `SEMANTICS.md`), the live-
mutation safety classes (see `POM.md`), the hardening scope (see `HARDENING.md`), and
the first measured performance gate (M1, see `ROADMAP.md`). Material from v2 that
remains load-bearing is carried forward here in condensed form; §12 lists every
terminology change.*

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

The house analogy: **COM : nginx configuration :: POM : the program itself.** Same
design, second instance — reflective tree, 1:1 with its source, mutated only through
capabilities, mutations classified by safety class.

---

## 1. Primary target & motivation *(carried from v2, updated)*

**Primary deployment:** the nginx-family projects — js_com / pilgrim / mirror — where
JavaScript from **third parties that do not trust each other** (tenants, sub-tenants,
vendors, AI generators) runs inside one server. COMCON provides mutual protection
between tenants and between tenants and the host.

**Generality requirement (unchanged):** nothing in the core may be nginx-specific.
nginx specificity lives in policy libraries and the host integration layer, never in the
engine mechanism.

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

**The axiom under all of it (v3):**

> **Possession — no operation mints authority.** Capabilities are unforgeable values;
> the only way to have one is to be granted it; the only guarded operation is granting
> (you must hold what you grant). Everything else is free. Corollaries: a fragment can
> always self-restrict (weakening needs no permission) and can never alter an outer
> policy (its capabilities were never granted inward).

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
   composition is order-independent and confluent). Consequently **even a self-rebind
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

**The cap-free rule (load-bearing):** the quotation constructor **rejects capability
values, deeply** — a capability spliced into a "description" would smuggle a closure
inside a quotation. With the rule, the closure/quotation distinction is checkable, not
conventional; without it, the realizer-boundedness argument fails its audit
(SEMANTICS.md §2, rule QUOTE). Pleasing corollary: **POM reads return quotations** —
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

**Two policy profiles** *(new in v3)*:

- **Restrictive** — deny/attenuate only; behavior under policy ⊆ behavior without it.
  The program remains a correct standalone JS program. Hardening existing code **must**
  stay in this profile.
- **Adaptive** — mediation *transforms* behavior (rewrites, injected protocols —
  mirror-as-policy lands here). The program then depends on COMCON semantics; the
  policy must **declare** the profile, making "runnable without COMCON" a checkable
  claim instead of a hope.

**Failure semantics, declared per rule:** `reject-at-compile` / `deny-at-runtime` /
`attenuate-silently` / `audit-only`. Hardening brownfield code starts `audit-only` and
tightens — this is v2's learning mode (§11) meeting the binding surface.

---

## 8. Enforcement: three sub-languages, four moments

The **enforcement language** is not JavaScript and is never embedded in it; it is three
declarative sub-languages — all *data*, hence compilable, analyzable, diffable:

1. **Target (WHERE):** POM selectors (§7).
2. **Authority (WHAT):** environments built by grant/mediate — subsuming v2's
   `import_list` / `export_list` / `internal_list` descriptors (an env *is* the
   import list; export policing is mediation on what crosses outward).
3. **Contract (MUST):** types + pre/post predicates + tests + natural-language spec
   (one artifact, three consumers: human intent, AI generator, machine verifier).

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

---

## 13. Open questions (v3 — updates v2 §10)

Resolved since v2: formal semantics + monotonicity sketch (was §10 preamble) —
`SEMANTICS.md`; POM node interface — `POM.md`; hardening scope — `HARDENING.md`;
empirical endpoints of the cost model (v2 §10.13, partially) — M1.

Still open, in priority order:

1. **COW-domain / inline-cache cost** (v2 §10.3) — *the* performance risk; prototype
   early; design together with revocation-epoch invalidation.
2. **The two closed enumerations** (v2 §10.2) — p_symbols (POM node kinds, versioned)
   and compile portals; completeness makes the sandbox-escape claim checkable.
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
