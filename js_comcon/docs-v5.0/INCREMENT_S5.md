# COMCON — S5: configurable tenant execution budget (gas) — scope

Follows the v5.27 gas work (per-request CPU bound on both tiers). That landed the
*mechanism*; S5 makes the bound **deployable** (operator-configurable) and, optionally,
**metered** (load-independent).

## The gap

Tenant CPU is bounded by a **fixed** wall-clock deadline: `NGX_JS_TENANT_TIMEOUT_MS = 1000`
(`ngx_js.h:90`), set at `ngx_js_http_module.c:8544` as `now + 1000` around the tenant
`JS_Call`, enforced by `ngx_js_interrupt_handler` (aborts when `now >= w->request_deadline_ms`;
a no-op when the deadline is `0`). It covers **both** tiers — the interpreted poll and the JIT
back-edge gas share the one handler + deadline. Not deployable: operators can't tune it, can't
grant "unlimited" to a trusted fragment, and it's wall-clock (load-dependent), not metered.

**Why a directive, not a JS knob.** The regular path already exposes a configurable
per-request deadline — but via **JS** (`nginx.workerRequestTimeout`, `ngx_js_http_module.c:6689`;
`nginx.workerMemoryLimit` likewise), i.e. *host-trusted* config. A tenant is untrusted and
must not set its own budget, so the tenant timeout is an **operator-only directive**.

## S5-a — `js_tenant_timeout` directive (core, must-have)

- New directive `js_tenant_timeout <time>;` — `NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1`,
  mirroring the `js_tenant_mode`/`js_tenant_source` family. Custom handler (like
  `ngx_js_tenant_mode`) parsing `ngx_parse_time`-style values ("1s"/"500ms") or plain ms.
- Field `ngx_msec_t tenant_timeout;` in `ngx_js_conf_t` (next to `tenant_mode`). **Default =
  1000 ms.** `0` = unlimited (the handler already no-ops on deadline `0`).
- Swap the `#define` use at `:8544` for `jcf->tenant_timeout`: deadline =
  `timeout == 0 ? 0 : now + timeout`. **Both tiers covered automatically** (shared deadline).
- **GOTCHA (resource-safety trap):** `ngx_pcalloc` zeros the conf (`ngx_js_module.c:521`), and
  `0` means *unlimited* — so the default must be set to `1000` **explicitly** right after the
  pcalloc, otherwise an operator who never writes the directive silently gets an *unbounded*
  tenant. This is the one must-not-miss detail.
- **Doubles as the policy knob** from the earlier tracked question ("regulate compiling / allow
  time-critical calcs without gas checking"): `js_tenant_timeout 0;` on a trusted/time-critical
  profile disables the bound. No separate mechanism needed.

## S5-b — metered budget (optional, deeper — recommend deferring)

Wall-clock is load-dependent (real time includes preemption/blocking). A **metered** budget
counts work units → deterministic, load-independent. Feasible with the **existing** mechanism:
the interrupt handler is polled ~every 100 bytecodes (interpreter) / on back-edges (JIT); add a
per-request decrementing counter and abort at 0 (granularity ~100 instructions). Directive
`js_tenant_gas <units>;`, run alongside the wall-clock deadline (whichever fires first).
Trade-off: deterministic but **abstract** — operators don't know a request's unit cost, whereas
ms is intuitive. Defer unless load-independent budgets become a hard requirement.

## Granularity & non-goals

- One tenant runtime today (`jcf->tenant_rt` serves all `js_tenant_source` files) → a single
  main-conf value. **Per-tenant / per-profile** budgets are future multi-tenancy work.
- Orthogonal to `tenant_mode` (enforce/audit/learn) and to the 64 MB memory cap (already done).

## Verification (extend `t/comcon_gas.t`, both builds)

- `js_tenant_timeout 200;` → a `while(true){}` tenant aborts at ~200 ms (not ~1000).
- `js_tenant_timeout 0;` → a bounded-but-long compute that would exceed 1 s **completes**.
- No directive → the 1000 ms default is preserved (guards the pcalloc-zero trap).
- Regression: the existing `comcon_gas.t` default-1s cases stay green on interpreter **and** JIT.

## Effort & recommendation

- **S5-a: small** — 1 directive handler + 1 conf field + explicit default + 1 call-site swap +
  tests. The only real risk is the pcalloc-zero default trap.
- **S5-b: medium** — interrupt-handler metering + directive + documenting unit-cost intuition.

**Recommendation: do S5-a now** (makes the resource bound deployable and resolves the
time-critical policy knob); **hold S5-b** unless load-independent budgets are required.
