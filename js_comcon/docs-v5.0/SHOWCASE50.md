# COMCON in nginx — Look & Feel, Part VII: Scenario 50 (v5.2)

> **Status: illustrative, not normative.** This scenario demonstrates the v5.2
> provenance ruling: **substrate follows provenance, not language** — foreign-born code
> enters as WASM through the `wasm` facet; our-born JS never leaves the T1/T2 path.
> Design: `ROADMAP.md` §M-LIB (v5.2), memory branch §17. Syntax hypothetical.

---

## 50. The border crossing: a Rust module moves in

**Problem:** a fraud-detection partner ships their scoring model as a compiled WASM
module, built from Rust. You want it *in the request path* — you will never see the
source, and you can't audit what you can't read.

**Today:** run it as a sidecar service (a network hop on every request) — or link a
black box into your process and hope.

**With COMCON** — foreign-born code enters through the `wasm` facet, and the facet is
just the kernel wearing WASM's clothes: **validation *is* `admit`, the import object
*is* an environment, fuel *is* a budget mediation.** The module gets exactly the
grants you hand it — which is how WASM always secretly wanted to be used:

```js
const scorer = wasm.admit("./partner/fraud-scorer.wasm", {
    imports: {                                     // its ENTIRE world — deny-by-default
        get_header: mediate(reqview.header, only(["x-txn-id", "x-amount"])),
        log:        mediate(host.log, rateLimit(5)),
    },
    budgets: { fuel: "2M/request", memory: "4MB" },
    pin:     "sha256:7d41…",                       // the module you audited is the
});                                                //   module that runs — forever
```

```
$ comconctl evaluate ./partner/fraud-scorer.wasm
  imports demanded: get_header, log, clock(!)      ← clock refused: not granted;
  verdict: 3 authorities requested, 2 granted       partner notified, module adapted
```

**Hot path? Same funnel as everything else.** A cold module runs on the embedded
runtime; a hot one is **ingested via wasm2c** — the foreign WASM lowered to C (its SFI
bounds checks preserved in the generated source) and fed through the *same* TCC/GCC
pipeline, `.so` loading, back-edge gas, and generation-check revocation as maxim's own
output. One trusted path — "C emitted by a tool we trust" — with two provenance
front-ends: maxim for our JS, wasm2c for their WASM. (Honest note: wasm2c thereby
joins the TCB, beside maxim.)

```
$ comconctl admit partner/fraud-scorer --lane hot
  ingested: wasm2c → C → .so   (SFI checks preserved; gas: back-edge;
  revocation: generation check at entry — CVE day works here too)
```

**And the door swings one way.** Your own JS never crosses *out* through this border —
JS→WASM would be a category error: a second sandbox around an already-safe language,
paying the boundary-marshaling tax for zero trust gained. (The separate *export* lane —
maxim emitting WASM to carry an admitted fragment onto a foreign Proxy-Wasm host — is
the reverse trip: your admission guarantees travel with the artifact; the foreign
host's coarse permission ABI is what enforces there, and the artifact's report says
exactly that.)

**The point:** WASM is the **border crossing, never the interior**. Code shows its
passport (provenance), gets exactly the visa you stamp (imports = grants), pays the
same taxes as citizens (gas, pins, revocation, admission) — and once through the hot
lane, it runs on the same roads as everything else.

---

*Parts I–VI: `SHOWCASE.md` (1–7) · `SHOWCASE17.md` (8–17) · `SHOWCASE37.md` (18–37) ·
`SHOWCASE45.md` (38–45) · `SHOWCASE47.md` (46–47) · `SHOWCASE49.md` (48–49) ·
Design: `FOUNDATION.md` · Ruling: `ROADMAP.md` §M-LIB (v5.2).*
