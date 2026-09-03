# INCREMENT — CONVERGE: one confined mechanism (`include`) — scoping

**Status:** SCOPE (2026-09-02). Design/plan only; no code yet. Follows the directive-retirement
work in `INCREMENT_MCFG.md` (steps 4–6) and the Option-1 landing (`js_tenant_handler` retired via
`location.handler`, `t/comcon_operator_handler.t`).

## 1. Goal

Collapse the **two parallel confined-fragment mechanisms** into **one primitive** — `include`
(host-JS, driven from the single `js_source` root) bound to a location through the existing
`location.handler`. Then delete the `js_tenant_*` directives. This is the shell fundament taken to
its conclusion: one recursive primitive, no directives, no parallel subsystem.

The end-state a root script writes:

```js
const h = comcon.include(src, { imports, grants, meter, deps, identity, mode });
loc.handler = req => { const o = h(shape(req)); req.respond(o.status, o.headers, o.body); };
```

## 2. The two mechanisms today

| | **A — tenant path (OLD)** | **B — include path (NEW, M-CFG)** |
|---|---|---|
| entry | `js_tenant_source` / `comcon.tenant()` | `comcon.include(src, contract)` |
| compartment | `tenant_rt` + `tenant_ctx` (shared) | `comcon_rt` + `comcon_ctx` (shared) |
| request | tenant `onRequest(fn)` + `js_tenant_handler` routes to it (C-side marshal + `{status,body}` contract + size cap, `ngx_js_http_module.c:8547–8749`) | fragment returns a value; `location.handler` wrapper marshals/responds (host-JS) |
| grants | `nginx.grantToTenant(name, sock)` — reach-gated | live-cap closure params: masked sockets + COM facets (**strictly better**) |
| admission | full C3/C4 gate in `ngx_js_eval_tenant_sources` (free-names, dyn-code, request-sealed, onRequest-sig) + artifact identity pin | **none** — `include` only compiles + wraps; the C3 gate lives in the *separate* `admit` operator (`ngx_js_com.c:2849`) |
| deps | `js_tenant_dependency` / `comcon.dependency()` — pinned pure libs bound on the tenant global | — |
| mode | `js_tenant_mode` / `comcon.mode()` — process-global (`policy_init`) | inherits the same global mode via the reach gate |
| compiled tier | **C5 lowers the tenant `onRequest` handler to native C** (`ngx_js_module.c:2252`) | interpreted only |

B already wins on grants and on the directive-free entry. A still owns admission, deps, the
compiled tier, and the request contract.

## 3. Parity gaps (tenant feature → `include` realization)

| # | Gap | Realization on `include` | Effort | Risk |
|---|---|---|---|---|
| **G1** | Admission (C3) | `include` composes `admit`: run `js_comcon_uses_dynamic_code` + `js_comcon_collect_free_globals` (free-names ⊆ `contract.imports`) + request-field checks at compile time; refuse on violation | M | low — reuse the `admit` C funcs verbatim |
| **G2** | Identity pin (`js_tenant_artifact`) | `contract.identity`: after admit, compute `H(H(source) ‖ schema)` and refuse on mismatch | S | low |
| **G3** | Deps (`js_tenant_dependency`) | `contract.deps=[{name,path,sha}]`: load each pinned pure lib in a bare cage, verify hash, bind by name as a **closure param** (same mechanism as grants) | M | med — pure-cage eval in `comcon_rt` |
| **G4** | Mode (`js_tenant_mode`) | keep `comcon.mode()` **global** (recommended) — the compartment is shared, per-fragment mode needs per-fragment policy state; revisit only if multi-tenant isolation demands it | XS | low |
| **G5** | Request/response contract | a host-JS helper `comcon.serve(h)` → a `location.handler` function that marshals the request to data, calls `h`, and enforces `{status, headers, body}` + a size cap (moves `js_tenant_handler`'s C contract into host-JS, once) | S | low |
| **G6** | **Compiled tier (C5)** | lower an `include` fragment to native C like the tenant `onRequest` handler — the fragment is already a pure `req→{status,body}` function, the ideal lowering shape | **L** | **high** — the C5 entry is wired to `eval_tenant_sources`/`tenant_request_handler`; retargeting to `comcon_ctx` fragments is the hard part |
| **G7** | Test migration | port the tenant-based `comcon_*.t` (source/handler/deps/artifact/admission/lowering/faithfulness) to `include + location.handler`; keep both green during the transition | L | med — many files; do file-by-file |

## 4. Sequencing (phases)

1. **P1 — admission into `include` (G1+G2).** `include` composes `admit` + optional `identity`.
   New `t/`: an `include` fragment with a bad free-name / dynamic code / wrong identity is refused.
   *After P1, `include` fragments have tenant-grade admission.*
2. **P2 — the serve helper (G5).** `comcon.serve(h)` — the ergonomic `location.handler` wrapper
   with the `{status,headers,body}` + size-cap contract. Re-express `t/comcon_operator_handler.t`
   through it. *After P2, request serving is one line and contract-checked.*
3. **P3 — deps (G3).** `contract.deps` pinned pure libs as closure params. Port
   `comcon_dependency` semantics onto `include`; keep `comcon.dependency()` as sugar.
4. **P4 — migrate `comcon_*.t` (G7).** File-by-file, tenant form → `include + serve`. The old
   directives/tenant path still work throughout (nothing removed yet).
5. **P5 — compiled tier (G6).** Retarget C5 lowering to `include` fragments. **Gated on SR-2
   faithfulness** (compiled ≡ interpreted) exactly as the tenant path is. This is the crux; it may
   warrant its own increment. Until P5 lands, the tenant path stays for compiled-tier tenants.
6. **P6 — remove the directives.** Once every `comcon_*.t` runs on `include` and the compiled tier
   is retargeted, delete `js_tenant_source/mode/dependency/artifact/handler` + the now-dead
   `tenant_ctx`/`onRequest`/`tenant_request_handler` machinery. This is the payoff: one mechanism.

## 5. Open decisions (with recommendations)

- **Mode granularity (G4):** keep **global** `comcon.mode()`. The shared compartment makes
  per-fragment mode a larger change with little near-term value; revisit if/when fragments from
  mutually-distrusting authors must run under different modes in one worker.
- **Compiled tier (G6/P5):** treat P5 as a **separate gated increment**, not part of the first
  convergence pass. Landing P1–P4 already unifies the *interpreted* path and lets us retire 4 of 5
  directives' machinery; the compiled tier is the one feature that genuinely only exists on the
  tenant path and carries the SR-2 faithfulness gate.
- **Keep the tenant path during migration:** yes — do not remove anything until P4+P5 are green.
  A/B coexist behind identical behavior (the directives already forward to the same fields).
- **`admit` vs `include` composition (G1):** `include` should *call* the existing `admit` C
  helpers rather than duplicate them, so there is one C3 implementation.

## 6. Non-goals / risks

- **Not** changing the confinement model (reach gates, M-SES lockdown, gas) — only unifying the
  *entry* + *handler* path onto `include`.
- **Risk:** the compiled tier (G6) is the tall pole; if it proves too coupled, the fallback is to
  keep the tenant path *solely* as the compiled-tier backend and route everything else through
  `include`, deferring full removal.
- **Risk:** test migration (G7) is broad; mitigate by porting one `comcon_*.t` at a time with both
  paths green.

## 7. Definition of done

Every confined fragment — request handler or config fragment — is a `comcon.include(...)` bound
through `location.handler` (or another COM setter); the `js_tenant_*` directives and the
`tenant_ctx`/`onRequest` subsystem are deleted; the full `comcon_*.t` suite runs on the one
mechanism on both builds (interpreted + JIT); SR-2 faithfulness holds for the retargeted compiled
tier.
