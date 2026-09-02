# COMCON — M-SES scope (curated intrinsics / SES-style lockdown)

*Scope note, 2026-09-01. Driven by the front-end soundness audit (VERIFICATION.md
"Front-end soundness audit"; FOUNDATION v5.18): the audit promoted M-SES from optional
hardening to a **hard prerequisite for C5 erasure soundness and C4 env-signature
completeness**. This note scopes the work, grounded in the vendored engine's actual
intrinsic API and a feasibility spike.*

---

## 1. Why (the audit's demand, precisely)

C3-rest claims "no dynamic code, therefore the static free-name manifest is complete."
The audit falsified the antecedent: dynamic code is reachable via
`[].constructor.constructor`, the async/generator function constructors, and
`Reflect.construct` — none named `eval`/`Function`, none emitting `OP_eval`, so the
name-deny-list and the opcode scan miss them all. Confinement still held (the reached
global is deny-by-default), so it is not an escape — but the **soundness** properties C5
will rely on are not delivered until the reflective intrinsics that defeat them are gone.

**M-SES's job for the C track:** make "the tenant context contains no dynamic-code
portal" *true*, so (a) C3-rest's guarantee holds, (b) C4's free-name manifest is a
complete over-approximation of reached capabilities, and (c) C5 may erase against it.

## 2. The pivotal finding (why this is taming, not intrinsic selection)

QuickJS's `JS_NewContext` = `JS_NewContextRaw` + eleven `JS_AddIntrinsic*` calls. One
portal is its own intrinsic and can simply be **omitted**:

- **`Proxy`** is installed only by `JS_AddIntrinsicProxy` — omit it (it also made HIGH-1
  trivially exploitable, THREATS LOW-6).

The **`eval`** global comes from `JS_AddIntrinsicEval`, but that intrinsic *also* installs
`ctx->eval_internal` — the compiler entry point every `JS_Eval` needs, including the MODULE
compilation that runs tenant sources. So the Eval intrinsic **cannot be omitted**; the
lockdown **deletes the `eval` global** instead (§5, M-SES-0 note).

But the dangerous reach is **welded into `JS_AddIntrinsicBaseObjects`**, which is
mandatory (Object/Array/Error/… need it):

- the **`Function` constructor** (`quickjs.c:58802`, `JS_NewCConstructor(..., "Function",
  ...)`), hence `Function.prototype.constructor` and the `.constructor.constructor` reach;
- the **`Reflect`** object incl. `construct`/`apply` (`quickjs.c:58935`).

So M-SES cannot drop these by choosing intrinsics — it must **tame** them after the
context is built (SES "lockdown"). The generator/async/async-generator function
constructors are not globals; they are reached through the respective prototypes'
`.constructor`, so they are tamed the same way.

## 3. Feasibility — spiked and PROVEN

A `qjs` spike confirmed the taming approach works and does not disturb ordinary code:

- `Function.prototype.constructor` is `writable:true, configurable:true` → redefinable.
- Redefining `constructor` to a throwing stub on all four evaluator prototypes
  (`Function.prototype`, and the prototypes of `function*(){}`, `async function(){}`,
  `async function*(){}`) makes `[].constructor.constructor("return 1")` **throw**
  `TypeError: dynamic code disabled`.
- Normal functions, arrow functions, and `JSON.stringify` are unaffected.

No engine patch is required — the lockdown runs from the host against the tenant context.

## 4. The curated intrinsic set

Grounded in what confined tenants actually use today (JSON.parse/stringify, Object/Array
prototypes, Date, Math.random, RegExp — from `t/comcon_*` and the demos):

| Intrinsic | M-SES action | Why |
|---|---|---|
| `BaseObjects` | **KEEP + TAME** | mandatory; tame Function/Generator/Async ctors + delete `Reflect`, global `Function`/`GeneratorFunction`/… bindings |
| `Eval` | **KEEP + delete `eval` global** | the intrinsic installs `ctx->eval_internal` (the compiler `JS_Eval`/modules need); can't omit — delete the reflective `eval` global in lockdown |
| `Proxy` | **OMIT** | removes `Proxy` (LOW-6; membrane-defeating) |
| `Date` | KEEP | tenants use it; V1 numeric discipline already covers ms-not-ns |
| `RegExp` | KEEP (revisit) | tenants use it; ReDoS is a *budget* concern (S5), not a portal — note for M-LIB pattern profile |
| `JSON` | KEEP | core tenant use |
| `StringNormalize` | KEEP | harmless |
| `MapSet` | KEEP | general-purpose, safe |
| `TypedArrays` | KEEP | safe; needed if tenants touch binary |
| `Promise` | KEEP | async handlers |
| `WeakRef` | KEEP (revisit) | GC-observability side channel is a *timing* concern (T9), not a portal |
| module loader | **already absent** on the tenant runtime (`trt`) → `import()` inert; keep absent, assert in a test |

**Taming list (from C, after intrinsics, before any tenant source runs):**
1. neutralize `constructor` on the four evaluator prototypes (throwing, non-writable,
   non-configurable);
2. delete the global bindings `Function`, `Reflect` (and `GeneratorFunction`/
   `AsyncFunction`/`AsyncGeneratorFunction` if present as globals);
3. confirm `eval`/`Proxy` absent (omitted) and `import()` inert (no loader).

Recommended implementation: a **frozen lockdown snippet** evaluated in the tenant context
immediately after `JS_AddIntrinsic*` — auditable, matches SES lockdown, unit-testable —
with the taming stubs frozen so a tenant cannot restore them.

## 5. Phasing

- **M-SES-0 — dynamic-code lockdown (THIS slice; the C5 prerequisite). DONE
  (2026-09-01).** The tenant `JS_NewContext` became `ngx_js_tenant_context_new`
  (`JS_NewContextRaw` + curated intrinsics) + `ngx_js_tenant_lockdown` (the taming
  snippet, run as a module). `[].constructor.constructor(...)`, the generator/async
  constructors, `Reflect`, `eval`, and `Proxy` are all unreachable in the tenant; every
  `comcon_*` test passes; `t/comcon_frontend_audit.t`'s A1 line now asserts the route
  **throws**, and `t/comcon_mses.t` covers the full surface. C3-rest's claim is corrected
  back to sound; audit finding A1 closed in THREATS/VERIFICATION.

  **One scope correction (learned during impl):** the **Eval intrinsic cannot be
  omitted** — `JS_AddIntrinsicEval` installs `ctx->eval_internal`, the compiler entry
  point every `JS_Eval` (incl. MODULE compilation — how tenant sources run) depends on;
  omitting it throws "eval is not supported" for *all* tenant code. So the Eval intrinsic
  stays and the lockdown **deletes the reflective `eval` global** instead (equivalent
  effect: no JS-reachable `eval`, since the Function-ctor route is tamed too). Also: the
  lockdown must run as a **module**, not `JS_EVAL_TYPE_GLOBAL` (indirect global eval needs
  the eval global, which we remove). Proxy is genuinely omittable (nothing in the tenant
  path uses JS `Proxy`; the learn recorder is a C exotic class).
- **M-SES-1 — intrinsic freezing (cross-tenant isolation). DONE (2026-09-01, v5.26).**
  `Object.freeze` the shared intrinsics / prototype-pollution defense (S1 "frozen
  intrinsics"). Distinct from M-SES-0 (which closes *dynamic code*, not *prototype
  tampering*). **Built:** the tenant lockdown (`ngx_js_tenant_lockdown`) now transitively
  hardens the intrinsic graph — from every intrinsic reachable off `globalThis` (+ the
  generator/async + array-iterator prototypes), `Object.freeze` constructors, prototypes,
  methods and accessor functions; `globalThis` itself is left extensible (caps + COM protos
  install afterwards). Confirmed exposure it closes: a tenant setting `Object.prototype.evil`
  in req 1 was visible in req 2 (long-lived tenant runtime); now the write throws (frozen +
  strict) and never persists. Ordinary JS is unaffected (own-object mutation + built-in
  method calls still work). Test `t/comcon_freeze.t`; full comcon green on both builds
  (21/180). **Noted extension:** the COM/Socket prototypes are installed *after* the lockdown
  so this pass does not freeze them — their mutators are C-side-gated regardless; freezing
  them too is a small follow-up (a second harden after `ngx_js_com_install_protos`).
- **M-SES-2 — portal taming + escape-probe gate (= SR-3). DONE (2026-09-01, v5.28).** The
  adversarial pentest of intrinsic/engine escape completeness. **Verdict: no sandbox
  escape** — every dynamic-code route stays tamed (error/bound-fn/`Symbol.species`
  `.constructor.constructor` all throw), strict `this` is `undefined`, the core + iterator
  + shared generator prototypes are frozen, unbounded recursion is caught. One MEDIUM
  freeze-completeness gap found + fixed (**SR3-1**: the M-SES-1 value-walk missed the
  sibling iterator instance-prototypes — `%String|Map|Set|RegExpStringIteratorPrototype%`,
  reachable only by calling a method — so a tenant could pollute them across requests;
  added as explicit harden roots) and one availability case contained by the existing gas
  (**SR3-2**: Promise microtask loop, ~1 s, worker recovers). Re-audited clean on both
  tiers; `t/comcon_freeze.t` +2 SR-3 cases. Full record in VERIFICATION.md (SR-3
  escape-completeness audit). Memory-safety / full-test262-under-AOT for untrusted-native
  production remains gated on maxim finalization (a separate compiler-conformance gate,
  not a confinement gap).

## 6. Risks / open questions

- **Completeness of the reflective surface. CERTIFIED (SR-3, v5.28).** The four evaluator
  prototypes + Reflect + eval + Proxy are the known portals; SR-3 adversarially confirmed
  none remain reachable (`Symbol.species`-keyed reach, error-constructor chains, bound-fn
  all tamed). The one gap SR-3 surfaced was *freeze* completeness, not a portal (SR3-1
  sibling iterator prototypes — cross-request pollution, now fixed), plus one gas-contained
  availability case (SR3-2). M-SES-0 closed the *audited* dynamic-code vectors; SR-3
  certified the rest.
- **Curated-set breadth is a knob.** The table above is the recommended default; trimming
  RegExp/WeakRef/TypedArrays tightens the surface at the cost of tenant expressiveness —
  a per-profile decision, not a blocker.
- **Freeze vs. per-tenant fresh context.** M-SES-0 tames per-context at creation; today
  each cycle builds one tenant context, so freezing the stubs is sufficient. Cross-tenant
  sharing (M-SES-1) raises the freezing bar.

## 7. Placement

M-SES-0 is a **prerequisite for C5** and, unlike C5/C6/C7, is **not blocked on maxim
finalization** — it is interpreted-tier context surgery. Recommended order:
**M-SES-0 → C5 lowering (with the erasure-complete typing) → C6 → C7/M8 (=SR-2)**, with
M-SES-1 slotting in before multi-tenant-shared-runtime work and M-SES-2 = SR-3 after the
lockdown is complete.
