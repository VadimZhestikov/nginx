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
| **G5** | Request/response contract | **NON-GAP — already js_com.** `location.handler` binds the handler; `req.respond`/`req.json`/`req.text`/`req.html` shape the response; the request→data marshal is a one-object field read. Proven in Option 1 (`t/comcon_operator_handler.t`). No kernel `comcon.serve` — baking it in would grow the kernel for what composition already gives. If the `{status,body}`+cap ergonomics are wanted, ship a **library** snippet (showcase docs), not a `comcon.*` primitive. Body cap is backstopped by the tenant runtime's 64 MB memory limit. | — | — |
| **G6** | **Compiled tier (C5)** | lower an `include` fragment to native C like the tenant `onRequest` handler — the fragment is already a pure `req→{status,body}` function, the ideal lowering shape | **L** | **high** — the C5 entry is wired to `eval_tenant_sources`/`tenant_request_handler`; retargeting to `comcon_ctx` fragments is the hard part |
| **G7** | Test migration | port the tenant-based `comcon_*.t` (source/handler/deps/artifact/admission/lowering/faithfulness) to `include + location.handler`; keep both green during the transition | L | med — many files; do file-by-file |

## 4. Sequencing (phases)

1. **P1 — admission into `include` (G1+G2). ✅ LANDED (2026-09-02).** The C3 gate was factored into
   `ngx_js_comcon_admit_check(ctx, fn, imports, check_request, reason, len)` (non-static in
   `ngx_js_com.c`), shared by the `admit` operator and `include`. `__includeConfined` now takes a
   5th arg `{ imports, checkRequest?, identity? }` and, **when present** (opt-in, so un-admitted
   `include` stays backward-compatible), runs `admit_check` on the compiled fragment **in the
   compartment** (`sctx`) — where grants are closure var-refs, auto-excluded from the free-name
   check — plus an optional identity pin `H(H(source) ‖ NGX_JS_C4_SCHEMA_VERSION)` compared to
   `contract.identity`. **Verified** (`t/comcon_include_admit.t`; comcon 34/267): a clean fragment
   is admitted, an ungranted free name (`nginx`) is refused (unless listed in `imports`), `eval` is
   refused, a correct identity pin admits, a wrong one refuses. The `admit` operator + all existing
   include tests stay green (the refactor preserved its exact reject messages).
2. **P2 — serve helper (G5). ✅ RESOLVED as a NON-GAP (2026-09-02).** Request/response is already
   js_com (`location.handler` + `req.respond`/`req.json`/…), proven in Option 1. No kernel
   `comcon.serve` — reuse the existing primitive per the recursive-inclusion fundament
   ([[feedback-reuse-jscom-primitive]]). An optional library snippet may ship in the showcase docs.
3. **P3 — deps (G3). ✅ LANDED (2026-09-02).** `include`'s `contract.deps = [{name, path,
   sha256}]` loads pinned pure libraries onto the include primitive.
   `ngx_js_comcon_eval_dep(ctx, cycle, path, sha256, out, …)` reads the file, verifies its bytes
   hash to the pin, evaluates it as a bare-global pure script (a lib reaching for host authority
   throws), and its completion value is bound as a **per-fragment closure param** (the dep names
   join the wrapper's param list after the grants; the eval'd values join the closure args) — not
   on a shared global, so deps don't leak across fragments. **Verified**
   (`t/comcon_include_deps.t`; comcon 35/271): a pinned lib (`lib.greet(...)`) is bound and usable
   in the fragment, and a hijacked update (hash mismatch) refuses the include. Since deps are
   closure params, admission (P1) auto-excludes them from the free-name check, exactly like grants.
4. **P4 — migrate `comcon_*.t` (G7). 🔨 IN PROGRESS (2026-09-02).** Add include-based parity
   siblings file-by-file (keeping the tenant tests until P6, so both paths stay green). Landed:
   `t/comcon_include_request.t` (the core confined request handler — live-cap grant + A1 reach-gate
   isolation + admission + host-unreachable + call-stability + host-locations-unaffected),
   `t/comcon_include_headers.t` (data-in/out headers + CRLF drop), `t/comcon_include_deny.t`
   (deny-by-default: `typeof nginx === "undefined"`, withheld `createSocket` unreachable). Together
   with the earlier include tests (grant/admit/deps/handler/mediate/facet) this now covers the
   interpreted tenant scenarios. **Security fix surfaced by P4:** `req.respond` did NOT drop
   CR/LF-injected response headers (only `js_tenant_handler`'s C path did) — a response-header
   CRLF-injection affecting *every* js_com handler. Added `ngx_js_header_has_crlf` +
   a drop-with-warning guard in `ngx_js_request_respond` so the whole js_com response path is safe
   (host + confined). **Still to add:** parity siblings for audit/learn mode, denial_log,
   schema_conformance, restricted, freeze/mses/sr1 (confinement-property tests). The compiled-tier
   tests (`faithfulness`, `lowering`) stay on the tenant path until P5.
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
