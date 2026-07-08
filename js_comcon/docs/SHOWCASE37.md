# COMCON in nginx — Look & Feel, Part III: Scenarios 18–37

> **Status: illustrative, not normative.** Continuation of `SHOWCASE.md` (1–7) and
> `SHOWCASE17.md` (8–17). All syntax is **hypothetical**; the design lives in
> `FOUNDATION.md`.

---

## 18. Prototype poisoning is dead

**Problem:** the classic JS attack — override `Object.prototype.toString` (or any shared
builtin) and every tenant's "harmless" code starts working for the attacker.

**With COMCON** — package defaults freeze the world's floor:

```js
Object.prototype.toString = evil;   // ✗ denied: natives & prototype chains are read-only
Array.prototype.map = miner;        // ✗ same — for every tenant, always
```

A tenant that legitimately wants its own `toString` gets it as a **private COW overlay** —
visible to itself, invisible to everyone sharing the object.

**The point:** an entire attack family (prototype pollution — a recurring CVE headline)
becomes a compile-/run-time impossibility, not a lint rule.

---

## 19. PII that can't wander

**Problem:** GDPR/CCPA: personal data may only be touched by approved processors, and must
not leak into logs, analytics, or the wrong region.

**With COMCON** — PII fields arrive opaque, with named deconstructors:

```js
imports: {
    customer: db.customers.view({
        email: opaque.str({ pass_to: ["mailer.send"] }),      // usable, unreadable
        name:  opaque.str({ pass_to: ["renderer.eu-region"] }) // exits only into EU fragment
    })
}
```

`console.log(customer.email)` → denied. `analytics.track(customer)` → the opaque fields
simply aren't serializable. Region routing is a `pass_to` list.

**The point:** data-protection policy stops being a PDF and becomes a descriptor the
engine enforces. "Who can see PII?" has a queryable, provable answer.

---

## 20. Offboarding in one command

**Problem:** a tenant leaves (or is terminated for abuse). Are they *really* gone —
including every capability they ever delegated onward?

**With COMCON:**

```
$ comconctl remove-subtree tenants/acme --cascade
  revoked: 214 grants (31 delegated onward — followed grant chains)
  freed:   COW overlays, handles, hook registrations
```

Grant chains guarantee nothing survives — including the capability ACME lent to its
partner last spring that everyone forgot about.

**The point:** offboarding = deleting a subtree. The forgotten delegation is found by the
*system*, not by the postmortem.

---

## 21. Emergency lockdown — always safe to hit the button

**Problem:** active incident, unclear blast radius. You want the platform in "read-only
crouch" *now*, without fearing the lockdown itself breaks invariants.

**With COMCON** — narrowing is monotone, so a lockdown is *provably safe to apply*:

```
$ comconctl apply-overlay '**' std.postures.lockdown   # writes: deny, exports: freeze,
                                                       # portals: deny, budgets: halved
```

Because it only ever *narrows*, it cannot create new authority anywhere — the one
guarantee you want most when acting fast at 3 a.m. Lifting it restores the reviewed
baseline, not an improvisation.

**The point:** the panic button is mathematically incapable of making things worse.

---

## 22. Codemods at the door

**Problem:** a deprecated API must disappear from fifty tenants' code. Asking fifty teams
= a two-year migration.

**With COMCON** — the mirror-style transform is a *compile-time policy*:

```js
policy(std.transforms.migrate({
    from: "http.fetchUrl(url, cb)",          // deprecated production
    to:   "await http.get(url)",             // rewritten at admission, provably narrower
}))
```

Tenant source stays untouched in their repos; the *admitted* code is the migrated form.
The transform is itself caged (it can rewrite only within the grants it holds).

**The point:** platform migrations execute at the door, not in fifty backlogs.

---

## 23. AI tunes your hottest script — without seeing your data

**Problem:** the busiest tenant script burns CPU; the AI that could optimize it must not
see the traffic it processes.

**With COMCON:**

```
$ comconctl optimize acme/pricing --by ai --data opaque
  → AI sees: code, grammar, policy, budgets, *opaque* traffic shapes — never payloads
  → candidate passes: identical allow-suite (1,214 cases), identical-or-narrower policy
  → speedup: 3.4×; admitted, signed: maker-chain += ai-optimizer v7
```

Equivalence is demonstrated by the recorded suites; authority is verified by policy
comparison (narrower or equal — auto-checkable); the data stayed opaque throughout.

**The point:** "AI under the hood" with a structural guarantee: it optimized the program
it was shown, and it was shown nothing else.

---

## 24. Trust is earned in descriptors

**Problem:** a brand-new tenant signs up. Full authority on day one is reckless; manual
review per upgrade doesn't scale.

**With COMCON** — a trust ladder of profiles, climbed on evidence:

```js
tenants.new()      → std.profiles.probation   // strictest: no portals, tiny budgets
after 30 days clean + coverage report:
                   → std.profiles.standard    // widened by REVIEWED diff
after 6 months + attested maker chain:
                   → std.profiles.trusted
```

Each rung is a descriptor diff — reviewable, revertible, and the history is the tenant's
objective track record.

**The point:** graduated trust becomes an auditable pipeline instead of a gut feeling.

---

## 25. The five-minute vendor evaluation

**Problem:** procurement asks: "what does this vendor SDK actually *do*?"

**With COMCON** — drop it in a fully closed cage and read the static harvest:

```
$ comconctl evaluate ./vendor-sdk.js --policy std.profiles.closed
  static harvest: imports touched: network(4 hosts: 2 undocumented), fs, eval(!), Date
  verdict: requests 9 authorities; vendor docs mention 3
```

No execution required for the static report; a caged demo run adds the dynamic one.

**The point:** vendor claims become checkable in minutes — the SDK's *appetite* is
measured, not asserted.

---

## 26. Protocols with an enforced order

**Problem:** streaming/WebSocket code that sends frames before the handshake, or writes
after close — whole bug classes are just *wrong order*.

**With COMCON** — the sequence is policy (`compose_seqs`):

```js
exports_to: { tenant: ws.facet(protocol("handshake", "frames*", "close")) }
```

```js
ws.send(frame);        // ✗ before handshake completed — denied at the interface
ws.handshake(hdrs);    // ✓ state advances; frames now legal
```

**The point:** "correct sequence of operations" is enforced like types — the API's manual
becomes its mechanics.

---

## 27. The cluster is a tree too

**Problem:** nginx master + N workers + helper processes — inter-worker messages are
today's wild west.

**With COMCON** — the master is the root fragment; workers are its children; **a message
is an export** like any other:

```js
workers.broadcast(routes_update);     // ✓ master → workers: granted
worker[3].send(worker[5], payload);   // only if a communication edge exists — else denied
```

The same lattice that cages tenants inside a worker governs traffic *between* workers —
one model from a single property access up to cluster topology.

**The point:** cluster-internal trust stops being implicit. Same physics at every scale.

---

## 28. Undo for production

**Problem:** the config/tenant change was fine — until an hour later it wasn't.

**With COMCON** — the config tree is versioned and tenant state is snapshot-able
(app-registered serializers):

```
$ comconctl snapshot tenants/acme --label pre-campaign
$ ... an hour of trouble ...
$ comconctl rollback tenants/acme pre-campaign     # subtree swap; transient state drains
```

**The point:** "roll it back" is a subtree operation with a label, not an archaeology
session.

---

## 29. Documentation that can't lie

**Problem:** tenant docs say one thing; the deployed reality says another.

**With COMCON** — a tenant's docs are *generated from its effective policy*:

```
$ comconctl docs acme
  # ACME — your available API (derived from your grants, build 2026-07-08)
  http.get(path: "/api/*")           — 5ms budget/request
  db.query(ParameterizedQuery only)  — table: orders (read)
  ...
```

If it's in the docs, it works; if it works, it's in the docs — both are projections of the
same descriptors.

**The point:** the reference manual is a *query*, perpetually current, per tenant.

---

## 30. The intern-proof deploy

**Problem:** a junior dev's first change ships to production. Everyone holds their breath.

**With COMCON** — the change lands in a fragment whose authority *is* the blast radius:

```js
include js "acme/features/recommend-v2" policy(acme.profiles.feature({
    imports: { catalog: "read" },  exports_to: ["renderer"],  budgets: { cpu: "2ms" },
}))
```

The worst possible bug in that fragment can mis-recommend products. It cannot touch
orders, payments, other tenants, or the disk — not by review, by *reachability*.

**The point:** code review checks quality; the cage bounds catastrophe. Breathing normally
resumes.

---

## 31. Authority with office hours

**Problem:** risky operations (schema migrations, cache flushes) should only happen in
maintenance windows, with a second pair of eyes.

**With COMCON** — schedule and co-sign are grant properties:

```js
grant(db.migrate, { to: ops, window: "Sat 02:00–04:00 UTC", cosign: ["sre-lead"] });
```

Outside the window the capability *doesn't exist*; inside it, exercise requires the
co-signature. No process document — descriptor arithmetic.

**The point:** change-management policy becomes engine-enforced, not calendar-enforced.

---

## 32. The parallel universe (deception for defense)

**Problem:** a tenant behaves suspiciously. Kill it and you lose the forensics; let it run
and you risk the platform.

**With COMCON** — COW views make a *shadow world* cheap:

```
$ comconctl shadow-world tenants/suspect --writes sandboxed --reads live
```

The suspect keeps running against live-looking reads while every write lands in a private
overlay nobody else sees. Forensics watches through the (opaque-respecting) console.

**The point:** containment without tipping off — the suspect executes in a universe that
diverged the moment you got suspicious.

---

## 33. Selling compute on your edge

**Problem:** partners want to run logic on your edge nginx fleet. Revenue opportunity;
terrifying operationally.

**With COMCON** — a rented slot is a caged fragment; **budgets are the meter**:

```js
include js "market/partner-x" policy(std.profiles.marketplace({
    budgets: { cpu: "2ms/req", memory: "8MB" },     // enforcement AND billing input
    imports: { req: http.readonly_facet() }, exports_to: ["responder"],
}))
```

The same counters that stop a runaway partner produce the invoice line items.

**The point:** multi-tenancy strong enough to *sell* — the security model doubles as the
billing model.

---

## 34. Platform upgrades without hostage tenants

**Problem:** js_com v2 ships breaking changes; tenant Y can't migrate this quarter. Today
that blocks the whole fleet's upgrade.

**With COMCON** — API version is part of the grant:

```js
tenants/acme:   imports: { js_com: platform.api("v2") }
tenants/y:      imports: { js_com: platform.api("v1-compat") }   // pinned, deprecated
```

`comconctl diff` lists exactly which tenants still hold v1 grants — the migration's
burn-down chart is a query.

**The point:** the platform moves at the platform's pace; stragglers are pinned, visible,
and contained — not blocking.

---

## 35. The log that can't be un-written

**Problem:** audit logs are only as trustworthy as the code that *could* rewrite them.

**With COMCON** — append-only is a capability shape:

```js
imports: { audit: log.facet({ append: true, read: false, truncate: NEVER }) }
```

No fragment on the platform — including the host program itself, if so configured — holds
a delete/rewrite capability on the audit stream; verification lives with an external
reader holding the read grant.

**The point:** log integrity by construction: the writer provably cannot read or rewrite
what it wrote.

---

## 36. The whole config is a program (finally, safely)

**Problem:** `nginx.conf` templating grew into a fragile generator zoo — because config
*wants* to be a program, and raw programmability was too dangerous to grant.

**With COMCON** — config is a policy-governed program (your v1 note: "ability build full
nginx.conf from js"):

```js
`comcon: policy(std.profiles.config_builder)`      // no I/O, no network, deterministic
for (const region of regions)
    server({ listen: region.port, tls: certs.for(region), routes: standard(region) });
```

Deterministic by policy → reproducible builds, diffable output, testable infra — and the
generator provably can't do anything *but* emit config.

**The point:** infrastructure-as-code without the "what else can the code do?" anxiety.

---

## 37. The audit is a query, not an interview

**Problem:** the annual security audit: weeks of interviews, spreadsheets, and hope.

**With COMCON** — the evidence pack is generated:

```
$ comconctl trust-report --scope tenants/acme
  authority:    transitive closure of grants from root (proof: reachability)
  provenance:   maker chains — every policy & fragment, signed, dated
  verification: allow/deny suites: 100% pass; declarative coverage: 96% verified static
  incidents:    denials last 90d: 12 (all deny-suite-known classes)
  delegations:  3 active grant chains (longest: 2 hops, all attenuated)
```

Every line is derived from descriptors and signatures the engine already maintains —
the auditor can re-run any of it.

**The point:** compliance stops being testimony and becomes reproducible computation.
*"Show me who can touch what"* has an answer that is exact, current, and checkable.

---

*Part I: `SHOWCASE.md` (1–7) · Part II: `SHOWCASE17.md` (8–17) · Design: `FOUNDATION.md`.*
