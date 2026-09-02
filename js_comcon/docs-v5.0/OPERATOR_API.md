# COMCON — operator API design (the host-JS kernel surface)

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

## 6. Pilgrim binding — each op wraps existing machinery (mostly rewiring)

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

## 7. Open decisions (resolve before implementing)

1. **Surface:** top-level `grant/mediate/bind/admit/include` (as in MANUAL root.js) vs on a host
   object (`nginx.admit …`). MANUAL uses top-level; pilgrim namespaces under `nginx.*`. — *Lean:
   a small imported module (`import {…} from "comcon"`) so the names are lexical, not ambient.*
2. **What the root holds to grant *from*:** the COM API (`js_com.http` = `nginx.http`) is the
   root's held capability set; `host.log` etc. Enumerate the root's initial caps.
3. **`meter` units:** `meter({timeoutMs})` (wall-clock, now) vs `gas` units (S5-b, later).
4. **Closure vs quotation default** for `include`: caps-carrying policy vs propose-only.
5. **Handle exposure:** does the root get live POM handles (`h.root`, F-rights) or **init-time
   `include` only** (L-rights)? — *Init-first; F/X are follow-ons.*
6. **Contract schema source:** the M2 dual-role typed nginx-API schema (types of granted caps
   *and* the config surface type system).

## 8. Worked example — the whole fundament, end to end

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
