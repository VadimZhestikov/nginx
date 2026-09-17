# COMCON — operator API (the host-JS kernel surface)

> **IMPLEMENTATION STATUS (v5.35).** This is no longer design-only — the kernel operators are
> BUILT and shipped as the `comcon` object on the host js_source context, and they are now the
> ONLY confined-fragment mechanism (the `js_tenant_*` directives are removed; see
> INCREMENT_CONVERGE.md).
> - **Shipped:** `env`, `grant`, `mediate` (flavors `revoke`/`redact`/`allow` field-masks on
>   sockets + `routes(glob)` on COM-node facets), `admit` (free-name ⊆ imports + dynamic-code
>   refusal + request-field check + optional identity pin + **the test-phase**: `contract.tests`
>   runs against the compiled fragment IN the confined compartment — zero blast radius, host
>   authority + IO denied — refusing admission if any test throws; the remaining refinement is
>   swapping clock/RNG for fixed doubles during the run), `include` (`parse∘admit∘bind`, both
>   interpreted + AOT tiers, with
>   `contract = {imports, identity, checkRequest, tests, grants, deps, meter}`), `meter`, `comcon.mode`
>   (the process policy mode). Handlers are bound via the existing `location.handler`, not a
>   directive.
> - **`bind` resolved (v5.37):** `bind(env, source, opts)` is now the **env-first spelling of
>   `include`** — it compiles the source in the confined compartment under the env
>   (`grants`/`imports`/`meter`/`tests`/`identity` sourced from `grant(env(), …)` + `opts`), the
>   real kernel bind (you cannot re-bind an already-compiled host closure to a restricted env —
>   confinement requires compile-in-env). `bind(grant(env(),"x",cap), src, {meter})` ≡
>   `include(src, {grants:{x:cap}, meter})`. A standalone `bind` over a *parsed POM node* (as
>   opposed to source) still awaits POM nodes (increment D).
> - **`realize` + `quote` resolved (v5.38):** `comcon.quote(source)` is an inert, frozen,
>   cap-free **description** (zero authority — the quotation half of closure-vs-quotation);
>   `comcon.realize(q, contract, realizerEnv)` gives it force under the REALIZER's authority —
>   the operator-realizes-a-tenant-proposal path (showcases 46–47), distinct from `bind`/`include`
>   (which use the *producer's* env). Least-authority realization (R6) is enforced: the contract
>   is **mandatory**, and the realization environment is the realizer's grants **restricted to the
>   quotation's declared free-name manifest** (`contract.imports`) — `ρ_R ↾ manifest` — so a
>   proposal reviewed as "needs a,b,c" cannot touch anything else the operator's session holds
>   (confused-deputy fix); the existing `admit` gate then enforces free-names ⊆ imports, charging
>   refusal at the realizer. `realize` **refuses a closure** as arg0 (a bound `include()` result),
>   making the closure/quotation bit real. `t/comcon_realize.t`. **Stone splices shipped (v5.42,
>   D3):** `quote(source, splices)` takes producer data deep-checked cap-free (stone); `realize`
>   binds each as a JSON literal (escaped data, never text — injection-immune) and a POM node's
>   `quote()` is realizable (`t/comcon_pom_splice.t`). The **structured POM-node splice** (into a
>   *parsed subtree*, preserving sub-node handles / intensional splice sites) still awaits stmt/expr
>   nodes (D5).
> - **`includeAt` — folded, not a standalone deliverable** ([[feedback-reuse-jscom-primitive]]).
>   §3a below is retained as the design reference, but there is nothing left to build *called*
>   `includeAt`: (i) over a *concrete* COM node, `includeAt(loc, src, K)` is exactly
>   `loc.handler = realize/include(src, K)` — already works, so a standalone operator would
>   duplicate a js_com primitive; (ii) its only non-redundant forms — **anchor-splice** (§3a) and
>   **query/selector** targeting ("attach to every location matching a glob") — are just uses of
>   the POM tree, and fall out of `query()`/anchor-splice + `realize`/`include` once **POM nodes
>   (increment D)** exist. So it is subsumed by increment D, not scheduled on its own.
> - **Still design (not built):** POM nodes (increment D) — parsed-subtree quotations, structured
>   splices, `query()`/anchor targeting, live rewrite/epochs. `policy({...})` as a reified value,
>   and the `rateLimit`/`transform`/`audit` mediate flavors, remain design. `meter`'s `gas` unit is
>   forward-declared (only `timeoutMs` maps to the shipped deadline — which now also
>   has a **default**, applied when a contract carries no meter at all; see §3).
>
> The rest of this document is retained as the design reference for the shapes.

Step 1 of the M-CFG kernel-operator track (`INCREMENT_MCFG.md`): the concrete host-JS shapes
for FOUNDATION §4's `grant`/`mediate`/`bind`/`admit` + the derived `include`/`policy`/`realize`,
so the root script (loaded by the single `js_source`) admits and governs fragments recursively —
retiring `js_tenant_*`. **Consolidated, not invented:** formal semantics from `SEMANTICS.md`,
handles/rights from `POM.md`, the JS shapes from `MANUAL.md`. No new nginx directives
([[pilgrim-shell-fundament-principle]]).

## 0. The one formula

`include = parse ∘ admit ∘ bind` (SEMANTICS §, "derived forms"). This *is* "the root script
syntactically includes a fragment under a policy it defines"; applied at every level it is the
whole recursive-inclusion model. Everything below serves this.

## 1. Values

- **Capability** `cap(r, π, i?)` — (resource `r`, permission set `π ⊆ Perm`, optional
  interceptor stack `i`). Unforgeable. Authority `A(cap) = π ∪ A*(closure(i))`.
- **Environment** `env()` → a fresh environment with **zero authority**. The container a policy
  grants *into*.
- **POM handle** `h` — a handle-scoped view of a program/config subtree (`h.root`); ops gated by
  rights **R/L/F/X** (§5).
- **Policy** — a reified `policy({...})` **closure** (carries caps) or a **quotation** (cap-free
  description the operator realizes — the propose-don't-hold variant).
- **Fragment handle** — what `include`/`admit` returns: the certified subtree + **attenuated**
  operators for recursion.

## 2. The four primitives

### `env() → environment`
Fresh, zero-authority.

### `grant(env, name, cap) → env`
Place a **held** capability under `name`. **Possession is a metatheorem** — the cap was
necessarily evaluated from the caller's environment, so `A(cap) ⊆ A*(caller)` automatically; the
"guard" is the type system + unforgeability (`HARDENING.md`), not a runtime check.
```js
grant(acme, "http", mediate(js_com.http, routes("/acme/*")));
grant(acme, "log",  mediate(host.log,    rateLimit(10)));
```

### `mediate(cap, interceptor) → cap′`
A membrane over every operation of `cap`; `A(cap′) ⊆ A(cap) ∪ A*(closure(interceptor))`. A pure
interceptor (holds no caps) ⟹ **attenuation** (`A(cap′) ⊆ A(cap)`); an interceptor closing over
other caps ⟹ a **facet** (a safe transaction over raw authority never leaked). Flavors (all
derived): `deny / attenuate / transform / meter / revoke / redact`. Interceptor library (the
concrete filters the root composes): `routes(glob)`, `rateLimit(n)`, **`meter({timeoutMs, gas})`**,
`redact` (interfaces visible, bodies hidden), `revoke` (narrow to zero), `audit` (log + allow).

### `bind(env, node|handle, opts?) → ok`
Attach the **frozen** env over a POM subtree; inside, names resolve **only** through it (no
ambient globals / free imports / eval). **Meet** semantics: `B[n ↦ freeze(env) ⊓ B(n)]` — a new
binding intersects (narrows) any existing. Restrictive bindings meet freely; **at most one
adaptive-profile binding per node** (R1 → `E_BIND_ADAPTIVE_CONFLICT`); failure modes compose by
strictness `reject > deny > attenuate > audit` (R12). **Self-rebind can only narrow**; *widening*
is `rebind(env, opts)` — administrative, a **new epoch**, admin-class handle only.

### `admit(node, contract) → certified(node) | reject`
The gate: typecheck `node` against the contract's declared env signature + syntactic predicates,
then run the contract's **tests inside the sandbox with determinism caps (clock/RNG/IO) denied** —
reproducible, **zero blast radius**. `contract = { schema, tests, identity?, imports? }`
(`identity` = hash pin; `imports`/manifest = the names the fragment may resolve).

## 3. `include` — the derived form the fundament needs

```js
// root.js — loaded by the single `js_source root.js;`
const acme = env();
grant(acme, "http", mediate(js_com.http, routes("/acme/*")));
grant(acme, "log",  mediate(host.log,    rateLimit(10)));

include("tenants/acme/main.js", std.profiles.tenant(acme),
        { profile: "restrictive", onViolation: "audit" });   // observe-first rollout
```
`include(source, policy, opts) = parse(source) ∘ admit(·, policy.contract) ∘ bind(policy.env, ·)`.

> **Every fragment runs under a wall-clock deadline — including one with no
> `meter` (SHIPPED 2026-09-11).** `meter({timeoutMs})` sets it; when the contract
> carries no meter, `NGX_JS_COMCON_FRAGMENT_TIMEOUT_MS` (**5 s**) applies. It is
> **not** opt-in, and there is no way to ask for "unbounded": a fragment is the
> one place untrusted code runs, and before this an accidental infinite loop
> hung the worker with no escape involved (measured: 4474 ms to completion,
> nothing stopping it).
>
> An explicit `timeoutMs` overrides the default **in either direction** — it may
> ask for longer as well as shorter. Independently, a fragment can only
> **tighten** an enclosing request deadline, never extend it, so
> `min(enclosing, fragment)` always wins.
>
> Enforced in `ngx_js_comcon_invoke_confined`, not in the JS wrapper that
> computes the value, so calling `__invokeConfined` directly cannot skip it.
> Asserted by `t/comcon_fragment_deadline.t`, which must live in its own file:
> the per-request deadline is computed at *request entry* from
> `nginx.workerRequestTimeout`, so any test that sets that property already has
> a deadline in force and cannot observe the fragment's own default.
>
> `meter` maps three words to shipped mechanisms, each narrowing only against what
> is in force: `timeoutMs` (the deadline above), `memoryBytes` (the per-INVOCATION
> allowance, 16 MB default — a burst), and, since v5.122, `retainedBytes` (what the
> fragment may hold ACROSS calls, 8 MB default). Every invocation charges its fragment
> with what it left behind — the compartment's malloc delta around the call, corrected
> for cycles at O(1) per call — and a fragment past its cap is REFUSED at its next
> invocation with `E_MEM_RETAINED`, not run, until its epoch is replaced (the slot is
> freed, the memory returns) or its contract raises the cap; a sub-fragment held across a
> parent's calls charges its own slot under the parent's cap and is refused inside the
> parent. `comcon.memStatus(fragment)` → `{retained, invocations, refused, cap}` reads the
> count on the host (`t/comcon_retained_memory.t`, ASSURANCE G7.22). `meter`'s `gas` unit
> remains forward-declared.
The returned fragment handle carries **attenuated** operators, so the fragment can `include(...)`
its **own** sub-fragments under its **own** policy — and by No-Amplification an inner policy can
only **narrow** what its includer granted. `js_source → root → fragment → sub-fragment → …`, one
primitive throughout.

**Config-proposal sibling:** `realize(quotation) = admit(q, K) then bind(ρ_R ↾ q.manifest, q)` —
the operator realizes a tenant's cap-free config **proposal**, restricting the realizer's grants
to the quotation's manifest (the snapshot/rollback console; the M-CFG config-instance path).

## 3a. `includeAt` — anchored inclusion (textual splice) & the stage-0 link

> **Folded (v5.38):** `includeAt` is **not** a standalone deliverable — see the implementation-
> status banner at the top. A concrete-node `includeAt` is just `loc.handler=include(...)`; the
> anchor-splice form below and the query-targeting form are uses of the POM tree, subsumed by
> **increment D**. This section is retained as the design reference for those POM-targeting shapes.

`includeAt(anchor, source, policy)` is `include` **targeted at a named anchor site** — the
fragment-**insertion** form (vs plain `include`'s standalone callable): the fragment's text fills
a named hole in the host program. Used for mid-program / inner-loop splicing (`SHOWCASE51` §51c).

**Signature.** `includeAt(anchorName, source, policy) → fragment` — splices the fragment at the
site marked by the inert directive `"use comcon: <anchorName>";`. `policy = { env, expose, meter?,
onViolation?, contract }`; `expose = { in: [names], out: [names] }` is the *only* enclosing
bindings the spliced block may read / write.

**Mechanism** (`include` + anchor-targeting):
```
includeAt = resolve(anchor) ∘ parse(source) ∘ admit(·, contract) ∘ bind(env ⊕ expose, ·) ∘ splice-at(site)
```
Steps 2–4 are `include`; 1 and 5 make it *at a site*. For a spliced block, `admit`'s syntactic
predicates additionally require **control-flow hygiene** — no non-local `break`/`continue`/
`return`/labeled jump out of the block (SHOWCASE51 §51c).

**When — resolved by a stage-0 *link*, not by execution order.** The anchor lives in **dormant
stage-1 code** (a handler body defined but not yet called at stage-0); it is discovered by
**parsing** (static), never by running. Three stage-0 passes, like a linker:
1. **Discover** — parse all units → collect every anchor (hole) + every `includeAt` registration.
   Order-independent (the whole compilation unit).
2. **Register** — stage-0 top-level runs; `grant`/`mediate` compute the env, and each `includeAt`
   runs as a stage-0 statement that *registers a fill* for its anchor (its policy value computed
   here).
3. **Fill + lower** — at **end of stage-0**, the linker matches each fill to its anchor, does
   `admit ∘ bind ∘ splice`, validates, and AOT-lowers the composed program; **then** fork.
The fill lands on the not-yet-executed handler POM, so the anchor is filled **before the handler
ever runs** (stage-1). **L-rights.** (Live re-fill on a running server = an **F-rights** epoch
switch — deferred; removal = **X-rights**.)

**Order-independence (the key property).** Because anchors are discovered by parse and filled at
end-of-stage-0, `"use comcon: enrich";` and its `includeAt("enrich", …)` need **no** textual or
execution ordering — either order, same file, or (the common case) a separate policy unit. The
only requirement: `includeAt` runs *during* stage-0 (any top-level statement does). This is why
the design must be a **link, not an imperative define-then-use** — a policy unit in another file
is unorderable against the anchor by construction.

**Error cases (all stage-0):**
- anchor with **no** matching `includeAt` → dangling-hole error (design option: default to no-op);
- `includeAt` targeting a **non-existent** anchor → error;
- **more than one** fill for one insertion anchor → error (a hole takes exactly one fragment; an
  *attachment* anchor over existing code is different — there binds compose by meet, §2 `bind`);
- fragment references a free name ∉ (granted caps ∪ `expose`) → `admit` error;
- non-local control transfer out of the spliced block → `admit` rejection.

**Status.** A proposed *derived* form (introduced in `SHOWCASE51`). The anchor mechanism (inert
`"use comcon:"` markers + external policy units — FOUNDATION §, SPEC:61 "policy attachment site")
and `include = parse ∘ admit ∘ bind` (SEMANTICS) are established; the three-phase stage-0 link is
the design resolution of the anchor↔fill ordering, to be formalized in `SEMANTICS.md` by the
M-CFG track.

## 4. `policy()` — reified policy value

```js
// acme.policy.js — a stage-0 program under the policy-for-policies
export default function (ctx) {              // ctx = what the HOST granted THIS policy
  const e = env();                           // empty env (zero authority)
  grant(e, "http", mediate(ctx.js_com.http,  // grants: only what ctx holds (attenuation)
                           routes("/acme/*")));
  return policy({ env: e, profile: "restrictive",
                  onViolation: "deny", contract: { tests: "./acme.suite.js" } });
}
```
A `policy` closure carries its caps; the **quotation** variant carries none (propose-don't-hold).

## 5. Rights R/L/F/X — *where/when* each op is legal (POM.md)

- **R — read** (`text/quote/query/describe`) — always safe; source visibility = presence of a
  read-capable handle (`mediate`'s `redact` flavor: interfaces visible, bodies hidden).
- **L — local/init** — stage-0 (master, pre-fork) or edits to not-yet-admitted/bound fragments
  (config parse-time). No coordination. **← init-time `include`/`grant`/`bind` live here — this
  track's core.**
- **F — fan-out-required** — live rewrite / bind / epoch switch on admitted, running nodes
  (multi-worker fan-out). **← truly dynamic post-fork admission; harder, deferred.**
- **X — irreversible/guarded** — `remove()` without tombstone, **revoking an admission**.

## 6. Staging & policy-as-program (stage-0, no new syntax)

A policy **is a program**, and it runs at **compile time** — SEMANTICS §4: *"Policy units are
themselves **stage-0 programs** bound under deny-by-default environments."* But this needs **no
new JavaScript syntax** (no `comcon program { … }` construct); the design chose **anchors, not
syntax — JS stays pure** (FOUNDATION: *"anchors (inert markers) + external policy units; policy
text must not be trapped in strings"*). Staging is a phase property, not a keyword.

**Two stages:**
- **Stage-0 = admission / config-load (`init_conf`, pre-fork).** The root script and the policy
  units run here: `env`/`grant`/`mediate`/`bind`/`admit`/`include` execute to *build* the
  confinement structure, and each included fragment is admitted (C3/C4) and AOT-lowered
  (C5/maxim). "Executed during compilation" = executed at stage-0.
- **Stage-1 = request runtime.** The admitted, lowered fragment handlers run per-request inside
  their frozen policy environments.
- **Principle 8:** *soundness is stage-independent; staging is purely a performance property —
  moving a check earlier never changes what is allowed.* (The erasure-soundness principle, at
  the policy level.)

**What marks a program as stage-0 policy — three anchors, all pure JS:**
1. **It is an external *policy unit*** (a module, e.g. `*.policy.js`, or under the `comcon:`
   namespace), consumed by `include`/`realize` — not inline, not a new grammar.
2. **The operators are *granted into its deny-by-default env*, not ambient** (`// stage 0; host
   granted {env, grant, mediate, bind, admit, …}`). That granting — not a keyword — is what
   makes it a policy program; **every free identifier resolves in the bound env or is a stage-0
   error** (compile-time enforcement, SEMANTICS §4).
3. **In-source site marker = an inert *anchor* string directive** — `"use comcon: <site>";` —
   which reads like a keyword but is pure JS (exactly like `"use strict"`). Use this where a
   fragment's own source should visibly name the governed site.

**`include = parse ∘ admit ∘ bind` is the compile-time boundary** — a function call, not syntax.
Calling it at stage-0 parses the fragment, admits it (typecheck + tests, determinism caps
denied), and binds the policy env, emitting the lowered structure.

**Compile-through into native, proven faithful.** The stage-0 policy doesn't just gate at
compile time — its mediations **lower into the tenant's compiled C**: SEMANTICS shows a
prefix-check mediation becoming a `strncmp` guard in the C stub ("the membrane costs ~nothing").
The **M8 / SR-2 faithfulness gate** (done) is exactly the guarantee that the lowered C simulates
every mediation the stage-0 policy erased. And `quotation` mode (`A(q)=∅`) is **"data is code
bound to ∅"** (Principle 9) — the propose-don't-hold policy that the operator `realize`s.

## 7. Pilgrim binding — each op wraps existing machinery (mostly rewiring)

| API | wraps (existing C) |
|---|---|
| `env()` | a fresh deny-by-default capability map (the tenant global) |
| `grant` | tenant grant + pinned-dep install (`jcf->tenant_grants` / `tenant_deps`) |
| `mediate(meter)` | the gas deadline + `ngx_js_interrupt_handler` (`NGX_JS_TENANT_TIMEOUT_MS`) |
| `mediate(routes/rateLimit/redact/revoke/audit)` | the A1 reach gates + audit mode (`tenant_mode`) |
| `bind` | attach the frozen env over the tenant compartment (`ngx_js_tenant_lockdown` ctx) |
| `admit` | `ngx_js_tenant_lockdown` + C3 (`uses_dynamic_code`/`collect_free_globals`/`check_request_fields`) + C4 (artifact identity) + C5 (`js_comcon_aot_compile`) |
| `include` | `parse(source) ∘ admit ∘ bind` — replaces the `js_tenant_source` eval path |
| handler placement | host places `frag.onRequest` via COM (replaces `js_tenant_handler`) |

**Faithfulness:** `admit()` must run the *identical* C3/C4/lockdown/C5, and `mediate(meter)` the
*identical* gas, so the SR-2 faithfulness gate still holds. The thin-sugar migration
(`INCREMENT_MCFG.md` step 4) proves directive-path ≡ operator-path.

## 8. Decisions (resolved 2026-09-02)

1. **Surface → an imported `comcon` module; the operators are *granted capabilities*, not
   ambient.** `import { env, grant, mediate, bind, admit, include, includeAt, policy } from
   "comcon";`. The imported names resolve to capabilities **granted into the program's
   deny-by-default environment** — *not* ambient globals (that would be ambient authority) and
   *not* on `nginx.*` (that is the COM config API, a different capability). Because the operators
   are themselves capabilities, a fragment holds only the ones its includer granted — e.g.
   **withhold `admit`/`include` to forbid a fragment from admitting sub-fragments.** Pure JS (no
   syntax); consistent with the tower (the kernel is grantable/attenuable like any authority).

2. **Root's initial capability set (what it grants *from*).** The root script (`js_source`,
   HOST_ROOT) holds: **`nginx`** — the COM config object (servers/locations/upstreams/…, the
   dynamic-config authority, = `js_com`); the **`comcon` kernel operators**; **`nginx.log`**;
   **`nginx.shared`**; **`pom`** — the program-tree handle (anchors/queries/`admit` targets); and
   the host **determinism/IO caps** `clock`/`rng`/`net` (held by the root, **denied inside `admit`
   tests**, and reaching a fragment only via explicit mediation). Everything a fragment receives
   is a subset (attenuation) of this.

3. **`meter` units → `timeoutMs` now, `gas` later.** `meter({ timeoutMs })` maps directly onto the
   shipped deadline mechanism (the interrupt handler + JIT back-edge gas). The deterministic
   instruction-count form `meter({ gas })` (S5-b) is an additive field later; the `meter` shape is
   forward-compatible.

4. **`include`/`includeAt` default → closure; quotation via `realize`.** An includer defining a
   policy over a fragment **holds** the caps and grants them into the fragment's env — the closure
   case (the default). The **propose-don't-hold** case (a party proposes a fragment/config it
   cannot apply; the operator supplies the caps) is the **quotation** path, handled explicitly by
   `realize(quotation)` (the M-CFG config-proposal / snapshot-rollback flow). Both exist; `include`
   defaults to closure.

5. **Scope → init-time `include`/`includeAt` only (L-rights) for v1.** Stage-0, pre-fork,
   COW-inherited — this reuses today's model and *is* the M-CFG core. **Live** re-fill / rewrite /
   epoch switch on running workers (F-rights) and revoke / remove (X-rights) are follow-ons (they
   need per-worker fan-out coordination).

6. **Contract schema → the M2 dual-role typed nginx-API schema.** `admit`'s typecheck is against
   the **M2 typed nginx-API schema**, which does double duty: the types of the **granted caps**
   *and* the **config surface** (M-CFG). **Dependency:** full type-checking waits on M2; **interim
   (pre-M2)**, `admit` uses the existing C3 structural checks (free-name deny-list / no-dynamic-
   code / sealed-Request-fields) as the schema, upgrading to M2 types when available.

## 8b. `uses(key, limit, window)` — a budgeted capability *(v5.67)*

The first mediation flavor that attenuates **how many times** rather than **what**.

```js
var limited = comcon.mediate(sock, comcon.uses('acme-sock', 100, 60));
// 100 uses per 60 seconds, fleet-wide, shared by anything naming 'acme-sock'
```

- **A use is any gated operation** on the capability — a read of a mediated field as much
  as a method call. Charging only calls would make `s.address` free and let a tenant spend
  the interesting part of a capability without touching its budget. A **redacted** read is
  not charged: a field the membrane hides was never an exercise of the capability.
- **The counter is fleet-wide**, in `nginx.shared`. A per-worker budget would give the
  operator who wrote `100` four hundred on a four-worker box — the number they did not
  write. Measured: `+0.037 µs` per charged use (0.060 vs 0.023 unbudgeted, 300k reads).
- **The window is FIXED, not sliding.** The counter is created on first use with a TTL; at
  a boundary a caller can spend `limit` at the end of one window and `limit` at the start
  of the next. A sliding window costs per-use timestamps in shared memory; the honest move
  is to say which one this is.
- **The key names the counter**, so two capabilities share a budget exactly when you say
  so. Deriving a key would make sharing unsayable and tie the counter's identity to
  wrapping order.
- **Nothing is defaulted.** A missing key, limit or window is refused: a budget with no
  limit is a mistake, not "unlimited", and the one direction a mediation may never take is
  toward more authority.
- **Re-mediating with a different budget is refused** — 10/min and 100/hour are not
  ordered, so a meet would have to guess, and the guess would widen one of them. Same rule
  as a different route glob. An identical budget composes; a field mask composes freely.
- **Exhaustion is a denial, not a refusal:** code `budget.uses`, counted in
  `nginx.tenantDenials()`, and in **audit mode it is logged and ALLOWED** — so a limit can
  be watched before it is switched on, like every other gate.

---

## 8f. `window(spec)` — a RECURRING lifetime (office hours) *(v5.87)*

```js
var signing = comcon.mediate(key, comcon.window({
    days: 'Mon-Fri', from: '09:00', to: '17:00'    // UTC
}));
```

`ttl` says "for the next N seconds"; `window` says "on these days, between these hours".
THREATS.md wants exactly this for the signing key, where the useful attenuation is a schedule
rather than a countdown. Denial code `cap.window` — **its own code, not `cap.expired`**, because
"your capability has run out" and "your capability is outside its hours" are different
operational facts and an operator paged at 02:00 needs to know which they are looking at.

- **TIMES ARE UTC, and the operator converts.** "Office hours" is a local-time idea, but a gate
  whose behaviour depends on the host's TZ cannot be tested identically on two machines and
  shifts under a daylight-saving transition with nothing edited. Converting once, where you can
  see what you are doing, is the lesser evil.
- **`from` > `to` wraps midnight.** `22:00`–`02:00` is a real shift pattern, and a naive
  `from <= now < to` makes it permanently closed.
- **`from` === `to` means the WHOLE of an allowed day**, not "never". An operator writing
  `00:00`–`00:00` means all day; a capability that is never open is spelled by granting nothing.
- **Day ranges may wrap the week** — `Fri-Mon` is a weekend-plus schedule, and refusing it would
  make that unspellable.
- **Nothing is defaulted.** No days, no hours, an unknown day name, a time that is not `HH:MM`,
  an hour past 23 — all refused with `E_CAP_FLAVOR`. A window with no days is a mistake, not
  "always open".
- **It composes with a mask, a budget, a lifetime and `allowHosts`** — "only these hosts, only in
  hours, at most N an hour, for the next day." Two *different* windows are **refused**: two
  schedules do not intersect in one schedule (Mon-Wed 08:00–12:00 ∩ Tue-Thu 10:00–14:00 is not
  expressible as a single days/from/to), so a meet would guess, and guessing widens.
- Audit mode logs and allows, like every gate.

---

## 8i. `onViolation` and `profile` — the posture is a property of the BINDING *(v5.91)*

```js
// phase 1: observe.  This binding is shadowed; every OTHER binding still enforces.
var shadow  = comcon.include(src, { imports: [], grants: g,
                                    profile: 'restrictive',
                                    onViolation: 'audit' });
// phase 2: the same policy, now biting
var enforce = comcon.include(src, { imports: [], grants: g,
                                    profile: 'restrictive',
                                    onViolation: 'deny' });
```

**`onViolation` is per-BINDING, and that is the point.** `comcon.mode()` switches the whole
worker, which is the wrong granularity for an observe-first rollout: shadowing one tenant's new
policy by putting the fleet in audit **also stops enforcing every other tenant's**, which is a
strictly worse posture than the one you are carefully trying to reach.

- Values: `'audit'` (log and allow), `'deny'` (or `'enforce'`), `'learn'` (audit + harvest the
  withheld host surface). Omit it and the binding **inherits** the fleet posture rather than
  silently picking one. Anything else is refused — a posture word nobody enforces is worse than its
  absence, because it would be believed.
- **It wins in both directions**: a `deny` binding enforces while the fleet is in audit, and an
  `audit` binding is shadowed while the fleet enforces. Which way is stricter is your call.
- **It can WEAKEN**, and that is safe only because the contract is written on the TRUSTED side. The
  fragment's *source* is untrusted; the contract around it is your own configuration — the same
  argument `cosign`'s `as` rests on. Do not build a contract from tenant-supplied data.
- The override is applied in C for the duration of one invocation and **restored afterwards,
  including when the fragment throws** — otherwise one bad fragment would quietly unshield every
  later request in that worker.

**`profile` is read by being refused where it cannot be honoured.** `'restrictive'` (the default)
is what every mediation here already is: the vocabulary attenuates and never transforms.
`'declarative'` is the existing review profile. `'adaptive'` is **refused** with
`E_ADMIT_CONTRACT` rather than ignored — a transforming profile has no implementation, and
accepting the word would make *"this program runs standalone without COMCON"* unfalsifiable for
exactly the fragments where that claim matters. An unknown profile is refused too.

**`std.postures.*` is still absent**, and the reason has changed. It is no longer that nothing
enforces — ten words do. It is that *what `lockdown` should narrow to is a decision nobody has
made*: MANUAL says "writes: deny, exports: freeze", which needs a per-member mutating/reading split
across a whole environment rather than one capability. Assembling it here would be inventing policy.

---

## 8k. `std.evaluate`, `std.policy.diff`, `std.docs`, `comcon.denials` — the library-kind gaps closed *(v5.127)*

```js
var report = comcon.std.evaluate(sdkFn /* or its source */, { declares: ['fetch'] });
report.verdict          // "requests 5 authorities (Date, buildUrl, createSocket, fetch, nginx); declared 1 of 5"
report.names            // [{name, kind: 'intrinsic'|'authority'|'denied', calls, lines, references}, …]
report.contract         // {imports: [...]}: the manifest a contract would need, ready to paste

var d = comcon.std.policy.diff(current, candidate);     // contracts, include results or bindAt handles
d.verdict               // 'narrowing' | 'widening' | 'unchanged' | 'incomparable'
d.autoSafe              // true iff nothing gained authority
d.changes               // [{path: 'grants.s.fields', from, to, direction}, …]

comcon.std.docs.render('acme', fragmentOrContract)      // the tenant's manual, Markdown
comcon.std.docs.model('acme', fragmentOrContract)       // the same, as the query it is
comcon.denials(fragment)                                // {total, byOp, posture}: this binding's own rows
ops.wouldDeny(fragment); ops.diff(name, candidate); ops.docs(name)
```

Three library programs over shipped operators, closing `SHOWCASE-gaps.md` G-13, G-05 and
G-16; one small mechanism under the second.

- **`evaluate` is one static read of a module's whole appetite.** `admit()` stops at the
  first undeclared free name because one is enough to refuse; an evaluation wants all of
  them. The names come from the **same C collector admission uses** (`comcon.__freeNames`),
  classified against the same intrinsics allowance, with call sites and lines from the
  bytecode (D5a) and the dynamic-code flag. A **source string** is accepted only after the
  vendored parser says it is exactly one function expression; text after it is refused, so
  `function(){}; evil()` never runs. Nothing is executed. `t/comcon_std_evaluate.t`.
- **`policy.diff` reads the kernel's own lattice.** The grant translation `include()` feeds
  the C side (`polOf`) is the function the diff reads, so a narrower contract cannot read as
  wider in a report. Masks compare by inclusion, lifetimes and deadlines by size, budgets by
  limit under an identical key and window, quorums up and windows down; globs and protocols
  compare by identity only, and anything else is **`incomparable`, never guessed** — the
  `routes` rule, worn by a report. A mixed change (one axis narrows, another widens) is not
  auto-safe. An include result and a `bindAt` handle carry their contract (`.contract`, a
  frozen shallow copy) so a live binding can be diffed against a candidate.
  `t/comcon_std_policy_diff.t`.
- **`comcon.denials(f)` attributes the gates to the binding.** The invoke sets a pointer to
  the running fragment's own counters for the duration of the call (restored after, nested
  for a sub-fragment), and the compartment's one counting site increments both the fleet
  counter and the fragment's. Under `onViolation: 'audit'` the nonzero rows are the
  **would-deny list** the rehearsal scenario asked for; `ops.wouldDeny(f)` reads them against
  the posture and says whether it is observing or denying. Per worker, like `memStatus`.
  Negative control: `t/tools/controls/denials-not-attributed.patch`. `t/comcon_would_deny.t`.
- **`docs` is a projection of the contract.** Every line — the fields a mask leaves, a
  facet's ops within its glob, a budget, a lease, a window, a cosignature, a protocol, the
  bounds with defaults named as defaults, the admission fields, the pins — is read from the
  descriptors the kernel enforces. A redacted field is absent from the manual because it is
  absent from the capability. `ops.docs(name)` renders a registered binding with its live
  epoch. `t/comcon_std_docs.t`.
- **`std.describe()` told the truth again.** Its `absent` list had said for months that six
  mediation words needed C-side enforcement and that `std.ops` was not started, after all of
  them had shipped — the drift check [8] guards, one surface over. It now lists the canonical
  NOT BUILT set (`std.postures.*`, `opaque.*`) and nothing else, and `onViolation` /
  `profile` appear in `enforced` with what enforces them.

---

## 8n. A live epoch compiled in the master — the tier follows the text, without a reload *(v5.131)*

```js
var h = comcon.bindShared('score', comcon.quote(V1), { imports: [] }, onReq);
h.replace(comcon.quote(V2));         // admitted at request time, on every worker: interpreted at first
comcon.aotStatus(callable)            // {jit, functions, compiled, pending: true, via: null, tries: 1, unavailable: false}
…  a second or two later, on any worker …
comcon.aotStatus(callable)            // {compiled: 3, functions: 3, pending: false, via: 'master'}
```

Closes `SHOWCASE-gaps.md` G-21 (scenario 41). Nothing new to configure: the operator's
verbs are the ones live ops already had; the tier now follows.

- **Why it needed the master.** The compiled tier's gcc thread is a pthread, and a pthread
  does not survive `fork()`; so a request-time include — every `bindShared` epoch, every
  `bindAt.replace()` — stayed interpreted, and `aotStatus()` said so. Workers must not compile
  anyway: a request would block on gcc, N workers would compile the same text N times, and
  the master's minimal environment has no PATH for gcc.
- **The protocol, in one paragraph.** The worker keeps the wrapper text it was admitted from
  and sends it to the master over the channel it already has (`NGX_CMD_JS_COMCON_AOT`, at
  most one message of 64 KB). The master spawns **one detached helper** — a fork of itself
  that has the compartment and the cache directory — and otherwise does nothing: no thread,
  no blocking, its signal loop untouched. The helper compiles the text **compile-only**
  (nothing runs there, not the IIFE a source may be, not a statement), enqueues every
  function under the wrapper on the gcc thread it starts for itself, drains it, writes an
  **index** beside the cache atomically, and exits. On every invocation, at most once a
  second, the worker looks for the index and installs the artifacts it names; unanswered
  for five seconds it asks again, up to six times, then the epoch stays interpreted and
  `aotStatus()` says `unavailable`. The interpreted epoch serves meanwhile.
- **Why an index.** The two processes never share a bytecode hash: atom operands are
  per-runtime, and the cache is keyed by them. The index maps each function's **source key**
  (its own text and shape, identical wherever that text is compiled) to the artifact hash the
  helper produced; the worker installs by explicit hash, with every check the cache-hit path
  makes — the codegen version symbol, no process-bound direct calls, the atom table rebound
  to its own runtime.
- **Portable codegen.** The helper compiles in a mode that emits no symbol-named direct
  JIT-to-JIT calls (the inline-cache call, resolved where the code runs, is taken instead),
  and that mode is folded into the hash, so a portable artifact and an in-process one never
  share a cache entry.
- **Two things found on the way, fixed.** A worker that created its runtime *after* the fork
  (the compartment at its first request-time include, a SharedWorker's runtime) used to start
  a gcc thread of its own: it compiled synchronously inside a request, N times across
  workers, and under the master's environment every job failed and wrote a skip marker that
  poisoned the cache. Workers now forbid the thread at process init (`js_jit_forbid`); the
  helper is the one process that compiles a live epoch. And the helper resets `SIGCHLD` to
  its default, so nginx's handler cannot reap the gcc children the compile thread waits for.
- **Bounds.** One helper at a time (a busy master defers to the worker's next request);
  one message size; one index read per second per pending fragment; six requests. The
  master never executes tenant code; a worker never runs an artifact it cannot verify.

Negative control: `t/tools/controls/aot-master-inert.patch` (the master ignores the
request; nothing is ever native by the master). `t/comcon_aot_master.t` (17, compiled tier;
skipped on the interpreter build), demo `L_Live_Ops/L4`.

---

## 8m. `comcon.std.suite` — the allow-suite: a cage derived from observed behaviour, and a candidate admitted against it *(v5.130)*

```js
var S = comcon.std.suite;
S.record(f, { max: 1000 });      // from now on f keeps (input, output) as the JSON the boundary carries
…                                // traffic
S.cases(f)                       // {recorded, distinct, dropped, max, cases: [{input, output, n}], unstable: [{input, outputs, n}]}
S.tests(f)                       // a contract `tests` quotation: one function expression replaying every case
S.check(candidate, S.cases(f))   // the same replay on the host: {total, passed, failed: [{i, input, expected, got}], ok}
S.coverage(f)                    // {functions: {total, called, percent, uncalled: [{name, line, calls}]}, gates, tier, exact}

h.record(); h.suite(); h.coverage(); h.guard()      // bindAt / bindShared: guard pins the suite into the contract
ops.record('acme'); ops.suite('acme'); ops.coverage('acme'); ops.guard('acme')
ops.rebind('acme', candidate)    // refused with E_ADMIT_TEST if it answers a recorded case differently
```

Closes the last half of `SHOWCASE-gaps.md` G-05 (scenario 5: "allow-suite generated: 1,214
recorded cases; coverage 91%"). Library programs over shipped operators, with one small
mechanism under `coverage`.

- **A case is one distinct input and the answer it got.** The recorder sits in the include
  result's own callable and keeps the JSON text the boundary marshals anyway, so recording
  costs one property test per call when off. An input answered two ways is **unstable**,
  kept apart with both answers and never pinned; past `max` distinct inputs the recorder
  counts what it dropped rather than grow. A thrown answer is recorded as `threw` without
  its text, because an exception's message crosses the boundary prefixed and a case must
  compare the same on both sides.
- **`tests` is the suite as a contract field.** The quotation is a single function
  expression — what the admission test phase accepts — that parses each input, calls the
  candidate inside the compartment, and throws on the first divergence naming the case, its
  input, the expected and the actual answer. Nothing else runs; the host is unreachable
  there as in every test phase. `check` is the same replay on the host for a rehearsal.
- **`guard` pins it to a binding.** `bindAt.guard(tests?)` (default: the recorded suite) sets
  `tests` on the binding's own copy of the contract, so every later `replace()` is admitted
  against it; the policy diff reads a removed pin as a widening. `bindShared.guard` carries
  the suite on the shared record — in fixed chunks under sibling keys, since a shared value
  is at most 511 bytes — and every worker admits its next epoch under it. A shared
  `replace()` now realizes the candidate **before** publishing it (the rule bindAt already
  followed), so a refused candidate never reaches the record where every worker's next
  reconcile would trip over it.
- **Coverage is function-level and says what it can stand behind.** The engine counts
  entries per function in every build (`comcon_call_count`, one increment at the
  interpreter's call entry); `coverage` reports the delta over the recording window: the
  functions entered, the ones never entered by name and line, the percentage, and the gates
  that fired. On the compiled tier a lowered function's direct calls into other lowered
  functions bypass that entry, so the report carries `tier` and `exact: false` rather than a
  number it cannot defend. Per worker, like every counter here.

Negative control: `t/tools/controls/suite-guard-inert.patch` (guard reports success and pins
nothing; the divergent rebinds are admitted). `t/comcon_std_suite.t` (27), demo
`O_Operators/O4`.

---

## 8l. `comcon.withdraw(f, name?)` — live revocation of a grant, cascading over delegations *(v5.129)*

```js
comcon.withdraw(f, 'lib')       // {revoked: ['lib'], delegated: 1}: this grant, and every copy re-granted from it
comcon.withdraw(f)              // every grant the fragment holds (offboarding)
comcon.withdrawn(f)             // ['lib']: read back from the kernel's table, not a JS shadow

h.withdraw('lib'); h.withdrawn()            // a bindAt / bindShared handle: sticks to the binding
ops.withdraw('acme', 'lib', { confirm: 'acme' })   // class X, like remove: the confirmation names the binding
ops.withdrawn('acme'); ops.docs('acme')     // the manual marks the grant REVOKED
```

The grant is a switch the host holds **at run time**, closing `SHOWCASE-gaps.md` G-01 (the
CVE-day story, scenario 2; offboarding, scenario 20).

- **The mechanism is one pointer per wrapper.** Every granted wrapper — socket, outbound,
  COM facet, author — holds a refcounted **grant record**; the authoring tier's
  copy-then-narrow gives the copy a record of its own whose *parent* is the original's. The
  gate asks "is any record on the chain revoked?" right after `cap.owner`, so a revocation of
  the original reaches every delegation without a search, and a delegation chain outlives
  any one holder. The fragment's stats slot keeps its records under the contract's names,
  which is how the host reaches a grant by `(fragment, name)` after the wrappers have
  vanished into the closure. `delegated` counts the live copies made directly from the
  revoked records — the delegations the cascade reached.
- **`cap.revoked` is the second unconditional code.** Like `cap.owner`, it denies in audit
  and learn as in enforce (`mode=audit … unconditional=1` in the log): the operator who
  withdrew a grant is not observing a policy, they are exercising one, and a posture that
  let the capability through would hand back what the one person entitled to withdraw it had
  just withdrawn. Frozen in the golden corpus with a `revokeBefore` row shape — the only way
  the code can be reached, since nothing a fragment does can withdraw its own grant.
- **Why `withdraw` and not `revoke`.** `comcon.revoke()` is already the grant-time *flavour*
  (narrow to zero at admission). One name for two acts would let a mistaken call — a handle
  where a fragment was meant — return a descriptor where a revocation was intended. Both
  leave a *revoked* capability, which is what the code names.
- **A revocation sticks to a binding.** `bindAt.withdraw` applies it to the live epoch and to
  every epoch a `rollback()` could restore, and re-applies it to every epoch a `replace()`
  realizes: the same contract makes the same grants, and an edit must not lift a revocation.
  `bindShared` carries `revoked` and a `rev` counter on the shared record beside the epoch,
  so the other workers apply it on their next request without realizing a new epoch.
- **Irreversible by design.** There is no verb that un-withdraws: restoring authority is a
  widening, and every widening in this system is a new admission under a new contract — a
  new `bindAt`, a new `include`. Idempotent; a name the fragment was not granted is a
  `TypeError`, because revoking nothing must not read as revoking something.
- **Per worker for a raw fragment, fleet-wide for a shared binding** — the same scope rule as
  `comcon.mode` and `bindShared`. Withdrawing the `author` grant stops further authoring; the
  sub-fragments already authored keep their own records, under the grants they were copied
  from, and die with *those*.

Negative control: `t/tools/controls/revoke-not-checked.patch` (the chain walk blinded — the
flips still read back and every capability keeps working). `t/comcon_revoke.t` (31), the
`cap.revoked` row in `t/comcon_v12_denial_codes.t`, demo `S_Security_Teams/S5`.

---

## 8j. `author({subFragments, ttlSeconds?})` — a fragment that authors fragments *(v5.106–v5.107)*

```js
// the host: a reseller may HOLD up to 8 sub-fragments at once (a live count, v5.111)
var acme = comcon.include(src, {
    imports: [],
    grants: { sock:   comcon.mediate(sock, comcon.allow(['address', 'port'])),
              author: comcon.author({ subFragments: 8 }) } });

// inside the fragment: the same pipeline, from the other side of the membrane
var sub = author.include('function(req){ return { where: s.address }; }', {
    imports:   [],                                // mandatory: a sub-fragment is always admitted
    grants:    { s: sock },                       // the reseller's OWN wrapper, copied
    attenuate: { s: { redact: ['port'], ttlSeconds: 600 } },   // narrowed: mask AND, expiry min
    tests:     function (f) { if (typeof f({}).where !== 'string') throw new Error('no address'); },
    timeoutMs: 200, memoryBytes: 1048576 });     // min() against what is in force
var r = sub({ id: 41 });                          // JSON in, JSON out; sync; text-only exceptions
```

**It is not a mediation word and it wraps no host object.** `author()` is a descriptor that
`include()` grants; the far side is a `NginxComconAuthor` whose whole authority is "run the
admission pipeline `subFragments` times, as the fragment I was granted to". It is not
mediatable (a `mediate()`d author descriptor is refused as a grant) and not re-grantable.

**The contract is the host contract's subset that a less-trusted author may write:** `imports`
(required), `intrinsics`, `checkRequest`, `tests`, `grants`, `attenuate`, `timeoutMs`,
`memoryBytes`. `deps`, `identity`, `onViolation`, `profile` and `meter` are refused
(`E_ADMIT_CONTRACT`) — the posture is the parent's, and the bounds are plain numbers here.

**`grants` and `attenuate` — copy, then narrow.** A grant must be a wrapper the parent itself
holds; the child is a copy of it with the owner changed, and `attenuate` can move exactly two
things downward: the field mask (`allow: [...]` — a subset of the parent's, else
`E_CAP_ESCALATE` — or `redact: [...]`) and the expiry (`ttlSeconds`, min). Everything else
(budget, window, cosignature, glob) is inherited verbatim; any other word is `E_CAP_FLAVOR`; a
session-typed wrapper is not re-grantable (`E_CAP_ESCALATE`). A stale parent yields a stale
child — the handle is never re-wrapped, because that would mint a fresh one.

**What crosses is text.** The argument and result are JSON; an exception reaches the parent as
a new error carrying message and string `code`. A nested call is synchronous (a promise is
`E_INVOKE_PENDING`) and drains nothing: a sub-fragment's queued jobs are the parent's. A
sub-fragment past its deadline aborts the whole invocation; past its allowance it throws an
ordinary exception, as the host boundary does. `subFragments` is a LIVE count *(v5.111)*: a
callable is an object with a finalizer, and the parent dropping its last reference releases the
slot and refunds the count at once — so a parent that authors per request and drops the callable
spends nothing lasting, and one that caches eight holds eight. Holding the full count and asking
for another is `E_AUTHOR_LIMIT`, and so is nesting deeper than two.

**Reading it back:** `author.subFragments`, `author.used`; `nginx.describeType('NginxComconAuthor')`.

---

## 8h. `protocol(step…)` — enforced operation order *(v5.90)*

```js
var handle = comcon.mediate(sock, comcon.protocol('address', 'port*', 'fd'));
var once   = comcon.mediate(cap,  comcon.protocol('request'));   // one intent, ever
```

A session type over the capability's own operations. `allow`/`redact` say **which** operations
exist, `uses` **how often**, `ttl`/`window` **when**, `cosign` **by whom** — this says **in what
order**. Denial code `cap.protocol`. With it, the mediation vocabulary is complete: **ten of ten.**

- **A starred step may happen any number of times including zero; a bare step must happen exactly
  once, in place.** Once the last step is consumed the conversation is **over** and every further
  operation is denied — which is what makes `protocol('fd')` a **one-shot capability**. That is a
  different attenuation from `uses(1)`: a budget is fleet-wide and resets with its window, a
  protocol is per-wrapper and never resets.
- **IT ENFORCES ORDER, NOT COMPLETION.** "You cannot take the fd before you have looked at the
  address" is checkable at the moment of the call. "You must eventually close" is not — a fragment
  can simply return, and there is no event at which the host could notice. Stated because a
  session type that silently enforced half of what session types usually mean would be worse than
  one that says which half.
- **The cursor is PER WRAPPER**, deliberately not fleet-wide the way a cosign record is: a session
  type describes ONE conversation, and two holders sharing a cursor would interleave into nonsense.
  Each `include()` of a fragment starts a fresh conversation; repeated calls to the same bound
  fragment continue it.
- **A violation does not advance the cursor**, so a fragment that calls out of order and then
  proceeds correctly still works. Nor does an operation another gate refuses: the transition is
  **checked** before the cosignature and **committed** only after the budget. *A gate whose
  decision is also its effect can only ever be last; one whose effect can be deferred must be.*
- **The operations.** A socket has `address`, `port`, `fd`, `listener` (its gated field reads). An
  outbound capability has `request` — and only that, because `pending`/`clear` are the host's half
  and are reach-gated, so a fragment can never perform them. Listing them would let you write a
  protocol that can never advance.
- **Nothing is defaulted.** No steps (a dead capability is spelled `revoke()`), an operation no
  capability has, a **mixture** of two capability kinds, a **repeated** step (whose enforced order
  would depend on which reading the matcher takes), more than 8 steps, a name that is not one: all
  `E_CAP_FLAVOR`. A protocol naming the **wrong capability kind** is refused at `mediate()` in both
  directions — every step would be a violation, so the capability would be dead, and that is a
  policy error rather than a run-time denial.
- **Composition.** An identical protocol composes; a different order or a different starring is
  **refused** — two session types do not intersect in one session type. It composes freely with a
  mask, `uses`, `ttl`, `window`, `cosign` and `allowHosts`.
- Audit mode logs and allows, like every gate — and the cursor does not advance on a violation that
  was merely logged, because a transition that was not legal is not a transition.

---

## 8g. `cosign(spec)` — the two-person rule *(v5.88)*

```js
var rotate = comcon.mediate(key, comcon.cosign({
    key:    'rotate-signing-key',   // names the DECISION
    quorum: 2,                      // distinct principals required
    within: 900,                    // seconds, from the FIRST signature
    as:     resolved.principal      // who THIS capability acts for
}));
```

`ttl` and `window` bound *when* a capability may be used and `uses` bounds *how often*. `cosign`
bounds **who**, and it is the only mediation in the vocabulary that the holder cannot satisfy
alone. Denial code `cap.cosign`.

- **THE ATTEMPT IS THE CONSENT — there is no `approve()` verb.** Calling the gated operation
  records the caller's principal and, if the quorum is not yet met, denies. The second operator
  simply **retries the same operation**, which is what a two-person rule looks like in an
  operations room. So: **a `cap.cosign` denial is not "nothing happened"** — the denied attempt
  recorded a signature. This is the one denial in the set with a side effect, and the one that
  means "go find a colleague" rather than "no".
- **`as` is written on the TRUSTED side and cannot be set from inside a compartment.** COMCON does
  not authenticate (§8b): the host asserts the principal and this maps it, so `as` comes from your
  own configuration — typically `std.sessions.resolve()`'s resolved principal. A fragment
  therefore holds exactly one identity per invocation and can cast exactly one vote:
  **distinctness is structural.** Had `as` been something the fragment could write, the word
  would be theatre.
- **The quorum assembles ACROSS INVOCATIONS**, not within one: alice runs the policy and is denied
  pending a cosignature; bob runs the same policy and it executes. The record is **fleet-wide**
  (`nginx.shared`), because your two operators land on whichever workers accept their connections.
- **The record is the SET of principals, not a count of attempts.** One operator pressing the
  button twice is still one signature — a rule that counted attempts would be a one-person rule
  with extra steps.
- **`within` is FIXED and anchored at the FIRST signature**, the same shape and the same
  disclosure as a `uses` window: a quorum must assemble within `within` seconds of the first
  consent, not of the last. A missing `within` is refused — the dangerous reading of "no expiry"
  is "consent lasts forever", and an approval gathered last month is not consent to an operation
  attempted today.
- **`key` names the DECISION, not the capability.** Two capabilities given the same key cosign
  each other — the same deliberate act as two capabilities sharing a `uses` counter, and the same
  residual if done by accident.
- **There is no `of:[...]` allow-list, deliberately: holding the capability is the membership.**
  Only a principal you chose to hand a cosigned capability to can attempt at all, so a list inside
  the descriptor would re-state in a weaker place what the grant already decided.
- **Nothing is defaulted.** No key, no quorum, no `within`, a quorum of 1 (which is not a weak
  two-person rule but the absence of one — spell that by not cosigning at all), a quorum past 8:
  all `E_CAP_FLAVOR`. A missing or comma-bearing `as` is **`E_CAP_PRINCIPAL`**, its own refusal
  code, because nothing was misspelled and nothing composed — the policy is simply incoherent.
  A comma is refused rather than stripped: rewriting the name would merge two principals into one.
- **Composition.** `quorum` meets by **MAX** and `within` by **MIN** — both narrowing, in opposite
  directions, which is a third lattice shape beside the mask AND and the lifetime MIN. A different
  `key` or a different `as` is **refused**: merging keys would let consent given for one decision
  authorize another, and a capability with two acting principals would have to vote as somebody.
  It composes freely with `allowHosts`, `ttl`, `uses`, `window` and a field mask.
- **Gate order matters and is deliberate:** expiry and window first, then (for an outbound cap)
  the destination glob, then the cosignature, then the budget. Consent is never recorded for an
  operation another gate would refuse — otherwise signatures could be gathered against a
  destination the capability can never reach and spent on the one it can — and a denied operation
  never spends budget.
- Audit mode logs and allows, like every gate, which is unusually useful here: you can see which
  operations *would* need a second pair of hands before the rule bites.

---

## 8e. `allowHosts(glob)` and `nginx.outbound()` — reach outward, as a capability *(v5.85, round trip v5.86)*

```js
var cap = nginx.outbound();                              // the host mints it
var out = comcon.mediate(cap, comcon.allowHosts('https://*.example.com'));
var policy = comcon.include(src, { imports: [], grants: { out: out } });

policy({ phase: 'ask' });                                // the fragment asks
var results = await comcon.std.outbound.perform(cap, req);   // the HOST performs
policy({ phase: 'decide', results: results });           // the policy decides
```

**It is not a `fetch`, and the reason is structural.** A confined fragment is invoked
synchronously — `JS_Call`, then JSON-stringify the result — with no promise detection and no
pending-job drain. A capability that performed network I/O could not be handed to a fragment
without making invocation asynchronous, which touches the host-JS deadline (§8c) and the
per-invocation memory allowance. So the capability **records intent** and the host performs it,
which is what `comcon.std.config` already does for configuration: the tenant proposes what it
cannot apply.

- **The glob is checked IN THE COMPARTMENT**, where the capability is exercised. That is what
  makes this an attenuation of authority rather than a filter applied to data afterwards.
- **Host globs wildcard on the LEFT** (`*.example.com`), where route globs wildcard on the right.
  One matcher in C knows both shapes.
- **A glob may be scheme-qualified** — `https://*.example.com` — and the scheme is matched
  **exactly, never globbed**: `http*://` admits neither http nor https, because a wildcard scheme
  that accepted TLS and plaintext alike is the opposite of what writing a scheme asks for. A glob
  with no scheme matches any scheme, which is what shipped at v5.85.
  **This is NOT `protocol`.** MANUAL's `protocol("handshake", "frames*", "close")` is enforced
  operation ORDER — a session type over a capability's methods — and is still unbuilt. Restricting
  the destination's scheme belongs to the destination word.
- **A URL with credentials is refused outright**, not parsed around: `https://good@evil.net/x` is
  an invitation to smuggle a host past a glob.
- **Two gates, both counted denials.** `out.host` is the glob refusing a destination — and the
  refused intent never reaches the queue, so the host cannot perform what the glob denied.
  `out.drain` is the reach gate on `pending()`/`clear()`: those are the host's half, and a fragment
  able to drain the queue would read what a *sibling fragment sharing the same capability* had
  recorded. In audit mode both log and allow, like every gate.
- **`clear(n)` clears only the first n.** `std.outbound.perform()` reads the queue, awaits the I/O,
  then clears exactly what it performed — another request sharing the capability can append while
  it is awaiting, and a bare `clear()` would discard those intents unperformed.
- **Composes with `uses` and `ttl`** — "only these hosts, at most N an hour, for the next hour."
  Two *different* host globs are refused rather than guessed (the `routes` rule: globs are not
  ordered, so a meet would widen one).
- **Past 32 queued intents a request is dropped and COUNTED** (`pending().dropped`), never
  silently lost: a queue that overflowed quietly would let a fragment hide an intent behind
  thirty-one others.

---

## 8d. `ttl(seconds)` — a capability with a lifetime *(v5.74)*

```js
var leased = comcon.mediate(sock, comcon.ttl(3600));   // usable for an hour
```

**Why it exists:** `std.sessions` gives a grant a lease, but `include()` binds capabilities
as closure parameters at ADMISSION. Resolve a session once, bind a fragment with it, and
that fragment holds those capabilities for as long as it lives — the mapping expires, the
authority does not. `ttl` is what makes a lease bite on authority already handed out, so
`resolve()` can stamp the lease's remaining seconds onto what it returns.

- **The clock starts when the capability crosses into the compartment** (include time), not
  when `mediate()` built the descriptor. The descriptor carries a duration, so there is one
  clock — nginx's — rather than two that could disagree. A test that waits *before*
  including gets a fresh lifetime, which is exactly how the first version of this feature's
  own corpus row reported "alive" forever.
- **Lifetimes compose by taking the shorter**, in either order. This is the opposite outcome
  to `uses`, and for a precise reason: two budgets are not ordered (10/min vs 100/hour), so
  re-mediating with a different one is refused rather than guessed; two lifetimes ARE
  ordered, so `min` is a real meet. Same rule — never widen — different lattice.
- **Nothing is defaulted.** Zero, negative, fractional and absent lifetimes are refused: a
  missing lifetime is a mistake, not "forever".
- **Expiry is a denial, not a refusal:** code `cap.expired`, counted in
  `nginx.tenantDenials()`, and in **audit mode it is logged and ALLOWED** — so a lifetime can
  be watched before it is enforced, like every other gate.
- It composes freely with a field mask and with a `uses` budget; the expiry is checked
  *before* the budget is charged, because spending budget on an operation that cannot happen
  would make the audit read as though the tenant were still working.

---

## 8c. `nginx.workerRequestTimeout` — host JS is bounded by default *(v5.71)*

```js
nginx.workerRequestTimeout        // 10000 — the shipped default, readable
nginx.workerRequestTimeout = 0    // explicit opt-out: unbounded, deliberately
nginx.workerRequestTimeout = 500  // takes effect on the NEXT request
```

A runaway `location.handler` used to hang the worker until SIGKILL, taking every other
client on it down, and this knob defaulted to `0`. It now defaults to **10 s** — ten,
not the tenant fragment's one, because host JS is trusted and may legitimately spend real
*synchronous* time in a request (a COM tree walk, a large parse).

Three things to know:

1. **It is read once, before your handler runs**, so assigning it inside a handler affects
   the *next* request, not the current one. That is deliberate (the value is a worker-level
   setting, not a per-request argument) and is what makes it cheap.
2. **A malformed value reads as the default**, not as unbounded — a setting nobody can read
   must not silently remove the bound.
3. **It bounds one synchronous entry.** The deadline is cleared when a handler suspends, so
   a continuation resumed from an event callback runs without one: an `await` resets your
   protection. Recorded as ASSURANCE.md finding F12.

An abort is not catchable in the handler: the interrupt unwinds the whole call, so nginx
answers 500 (or the client sees a timeout, if it is less patient than the deadline) and the
error log carries `InternalError: interrupted`. That log line is how you find these.

---

## 8a. Sessions — turning an authenticated principal into an environment *(v5.65)*

The operator-facing half of FOUNDATION §8b (which owns the design and the reasoning).

```js
// Held by whoever may hand out sessions. No capability -> no verbs at all.
var S = comcon.std.sessions({ sessions: nginx.shared });

// A grant is a DESCRIPTOR: cap-free data, optionally leased.
S.grant('ci@acme', { imports: ['JSON', 'fetchAcme'], routes: '/acme/*', ttl: 3600 });

// Per USE, not per login. `env` is the environment YOU hold; the result is it,
// narrowed. You cannot resolve authority you do not have.
var s = S.resolve(principalTheHostAuthenticated, operatorEnv);
s.env        // an env: operatorEnv ∩ the descriptor, mediated per `routes`
s.granted    // the names it actually carries
s.reason     // why it is empty, when it is

S.revoke('ci@acme');   // the next resolve returns the empty env. No token to chase.
```

**Three rules an operator has to know:**

1. **You authenticate; COMCON maps.** `principalTheHostAuthenticated` must come from
   something you verified — an mTLS subject, a JWT you checked, a peer credential.
   Passing a client-supplied string here hands the client the session, and nothing in
   the platform can tell the difference.
2. **A mapping can only narrow, and it narrows YOUR env.** If a descriptor names
   something the env you passed does not grant, `resolve` **throws** rather than
   returning a smaller session — because a mapping that silently grants less than it
   says is one you cannot audit.
3. **Unknown, revoked and expired are all the same answer:** an empty environment. Not
   an error to catch, not a default role — nothing.

`describe()` reports what the session holds and states `authenticates: false` in the
surface itself, so the boundary shows up in a REPL rather than only in a document.

---

## 9. Worked example — the whole fundament, end to end

`nginx.conf`: **only** `js_source root.js;` — nothing else.
```js
// root.js  (holds the root handle; runs in init_conf, HOST_ROOT + COM)
import { env, grant, mediate, include } from "comcon";
import { routes, rateLimit, meter } from "comcon:interceptors";

const acme = env();
grant(acme, "http", mediate(nginx.http, routes("/acme/*")));
grant(acme, "log",  mediate(nginx.log,  rateLimit(10)));

const acmeFrag = include("tenants/acme/main.js",
  { env: acme, profile: "restrictive", onViolation: "enforce",
    contract: { schema: "acme.d.ts", tests: "acme.suite.js", identity: "<sha256>" },
    meter: { timeoutMs: 200 } });                 // the S5 timeout — a meter mediation

nginx.http.servers[0].locations["/acme"].handler = acmeFrag.onRequest;   // inject via COM

// …and acme/main.js may itself:
//   include("plugins/x.js", { env: subEnv, meter: { timeoutMs: 50 }, … })
//   where subEnv ⊆ acme (attenuation-only) — sub-fragment under acme's policy.
```
No `js_tenant_*` anywhere; the untouched operator `nginx.conf` gains only `js_source`.
