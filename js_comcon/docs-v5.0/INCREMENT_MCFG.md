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

## The core mechanism: recursive policed inclusion

The primitive is a **policed, syntactic, per-includer inclusion** (FOUNDATION §2a's "policed
include"). The including script **includes a fragment's text into itself** — at a syntactic
position it chooses (a function, an inner block, a handler slot) — **under a policy it
defines.** Realized as `admit` (gate: typecheck vs the includer's schema + contract tests,
determinism caps denied) + `bind` (attach the includer-defined policy env over the included
subtree — names resolve only through it, no ambient globals) + `grant`/`mediate` (populate/meter
that env). **`bind` is the inclusion operator; `admit` is its gate.**

- **Injection is one kind of "where":** placing `frag.onRequest` at a COM slot
  (`servers[i].locations['/t'].handler = …`, a filter, a phase hook) is the request-path case
  of the general "choose a syntactic position and bind a policy over it." Replaces
  `js_tenant_handler` — but the model is broader than handler placement.
- **Recursion = the same primitive, every level:** an included fragment holds only *attenuated*
  operators over its own subtree, so it includes *its* fragments under *its* policies, and an
  inner policy can only **narrow** what its includer granted (No-Amplification; widening needs
  an admin handle it doesn't hold). `js_source → root script → fragment → sub-fragment → …`,
  one mechanism throughout.
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
   design doc. ✅ **DONE** — `OPERATOR_API.md` (grant/mediate/bind/admit + include/includeAt/
   policy/realize, staging §6, the §8 decisions all resolved 2026-09-02: comcon-module surface,
   root cap set, meter=timeoutMs-now, closure default, init-time-only v1, M2 schema w/ C3 interim).
2. **Implement `admit()`** as the umbrella over the existing `init_conf` sequence, callable from
   host JS. 🔨 **IN PROGRESS — first slice landed (2026-09-02):** `comcon.admit(fn, contract)` on
   the host (HOST_ROOT) global — the C3 **gate** (`ngx_js_com.c`, `ngx_js_comcon_admit`): reuses
   `js_comcon_uses_dynamic_code` + `js_comcon_collect_free_globals` (free names ⊆ `contract.imports`,
   eval/Function/globalThis/… denied) + optional `js_comcon_check_request_fields`; returns
   `{certified, reject?}`. `t/comcon_admit.t` (comcon 23/195). **Interim/next slices:**
   `contract.imports` is the *full* manifest (intrinsics must be listed — auto-intrinsics come with
   env-based checking); then `bind` (compartment creation + the M-SES lockdown) and C5 lowering, so
   `admit`+`bind` reproduce the directive path's full admission.
3. **Add `grant`/`mediate`/`bind`**. 🔨 **IN PROGRESS — capability layer + metered `bind` landed
   (2026-09-02):** `comcon.{env,grant,mediate,meter,bind}` — `env()` a fresh deny-by-default
   environment, `grant(env,name,cap)`, `meter({timeoutMs})`/`mediate(cap,interceptor)` build the
   interceptor structure (JS bootstrap in `ngx_js_com_init`). **`bind(env,fn,{meter})` enforces the
   `meter`** — a C helper (`ngx_js_comcon_run_metered`) tightens the worker's gas deadline around
   the call (reuses the shipped interrupt handler + `request_deadline_ms`) and converts a
   meter-abort into a clean catchable error at the bind boundary. `t/comcon_operators.t` (comcon
   24/203). This slice gives **resource** confinement (the meter).
   **SCOPE isolation (authority) — attempted 2026-09-02, mechanism PROVEN, reverted for a clean
   re-do.** `comcon.include(source, contract)` compiled a fragment in a shared confined compartment
   (`ngx_js_tenant_context_new` + `ngx_js_tenant_lockdown`) with grants injected as **closure
   params** (not per-fragment contexts). *Functionally correct* — verified a confined fragment
   cannot reach host authority (`typeof nginx` → `undefined`), can use its granted caps + intrinsics,
   and separate fragments are isolated. But it hit **two engine-lifetime crashes** that must be
   solved first, both cross-realm (host ctx ↔ compartment ctx): (1) **metering a confined fragment**
   — interrupting a cross-realm `JS_Call` aborts, even when calling with the compartment's own
   context; (2) **teardown** — freeing the runtime while the compartment holds cross-realm grant
   caps (host objects closed over by compartment fragments) aborts (no printed assertion). Reverted
   (tree stays crash-free at the metered-bind state). **Fix path:** mirror the *tenant* compartment
   lifecycle exactly (it works): create the include compartment at init like `tenant_ctx`, install
   grants and call fragments entirely *within* the compartment context (avoid cross-realm value flow
   into the host), and tear it down on the tenant/`comcon_ctx` teardown path — i.e. reuse the proven
   `ngx_js_eval_tenant_sources` model rather than compiling-from-host. Also: `mediate` membrane
   *enforcement* (via the reach gates) remains.
   **2nd attempt (tenant-model, 2026-09-02) — again functionally PROVEN, again reverted for a
   lifetime issue.** `comcon.include()` now held the fragment C-side (a `jcf->comcon_frags`
   `JSValue[]`, off the compartment global) and invoked it *in* the compartment with **JSON
   round-trip marshaling** in C (no object crosses the realm) — verified correct
   (`{sees:"undefined", got:42}` from `f({a:21})`) and no cross-realm interrupt/abort. But teardown
   still aborts `JS_FreeRuntime: list_empty(&rt->gc_obj_list)` (a leak), and it reproduces with
   **include-only, no invoke** — so the leak is in *compartment-create + frag-hold*, not marshaling
   or the frag↔global cycle. Freeing each frag before `JS_FreeContext(comcon_ctx)` did not clear it.
   **Key insight for the next attempt:** the crash-free tenant compartment lives on its **own
   runtime** (`tenant_rt`), whereas this shared it with the host runtime (`jcf->rt`) — two contexts
   on one runtime is the likely culprit; give the include compartment its **own JSRuntime** (mirror
   `tenant_rt` exactly: create at init, `JS_FreeRuntime` at teardown), or gdb the leaked object.
   This needs a dedicated, gdb-assisted pass — it did not converge via incremental probing.
   **✅ 3rd attempt — LANDED (2026-09-02).** `comcon.include(source, contract)` now works and
   teardown is clean. Two fixes over attempt 2: (a) the compartment gets its **own runtime**
   (`jcf->comcon_rt` — mirror `tenant_rt`: `JS_NewRuntime` + `ngx_js_com_register_classes` +
   context + lockdown; `JS_FreeRuntime` at teardown), so its objects are freed by *its* runtime
   teardown rather than leaking on the host runtime; (b) `jcf` is cached in a module-static at
   `init_conf` (gdb showed the init-time crash was `ngx_get_conf(ngx_cycle->conf_ctx, …)` — during
   `init_conf` `ngx_cycle` is not yet the current cycle). Fragments held C-side (`comcon_frags`),
   invoked in the compartment, arg/result JSON-marshaled (strings cross, no object). **Verified:**
   authority isolation (`typeof nginx → "undefined"`), data-in/out (`x.a*2 → 42`), metered confined
   abort (`~100 ms`), **clean teardown** (`t/comcon_include.t`; comcon 25/209). Interrupt handler
   wired on `comcon_rt` in `init_process` (gas).
   **✅ LIVE-CAP GRANTS — LANDED (2026-09-02).** `comcon.include(source, {grants:{name: cap}})`
   now hands a fragment a **live host capability** (a `NginxSocket`), mirroring the tenant grant
   mechanism exactly. `__includeConfined(source, names[], caps[])` wraps the fragment source in a
   closure `(function(<names>){ "use strict"; return (<source>); })`, re-wraps each granted socket
   **compartment-native** via `ngx_js_socket_wrap(comcon_ctx, handle)` (a fresh wrapper around the
   same C handle — `ngx_js_socket_handle()` extracts it, as `grantToTenant` does), and applies the
   closure so the fragment closes over the cap by name. No object crosses the realm — only the C
   handle. Three fixes made it correct: (a) `ngx_js_com_install_protos(comcon_ctx)` after lockdown
   (mirrors the tenant — without the socket/listener protos the wrapper had no getters, so
   `.address`/`.listener` read `undefined`); (b) `JS_SetContextOpaque(comcon_ctx, ngx_cycle)`;
   (c) the invoke's `JS_Call` runs inside `ngx_js_compartment_enter(NGX_JS_COMPARTMENT_TENANT)` /
   `…leave()`, so the **A1 reach gate** confines the fragment (the compartment flag is a per-worker
   static, not per-context). **Verified** (`t/comcon_include_grant.t`; comcon 26/215): host authority
   unreachable (`typeof nginx → "undefined"`), the fragment **holds** the granted socket
   (`typeof granted → "object"`), an ungated scalar read works (`granted.address` →
   `"127.0.0.1:…"`), and the reach edge is gated (`granted.listener === null` cross-compartment,
   while the host sees the listener). Clean teardown unchanged (own-runtime `JS_FreeRuntime`).
   **M-SES-1b landed with it (the flagged prerequisite):** a live grant makes the (unfrozen)
   cap prototype reachable via `Object.getPrototypeOf(granted)`, so
   `ngx_js_comcon_harden_cap_protos(comcon_ctx)` runs after `install_protos` and makes the socket
   family protos **non-extensible** — a fragment cannot plant a persistent property on a shared
   cap proto (`t/comcon_include_grant.t` asserts `"planted":false`). Locking the existing getters
   non-configurable (shadowing) is deferred to an engine-level getter-hardening pass — both the
   in-compartment `Object.freeze` and a C `JS_DefineProperty` redefine destabilize the compartment
   (parser corruption / reach-gate breakage). Detail in `INCREMENT_MSES.md` § M-SES-1b.
   **Still follow-on:** grants of *other* cap kinds (COM nodes) — the socket is the proven
   first cap.
   **✅ `mediate` MEMBRANE ENFORCEMENT — LANDED (2026-09-02).** `mediate(cap, interceptor)` is
   now enforced for a granted socket as an **attenuation-only field membrane** (`A(cap′) ⊆
   A(cap)`), realized in C — no cross-realm object. The socket wrapper carries a per-wrapper
   field **mask** (`ngx_js_socket_opaque_t.mask`, bit index == getter magic:
   0 address/1 port/2 fd/3 listener); a redacted field's getter returns `undefined`
   (`ngx_js_socket_wrap_masked`; the default `ngx_js_socket_wrap` sets `MASK_ALL`, so every
   existing socket is unchanged). Interceptor library in the bootstrap: `comcon.revoke()`
   (narrow to zero — the grant is withheld, name `undefined` in the fragment), `comcon.redact
   ([fields])` (hide the listed fields), `comcon.allow([fields])` (expose *only* the listed
   fields). `include`'s grant loop unwraps a mediated grant (`v[FACET]`) into `(cap, mask)` and
   passes a parallel `masks[]` to `__includeConfined(source, names, caps, masks)`, which wraps
   each granted socket with its mask. **Verified** (`t/comcon_mediate.t`; comcon 27/223):
   `allow(['port'])` → the fragment reads `port` but `s.address` is `undefined`; `redact
   (['address'])` → `address` hidden, `port` still readable; `revoke()` → `typeof s ===
   "undefined"`. Also fixed a **latent bug in the live-cap-grant wrapper**: the
   `(function(<names>){…})` buffer was not NUL-terminated, which `JS_Eval` requires — short
   fragments survived, longer ones hit a garbage byte (parse error).
   **✅ COM-NODE `mediate` — LANDED (2026-09-02, read slice).** `mediate(nginx.http.servers[i],
   routes(glob))` grants an attenuated COM cap. **Why not the socket pattern:** a COM server is
   a *stateful* node — `ngx_js_server_opaque_s` carries per-wrapper dynamic-location state
   (`prefix_locs`/`dyn_pool`/`tree_pool`) and `addLocation` repoints the *single live*
   `cscf->static_locations` from its own `prefix_locs`. Re-wrapping a server into the confined
   compartment's separate runtime gives a second op that (a) clobbers the live tree
   (last-writer-wins over the host op) and (b) UAFs at compartment teardown (its `dyn_pool`
   frees `clcf` structs still in the live tree). The host avoids this by keeping exactly ONE
   canonical op per server (`nginx.http.servers` is a stored array, not a rebuilding getter).
   **The fix — `NginxComFacet`:** a thin, stateless C cap that *borrows* the canonical `srv_op`
   (never re-wraps it) and routes reads through a route-glob membrane. `ngx_js_server_srv_op
   (val)` extracts the canonical opaque (as `void*`); `ngx_js_com_facet_wrap(ctx, srv_op, glob)`
   builds the facet in the compartment; `comcon.routes(glob)` is the interceptor. `include`'s
   grant loop now passes a per-grant **policy descriptor** (`{kind:0,mask}` socket /
   `{kind:1,glob}` route) to `__includeConfined(source, names, caps, pols)`, dispatching:
   socket → masked wrap, server → facet. Facet API (read slice): `facet.paths()` (glob-filtered
   location paths), `facet.allowed(path)`, `facet.route`. **Verified** (`t/comcon_com_facet.t`;
   comcon 28/230): a fragment granted `routes('/acme/*')` sees only `/acme/*` paths (not
   `/other`), and `allowed()` gates. The facet class is registered in the compartment runtime
   (`ngx_js_com_facet_register_class` in `ngx_js_com_register_classes`) and its proto joins the
   M-SES-1b harden set. **Still follow-on:** the facet **mutation** slice (gated
   `addLocation`/`removeLocation` routed to the canonical op under the glob), and
   `rateLimit`/`transform`/`audit` flavors.
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
