# COMCON — the kernel-operator realization (M-CFG track) — scope

Realize FOUNDATION §4's four kernel operators (`grant`/`mediate`/`bind`/`admit`) as the
**host-JS API**, so the root/host JS loaded by the *single* `js_source` directive admits and
governs fragments (program **and** config) dynamically — and **retire the entire `js_tenant_*`
directive family.** This is what makes pilgrim match its own fundament
([[pilgrim-shell-fundament-principle]]): a non-invasive shell whose only nginx.conf change is
`js_source`, doing dynamic reconfiguration through JS. FOUNDATION §2a/§4; ROADMAP work item
**M-CFG** (the config-instance facet).

## Why this is mostly rewiring, not rebuilding

The admission machinery already exists, driven by the directives in `init_conf`
(`ngx_js_module.c`): create the tenant compartment → `ngx_js_tenant_lockdown` (M-SES-0/1) →
`ngx_js_com_install_protos` → eval sources → C3 checks (`js_comcon_uses_dynamic_code` :1489 /
`collect_free_globals` :1504 / `check_request_fields` :1521) → C4 artifact identity+pin → C5
`js_comcon_aot_compile` :1581 → per-request gas. The host-JS `nginx.*` surface is already a
registration point (`ngx_js_com.c` ~:2797, `JS_SetPropertyStr(nginx_obj, "log"/"gc"/…)`). The
track **exposes that sequence as `admit()`** callable from host JS; it does not re-derive
confinement.

## The operator API (HOST_ROOT host-JS only; the host holds the root handle)

- **`admit(source, contract) → fragment | reject`** — the gate. Wraps the init-time sequence:
  fresh compartment + lockdown + proto-install/freeze, eval `source`, C3 checks *against the
  contract's schema*, C4 identity pin (`contract.identity`), C5 lowering. `contract =
  { schema, tests, identity(hash), mode, grants, meter }`. Tests run with determinism caps
  denied (§4) → reproducible, zero blast radius. Returns a **fragment handle** = the registered
  handler(s) + **attenuated** operators.
- **`grant(env, name, cap)`** — place a *held* capability into a fragment's env (possession is
  a metatheorem — no runtime guard).
- **`mediate(cap, interceptor) → cap′`** — membrane: deny / attenuate / transform / **meter** /
  revoke / redact.
- **`bind(env, node)`** — attach a frozen env to a POM subtree (names resolve only through it).

## Directive → operator retirement map

| Retire | Becomes |
|---|---|
| `js_tenant_source` | `admit(source, contract)` |
| `js_tenant_dependency` | pinned `grant(cap)` in the contract |
| `js_tenant_artifact` | `contract.identity` (hash) |
| `js_tenant_mode` | `mediate` policy (enforce=deny / audit) |
| **`js_tenant_timeout` (never built)** | `mediate(exec, meter({timeoutMs}))` |
| `js_tenant_handler` | host **places** `fragment.onRequest` via COM |

## Injection & recursion & lifecycle

- **Injection = host-JS + COM** (the dynamic-config role): the host places the fragment's
  function wherever policy allows — `servers[i].locations['/t'].handler = frag.onRequest`, a
  filter, a phase hook. Replaces `js_tenant_handler`.
- **Recursion = free:** the fragment handle carries *attenuated* operators → it admits/governs
  its own sub-fragments, **narrowing-only** (No-Amplification; widening needs an admin-class
  handle it doesn't hold). One mechanism, every level.
- **Lifecycle:** host JS runs in `init_conf` (HOST_ROOT + COM, before fork) → `admit()` creates
  compartments there → COW-inherited by workers. Preserves today's model. **Init-time admission
  first;** admitting a fragment *after* fork / at request time (truly dynamic) is a harder
  follow-on (per-worker compartment creation), out of this track's core.

## M-CFG proper (the config instance) — second phase

`admit()` generalizes from program fragments to **config** fragments: a tenant proposes config
as *sentences of a restricted config grammar* (`syntax_allowed` over config productions —
"may write `server{}`, `listen` only in your port range, no `proxy_pass` outside your
namespace"), typed against the dual-role M2 schema, quotation-based (**tenant proposes what it
cannot apply; the operator realizes** — formalizing the snapshot/rollback console). Builds on
the same `admit`; follows the program-fragment operators.

## Migration sequencing (each step keeps `comcon_*.t` green)

1. **Design** the operator API shapes (contract, interceptor, attenuation/meet semantics) — a
   design doc.
2. **Implement `admit()`** as the umbrella over the existing `init_conf` sequence, callable from
   host JS.
3. **Add `grant`/`mediate`/`bind`**.
4. **Reimplement `js_tenant_*` as thin *deprecated sugar*** that internally calls the operators
   — behavior identical, the whole existing suite stays green, migrate file-by-file.
5. **Migrate `comcon_*.t`** to the host-JS `admit` form.
6. **Remove the directives.**

## Verification

- **Equivalence:** a fragment via host-JS `admit()` ≡ the same via the old directives (same
  C3/C4/gas/faithfulness) — the ported `comcon_*.t`.
- **Attenuation:** a fragment cannot widen its own or a child's budget beyond its grant
  (No-Amplification test).
- **Recursion:** a fragment admits a sub-fragment; the sub is confined under the attenuated
  policy.
- **Fundament:** an nginx.conf with **only** `js_source` (+ host JS that admits a fragment and
  wires it via COM) serves end-to-end — nothing else in the conf.

## Effort, risks, recommendation

- **LARGE** — the biggest COMCON increment (the kernel realization A–C approximated). But
  mostly rewiring + API surface + test migration, not new confinement logic.
- **Risks:** (a) getting the operator *shapes* right (admit contract, mediate interceptor,
  attenuation meet); (b) init-time-only admission for now (runtime/dynamic admission deferred);
  (c) the thin-sugar step is what de-risks the confined-tier regression; (d) M-CFG config-grammar
  admission is its own sub-effort — sequence it **after** the program-fragment operators.
- **Recommendation:** start with step 1 (the operator API **design doc**), then `admit()` as the
  umbrella (step 2), then retire directives via thin sugar (steps 4–6). The config-instance
  M-CFG phase follows once `admit` is solid.
