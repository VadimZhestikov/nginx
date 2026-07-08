# COMCON — Foundation & High-Level Architecture (v2)

*Distilled from `comcon-notes/notes-my-old.txt`, `notes-my-old1.txt` (v1, 2026-06-24),
integrated with `00100-big-idea.txt`, `policy_definition.txt` and the author's answers
on deployment target and policy-as-API (v2, 2026-07-06).*

*This is the intended foundation, not a spec — terminology and boundaries will harden later.*

---

## 0. One-sentence thesis

> **COMCON makes QuickJS enforce *mutual*, *hierarchical*, *policy-based* protection of
> code fragments — at both compile time and run time — where a policy is itself a
> (policy-restricted) JavaScript program driving a low-level compiler/runtime API
> (the POM), attached via directive literals in the spirit of `"use strict"`, and where
> every inner policy is strictly no more permissive than the policy enclosing it.**

The v2 shift: a policy is not a declarative data blob interpreted by the engine — it is a
**program executing against an engine API**. Declarative convenience comes later, as
*libraries* built on that API.

The v2.1 refinement (2026-07-06): "policy code" is not a *kind* of code at all. Control is
a **relation between fragments** (§3.3); a *controller* is any fragment granted the control
capability, and "policy" names the **reified policy object** a controller installs —
declarative descriptors the engine *consults* plus hooks the engine *invokes* (§3.5),
delivered in three stages (§8.1). One kind of code; two kinds of edges (§3.4).

---

## 1. Primary target & motivation

**Primary deployment:** the nginx-family projects — **js_com / pilgrim / mirror** — where
JavaScript provided by **third parties** must run inside one server, e.g. **multi-tenant
configurations** (tenants, with sub-tenants, each supplying scripts/config fragments).
COMCON provides mutual protection *between tenants* and *between tenants and the host*.

**Generality requirement:** nothing in the core may be nginx-specific; COMCON must remain
usable by any qjs embedder. nginx specificity lives in the policy libraries and the host
integration layer, never in the engine mechanism.

Concrete host-side picture (from `00100-big-idea.txt`):
- Classic `nginx.conf` stays as-is, accepted unchanged.
- A JS source file (`nginx_com.conf`-like, "continuation of nginx.conf in some sense") is
  interpreted by restricted/extended JS **under a global policy**. Its skeleton:
  1. load defaults + the policy library;
  2. load `js_com` (or another API to the underlying host);
  3. load helpers;
  4. the program itself — with parts *exposed for third-party modification* and parts
     *hidden*, including `include <language-symbol> <link-to-file>` statements
     (**no arbitrary text inclusion** — an include names its interpreter, the grammar
     symbol it must produce, and the restrictions it runs under).
- Tenant configs are **movable subtrees** of the full configuration tree, with change
  tracing, rollback, and state save/restore delegated to application-registered
  serialize/deserialize methods (transient state is not preserved — the nginx idiom).

---

## 2. Spirit — the first principles (unchanged from v1, still load-bearing)

1. **Trust is layered and directional.** Authority flows *downward* as explicitly granted
   capabilities, never assumed upward (POLA; capability/actor model).
2. **Protection lives *inside* the language.** The compiler and interpreter are the
   enforcers; external linters are useless in dynamic environments.
3. **Make wrong results impossible, not discouraged.** Remove the capability instead of
   advising against its use. *"The reason something can do X is because we allow it."*
4. **Protection is mutual and symmetric.** Exporter and importer both declare policy;
   code and its fragments protect each other. Recurs at system scale (tenant ⇄ tenant,
   tenant ⇄ host, client ⇄ server ⇄ proxy).
5. **Restriction is monotonic — a narrowing lattice.** Inner policies only tighten.
   *(v2 nuance: a policy may **extend** the language with new forms — typed declarations,
   annotations — provided the extension does not widen ambient authority; see §6.)*
6. **Policies are programs (reflexive).** A policy runs under its own governing policy;
   a grounding "policy-for-policy" stops the regress (§4.4).
7. **Tame Turing-completeness.** Derive provably-safe sublanguages; the safest profiles
   compile to "Safe-C" (§8).

---

## 3. Core model

### 3.1 The unit of protection: the *fragment*
A fragment is a **grammar production symbol** (terminal/non-terminal) of the guarded
language. Policies are injected **before a concrete p_symbol**, not between arbitrary
lexemes — therefore **all p_symbols must be enumerated** as part of the design (a stable,
versioned naming of QuickJS grammar productions).

Fragments are **named**, and names form a **tree** (a path over the syntax tree), e.g. a
tenant's fragment tree nested in the host's. Export/visibility rules reference these
names (§4.2), and policy inheritance follows this tree.

### 3.2 Policy attachment — directive literals & the nested-backtick stack
Policies attach as string/template-literal directives, `"use strict"`-style:

```
`comcon: set_default_policies({...});`   // grounding: policy-for-policy + policy-for-script

global code
`
  `
     ` policy super-upper `
     policy upper
  `
  policy lower
`
code of the guarded symbol
normal_code
```

Nesting of backtick literals *is* the policy scope stack: each level is compiled and run
**under the policy established by the level above it** — the syntactic embodiment of the
narrowing lattice. Collision/spoofing mitigations from v1 still apply (crypto-prefixed
directives or external policy files; untrusted code cannot loosen — its inner policy
literals can be rendered inert).

### 3.3 One kind of code, one control relation
This section records two successive distillations (2026-07-06).

**Step 1 — one pipeline.** The original sketch (`policy_definition.txt`) showed two
pipelines: `x_compile/x_run` for policy code, `p_compile/p_run` for program code.
Mechanically they are the same operation, and principle 6 (policies *are* programs) plus
the reduction principle (§6) demand a single policy-parameterized compile/run:

```
compile(ctx, symbol, src, governing_policy) → code
run(ctx, code)                              → effects/value
```

**Step 2 — one kind of code.** Even the residual "policy role vs. program role" split
dissolves, because every difference between the roles is *relational*, not intrinsic:

- *Governing policy* — in the fragment tree, every fragment's governing policy is simply
  **its parent**; the "policy-for-policy" is just the grandparent.
- *Visible context* — `engine_control` is just a **capability**; code whose parent granted
  it can control children, code without it cannot.
- *Phase* — phase is relative: **my compile time is my controller's run time**. Since
  `compile()` is first-class (§7), "runs while something else is compiled" describes any
  code that calls it, not a special class.
- *Result destination* — "feeds the compiler" is just what calling
  `engine_control.add_policy(...)` does: effects via capability, like everything else.

So the model collapses to:

> **A tree (DAG) of uniform fragments. A "controls" edge runs from parent to child. The
> narrowing lattice applies along every control edge. A *controller* is any fragment
> holding the control capability over some child. "Policy" names the reified **policy
> object** a controller installs — declarative descriptors + hooks (§3.5). The controller
> is the *program*; the policy is its *installed effect*.**

Consequences:

- **Multi-tenancy is automatic.** A tenant is controlled code that is itself the
  controller of its sub-tenants. Sub-tenancy needs no new mechanism.
- **The backtick directive is syntax sugar** for "compile-and-run this child *now*, with
  `engine_control` scoped over the following symbol." Nested backticks = nested control.
  Ordinary code calling `compiler_context.compile()` performs the identical operation
  without sugar — directive syntax and embedding API are two spellings of one operation.
- **mirror-as-policy and AI-in-a-REPL fall out**: a transpiler is a controller that
  transforms child source before compiling it; AI output is a controlled leaf; granting AI
  limited authority = granting a sliver of `engine_control`.
- COMCON polices its own machinery with the same lattice it offers everyone else.

**Irreducible residues** — what survives the collapse and must stay explicit:

1. **The root.** A control tree has a root whose controller doesn't exist — the grounding
   fixed point. `comcon: set_default_policies({})` names it (§4.4).
2. **Acyclicity.** The old policy/program split implicitly prevented code from controlling
   its controller. Homogeneity makes acyclicity an **explicit, load-bearing invariant**:
   no fragment may control (or loosen) anything on its own ancestor path. Must be
   enforced, not assumed.
3. **Reified effects outlive the controller's *activation*.** Run-time checks (COW views,
   opaque ops) are enforced by the *engine* consulting installed policy state — not by the
   controller acting spontaneously. Because the reification may include code (hooks are
   closures, §3.5), a **residual part of the controller can remain resident** — but it is
   *reactive, never proactive*: inert until the engine invokes it at a defined check
   point, under pinned authority and budgets. The controller-as-activation dies; the
   residue acts only when invoked.
4. **Bounded controller execution.** Controllers run inside someone's compilation, so
   compile-time execution budgets (depth, time, memory) are a universal engine
   requirement, not a policy-plane special case.
5. **The error channel.** A controller failure is a **compile error** of the code it was
   controlling; a leaf failure is a runtime exception. Same mechanism, different fault
   mapping.

**Auditability, refined.** The unit of audit is no longer a syntactic class of code but
**the grant of `engine_control`**: "what can ever touch the compiler" = the transitive
closure of control-capability grants from the root. Grants must stay syntactically
distinguished so that closure remains greppable. Controller runs also carry
determinism/reproducibility requirements (compilation caching, signed builds; memoization
keyed by policy identity).

### 3.4 Two relations on the fragment tree — do not conflate
The homogeneity argument applies to the **control** dimension only. The fragment tree
carries two distinct edge types:

| | **Control edges** | **Communication edges** |
|---|---|---|
| Who | parent → child | fragment ⇄ fragment (siblings, cousins, …) |
| Shape | asymmetric, acyclic | symmetric-by-negotiation, arbitrary graph |
| Invariant | narrowing lattice | mediated by `export_list` / COW domains / handles |
| Story | hierarchy (tenants, sub-tenants, host) | **mutual protection between peers** |

"A controls B" must never fall out of "A talks to B." The project's *mutual*-protection
thesis lives on communication edges between peers; the *hierarchical* story lives on
control edges. One kind of fragment; two kinds of edges.

### 3.5 The policy object: descriptors + hooks
What a controller installs is not purely passive data (the v1 notes already foresaw
*"script handlers to decide if I'll accept or not terminals/non_terminals"*):

> **A policy object = declarative descriptors + installed hooks** — data the engine
> *consults* and functions the engine *invokes* at defined check points.

The two tiers have sharply different properties; the split is first-class:

| | **Declarative residue** (tables, descriptors) | **Programmatic residue** (hooks) |
|---|---|---|
| Cost | cheap, inline-cache-friendly | a call on a possibly hot path |
| Analyzability | static; lattice meets computable at compile time | opaque to analysis |
| Cacheable / signable | yes — serializable; feeds Maxim/Safe-C | no (unless certified) |
| Expressiveness | fixed vocabulary | arbitrary predicates, validators, **transformers** |

Hooks subdivide by *when they fire*: **compile-time hooks** (parser accept/reject,
source/AST transforms — this is where mirror-style rewriting lives, resolving the
transform-vs-filter question) and **run-time hooks** (value validators, dynamic predicates
at check points — the *resident residue* of §3.3 residue 3).

The controller/policy lifecycle is thus a **trichotomy**:
**activation** (controller code; dies when compilation ends) → **declarative residue**
(passive, analyzable, cacheable) → **resident residue** (reactive code: *spontaneous
never, invoked only*).

**Hook invariants** (without these, hooks are the escape hatch that breaks the lattice):

1. **Hooks are fragments too.** A hook runs under the policy that governed its controller
   *at install time* — **pinned**, not whatever is ambient when it fires.
2. **Conjunctive only.** Descriptors are consulted first; a hook fires only where a
   descriptor says *allow-with-check*, and its verdict can **deny but never widen**.
   Adding a hook can only narrow — monotonicity is preserved structurally.
3. **Confinement.** A hook fires inside the guarded program's execution; it receives a
   read-only view of the checked operation (subject fragment, operation, object, value)
   and no ambient authority — otherwise every check point is a TOCTOU machine and a side
   channel.
4. **Run-time budgets.** Per-invocation step/time limits — the run-time twin of the
   compile-time budgets in §3.3 residue 4.
5. **Fault mapping.** A hook that throws or exceeds budget fails the *checked operation*
   as a policy denial in the guarded code's error channel; hook internals never surface
   to the tenant.
6. **Lifetime.** Installed hooks are GC roots for their captured environments;
   revocation = uninstallation (same shape as handles expiring on connection close).

**Expressiveness ladder:** `declarative-only ⊂ certified-library hooks ⊂ arbitrary hooks`.
Safe-C/Maxim profiles demand the lower rungs; the ladder is both a policy-type dimension
(§4.2) and the delivery roadmap (§8.1).

*Mutual-protection note:* hooks see checked values — an information flow from guarded code
up to the controller's residue. On control edges this is fine (the parent is more trusted
by construction). On **communication edges** it must be stated precisely: an
exporter-supplied validator hook observes the importer's usage, acceptable only because it
runs with the *exporter's* authority over the *exporter's own* data. Do not generalize.

### 3.6 Dynamic compilation: eval/Function as compile portals
`eval` and `Function` need no new mechanism — they are the model's own core operation in
legacy clothing (the v1 notes said it verbatim: *"eval is 'compile(context, policy);
run()'"*). `eval(src)` ≡ `compile(caller_ctx, ExpressionOrProgram, src, caller_policy)`
+ run-now; `Function(args, body)` the same with a different symbol and context. That they
dissolve into the driver loop **without special cases** is itself evidence for the
foundation.

**The child-fragment rule:**

> Dynamically compiled code is a **new child fragment of the fragment that compiles it**.
> Its control edge comes from the evaluator; the lattice applies: it is governed by the
> caller's *pinned* policy, possibly narrowed further — never loosened.

Deeper: **calling `eval` is an act of control** — creating a controlled child exercises
the control relation. `eval` is thus a **pre-packaged sliver of `engine_control`**:
"compile a child under my own policy, in my local scope, run it now." Whether a fragment
has `eval` *at all* is a capability question like any other — withheld by default in
tenant profiles, grantable per policy. (Self-similarity paying rent.)

**Compile portals** — the dynamic-compilation entry points form a small **closed family**,
each conferring *different* ambient authority; their enumeration is a foundation-level
deliverable parallel to the p_symbol enumeration (§10):

| Portal | Ambient authority conferred |
|---|---|
| direct `eval` | caller's **local scope** |
| indirect `eval` | global scope |
| `Function` / `AsyncFunction` / `GeneratorFunction` | **global scope** (more dangerous than direct eval) |
| dynamic `import()` | module graph + I/O |
| host `JS_Eval` / `compiler_context.compile()` (§7) | whatever the host grants — the general form |

The classic sandbox escape — reaching the `Function` constructor through prototype chains
(`({}).constructor.constructor("...")()`)— is closed by mechanisms already in the model:
read-only natives (§3, package defaults) and capability-gated portals; nothing new is
required, but the portal list is what makes that claim *checkable*.

**Interaction with staging (§8.1):** a run-time `eval` opens a **compilation episode
during program execution**. The stage-1 invariant is therefore phrased in terms of
episodes, and stage 1 additionally rules that **new policy directives inside dynamically
compiled source are an install-time error** (`sub_policies_allowed: none` for dynamic
children) — the caller's already-reified descriptors govern the new fragment. Directives
inside dynamic code become legal in stage 2, where compile-time hook machinery exists.

*Deferred to design stage:* scope-capture mechanics, bytecode representation of dynamic
fragments, caching/identity of repeatedly eval'd sources, stack introspection for pinning.

---

## 4. POM — the Policy Object Model (the central v2 element)

**A policy is a JavaScript program (itself policy-restricted) that drives a low-level
engine API.** That API surface is the **POM**, visible to policy code as the `pom.*`
subtree — the set of properties and functions defined on the **export_policy objects of
the language symbols under protection**. For policy-for-policy scripts a predefined
low-level `engine_control` exists, analogous to the JS global object.

**Design stance:** keep the POM **low-level, minimal, and orthogonal** — mechanism, not
policy. Convenience, named profiles ("no globals", "freeze prototypes", "JSON-only"),
tenant templates, etc. are **libraries written on top of the POM**, shipped as a standard,
parameterized policy library ("*policy is not data, it is a program*"). The POM is the
single most safety-critical interface in the system and is *subject to careful
definition* — it defines what policies can ever express. Every POM descriptor/hook class
carries a **documented cost class** (O(1) bitmap / IC-cached / JS-call, §8.2) — policy
authors see the price as they write.

### 4.1 Policy anatomy (first sketch of the POM's core verb)

```js
fragment_name;                        // names this fragment for reference by others
expected_symbol.add_policy({
  syntax_allowed:   "none" | "*" | <list> | <tree>,  // admissible productions inside;
                                                     // may also *extend* syntax (typed forms → Safe-C)
  import_list:      { name: {read, write, call, opaque, type}, ... },  // outer names visible inside
  export_list:      { name: {…, to: [fragment_names]}, ... },          // what leaves, and to whom
  internal_list:    { name: {read, write, call, ...}, ... },           // limits on locals
                    // import+export+internal together make the fragment closed enough
                    // to compile to Safe-C
  sub_policies_allowed:  ...,   // where on the allowed syntax tree nested policies may appear
  what_policies_allowed: ...,   // under what policy those nested policies themselves run
})
```

Notable: `sub_policies_allowed` / `what_policies_allowed` make the **meta-level** explicit
— a policy governs not only code but *which policies may appear beneath it*, closing the
reflexivity loop structurally. Under the homogeneous model (§3.3) they read crisply as:
**"may this child receive the control capability, and over whom?"** — i.e. they govern the
propagation of `engine_control` down the fragment tree. They are also what makes
delegating policy authorship (e.g. to an AI) safe: a sub-policy can only narrow, so
authoring failures are liveness failures, never breaches — the *asymmetric failure
property*, §9.5(4). The import/export descriptors additionally carry a **delegability
dimension** (`delegable: no | once | to | attenuated_only`, §5.4) governing onward
transfer of granted capabilities.

### 4.2 Policy types & the policy hierarchy
A **policy type** is a policy not yet bound to a concrete symbol/content — a template.
Policy types form their own hierarchy (ordered by relaxed→denied features), *separate from*
the syntax tree; applying a policy walks: pick a subtree of the policy-type hierarchy →
bind it to a path in the fragment tree. Policy types define `*`-defaults for inner
policies (e.g. "no `while` loops (compile-time); export only opaque values (run-time)").

### 4.3 Compile-time / run-time intermixing (sharpened from v1)
The two planes are **intermixed by nature**, not merely parallel:
- What is *lexically decidable* is enforced at compile time (e.g. block `=` to any LHS).
- What depends on *object identity* is enforced dynamically — property assignment cannot be
  compile-blocked in general; it is checked at run time against the policy the object's
  **creator** attached at construction (§5).
- Var/const policy may be strict (compile error) or lazy (deferred to run time).

### 4.4 Grounding (the regress stopper)
`comcon: set_default_policies({...})` establishes the **policy-for-policy** and the
default **policy-for-script**; absent it, any script could serve as both. Policy scripts
run against `engine_control` under the policy-for-policy. This is the explicit bootstrap
demanded by principle 6.

---

## 5. Run-time object model: COW domains & opaque values

### 5.1 COW domains (new in v2 — the object-sharing mechanism)
At **object construction**, the creator's policy defines which fragments may access which
properties — for the object *and its prototype chain*. Copy-on-write changes are visible
only to the fragment (set) that made them:

- A **COW domain** = a set of fragments sharing one view of an object's properties.
- Per-property classes: `read-only (shared)`, `read-write (shared)`, `write-only`,
  `cow-set` (private to a fragment subset), `cow-set-on-cow` (stacked views — behaves
  like an additional prototype chain).
- Per-object visibility classes: `opaque` (no properties), `all`, `some_v` (listed
  visible), `some_vn` (listed invisible).

This is how **the same object identity** is shared across fragments with *different*
authority — the engine-level answer to why `Proxy.revocable` was rejected (a proxy is a
different object; a COW domain is a different *view* of the same object).

### 5.2 Opaque types (v1, retained)
`opaque{,Num,Bool,Str,RegExp,Fn,Arr,Obj}` — values that can be **held, passed, returned
but not inspected, operated on, printed, or branched on**. Requires redefined
operator/native-API semantics; plain⇆opaque conversion is a capability. Policed operation
classes: **read, write, pass_as_arg, return** (+ opaque-type transitions at function
borders). Known self-noted caveat: allowed operations still leak (set partitioning,
timing) — the "no-disclosure REPL" aspiration (constant response time, no logs) tracks this.

### 5.3 Capabilities & handles (v1, retained)
Facets/capabilities grant exact authority, revocable. Handles = opaque cross-boundary
references, operations forwarded to the owning side, returning only primitives/JSON/
further handles/nothing; handles die with the connection (`on_close`).

### 5.4 Capability delegation & revocation
*(distilled 2026-07-07)*

**Delegation is not a feature to add but a flow to police.** Capabilities are values
(handles, granted references, `opaqueFn`s); values flow, and the v1 operation classes
already name the delegation points: `pass_as_arg` and `return`. Unconsidered, delegation
would exist *unpoliced*. The policing vocabulary is deliberately small:

1. **Delegability is part of the grant:**
   `{delegable: no | once | to: [fragment_names] | attenuated_only}` — non-delegable is
   the default (default-deny).
2. **Attenuation — the lattice applied to values.** A delegated capability may be
   *narrower* than the original, never wider. The same narrowing principle governs both
   dimensions: control edges (policies) and communication flows (capabilities). Authority
   can only lose strength as it travels.
3. **Grant chains.** Every delegated capability carries provenance — who granted, who
   delegated, what attenuation at each hop. The sibling of maker chains (§9.6.2): maker
   chains for code, grant chains for authority. Feeds audit and enables cascade
   revocation.

Delegation on a **communication edge needs both sides' consent** (mutual protection): the
exporter's policy decides whether the grant may travel on; the *importer's* policy decides
whether to accept receipt — an unwanted powerful capability is a liability (it makes the
holder a target and a deputy).

*Honesty note — the confused deputy:* the voluntary form is not preventable. If B can
reach X and *chooses* to proxy for A, that is B exercising its own authority. COMCON's
answer is the standard ocap one plus accountability: no ambient authority (deputies must
be explicit), and grant chains + B's own export policy make B answerable for what it
proxies. The model does not claim more.

**Revocation — the classic ocap problem, dissolved by construction.** Object-capability
systems need an interposed caretaker/proxy to revoke a released reference; COMCON rejected
proxies (object identity, §5.1). The apparent tension is the payoff:

> **The check point is the indirection.** Every access already consults per-fragment
> policy state (COW views, descriptors); revocation is a *mutation of that state* — no
> proxy, no identity split, no caretaker. Engine-level enforcement makes references
> revocable without wrapping them.

> **Revocation = narrowing to zero** — monotone, always lattice-safe. (Re-granting
> afterward is *authoring*, on the grantor's side, policed by the grantor's own policy —
> same category as learning-mode generalization: outside the lattice, inside the review
> gate.)

Semantics to define (the genuine design work):
- **Cascade:** revoking a grant kills everything downstream on its grant chain.
- **Revocation authority** = each hop for its own onward delegation ∪ any controller on
  the grantor's ancestor path (grant chain ∪ control tree).
- **In-flight operations:** revocation takes effect at the next check point; in-flight
  primitive steps complete (rule to be confirmed at design time).
- **Failure mode:** use-after-revoke = an ordinary policy denial in the existing error
  channel, optionally with an `on_revoke` event to the holder (`on_close` generalized).
  Deterministic, never silent.

**Scenario catalog:**
- **Tenant offboarding** — subtree removal = mass revocation via the config tree; grant
  chains guarantee nothing survives.
- **Incident response** (the killer multi-tenant scenario): CVE in a shared library →
  revoke that library's capability across all tenants *now*, without restart.
- **One-shot capabilities** (v1: "secure handles… for performing one-time actions only")
  — use-once grants, auto-revoked on exercise.
- **Leases / TTL** — time-bound grants (v1 stream limits generalized).
- **Session end** — handles dying at `on_close`: the already-designed instance of the
  general mechanism.
- **The AI-safe grant profile** — composition: *leased + one-shot + attenuated +
  revocable* is what an AI actor is handed at the REL (§9.5(3)).

---

## 6. Multi-language: policy defines the fragment's *language*

The text under a policy is not necessarily plain JS. A policy defines the **language** of
its fragment, from a family:

- unrestricted JavaScript;
- restricted JavaScript — down to JSON-equivalent subsets (*the v1 reduction principle:*
  `JSON.parse(s)` ≡ `eval(s, json_policy)` — one policy-parameterized parser subsumes
  bespoke validators);
- restricted JS constrained enough to compile to **"Safe-C"** (§8);
- other languages entirely (e.g. **iRule Tcl**) via customizable per-language compile
  hooks (the `compile()` of §3.3 dispatched on language symbol);
- **extended** JS: a policy may *add* syntax (typed imports/exports, typed declarations)
  when the extension serves safety/AOT — backward compatible.

Uniform mechanism: `include <interpreter> <language-symbol> <restrictions>` — every
inclusion names what parses it, what grammar symbol it must produce, and its
compile-/run-time limits. The base `compiler_context` is created from a default policy,
so **the "language" is itself a policy artifact** — an expandable policy-description
language covers the family.

---

## 7. The embedding API (host-facing)

```js
compiler_context = new compiler_context(default_policy_or_id);

compiler_context.compile(this_frag_id, frag_group_id,
                         symbol_id, symbol_policy_or_id,
                         symbol_text_or_async_handler);
compiler_context.run();
```

- Compilation is **per-fragment**, addressed by fragment id/group, target grammar symbol,
  policy, and source (text or async producer — streams with their own limits: max bytes,
  max delay…).
- Only a **top-level** context runs compiled code in the *current* context; lazy
  compilation is allowed.
- Layering: **(js_com + universal JS) = low-level primitives**; **(restricted language +
  higher-level library) = the safe API** exposed to actors. On this rests the safe **async
  REPL** — protocol-driven sequences of finite length, safe to hand to *any* actor: a human
  operator, a script, or **an AI**.

---

## 8. Roadmap, staging, and performance
### Positioning: COMCON → Maxim (AOT/JIT), mirror as a policy

- **COMCON is step 1.** Its policies double as **compilation contracts**: a fragment whose
  imports/exports/internals are fully described (§4.1) is closed enough for AOT.
- **Maxim** (working name; also "kamerer" in notes — the JS AOT/JIT, "safe-C" backend) is
  **step 2**, consuming policy-annotated fragments. Policy-added typed syntax (§6) exists
  largely to feed it.
- The **mirror** project's transpiler *"can be expressed as a comcon policy"* — first
  external validation that policy-as-program-over-POM is the right abstraction: a
  transpiler is a **compile-time hook** (§3.5) — a policy whose effects rewrite rather
  than merely admit/reject. Lands in stage 2 below.

### 8.1 Staging: three stages, ordered by risk and cost
The conjunctive-only invariant (§3.5) makes this staging *provably safe*, not merely
convenient: hooks can only narrow what descriptors allow, so adding a later stage cannot
invalidate any property, proof, or behavior of an earlier one. Declarative semantics are
final on day one.

Note stage 1 is **not** "no policy code" — controllers are programs executing at compile
time; that is the heart of the model and cannot be deferred. What is deferred is policy
code executing *outside compilation*. Because dynamic compilation opens episodes during
program execution (§3.6), the invariant is phrased in terms of **episodes**, not a single
compile-then-run timeline:

> **Stage 1 invariant: policy code executes only *inside a compilation episode*, never
> during program execution outside one.**
> All run-time enforcement is the engine consulting declarative residue.

Stage-1 corollary (§3.6): new policy directives inside dynamically compiled source are an
install-time error (`sub_policies_allowed: none` for dynamic children); the caller's
already-reified descriptors govern the new fragment. Legal from stage 2 on.

- **Stage 1 — declarative only.** Controllers run at compile time and install descriptors
  (syntax_allowed, import/export/internal lists, COW classes, opaque markers). Run time =
  engine consults tables. Covers the multi-tenant nginx core: tenant isolation, capability
  propagation, COW views, opaque values. Bonus: declarative-only is exactly the
  Safe-C/Maxim-compilable profile — everything built in stage 1 is AOT-friendly by
  construction.
- **Stage 2 — compile-time hooks.** Parser callbacks (accept/reject terminals) and
  source/AST transforms — **mirror-as-policy lands here**. Cheap to add safely: these fire
  during compilation, a phase where policy code already runs — no hot path, no reentry
  into tenant execution; the compile-time budgets stage 1 already needs cover them. The
  stage-1 run-time invariant still holds.
- **Stage 3 — run-time hooks.** The resident residue: value validators, dynamic predicates
  at check points. Needs the full §3.5 machinery (pinned authority, confinement,
  per-invocation budgets, fault mapping, GC/revocation) and carries the hot-path cost.
  Arrives last, onto check points that already exist.

**Stage-1 reservations** (so stages 2–3 arrive without migration):

1. **Tri-state descriptors** — encode `allow / deny / allow-with-check` from the start;
   `allow-with-check` is an install-time error in stage 1. Boolean descriptors would force
   a format migration later.
2. **Check-point placement** — the engine sites where descriptors are consulted are
   precisely where hooks fire later; building declarative checks builds the hook sites.
   Stages 2–3 add only a branch on `allow-with-check`.
3. **POM field shapes** — `add_policy` descriptor fields defined so function values can
   appear later without schema breakage (stage 1: a function value is an install-time
   error, not an unrepresentable type).

Known **stage-3 customers** (value-dependent checks that will tempt early escape hatches —
keep them listed rather than widening the declarative vocabulary ad hoc): e.g. "header
value is a number passing this verifier" (v1 notes). The expressiveness ladder (§3.5) maps
one-to-one onto these stages: the ladder *is* the build order.

**Learning mode staging (§9.6.4):** the *static harvest* ships with stage 1 (it is a
dry-run compile, no new machinery); the *dynamic harvest* (run-time recording facility)
and shadow mode land with stages 2–3.

**Delegation/revocation staging (§5.4):** delegability flags, attenuation checks,
subtree/mass revocation, one-shot/TTL counters are all declarative — **stage 1**;
`on_revoke` events and refined in-flight semantics land with stages 2–3.

### 8.2 Performance model — overhead and anti-overhead
*(distilled 2026-07-07)*

**Governing principle:**

> **Pay at the boundary, not in the loop.** Policy costs concentrate at fragment
> boundaries and admission time; fragment-interior execution runs unchecked at full
> engine speed. The declarative import/export lists make this *provable*: an object that
> never escapes its fragment needs **zero** checks — statically known. Unlike JIT
> heuristics, the cost is predictable and auditable: read the policy, know where the
> checks land.

**Time — compile/admission.** Controller execution + a linear AST pass with hash/bitmap
descriptor lookups. For the nginx deployment this is **reload-time, not request-time**:
compile → sign → cache → workers inherit (§9.2); amortized ~zero per request. Dynamic
`eval` reopens episodes at run time — mostly denied in tenant profiles, budget-capped
where allowed.

**Time — run time, stage 1 (declarative):**

| Check | Mechanism | Cost |
|---|---|---|
| Fragment identity | static per bytecode function | **zero** — "current fragment" = current function's id |
| Opaque values | new value tags | **~zero marginal** — qjs already tag-dispatches every op; non-opaque values keep today's fast paths |
| Property access on *shared* objects | descriptor/bitmap test, cacheable per (fragment × shape) | few cycles, branch-predictable; make-or-break is IC integration (§10.3) |
| Cross-fragment calls | per-arg descriptor check, cacheable per call site | small, amortized |
| Delegation | tag + delegability bits at `pass_as_arg` | trivial |
| Revocation | epoch/generation bump → lazy IC revalidation | ~free normally; cost lands at the rare revocation event |

**Time — run time, stage 3 (hooks):** a JS call per checked operation — expensive by
construction; the tri-state keeps `allow`/`deny` fast and only `allow-with-check` pays.
The expressiveness ladder is thus *also* the performance ladder — its third reading
(assurance §9.6.1, delivery order §8.1, now cost).

**Memory.**
- Policy metadata ∝ policies + fragments (shared templates via parameterization) — never
  ∝ operations or objects.
- **COW views ∝ actual divergence** — views materialize only on write-divergence; most
  objects live in one domain; views can be shape-variants, not copies. You pay for
  exactly the sharing you use.
- Grant chains: only for delegable capabilities (default non-delegable ⇒ no chain).
- Learning logs: sampled, bounded, off hot path.

**Anti-overhead — restriction is optimization fuel:**

> **A policy is a set of declared invariants — and policy invariants never deoptimize.**
> A JIT *infers* "this prototype won't change" and pays deopt machinery when wrong;
> COMCON *enforces* the assumption — the lattice guarantees a policy-derived invariant
> cannot be violated later. Optimization without speculation.

In increasing order of payoff:
1. **Frozen prototypes / read-only natives** → those inline caches never invalidate —
   *faster than stock JS*, where proto mutation poisons ICs.
2. **`syntax_allowed` exclusions** → a no-`eval`, no-`with` fragment needs no scope
   materialization; stock engines pay everywhere for what *might* happen.
3. **Verify once, run free** — whatever admission-time reachability analysis proves
   (§9.5) needs no per-access check at all.
4. **Opaque = representation freedom** — unobservable representation may stay encrypted,
   compressed, or **remote** (handles avoid serialization; computation moves to data).
5. **Policy absorbs defensive programming** — application code sheds `typeof`/null/shape
   guards under policy guarantees; a system-level win invisible to engine-only
   benchmarks.
6. **Maxim/Safe-C endgame** — complete import/export/internal lists + typed extensions =
   a closed world: direct calls, no shape checks, no prototype walks, C-like code.
   **The narrower the policy, the faster the code** — restriction and optimization are
   the same declaration read twice.

**Scenario summary:**

| Scenario | Expected net |
|---|---|
| Static multi-tenant nginx, stage 1, no eval | ~zero overhead; some paths **faster** (frozen ICs); reload cost amortized |
| Heavy cross-tenant object sharing | §10.3 (COW/IC) is decisive — *the* performance risk; prototype early |
| Hook-heavy policy | expensive by construction; the ladder keeps you off this rung |
| REPL / dynamic eval | episode-per-input, budgeted — fine for interactive use |
| Maxim-compiled fragments | **negative overhead** — faster than stock qjs |
| Revocation storm (CVE response) | one epoch bump + IC re-warm; bounded, rare |

Practical consequence: **cost-transparent descriptors** — every POM descriptor/hook class
carries a documented cost class (O(1) bitmap / IC-cached / JS-call), so policy authors see
the price as they write (§4).

---

## 9. Host integration & authoring

### 9.1 Text files are projections of the fragment tree
The canonical structure is the fragment tree (§3.3) — control edges, policies,
communication grants. Every "how does the config look" question is: **which projection of
that tree do we serialize, for whom?** Three representations, one model:

- **A — inline** (single file, nested backticks). Policy directives embedded in JS source;
  the nesting *visibly is* the policy stack. Right for the host's own entry file, small
  setups, examples/tests. Wrong for multi-tenancy: splicing third-party text into a file
  violates the "no arbitrary text inclusion" rule, defeats per-tenant signing, and makes
  review/merge painful.
- **B — sidecar** (code and policy in separate files), bound at the policed include:
  `include <interpreter> <expected-symbol> <policy-ref> <file>`. Already the v1 collision
  mitigation (external policies); matches the "defaults file + customization files"
  layering. Right for tenant-submitted code: **the tenant writes JS, the host attaches the
  policy — the tenant never controls their own cage.** The tenant-facing contract.
- **C — the config tree on disk (canonical).** Movable subtrees, tracing, rollback (§1)
  imply a directory hierarchy mirroring the fragment tree; one node = code + policy +
  manifest (signature, maker chain, tests, docs — "alive knowledge", §5 of v1 context).
  Git-friendly — the v1 note "can I use git as CVS for nginx configs?" answers itself.
  Tenant onboarding = grafting a subtree; migration = moving one; rollback = git.

**Rule: C is canonical; A and B are projections of it.** Tooling converts between them
mechanically.

### 9.2 The bootstrap chain (nginx)
Classic `nginx.conf` stays untouched and remains **the root of trust** (v1: "nginx is
source of the trust, and it is root"). The chain is the §3.3 control tree made
operational:

```
nginx.conf (classic; root of trust)
  └─ one directive: comcon_load /etc/nginx/comcon/root.js
       └─ root.js: `comcon: set_default_policies({...})`   ← grounding fixed point
            ├─ loads policy library + js_com API + helpers
            ├─ host program (exposed / hidden parts)
            └─ per-tenant subtrees, each under a narrowed policy
                 └─ tenants repeat the same shape for sub-tenants (automatic, §3.3)
```

Every arrow is a control edge. **Admission pipeline:** tenant submits text → host compiles
it in a policed episode against the declared symbol+policy → runs its attached tests →
signs and caches the result → live reload swaps the subtree (nginx reload semantics;
transient state lingers gracefully).

### 9.3 Authoring: the engine is the language server
Because enforcement lives in the compiler, the same engine that *rejects* can *explain*.
The IDE story is not "build a bespoke IDE" but "expose the engine's knowledge through
standard channels":

1. **LSP server backed by COMCON-qjs itself.** Diagnostics are real policy compile errors
   from dry-run episodes — never a reimplementation that drifts. Hover = the **effective
   policy at the cursor** (the lattice meet at that fragment path). Completion offers only
   what `import_list` admits; constructs excluded by `syntax_allowed` don't complete. Any
   editor consumes this — one integration, every editor.
2. **Authority-aware structured editor** (the WYSIWYG from the notes): renders the
   fragment tree; third-party-editable regions highlighted; hidden parts invisible
   **according to the viewer's own position in the tree** — the editor obeys the same
   visibility model (COW / some_v / some_vn) as the runtime. A tenant literally cannot see
   what their policy hides.
3. **Explain-mode / deny traces.** Every denial (compile or run time) carries *which
   policy, installed by which controller, at which tree path* — plus a **policy diff**
   tool ("what authority changed between config v1 and v2"). The single
   highest-usability feature for operators; the denial data structure must be designed
   with the descriptors, not bolted on (§10).
4. **REPL / per-worker CLI** (already in the design, §7): query effective policy of a live
   fragment, dry-run a change against a running worker, snapshot/rollback.

Pleasing consequence: **policy usability is itself policy-governed** — LSP, editor, and
REPL are just more controlled fragments talking over communication edges; the tooling
never needs (or gets) a backdoor.

### 9.4 Integration contract with js_com / pilgrim *(specifics to be confirmed)*
What COMCON needs from the host, kept minimal:

- **(a) One load point** in the host lifecycle (module init → root episode).
- **(b) The host context object** (js_com's API surface) handed to the root as
  *communication* grants — js_com objects are imports under descriptors like everything
  else, **not** ambient globals.
- **(c) Reload / worker-fork semantics** — which episodes re-run on reload vs. survive as
  cached signed bytecode; fresh-fork timing per worker.
- **(d) Library shape for pilgrim** — the same entry packaged as a library, so the "single
  shell over nginx/big-ip" can host multiple COMCON roots.

**Requirement on js_COM's own shape:** policy cannot fix an API whose *shape* conflates
levels — one omnipotent `exec()`-style method cannot be subdivided by property visibility.
js_COM must be factored so that **authority boundaries fall on property/method lines**;
this is a design obligation on the host API, not something COMCON can retrofit.

### 9.5 Validation against the motivating threats
A check of the foundation against the four threats that motivated the project
(2026-07-06). All four answers reduce to mechanisms already defined — no new machinery;
this section records the arguments.

**(1) The DOM problem.** The browser DOM is unfixable *in situ* because it is an ambient,
fully-authorized object graph with enforcement outside the engine — and wrappers
(jQuery/YUI) fail because the raw object stays reachable underneath. COMCON removes the
precondition rather than repairing the artifact:

| DOM failure | COMCON structural answer |
|---|---|
| Ambient global graph | js_COM arrives as **communication grants** (§9.4b), never ambient globals — no fragment holds "the js_COM", only its granted view |
| Wrappers leak the raw object | **COW views are the same object** (§5.1) — no unwrapped original exists to reach (why proxies were rejected) |
| Traversal = total reachability | Object-graph edges are properties; property visibility is per-fragment — **reachability itself is policy**, and in stage 1 the transitive reachable set is *computable at admission time* |
| Level intermixing | Leveled APIs expressible as policies; a fragment granted the high level cannot reach *or re-derive* the low level (read-only natives + gated portals, §3.6) |
| Prototype poisoning | Read-only natives & prototype chains; COW-on-prototype for legitimate per-fragment extension |
| Irrevocable references | Handles + uninstallation; connection-scoped expiry |

**(2) Third-party scripts & libraries.** A 3rd-party unit is a controlled child fragment;
the host attaches its policy (§9.1B). The **supply-chain inversion**: an npm-style library
runs with the caller's full authority; under COMCON a library's authority is **what the
consumer granted, not what the library requests**, and its own includes are child
fragments under its (narrower) policy — transitive dependencies are policed. A compromised
update executes with exactly the granted slice. Protection is mutual: the 3rd party's
internals are invisible to neighbors — proprietary tenant logic is protected *from* its
peers, not only peers from it.
*Explicitly not guaranteed:* correctness of the policy you wrote (mis-granting; mitigated
by the standard library, profiles, policy diff — never eliminated); covert channels
(§10.12); TCB bugs (engine + POM are trusted — hence minimal POM, greppable grants);
resource exhaustion (budget machinery is design-mandatory).

**(3) REPL — safe for any actor.** Each REPL input is a dynamically compiled child
fragment (§3.6) under the connection's pinned policy; server objects are handles expiring
at `on_close`; outputs are policed down to values / handles / *nothing*:

> **REPL → REL** — computation *without disclosure*: drop the P(rint).

Plus protocol-driven finite command sequences (§7) and per-episode budgets. Security does
**not depend on the actor's alignment**, only on the grants — "any actor" includes an
adversarial one; an AI is an actor with high throughput. Residual risk: response *timing*
as a covert channel → the constant-time/no-logs REL profile for the strictest cases.

**(4) AI-authored scripts and policies.** The admission pipeline (§9.2) is
authorship-blind: compile under policy → attached tests → sign. The guarantee is
*structural*, not review-based — required, since AI output volume exceeds human
verification capacity (the v1 "собачий предел" argument): verification comes from the
cage, not from reading. §9.3 closes the loop: the engine-as-language-server feeds the
*generating* AI real deny traces, so it iterates against the actual enforcer.
For policies, monotonicity yields the

> **Asymmetric failure property: an AI-written sub-policy can deny too much, never grant
> too much** — "AI writes policy" is converted from a *security* risk into a *liveness*
> risk (worst case an outage, never a breach).

AI-authored policies are controlled fragments under `sub_policies_allowed` /
`what_policies_allowed` (§4.1); they arrive as *declarative* policies (lowest ladder rung
— auditable, diffable), are reviewed as a descriptor diff, dry-run in episodes, rolled out
through the config tree with rollback. Root/high policies remain human-authored, signed,
maker-chained.

### 9.6 Policy engineering: testing, trust, methodology
*(distilled 2026-07-06)*

#### 9.6.1 Testing — the dual test surface
A policy has two failure directions, hence **two test suites**, both shipped in the
fragment's manifest (§9.1C):

- **Allow-suite (liveness):** intended programs must still compile and run — *the cage
  admits the animal*.
- **Deny-suite (safety):** attack programs must be rejected — *the cage holds*.

A deny test must assert **the specific denial** — denied by *this* policy, at *this* tree
path, for *this* reason — never merely "it failed" (a syntax error must not masquerade as
enforcement). The denial/explain structure (§10.9) is therefore not just operator UX: it
is **the assertion language of the deny-suite** — one more reason it is designed with the
descriptors, not bolted on.

Stronger tools beyond example-based tests:

- **Intent vs. text.** Separate what was *meant* ("tenant must never reach `fs.write`")
  from the policy *program* written. Intent = invariants; testing = checking text against
  intent. For **stage-1 declarative policies this is decidable** — the transitive
  reachable set is computable at admission (§9.5), so reachability invariants are static
  checks. *Declarative policies can be verified; hook policies can only be tested* — the
  expressiveness ladder restated from the assurance side.
- **Differential testing.** Run a corpus under old and new policy versions; the policy
  diff (§9.3) *predicts* the delta; the observed delta must match. Diff-predicted vs.
  diff-observed catches both policy bugs and diff-tool bugs.
- **Adversarial generation.** Fuzz programs against the policy, declarative residue as
  oracle. The **asymmetric failure property (§9.5) allocates the budget**:
  over-restriction is self-announcing (tenants complain); under-restriction is silent —
  deny-suites and the adversarial corpus concentrate there.
- **Reproducibility.** Dry-run episodes + controller determinism (§3.3) make policy tests
  hermetic; re-run on every engine upgrade against the versioned p_symbol enumeration
  (§10.2).

#### 9.6.2 Trust — inverse to the lattice
The library stack (POM wrappers → generic profiles → domain profiles → application
policies) does not multiply trust obligations; it concentrates them:

> **The higher a policy sits, the *less* trust it requires — monotonicity caps its
> damage.** An application policy can only narrow within its parent's grants; its worst
> case is denying too much.

| Level | Trust requirement | Maintained by |
|---|---|---|
| Engine + POM (TCB) | maximal | small surface, C review; formal methods where possible |
| Root grounding policy | maximal | human-authored, signed, short |
| Wide-granting profiles near root | high | exhaustive suites, static intent checks, review |
| Certified hooks | moderate | certification criteria (§10.7), budgets, signatures |
| Domain / app policies | low | **structural containment** — the lattice does the work |

Provenance carries the rest (all anticipated in v1): **maker chains** — each level signed
by its maker, trust evaluated over the chain; **content verification** (sha) so the
deployed policy *is* the reviewed one; **revocation** = config-tree rollback (§9.1C) +
signature revocation. The libraries also police themselves: helpers execute inside
controller activations under the policy-for-policy — a helper cannot grant what its own
governing policy withholds. Trust is structural first, provenance second, never faith.

#### 9.6.3 Methodology — the method is application-independent, the policies are not
Bottom layers (blocks, FS, DB) get **standard, parameterized library policies**; the top
(application) layer is bespoke but *thin*, composing the profiles below. The transferable
rules:

1. **One layer per policy; the policy *is* the layer's protocol.** Grant one level's
   vocabulary; forbid level-crossing (the anti-DOM rule). An FS client gets FS verbs,
   never block verbs.
2. **Default-deny, closed world.** `import_list` is a whitelist; absence means no.
3. **Grant capabilities, not names.** Handles/facets scoped to need, revocable (POLA).
4. **State intent separately; check text against intent** (§9.6.1).
5. **Prefer the lowest ladder rung.** Declarative before hooks — every hook converts a
   verifiable property into a merely testable one.
6. **Narrow early.** Compile-time restriction beats run-time checking: cheaper, statically
   analyzable, Maxim-friendly.
7. **Parameterize, don't fork.** Profiles with parameters, so review effort accumulates in
   few artifacts.
8. **If order matters, encode it.** Sequences as protocols (v1 transaction/syntax
   wrappers, `compose_seqs`): open-before-read is policy, not convention.
9. **Layer owners author layer policies** — the people actually responsible for safety at
   that level of abstraction (v1). Organizational, not just technical.

Worked stack (the v1 leveled-API example): block-policy (only the FS implementation
fragment holds block verbs) → fs-policy (paths as capabilities; no raw fd escapes) →
db-policy (see below) → app-policy (business operations only).

**Flagship example — SQL injection as a grammar problem.** By the reduction principle
(§6), SQL is just another language under COMCON: a query is a fragment with a target
non-terminal, and the DB-layer policy admits only the *parameterized-query* production —
string-concatenated SQL becomes **inexpressible**, not discouraged. Principle 3 ("no tool
with which to produce the wrong result") applied to the most common injection class.

#### 9.6.4 Learning mode — observe, propose, tighten
*(distilled 2026-07-07)* Adoption-critical for brownfield code (nobody knows what an
existing tenant script touches); the proven pattern of SELinux `audit2allow` / AppArmor
complain mode / BIG-IP ASM policy builder, integrated on COMCON's own terms.

**The framing rule:**

> **Learning describes; intent prescribes.** A learned policy is *candidate text* — "what
> the code did during the window" — never "what it should do." It passes the same intent
> gate (descriptor-diff review, dry-run, staged rollout) as any hand-written policy.
> **Auto-deployment of learned policies is forbidden — otherwise you learn the attack in.**

**Two harvests:**

- **Static harvest** (compile-time; essentially free in stage 1): the productions a
  fragment uses, names it references, portals it touches — computed by a dry-run episode
  against the enumerated p_symbols. Exact; no observation-window problem; alone yields a
  strong candidate `syntax_allowed` + `import_list`. Ships with stage 1.
- **Dynamic harvest** (run-time; stages 2–3): properties actually accessed, exports
  actually consumed — an engine recording facility at the check points; sampling-based,
  window-limited; refines COW/property-level grants.

**Structural economies:**

- The observation record is **the denial record with the sign flipped** — "was used"
  vs. "was denied", same schema (§10.9). Design once.
- **Shadow ("complain") mode** = run the candidate with denials rerouted to the log —
  run-time dry-run, bridging candidate → enforcement.
- The observation corpus **is the allow-suite** (§9.6.1): replaying the recorded workload
  under the candidate must pass. One pass yields candidate text *and* its regression
  tests.
- The AI loop closes (§9.5(4)): logs → AI proposes a *declarative* candidate → human
  reviews the diff → shadow → enforce (the v1 "AI under the hood… safe experiments,
  final config" pipeline).

**Lifecycle:** harvest → generalize → **intent review** → shadow → enforce → monitor.
New denials after a code update signal drift and trigger a re-learn cycle — learning is
continuous; the intent gate is per-change.

**Dangers and mitigations:**

1. *Learning the attack in* → the intent gate. Non-negotiable.
2. *Coverage gaps* (unexercised error handlers, rare jobs) → over-**narrow** policies —
   an outage risk, not a breach (asymmetric failure property, §9.5). The engine reports
   **coverage against the syntax tree** (which fragments/productions were never
   exercised) — observability of incompleteness that `audit2allow`-class tools never had.
3. *Generalization is where widening hides.* Turning observed `user_1041.name,
   user_1042.name…` into `user_*.name` is a widening step — authoring, not nesting, so
   the lattice does not police it. Generalization heuristics are part of the reviewed
   diff, never silent.
4. *Observation is an act of control.* Only a fragment's **controller** may put it in
   learning mode — recording behavior is surveillance; a tenant can never harvest a peer.
   Learning-mode activation travels the control edge.
5. *Record names and operations, never values* (REL discipline, §9.5(3)) — the log must
   not become the disclosure channel.

No new mechanism class: learning mode = a policy profile (descriptors: allow+record) + an
engine recording facility sharing the denial schema + shadow mode as run-time dry-run.

## 10. Open questions (v2 — supersedes v1 §6, tensions there still apply)

1. **POM surface definition** — *the* critical design task: minimal orthogonal verb set
   (add_policy, symbol enumeration, import/export/internal descriptors, engine_control),
   which parts are frozen vs. extensible; every capability in the POM is a capability
   forever.
2. **The two closed enumerations** — (a) **p_symbols**: stable, versioned naming of
   QuickJS grammar productions that policies can target across engine upgrades;
   (b) **compile portals** (§3.6): the complete list of dynamic-compilation entry points
   (direct/indirect `eval`, `Function`/`AsyncFunction`/`GeneratorFunction`, `import()`,
   host `JS_Eval`) with the ambient authority each confers — completeness of this list is
   what makes the sandbox-escape claim checkable.
   *(referenced from §3.6)*
3. **COW-domain semantics & cost** — property lookup through domain views on the hot path;
   interaction with prototype chains, inline caches / hidden shapes; "COW-on-COW" ordering.
   **Design together with revocation cost (§5.4):** revocation mutates state that hot
   paths consult — inline-cache/shape invalidation strategy is shared between the two.
4. **Extension vs. monotonicity** — precise rule for when syntax *extension* is safe under
   a narrowing-only lattice (proposal: extensions may add expressiveness but not ambient
   authority).
5. **`engine_control` surface** — with policy code dissolved into "code holding the
   control capability" (§3.3), the question becomes: exactly what does `engine_control`
   expose to a controller (compile state? host state?), how are its effects sandboxed,
   and how is determinism/reproducibility enforced (needed for compilation caching and
   signed builds).
6. **Control-relation invariants** — enforcing acyclicity (no fragment controls its
   ancestor path) and keeping the two edge types (§3.4) from leaking into each other:
   no communication grant may confer control, and control must not implicitly open
   communication.
7. **Stage-3 hook machinery** — concrete design of the confinement context (what exactly a
   hook sees of the checked operation), per-invocation budget mechanics, certified-hook
   library criteria (§3.5 ladder), and hot-path cost of `allow-with-check` dispatch.
8. **Multi-tenant worked example** — tenants with sub-tenants; config subtrees movable
   across the tree; tracing/rollback; app-registered state serialize/restore. This is the
   validating scenario for the whole design.
9. **Denial/explain data structure** (§9.3) — every denial carries policy identity,
   installing controller, and tree path; must be designed together with the descriptors,
   not bolted on; feeds LSP diagnostics, deny traces, policy diff, **serves as the
   assertion language of deny-suites (§9.6.1), and doubles (sign-flipped) as the
   observation record of learning mode (§9.6.4)** — one schema for "was denied" and
   "was used".
10. **Config-tree ⇄ live-tree correspondence** (§9.1–9.2) — reload and subtree-swap
    semantics; what "the same fragment" means across reloads (identity for
    tracing/rollback); which episodes re-run vs. survive as cached signed bytecode.
11. **js_com / pilgrim contract specifics** (§9.4) — load point, host context surface,
    worker-fork timing; pilgrim's multi-root library shape; and the js_COM **API-factoring
    audit**: verify authority boundaries fall on property/method lines (no
    level-conflating omnipotent methods).
12. **Learning-mode machinery** (§9.6.4) — generalization heuristics (the reviewed
    widening step), recording-facility cost at check points (sampling strategy),
    observation-authority enforcement (activation strictly via control edge), and
    coverage-against-syntax-tree reporting.
13. **Empirical performance validation** (§8.2) — the cost model is argued, not measured:
    microbenchmarks in the first slice (§11.7); IC/shape integration strategy for
    (fragment × shape) caching; budget-metering overhead itself; verify the locality
    hypothesis (most objects never cross fragment boundaries) on real js_com workloads.
14. *(carried from v1)* directive collisions; disclosure side-channels; static/halting
    limits → forbid-or-defer.

---

## 11. Minimal first slice (v2)

Exercises every pillar, including the new POM/policy-as-program layer:

1. **Directive recognition** — recognize a `comcon:`-prefixed template-literal directive in
   the prologue path (start in `quickjs-ng`); parsed, stack-tracked (push/pop per §3.3),
   effects inert.
2. **Minimal POM** — expose `engine_control` with a single verb:
   `add_policy({syntax_allowed})` for one enumerated symbol; the policy *program* runs at
   compile time under a hardcoded grounding policy.
3. **One compile-time enforcement** — `syntax_allowed` excludes one production (e.g.
   `WhileStatement` or assignment-to-LHS) inside the guarded fragment → hard compile error.
4. **One run-time enforcement** — a single `opaque` value with operator propagation
   enforced in `JS_CallInternal`, *or* a two-fragment COW-domain property-visibility demo
   (pick the cheaper to prototype; opaque is likely simpler).
5. **Monotonic nesting test** — an inner policy can tighten but not loosen (2)–(4).
6. **Proofs** — paired `.c`/`.js` test cases for each claim (*every claim demonstrated by a
   passing run*).
7. **Microbenchmarks** (§8.2) — shared-property access, cross-fragment call, opaque-op
   dispatch vs. stock qjs on both vendored trees; §10.3 (COW/IC/revocation cost) is the
   make-or-break implementation question and must be measured, not argued.

> Validates: directive attachment + policy stack, policy-as-program over a real (tiny) POM,
> compile-time enforcement, run-time enforcement, and the narrowing lattice.
> The slice is a **stage-1 artifact** (§8.1) as written — declarative residue only — but
> must honor the stage-1 reservations: tri-state descriptor encoding, hook-ready check-point
> placement, function-tolerant POM field shapes.
