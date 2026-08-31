# COMCON in nginx — Look & Feel, Part VI: Scenarios 48–49 (v5.0)

> **Status: illustrative, not normative.** These two scenarios demonstrate the **typed
> profile** (ROADMAP §M3): what a policy that reaches the compiled tier actually looks
> like, and what happens when one line of it can't be typed. Syntax hypothetical;
> the profile rules live in FOUNDATION Principle 11 and ROADMAP §M3/§10.

---

## 48. Typed enough to vanish: what the 96% tier looks like

**Problem:** you've heard that a fully-typed policy compiles to C at ~96% of stock
nginx. The fear is obvious: "so I have to learn a new language."

**With COMCON** — there is no new language (Principle 11). A typed fragment is the same
policy-JS; types come from three places, and you only *write* the third:

```js
"use comcon: pricing";                        // anchor — inert; this file is plain JS

/** @typedef {{t: str, n: int}} Flow */       // 1 annotation: declare your flow slots
                                              // (the one thing inference can't guess)

export function onRequestHeaders(ev) {        // ev's whole surface is typed by the
  const t = ev.header("x-tenant") ?? "-";     //   SCHEMA (describe() registry):
  const n = ev.table.incr("cnt:" + t);        // t: str, n: int — INFERRED from the
  ev.flow.t = t;                              //   API signatures; nothing written
  ev.flow.n = n;
}

export function onResponseHeaders(ev) {
  ev.setResponseHeader("x-count", String(ev.flow.n));   // int → str made explicit
  ev.setResponseHeader("x-tenant-seen", ev.flow.t);
}
```

Three things to notice. **It runs anywhere** — the annotation is a comment; `node
pricing.js` executes it with types ignored, byte-identically (erasure soundness,
ROADMAP §10). **You wrote one type** — the schema typed the API, inference typed the
locals; annotation is the residue, not the style. **Type errors are ordinary
denials** at admission:

```
DENIED  E_TYPE_MISMATCH   pricing.js:14
  why:  setResponseHeader expects str; ev.flow.n is int
  hint: String(ev.flow.n)
```

And the payoff is printed, not promised:

```
$ comconctl admit acme/pricing.js
  types: fully resolved (1 annotation, 11 inferred, 14 from schema)
  tier:  COMPILED — eligible for the native path (see PERFORMANCE.md)
```

**The point:** "typed" is not a language you learn — it's a report you read. The
schema and inference do nearly all the writing; the compiler does all the vanishing.

---

## 49. One dynamic line, one slow fragment — the gradient is per-fragment

**Problem:** one handler genuinely needs dynamism — a computed field name out of a
JSON body. Does that condemn the whole tenant to the interpreted tier?

**With COMCON** — typing is per-fragment economics, and `any` is legal, visible, and
priced — never forbidden:

```js
export function onRequestHeaders(ev) {
  const blob = JSON.parse(ev.header("x-meta") ?? "{}");   // blob: any
  const v = blob[ev.flow.userField];                      // computed access → stays any
  ev.flow.score = Number(v) || 0;                         // re-enters the typed world
}
```

```
$ comconctl admit acme/scoring.js
  types: PARTIAL — 'blob' is any (computed member access, scoring.js:3)
  tier:  HYBRID — this fragment runs interpreted; guards inserted where
         'any' re-enters typed slots (Number(v) at :4)
  note:  11 of 12 fragments in this tenant remain COMPILED — tier is per
         fragment, not per tenant
```

The admission report *is* the optimization to-do list. If the score matters on the hot
path, type the escape hatch away — e.g. declare the metadata shape, or admit the blob
through a `pattern{}`-validated accessor — and the fragment returns to the compiled
tier on the next epoch. If it doesn't matter, leave it: correctness and confinement
are identical on both tiers (soundness is stage-independent — the tier changes what it
*costs*, never what it *may do*).

**The point:** the 28%↔96% gradient is not a platform setting or a tenant grade — it's
a per-fragment dial, read from a report, moved by adding types exactly where the
report says they're missing.

---

*Parts I–V: `SHOWCASE.md` (1–7) · `SHOWCASE17.md` (8–17) · `SHOWCASE37.md` (18–37) ·
`SHOWCASE45.md` (38–45) · `SHOWCASE47.md` (46–47) · Design: `FOUNDATION.md` ·
Profile: `ROADMAP.md` §M3/§10 · Numbers: `PERFORMANCE.md`.*
