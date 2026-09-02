# COMCON in nginx — Look & Feel, Part VI: Scenario 51 (v5.0)

> **Status: illustrative, not normative.** This scenario shows *mid-program, per-invocation*
> confined inclusion — a 3rd-party fragment spliced into the middle of the host program's
> control flow (an inner loop), not merely wired as a location handler. Design:
> `OPERATOR_API.md` (`include = parse ∘ admit ∘ bind`, staging §6), `FOUNDATION.md` §2a/§4,
> the shell fundament (nginx.conf gains only `js_source`).

---

## 51. A 3rd-party fragment in the middle of an inner loop

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
   host **explicitly passes** (`item`); any other free name is a stage-0 error. It is a
   **policy-bound callable you *call*, not raw text that shares your scope** — which is exactly
   why splicing untrusted code into the middle of your control flow is safe. Needs a host value?
   `grant` a cap or pass an argument; nothing leaks implicitly.
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
