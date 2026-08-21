# COMCON — Kernel Semantics & the No-Amplification Theorem (v4)

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

**(QUOTE)** — the cap-free rule as a constructor side condition:

```
ρ ⊢ tᵢ ⇓ vᵢ      ∀i. A(vᵢ) = ∅   (deep)
──────────────────────────────────────────
ρ ⊢ quote(s, t̄) ⇓ node(s, v̄)      A = ∅
```

Splicing a capability — even one buried inside a record — is a stage-0 error.

**(BIND)** — meet-composition inside the rule:

```
ρ ⊢ tₕ ⇓ h    bind ∈ ops(h)    ρ ⊢ sel ⇓ n    n ∈ reach(h)    ρ ⊢ tₑ ⇓ ρₑ
────────────────────────────────────────────────────────────────────────────
ρ ⊢ bind(tₑ, sel) ⇓ ok,      B′ = B[n ↦ freeze(ρₑ) ⊓ B(n)]
```

`ρ₁ ⊓ ρ₂`: domain = `dom₁ ∩ dom₂`; every operation must pass **both** bindings'
mediations (membrane stacking) ⟹ authority = permit intersection. Intersection is
ACI ⟹ multi-policy composition is order-independent and confluent. *(v4 clarification,
for data-like instances: meet composes mutation **rights**, never values — there is no
intersection of two listen ports; the right-to-set intersects like any permit, while
conflicting values are an ordinary admission error, not a meet.)* Post-bind the
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
flavors; `include = parse ∘ admit ∘ bind`; `realize(q) = admit(q, K)`, then evaluate
q's environment clause **under the realizer's ρ_R** (a sequence of GRANT instances),
then `bind`.

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
// policyB now EMITS descriptions (unbound POM nodes, zero authority)
export default tenants.map(t => quote`
  env {
    limit   = ratelimit.makeLimiter({ keyPrefix: "rl:${t.id}:", rps: ${t.rps} })
    metrics = host.metrics.scoped("${t.id}")     // B does NOT hold host.metrics!
  }
  bind -> module('tenants/${t.id}.js')  profile restrictive  onViolation deny
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

Plus the two boundary statements that keep the claim honest: unforgeability (U1–U3) and
compiler faithfulness (F) are *assumptions here and milestones elsewhere* — M-SES and
M8 respectively.
