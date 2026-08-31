# COMCON — Kernel Semantics & the No-Amplification Theorem (v5.0)

*Formal companion to `FOUNDATION.md` §4/§6. Status: rigorous sketch — precise enough to
implement against and to find design errors (it already found four, §5), not yet a
machine-checked development.*

---

## 1. Domains

```
Resources    r ∈ Res            host resources (COM nodes, tables, sockets, clock…) ∪ POM nodes
Permits      p ∈ Perm = Op × Res                    (operation-on-resource pairs)
Capabilities c = cap(r, π ⊆ Perm, i?)               unforgeable; optional interceptor stack
POM nodes    n ∈ Node,  subtree order  n' ⊑ n
POM handles  h = cap(POM, {read, rewrite, bind, admin} ↾ reach(h))   — ordinary capabilities
Values       v ::= d (data: scalars, frozen records) | c | q (quotation) | ρ (environment)
Environments ρ : Name ⇀ Value                       (frozen at bind; env() = ∅)
Quotations   q = node(text, v̄)   with side condition  ∀i. A(vᵢ) = ∅   (cap-free, deep)
State        Σ = (P : POM tree,  B : Node ⇀ Env,  W : host world)
```

**The authority measure** `A : Value → ℘(Perm)` — everything is proved against it:

```
A(scalar)        = ∅
A(record)        = ⋃ A(fields)        — data structures CAN carry caps ⇒ checks are deep
A(cap(r, π, i))  = π ∪ A*(closure(i)) — a facet carries its interceptor's closure authority
A(q)             = ∅                   — by the QUOTE side condition
A(ρ)             = ⋃ₓ A(ρ(x))
A*(ρ)            = transitive closure through records and closures
```

---

## 2. Evaluation rules (big-step `ρ ⊢ t ⇓ v, Σ → Σ'`)

**(NAME)** — deny-by-default lives here; there is no other way to obtain a value:

```
x ∈ dom(ρ)                        x ∉ dom(ρ)
─────────────────                 ────────────────────────────────
ρ ⊢ x ⇓ ρ(x)                      stage-0 error — no ambient fallback
```

**(GRANT)** — deliberately unguarded:

```
ρ ⊢ e ⇓ ρₑ (unfrozen)     ρ ⊢ t ⇓ w
──────────────────────────────────────
ρ ⊢ grant(e, x, t) ⇓ ρₑ[x ↦ w]
```

"You must hold what you grant" is **not an operational check**: any `w` passable to
`grant` was evaluated from ρ, so `A(w) ⊆ A*(ρ)` automatically. **Possession is a
metatheorem of the evaluation relation plus unforgeability** — which is why the
hardening milestone (`HARDENING.md`) is the enforcement mechanism of the axiom itself.

**(MEDIATE / FACET)**

```
ρ ⊢ t ⇓ c      ρ ⊢ tᵢ ⇓ closure(i, ρᵢ)
─────────────────────────────────────────────────────────────
ρ ⊢ mediate(t, tᵢ) ⇓ c′ = facet(c, i, ρᵢ)      A(c′) ⊆ A(c) ∪ A*(ρᵢ)
```

Invoking `c′(op, args)` runs `i` under `ρᵢ`: `{deny m}` → error or audit, per the
binding's declared failure mode; `{allow args′}` / `{transform …}` → forward to `c`.
Special cases: **attenuation** (`A*(ρᵢ) ∩ Cap = ∅` ⟹ `A(c′) ⊆ A(c)`); **revocation /
metering** (mutable flag / counter in `ρᵢ`); **facet** (`ρᵢ` closes over raw caps the
receiver never holds — the library "transaction" pattern). Always `A(c′) ⊆ A*(ρ)`:
a facet exercises only its *creator's* authority.

**(QUOTE)** — the cap-free rule as a constructor side condition *(v5.0 — R2,
strengthened)*:

```
ρ ⊢ tᵢ ⇓ vᵢ      ∀i. stone(vᵢ)      (deep-frozen plain data  ⇒  A(vᵢ) = ∅, stably)
────────────────────────────────────────────────────────────────────────────────────
ρ ⊢ quote(s, t̄) ⇓ node(s, v̄)      A = ∅
```

A purely *structural* `A(vᵢ) = ∅` check is TOCTOU-unsound: a spliced **mutable** record
can acquire a capability after the check, and a **getter/proxy** can lazily produce one
when later read (or execute side effects during checking). The side condition is
therefore **`stone`** — primitives and deep-frozen records/arrays of the same, no
getters, no proxies, no mutability — which makes cap-freeness a *stable* property.
Splicing anything else, capability or merely non-stone, is a stage-0 error at the
producer. Corollary: **opaque values carry authority** (their `pass_to` permit), so
they are never spliceable — secrets structurally cannot leave through quotations or
proposals.

**(BIND)** — meet-composition inside the rule:

```
ρ ⊢ tₕ ⇓ h    bind ∈ ops(h)    ρ ⊢ sel ⇓ n    n ∈ reach(h)    ρ ⊢ tₑ ⇓ ρₑ
────────────────────────────────────────────────────────────────────────────
ρ ⊢ bind(tₑ, sel) ⇓ ok,      B′ = B[n ↦ freeze(ρₑ) ⊓ B(n)]
```

`ρ₁ ⊓ ρ₂`: domain = `dom₁ ∩ dom₂`; every operation must pass **both** bindings'
mediations (membrane stacking) ⟹ authority = permit intersection. *(v5.0 — R1, scope
correction:)* the ACI/confluence claim holds for **restrictive mediations only** —
filters commute, **transforms do not** (two rewriting policies produce order-dependent
results). Rule: **at most one adaptive-profile policy per node**; a second adaptive
binding on the same node is an admission error (`E_ADMIT_ADAPTIVE_CONFLICT`), while
restrictive bindings continue to meet freely over the single adaptive one. *(v5.0 —
R12:)* declared failure modes compose by strictness — `reject > deny > attenuate >
audit`; the meet takes the strictest. *(v4 clarification, for data-like instances: meet
composes mutation **rights**, never values — there is no intersection of two listen
ports; the right-to-set intersects like any permit, while conflicting values are an
ordinary admission error, not a meet.)* Post-bind the
environment is frozen (later grants create *child* envs). Because plain `bind` only
meets, **even a self-rebind can only narrow**; *widening* is a distinguished
administrative `rebind` (a new binding **epoch**) requiring `admin ∈ ops(h)` — outside
the fragment algebra, and in a multi-worker server a fan-out-required safety-class
mutation (`POM.md` §3).

**(ADMIT)** — the authority-neutral gate:

```
K = (Γ, φ, T)
(i)   free(n) ⊆ dom(Γ), types match Γ          ← the typed host-API schema (ROADMAP §M2)
(ii)  φ(n)                                       syntactic predicates ("no reflect", …)
(iii) tests T pass with n bound under Γ_test    caps → doubles; clock/RNG/IO denied
────────────────────────────────────────────────
ρ ⊢ admit(n, K) ⇓ certified(n) | reject
```

Clause (iii) makes admission **reproducible** and gives it **zero blast radius**.

**(EXEC precondition — v5.0, R7.)** Executing a node requires `n ∈ dom(B)`: an unbound
node is **inert, and attempting to execute it is an explicit error** (`E_UNBOUND`) —
never an implicit ∅-run. Dually, **a node is never unbound once bound**: a pin/hash
mismatch or a failed re-admission refuses the *new epoch* while the previously admitted
epoch keeps serving (POM.md lifecycle). Between the two rules there is no reachable
state in which formerly-governed code runs ungoverned.

*(v4)* **ADMIT is instance-generic.** The contract's `Γ` (schema) and `φ` (grammar
predicates) are supplied per **language instance** (grammar, tree, schema, lowering) —
the same rule gates JavaScript fragments, config sentences (M-CFG), patterns, and SQL.
And the **∅-environment degenerate case**: binding a node to the empty environment
yields pure data — evaluation under ∅ can construct only values with `A = ∅`, so the
theorem below holds trivially there; FOUNDATION Principle 9 ("data is code bound to ∅")
is this observation stated as a principle. The environment-richness gradient — snapshot
(∅) → config → typed policy → full program — is covered by one semantics.

**(REFLECT ops)** — `read(h, n)` requires `read ∈ ops(h)`, `n ∈ reach(h)`, and returns
the subtree **as a quotation** — program text read from the POM is cap-free data by the
same invariant as QUOTE (one constructor, one rule). `rewrite(h, n, q)` requires the
op, an **admitted** q, and the node's mutation safety class; it marks `n` dirty
(→ hybrid bytecode fallback until re-AOT, `POM.md` §4).

**Derived forms** (no new rules): `attenuate`/`revoke`/`meter`/`audit` = mediate
flavors; `include = parse ∘ admit ∘ bind`; `realize(q) = admit(q, K)` (K mandatory),
then `bind(ρ_R ↾ q.manifest, q)` — the realizer's grants **restricted to the
quotation's free-name manifest** (least-authority realization, §4.4) — then EXEC.

---

## 3. Theorem (No-Amplification / Possession-Monotonicity)

> For every `ρ ⊢ t ⇓ v, Σ → Σ'`:
>
> **(a)** `A(v) ⊆ A*(ρ)` — no evaluation produces authority its evaluator did not hold.
> **(b)** Every host-resource operation performed during evaluation is permitted by some
> capability in `A*(ρ)`; the binding store `B` is modified only at nodes covered by a
> bind-capable handle in `A*(ρ)`, and only downward (`⊓`).
> **(c)** *Outer-policy immunity:* a fragment bound at `n` cannot alter `B(m)` for any
> `m` — including `n` itself — unless a handle covering `m` was granted in; and even
> then only monotonically down.

**Proof sketch** — induction on the derivation.

- *Base:* literals (`A = ∅`); (NAME): `ρ(x) ∈ A*(ρ)` by definition.
- *(GRANT):* `w` comes from a sub-derivation ⟹ IH gives `A(w) ⊆ A*(ρ)`; the extended
  environment stays inside `A*(ρ)`. Nothing minted.
- *(MEDIATE):* `A(c′) ⊆ A(c) ∪ A*(ρᵢ) ⊆ A*(ρ)` by IH on both premises.
- *(QUOTE):* `A(q) = ∅` by the side condition. **This is where the cap-free rule is
  load-bearing:** without it, (REALIZE) would transfer producer authority through a
  value that types as data — clause (a) would survive (still ≤ producer) but clause
  (c)'s audit story and the closure/quotation distinction would not.
- *(BIND):* meets only ⟹ `B′(n) ⊑ B(n)`; the store effect is guarded by the handle
  premise ⟹ (b).
- *(ADMIT):* authority-neutral; doubles ⟹ tests exercise no real authority.
- *(EXEC):* a bound fragment's steps are evaluations under `ρ = B(n)` ⟹ IH bounds it by
  `A*(B(n))`; values passed in at runtime were held by their passers (IH on the caller)
  ⟹ globally, authority flows only along held references. ∎ (sketch)

**Corollaries — the worked examples of §4 are instances:** closure mode is clause (a)
applied twice along T ≤ P ≤ B ≤ A; quotation mode is `A(q) = ∅` plus realize-under-ρ_R
⟹ `A(T's env) ⊆ A*(ρ_R)`.

**Stated assumptions (what the proof does *not* cover):**

- **(U1–U3) Unforgeability** — no capability literals; caps opaque (no resource/pointer
  extraction); kernel internals unreachable via prototype pollution, constructor
  ladders, `Function`/`eval`, stack leaks, or shared engine state. This is the M-SES
  milestone (`HARDENING.md`); without it (NAME) has back doors and the metatheorem
  collapses.
- **(F) Compiler faithfulness** — the theorem is proved for the interpreted (stage-0)
  semantics. Compile-through preserves it **iff** maxim's lowering is a refinement of
  the mediated semantics along the provenance links: *the lowered C must simulate every
  MEDIATE/NAME gate it erased.* That is the M8 gate, stated precisely.

**Slogan (Principle 8):** soundness is stage-independent; staging is purely a
performance property. Moving a check earlier never changes what is allowed — only what
it costs.

---

## 4. Worked examples (authority-traced)

Policy units are themselves stage-0 programs bound under deny-by-default environments.
Surface: `env()` (free literal), `grant` (the checked op), `mediate`, `bind(e, node,
{profile, onViolation})`, `admit`; derived: `pom.query`, `quote`, `realize`, libraries.

### 4.1 Hardening a third-party script (external query, restrictive profile)

```js
// harden-analytics.policy.js — stage 0; host granted {pom, grant, mediate, bind, net, log}
const frag = pom.query("module('vendor/analytics.js')");        // intensional target

const beacon = mediate(net.fetch, (op, args) =>
  op === "call" && String(args[0]).startsWith("https://stats.example.com/")
    ? { allow: args } : { deny: "analytics: net blocked" });

const e = env();                                                // deny-by-default
grant(e, "fetch", beacon);
grant(e, "log",   mediate(log, rateLimit(10)));

bind(e, frag, { profile: "restrictive", onViolation: "audit" }); // phase 1: observe
// phase 2: same policy, onViolation: "deny"
```

*Trace.* The vendor file is untouched and runs standalone without COMCON (restrictive
profile). Under the policy, every free identifier resolves in `e` or is a stage-0 error
(audit line during rollout). A corp-wide net policy on the same node composes by meet.
*Compile-through:* the static prefix test lowers to a `strncmp` guard inside the C fetch
stub — the membrane costs ~nothing.

### 4.2 policyA / policyB — closure mode (delegation ⇒ "A+B")

```js
// policyA builds B's world. A holds kernel ops + ratelimit lib + raw table + pom.
const eB = env();
grant(eB, "makeLimiter", ratelimit.makeLimiter);   // vetted transaction combinator only
grant(eB, "bind",        bind);
grant(eB, "pomTenants",  mediate(pom, subtreeOnly("tenants/")));
/* raw `table` deliberately NOT granted */
bind(eB, pom.query("module('gen/tenant-policies.js')"));
```

```js
// policyB under eB — the value placed in envT CARRIES a capability (closure mode)
for (const t of tenants) {
  const eT = env();
  grant(eT, "limit", makeLimiter({ keyPrefix: `rl:${t.id}:`, rps: t.rps }));
  bind(eT, pomTenants.query(`module('tenants/${t.id}.js')`));
}
```

*Trace.* `makeLimiter` holds the raw table capability in the **library's** closure (the
facet pattern) — B never holds it and can only mint limiter facets with baked tenant
prefixes. Chain: **T ≤ limiter ≤ library ≤ A's grant to B.** *Negative test:*
`grant(eT, "table", table)` inside B is a stage-0 resolution error — B cannot even
*name* what A withheld. Also note bind-reach: B can bind only inside `tenants/` because
that is all its POM handle covers.

### 4.3 policyA / policyB — quotation mode (specification ⇒ realizer-bounded)

Same A, same `eB`; **one thing changes — which value flows:**

```js
// policyB now EMITS descriptions — unbound POLICY-JS nodes, zero authority.
// Same language as everywhere else, just quoted (v4.2 — no second grammar):
export default tenants.map(t => quote`
  const e = env();
  grant(e, "limit",   ratelimit.makeLimiter({ keyPrefix: "rl:${t.id}:", rps: ${t.rps} }));
  grant(e, "metrics", host.metrics.scoped("${t.id}"));   // B does NOT hold host.metrics!
  bind(e, pom.query("module('tenants/${t.id}.js')"),
          { profile: "restrictive", onViolation: "deny" });
`);
```

```js
// Realizer R (ops/host) applies A's admission contract, then realizes:
for (const q of quotations) realize(admit(q, corpContract));  // no reflect; rps ≤ 1000…
```

*Trace.* `host.metrics` in the quotation is legal — text, not authority (the same line
is a stage-0 error in closure mode). Realization draws capabilities from **R**, so
**T ≤ R, not ≤ B**. A's control over the outcome: (i) `eB`'s vocabulary — B cannot
develop against names it cannot resolve; (ii) the `admit` contract A authors.
Unrealized quotations are inert forever.

*Honesty:* you cannot prevent B from *writing* descriptions — data construction is
free. You prevent effect (`admit`) and development-against-real-authority (vocabulary).

### 4.4 How `quote` works — and why there is no second policy language *(v4.2)*

**`quote` needs no definition or grant.** Syntactically it is a plain JS tagged
template (`quote(strings, ...values)` — no new grammar). Semantically it is a **free
intrinsic constructor** of the policy-JS profile — rule (QUOTE) of §2 — in the same
class as `env()` and object literals: exempt from the (NAME) rule because a grant would
be meaningless. It can mint nothing (`A(result) = ∅` by the deep side condition), and
free-ness is deliberate: a program can always build equivalent inert data by string
concatenation, so withholding the constructor adds friction, not security. All
enforcement lives where authority enters — `admit` and `realize`.

**What the constructor does.** It parses the literal text — **as policy-JS, the same
restricted profile everything else is written in** — into an unbound POM node
(a structured tree, not a string), attaching each `${…}` splice as an **atomic data
leaf** after deep-checking `A = ∅` (a spliced capability, even buried in a record, is a
stage-0 error at the *producer*; parse errors likewise). Because splices enter at data
positions, never as text, quotations are structurally immune to injection: an
attacker-controlled `t.id` cannot smuggle a `bind(…)` call — the same reason
parameterized SQL kills injection (scenario 3).

**There is no policy language other than policy-JS plus the governed library**
*(rev 3.3 — an earlier draft of this section specified a bespoke "policy-unit" grammar
(`env { … } bind -> …`); that was hypothetical showcase syntax mistakenly promoted
into a spec, and it is withdrawn — it violated the reduction principle and duplicated
machinery we already own).* The corrected picture, in one sentence:

> A **closure** is a policy-JS value that ran; a **quotation** is a policy-JS node
> that hasn't; **"declarative"** is a `syntax_allowed` profile of it; and the diffable
> **descriptor table** is its admission-time normal form.

Concretely:

- **Two-phase binding** (the correct part of the old section, unchanged): `${…}`
  splices are **early-bound producer data** (`A = ∅` enforced); the quoted code's free
  names (`ratelimit.makeLimiter`, `host.metrics`, `bind`, `pom`) are **late-bound
  mentions** — they resolve only when the node is bound and executed under the
  **realizer's** ρ_R, by the ordinary (NAME) rule. This is exactly why B may mention
  what it does not hold. No special mention semantics: staging *is* the mechanism.
- **`realize(q)` is not a desugaring — it is literally the kernel**, with two
  hardenings *(v5.0 — R6, least-authority realization)*: (i) the contract argument is
  **mandatory** — `realize(q)` without a `K` does not exist, and the default `K`
  admits only the declarative profile; (ii) the realization environment is **never the
  realizer's full ρ_R**. Quotations carry a **free-name manifest** (computed at quote
  time); realize binds `ρ_manifest = ρ_R ↾ manifest` — the realizer's grants
  *restricted to the names the quotation declares* — so a proposal reviewed as "needs
  `ratelimit`, `host.metrics`, `bind`" can never touch anything else even if the
  operator's session holds it. Then: `realize(q) = admit(q, K); bind(ρ_manifest, q);
  EXEC.` This closes the confused-deputy shape "run tenant text with admin rights":
  the deputy's authority is the reviewed intersection, not the operator.
- **The declarative profile** realizers demand is a `syntax_allowed` subset of
  policy-JS productions (straight-line `env`/`grant`/`bind` calls, literal or spliced
  arguments, free-name paths — no loops, no conditionals, no computed access). This
  reuses the p_symbols enumeration and the M3 front-end verbatim — checking a JS-AST
  shape is exactly as easy as checking a bespoke DSL would have been, and it is one
  grammar, parser, and profile-checker fewer to build, specify, and harden.
- **Diffs happen on the normal form, not the surface:** admission normalizes a
  declarative-profile quotation into canonical **descriptor tables** (v2's
  "declarative residue," reborn as the normal form). The descriptor diff is the review
  artifact; surface syntax never was.
- **Pure-data policies** need no quoting at all — a JSON descriptor is code bound to ∅
  (Principle 9).

What legitimately remains a "little language": **value-level DSLs** interpreted by
library functions under capabilities — selector strings (`pom.query("callsites(fetch)
within module('vendor/**')")`) and `pattern{…}`. Those are data arguments, squarely in
the reduction-principle category (`JSON.parse ≡ eval under json_policy`), same as SQL
text handed to a governed facet.

**Failure mapping:** parse/splice errors → producer's stage-0 denial; contract
rejection → `E_ADMIT_*` at the realizer; unresolved free name at realization →
`E_CAP_UNRESOLVED` charged against the realizer's environment — all through the one
denial schema.

---

## 5. What the formalization itself discovered

1. **Possession is a metatheorem** — `grant` is unguarded; unforgeability + name
   resolution *are* the axiom. Elevates M-SES from hygiene to the enforcement mechanism.
2. **Quotations must be cap-free by construction** — else a closure smuggles inside a
   "description"; the one-bit closure/quotation distinction becomes machine-checkable.
3. **`bind` only meets ⇒ widening is administrative-only** — a new epoch behind an
   admin handle; operationally a fan-out-required mutation. Formal and ops stories
   coincide.
4. **POM reads return quotations** — reflect and quote share a single cap-free
   invariant.
5. *(v4)* **The ∅-environment case unifies data and code** — no separate "data tree"
   semantics is needed; config snapshots are quotations, config fragments are
   near-∅-bound nodes, and meet on data instances composes rights, not values. One
   theorem covers the whole gradient.
6. *(v4.2, user-spotted)* **There is no second policy language.** Quotations quote
   policy-JS itself; "declarative" is a `syntax_allowed` profile; descriptor tables
   are the admission-time normal form; `realize` = admit → bind → EXEC. The bespoke
   policy-unit grammar of an earlier draft is withdrawn — the reduction principle
   applies to our own designs too.
7. *(v5.0, design review)* Four corrections from the adversarial pass: cap-freeness
   must be **stone**, not a structural check (TOCTOU via getters/mutation — R2);
   confluence holds for **restrictive mediations only**, adaptive = one per node (R1);
   realization is **least-authority** (manifest ∩ realizer grants, mandatory contract
   — R6); executing an unbound node is an **error**, and bound nodes are never unbound
   (R7). Failure modes compose by strictness (R12).

Plus the two boundary statements that keep the claim honest: unforgeability (U1–U3) and
compiler faithfulness (F) are *assumptions here and milestones elsewhere* — M-SES and
M8 respectively.

---

## 6. The numeric model *(v5.0 — V1, user decision)*

**JavaScript semantics is normative in both tiers: numbers are IEEE-754 doubles.**
BigInt remains what it is in JS — explicit and outside the typed profile. The typed
integer is therefore **`int` — a safe-integer refinement of double** (an integral value
with |x| ≤ 2⁵³−1), *not* a machine i64 with its own arithmetic:

- **Tier 1 is the specification**: whatever the interpreter's double arithmetic does —
  including precision loss past 2⁵³ — *is* the behavior.
- **Tier 2 must match it exactly**: compiled C may use a native `int64_t`
  representation **only where range analysis proves the safe range is never exceeded**
  (e.g. bounded counters, header lengths); everywhere else it computes in IEEE doubles
  — still native C arithmetic, still fast.
- Verified by **boundary-value conformance tests** (2⁵³ ± 1, negative zero, NaN
  propagation through `num`) in the T1-vs-T2 differential suite (VERIFICATION.md, V1).

Without this rule, erasure soundness ("types never change semantics") would fail
precisely where differential testing looks: a counter crossing 2⁵³ would saturate in
T1 doubles and keep counting in a naive T2 int64.

*(v5.0 refinements, second look at V1:)*

- **`i32`/`u32` via the asm.js idiom.** `(x|0)` *is* int32 and `x>>>0` *is* uint32 in
  every JS engine — the "annotation" is executable JS, so erasure soundness holds **by
  construction**, and the compiler unboxes to native int32/uint32 with no range
  analysis (a decade of asm.js precedent; WASM's ancestry). The fast lane for ports,
  status codes, lengths, bounded counters — an idiom inside the model, not a new type
  system.
- **`int` is a static refinement, not a runtime-guarded invariant** — with one
  exception: at R5 slot boundaries, the write guard for an `int`-typed slot is
  precisely `Number.isSafeInteger`.
- **Double-fallback is reported:** where range analysis fails and generated C computes
  in IEEE doubles, the admission report says which operations did (perf transparency —
  the report is the optimization to-do list, scenarios 48–49).
- The schema-side companion rule (numeric domains must fit the safe range — ms not ns
  timestamps, strings/opaque for true 64-bit ids) lives with M2 (ROADMAP).

**Implementation note *(V4 — monotonicity as an assertion)*:** the No-Amplification
theorem holds *given* an unforgeable TCB. Since environments are finite and
capabilities are registry-typed, `A*(child) ⊆ A*(parent)` is mechanically checkable —
the kernel **asserts the lattice inclusion at every grant/bind at admission time**, so
a TCB bug that would violate monotonicity fails loudly instead of silently.
