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
> - **Partial / divergent:** `bind` is currently META-only (a metered call wrapper); the real
>   env-freeze/Meet confinement is performed by `include` (compile-in-restricted-compartment),
>   which subsumes `bind`'s role for the request-handler case.
> - **Still design (not built):** `includeAt` (§3a — anchors/expose/3-phase link), `realize` +
>   quotations, `policy({...})` as a reified value, and the `rateLimit`/`transform`/`audit`
>   mediate flavors. `meter`'s `gas` unit is forward-declared (only `timeoutMs` maps to the
>   shipped deadline).
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
The returned fragment handle carries **attenuated** operators, so the fragment can `include(...)`
its **own** sub-fragments under its **own** policy — and by No-Amplification an inner policy can
only **narrow** what its includer granted. `js_source → root → fragment → sub-fragment → …`, one
primitive throughout.

**Config-proposal sibling:** `realize(quotation) = admit(q, K) then bind(ρ_R ↾ q.manifest, q)` —
the operator realizes a tenant's cap-free config **proposal**, restricting the realizer's grants
to the quotation's manifest (the snapshot/rollback console; the M-CFG config-instance path).

## 3a. `includeAt` — anchored inclusion (textual splice) & the stage-0 link

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
