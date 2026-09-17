# COMCON in nginx — Look & Feel, Part VI: Scenario 51 (v5.0)

> **Status: illustrative, not normative.** This scenario shows *mid-program, per-invocation*
> confined inclusion — a 3rd-party fragment spliced into the middle of the host program's
> control flow (an inner loop), not merely wired as a location handler. Design:
> `OPERATOR_API.md` (`include = parse ∘ admit ∘ bind`, staging §6), `FOUNDATION.md` §2a/§4,
> the shell fundament (nginx.conf gains only `js_source`).
> **Since v5.126 every scenario opens with a `REAL CODE` block:** what the shipped tree does today for that scenario, the tests that pin it, and the gap id (`SHOWCASE-gaps.md`) where the sample and the tree differ. The samples below it are the original hypothetical syntax, kept as written.

---

## 51. A 3rd-party fragment in the middle of an inner loop

> **REAL CODE (v5.125): SHIPPED** (the callable form). Admit once at config load, call per
> item, each call metered and scope-isolated:
> ```js
> var normalize = comcon.include(normalizeSource, {
>     imports: [], grants: { lookup: comcon.mediate(srv, comcon.routes("/norm/*")) },
>     identity: pin, meter: comcon.meter({ timeoutMs: 2 }), onViolation: "audit" });
> loc.handler = function (req) {
>     var items = JSON.parse(req.body || "[]"), out = [];
>     for (var i = 0; i < items.length; i++) {
>         "use comcon: normalize-site";                 // inert; queryable by anchors('normalize-site')
>         out.push(normalize(items[i]));                // sees `lookup` and its argument, nothing else
>     }
>     req.respond(200, { "content-type": "application/json" }, JSON.stringify(out)); };
> ```
> Tests: `t/comcon_include.t` (scope isolation, the meter), `t/comcon_pom_anchors.t`.

**Problem:** the host program processes a batch and wants a **3rd-party fragment to run on each
item, inside its own inner loop** — under a policy the host defines, without letting the
fragment see the host's locals, and without one slow item hanging the loop. "Inject a fragment
in the middle of my code," not "hand off a whole request to a tenant."

**Shape.** nginx.conf contains only `js_source root.js;`. The fragment is **admitted once at
stage-0** (config load) and **invoked per iteration at stage-1** (runtime).

```js
// root.js
import { env, grant, mediate, admit } from "comcon";
import { meter, readonly } from "comcon:interceptors";

// ── STAGE 0 (init_conf / "compile time"): admit the 3rd-party fragment ONCE ──
const dict = nginx.shared.dict("normalize-table");        // a host-held capability

const e = env();                                          // deny-by-default
grant(e, "lookup", mediate(dict.get, readonly()));        // may only READ the table

const normalize = admit(load("plugins/acme/normalize.js"), {
  schema:   "plugins/acme/normalize.d.ts",                // typed vs the granted surface
  identity: "sha256-…",                                   // pin the fragment
  env:      e,                                            // bound policy env — sees ONLY `lookup`
  meter:    meter({ timeoutMs: 2 }),                      // ≤2 ms PER INVOCATION
  onViolation: "audit",                                   // observe-first
});
// `normalize` is now a governed callable: normalize(item) → item′, confined to `e`.

// ── the host handler; the fragment runs in the MIDDLE of an inner loop ──
nginx.http.servers[0].locations["/batch"].handler = function (req) {
  const items = JSON.parse(req.body);
  const out = [];
  for (const item of items) {                    // ← host's inner loop (host control flow)
    "use comcon: normalize-site";                //   inert anchor — names the splice site
    out.push(normalize(item));                   // ← 3rd-party fragment, per iteration,
  }                                              //   confined + metered, data-in/out
  return { status: 200, body: JSON.stringify(out) };
};
```

**Why this is the correct realization of "spliced into the loop":**

1. **Stage split — admit once, invoke many.** `admit` runs at **stage-0** (checks + pin + maxim
   AOT-lowering at config load); the fragment is then **invoked per iteration at stage-1**, each
   call independently metered. No re-admission inside the loop.
2. **Scope isolation is the point.** Although `normalize(item)` sits lexically inside the host's
   loop body, the fragment does **not** see `items`, `out`, `req`, or the surrounding scope —
   `bind` gave it its own deny-by-default env `e`, so it resolves **only** `lookup` plus what the
   host **explicitly passes** (`item`); any other free name is a stage-0 error. Here it is a
   policy-bound callable you *call* (§51c shows the inline-**text** form); **either way the
   fragment resolves through its bound env, never the enclosing scope** — which is why splicing
   untrusted code into the middle of your control flow is safe. Needs a host value? `grant` a cap
   or pass an argument; nothing leaks implicitly.
3. **Anchor, not syntax.** `"use comcon: normalize-site";` is an inert string directive (pure JS,
   like `"use strict"`) that *names* the site — for audit, redaction, and binding-set-drift
   ("what policy governs this site"). No grammar added.
4. **Per-invocation metering keeps the host in control.** `meter({timeoutMs: 2})` bounds *each*
   call, so a pathological item can't hang the loop — the fragment's gas fires at 2 ms and
   control returns to the host loop.
5. **Recursion / attenuation.** `normalize.js` may itself `admit` a sub-fragment per call under an
   env `⊆ e` — only narrower (No-Amplification). Same primitive, one level down.
6. **Compile-through, proven faithful.** The `readonly`/`meter` mediations lower into the
   fragment's compiled C (a membrane costing ~nothing); the **M8 / SR-2 faithfulness gate**
   guarantees the lowered native simulates every mediation the stage-0 policy erased.

---

## 51b. The same in a *stage-0* loop (compile-time config-gen)

> **REAL CODE (v5.125): SHIPPED.** A stage-0 loop that includes N fragments is ordinary host
> JS at config load:
> ```js
> routes.forEach(function (r) {
>     var frag = comcon.include(r.source, { imports: [], grants: envFor(r), identity: r.pin,
>                                            meter: comcon.meter({ timeoutMs: r.budget }) });
>     locs.find(function (l) { return l.path === r.path; }).handler = function (req) { … frag(…) … }; });
> ```

If the loop is in the **root program itself** — generating N confined fragments at config load,
one per route — the same `admit` runs inside a stage-0 loop:

```js
for (const r of routes) {                        // stage-0 (compile-time) loop
  const frag = admit(load(r.src),
    { env: envFor(r), meter: meter({ timeoutMs: r.budget }), identity: r.pin });
  nginx.http.servers[0].locations[r.path].handler = frag.onRequest;   // inject via COM
}
```

So "a fragment in the middle of a loop" resolves cleanly either way: at **stage-0** it *generates*
N admitted fragments; at **stage-1** it *invokes* an admitted fragment per iteration. In both, the
fragment is a policy-bound callable placed at the site — never raw text merged into the host's
scope — and the untouched operator `nginx.conf` gained only `js_source`.

---

## 51c. Textual (hygienic-macro) inclusion — the fragment *is* the loop body

> **REAL CODE (v5.125): NOT BUILT** (gap G-25: `includeAt` with `expose: {in, out}` was folded
> into increment D and waits on structured POM splices; OPERATOR_API §3a). The text-level
> operation that exists is `comcon.harden(cst, query, wrapper)`, which rewrites matched
> sites of a *quotation* and installs the result as an epoch — a hygienic rewrite the host
> reviews, not a splice into the host's own loop body.

51 called a policy-bound function. But the fragment's **text** can instead be **spliced
directly into the loop body** at an anchor — a *textual include*. The host source carries only
an inert anchor; the fragment lands there at compile time:

```js
// root.js — the host's inner loop; the fragment's TEXT is spliced at the anchor
for (const item of items) {
  "use comcon: enrich";          // ← inert anchor: the 3rd-party fragment is spliced HERE
}
```
```js
// stage-0 declaration (a policy unit) — binds a fragment to the anchor under a policy
includeAt("enrich", "plugins/acme/enrich.js", {
  env:     e,                              // granted caps (e.g. `lookup`)
  expose:  { in: ["item"], out: ["item"] },// the ONLY enclosing bindings the block may see
  meter:   meter({ timeoutMs: 2 }),
  contract:{ schema: "enrich.d.ts", identity: "sha256-…", tests: "enrich.suite.js" },
});
```
**Stage-0:** the fragment text is spliced at `enrich`, admitted, bound, and AOT-lowered — the
compiled loop body *is* the confined fragment. **Stage-1:** the loop runs that inlined body.

**Why a *textual* include is still confined (it is NOT a naive `#include`):**

1. **Scope hygiene (`bind`).** Though the text sits inside the loop body, `bind` re-scopes it:
   names resolve **only** through the policy env — the ambient `items` / `out` / `req` are
   **invisible**; only `lookup` (granted) and `item` (exposed) resolve; any other free name is a
   **stage-0 error**. Textual position is shared; *lexical scope is not* — bind overrides ambient
   resolution for the spliced subtree. **This is the precise correction to 51's phrasing:** raw
   text may be spliced, but its scope is the bound env, never the enclosing scope.
2. **Control-flow hygiene (`admit`).** A spliced block could otherwise hijack the host loop
   (`break` / `continue` / `return` / labeled jumps to host labels). `admit`'s syntactic
   predicates require a **self-contained block** — non-local control transfer to the host's loop
   or function is rejected at admission. The block communicates *only* through the exposed in/out
   window.
3. **Explicit in/out (`expose`).** The fragment's manifest is the exact list of enclosing
   bindings it may read (`item` in) and write (`item` out); everything else in scope is
   unreachable. The live loop binding is re-exposed each iteration.
4. **Compile-through, and *faster* than the call.** The spliced block lowers **inline** into the
   host's compiled loop — no per-iteration call boundary (so 51c can beat 51's callable form on a
   hot inner loop); the `readonly`/`meter` mediations still lower to guards; **M8 / SR-2**
   guarantees the inlined native simulates every erased mediation.

**51 vs 51c.** Same confinement (both are `include = parse ∘ admit ∘ bind`, both re-scope via
`bind`); the only difference is whether the bound subtree is a **called function** (51 — clean
boundary, good for reuse across sites) or an **inlined block** (51c — no call cost, good for tight
inner loops, at the price of the extra control-flow-hygiene obligation on `admit`). "Inject a
fragment in the middle of my code" is exactly 51c: a hygienic, capability-scoped, stage-0 macro
splice at an anchored site.
