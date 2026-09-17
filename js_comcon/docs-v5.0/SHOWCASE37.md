# COMCON in nginx — Look & Feel, Part III: Scenarios 18–37 (v5.0)

> **Status: illustrative, not normative.** Continuation of `SHOWCASE.md` (1–7) and
> `SHOWCASE17.md` (8–17). All syntax is **hypothetical**; the design lives in
> `FOUNDATION.md` / `SEMANTICS.md`.
> **Since v5.126 every scenario opens with a `REAL CODE` block:** what the shipped tree does today for that scenario, the tests that pin it, and the gap id (`SHOWCASE-gaps.md`) where the sample and the tree differ. The samples below it are the original hypothetical syntax, kept as written.

---

## 18. Prototype poisoning is dead

> **REAL CODE (v5.125): SHIPPED.** Intrinsics and prototype chains are frozen at compartment
> init (M-SES S1), and the shared global bindings are frozen too (finding F14):
> ```js
> comcon.include("function(){ Object.prototype.toString = function(){ return 'evil'; }; return 1; }",
>                { imports: ["Object"] })({});      // throws: cannot assign to read only property
> ```
> `t/comcon_cross_identity.t` plants on `Object.prototype`, `Array.prototype[7]`,
> `Error.prototype`, `Map`, `JSON` from one fragment and reads next door: clean every time.
> Tests: `t/comcon_global_binding_freeze.t`, `t/comcon_mses_gate.t`. The private COW overlay
> for a tenant's own `toString` is not built (gap G-03).

**Problem:** the classic JS attack — override `Object.prototype.toString` (or any
shared builtin) and every tenant's "harmless" code starts working for the attacker.

**With COMCON** — the world's floor is frozen at compartment init (this is literally
hardening phase S1, `HARDENING.md`):

```js
Object.prototype.toString = evil;   // ✗ denied: intrinsics & prototype chains are frozen
Array.prototype.map = miner;        // ✗ same — for every tenant, always
```

A tenant that legitimately wants its own `toString` gets it as a **private COW
overlay** — visible to itself, invisible to everyone sharing the object. And the freeze
is not just defense: those inline caches never invalidate — shared paths get *faster*
than stock JS.

**The point:** an entire attack family (prototype pollution — a recurring CVE headline)
becomes a compile-/run-time impossibility, not a lint rule.

---

## 19. PII that can't wander

> **REAL CODE (v5.125): NOT BUILT** (gap G-03, opaque values; and the information-flow track
> the scenario's honesty note already names). Shipped: field-level `redact`/`allow` on a
> capability, and `checkRequest: true`, which refuses at admission a fragment that reads a
> request field the sealed schema does not have (`t/comcon_include_admit.t`).

**Problem:** GDPR/CCPA: personal data may only be touched by approved processors, and
must not leak into logs, analytics, or the wrong region.

**With COMCON** — PII fields arrive opaque, with named deconstructors:

```js
grant(acme, "customer", db.customers.view({
    email: opaque.str({ pass_to: ["mailer.send"] }),        // usable, unreadable
    name:  opaque.str({ pass_to: ["renderer.eu-region"] }),  // exits only into EU fragment
}));
```

`console.log(customer.email)` → denied. `analytics.track(customer)` → the opaque fields
simply aren't serializable. Region routing is a `pass_to` list.

*(Honesty: this is access control, and it is strong — but capabilities gate what code
may* touch*, not where data* flows *once legitimately touched. Full cross-tenant
confidentiality is the information-flow track, `FOUNDATION.md` §13.4.)*

**The point:** data-protection policy stops being a PDF and becomes a descriptor the
engine enforces. "Who can see PII?" has a queryable, provable answer.

---

## 20. Offboarding in one command

> **REAL CODE (v5.125): PARTIAL.** A registered binding is removed with an explicit
> confirmation naming it, the site answers 410, and the tombstone can be revived; a
> fragment's retained memory returns when its epoch is replaced or its slot freed; a
> reseller's sub-fragments die with the reseller's callable:
> ```js
> ops.remove("acme", { confirm: "acme" });    // E_ without the confirm; tombstoned: true after
> ops.revive("acme");
> ```
> Tests: `t/comcon_std_ops.t`, `t/comcon_retained_memory.t`, `t/comcon_author_basic.t`.
> Not built: a delegation chain to cascade over (gap G-01) — there is no re-grant except a
> reseller's copies, so there is nothing forgotten to find.

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

**The point:** offboarding = deleting a subtree. The forgotten delegation is found by
the *system*, not by the postmortem.

---

## 21. Emergency lockdown — always safe to hit the button

> **REAL CODE (v5.125): NOT BUILT, by decision** (`std.postures.*` is on the canonical NOT
> BUILT list: what `lockdown` should narrow to is a decision nobody has made — gap G-10). The
> button that exists is the fleet posture, `ops.enforce()` / `comcon.mode("enforce")`, which
> reaches every worker (`t/comcon_mode_fanout.t`). The meet is real at the capability level:
> masks AND, lifetimes MIN, an identical budget composes and a different one is refused —
> re-mediating can only narrow (`t/comcon_cap_ttl.t`, `t/comcon_budget_uses.t`).

**Problem:** active incident, unclear blast radius. You want the platform in
"read-only crouch" *now*, without fearing the lockdown itself breaks invariants.

**With COMCON** — a lockdown is one more binding on every node, and bindings compose by
**meet** (intersection of authority — order-independent, confluent, built into `bind`):

```
$ comconctl apply-overlay '**' std.postures.lockdown   # writes: deny, exports: freeze,
                                                       # portals: deny, budgets: halved
```

Because plain `bind` can only ever *meet*, applying it cannot create new authority
anywhere — widening a live binding is not even expressible; it would require an
administrative rebind. That is the one guarantee you want most when acting fast at
3 a.m. Lifting the overlay restores the reviewed baseline, not an improvisation.

**The point:** the panic button is mathematically incapable of making things worse.

---

## 22. Codemods at the door

> **REAL CODE (v5.125): PARTIAL, and the profile word is refused on purpose.**
> `profile: "adaptive"` is refused at admission (`E_ADMIT_CONTRACT`) because no transforming
> profile exists and accepting the word would make "runs standalone without COMCON"
> unfalsifiable (`t/comcon_posture.t`). The rewrite that ships is in the *host's* hands and
> reviewable: `comcon.harden(cst, query, wrapper)` returns a quotation with every matched site
> replaced (`$$` = the site's own source), installed through an epoch:
> ```js
> var rep = comcon.harden(comcon.cst(src), "call(fetchUrl)",
>                         comcon.quote("(function(u){ return get(u); })($$)"));
> h.replace(rep.quotation);              // a new epoch; rollback keeps the old one
> ops.rewrite("acme", "call(g)", wrapper) // the same as one ops verb
> ```
> Tests: `t/comcon_pom_harden.t`, `t/comcon_std_ops.t`. Gap G-11.

**Problem:** a deprecated API must disappear from fifty tenants' code. Asking fifty
teams = a two-year migration.

**With COMCON** — the mirror-style transform is a compile-time policy — an
**adaptive-profile** policy, and it says so (transforms change behavior, so the profile
must be declared; restrictive policies may only deny):

```js
std.transforms.migrate({
    from: "http.fetchUrl(url, cb)",          // deprecated production
    to:   "await http.get(url)",             // rewritten at admission
    profile: "adaptive",                     // declared: this policy TRANSFORMS
})
```

Tenant source stays untouched in their repos; the *admitted* code is the migrated form.
The transform is itself caged — it can rewrite only within the grants it holds.

**The point:** platform migrations execute at the door, not in fifty backlogs.

---

## 23. AI tunes your hottest script — without seeing your data

> **REAL CODE (v5.125): NOT BUILT** (gap G-04/G-09: no opaque traffic shapes, no recorded
> allow-suite equivalence, no maker chain). The pieces that would carry it: the contract's
> `tests` run inside the compartment against the candidate (`t/comcon_admit_tests.t`), a
> candidate is a quotation until realized, and `comcon.aotStatus(f)` says which tier it runs
> on. A program handle's reads are quotations, but bodies are not redacted.

**Problem:** the busiest tenant script burns CPU; the AI that could optimize it must
not see the traffic it processes.

**With COMCON** — the AI holds a **redacted program handle** (interfaces, grammar,
budgets — bodies and payloads withheld) plus opaque traffic shapes:

```
$ comconctl optimize acme/pricing --by ai --data opaque
  → AI sees: code (via its redacted handle), policy, budgets, opaque traffic shapes
  → candidate passes: identical allow-suite (1,214 cases), identical-or-narrower policy
  → speedup: 3.4×; admitted, signed: maker-chain += ai-optimizer v7
```

Equivalence is demonstrated by the recorded suites; authority is verified by policy
comparison (narrower-or-equal is auto-checkable); the data stayed opaque throughout.

**The point:** "AI under the hood" with a structural guarantee: it optimized the
program it was shown, and it was shown nothing else.

---

## 24. Trust is earned in descriptors

> **REAL CODE (v5.125): PARTIAL.** Two profiles ship (`std.profiles.tenant(env)`,
> `std.profiles.pure_library()`), and the posture words are per binding (`onViolation`,
> `profile: "restrictive" | "declarative"`). A widening is a new epoch through
> `bindAt.replace()` / `ops.rebind(name, quotation)` and is recorded as such
> (`ops.bindings()` lists epoch and snapshots), but nothing distinguishes an admin handle
> from any host caller — the host is trusted. Not built: the ladder itself and the
> widening-requires-admin gate (gap G-12). Tests: `t/comcon_std_lib.t`, `t/comcon_std_ops.t`.

**Problem:** a brand-new tenant signs up. Full authority on day one is reckless;
manual review per upgrade doesn't scale.

**With COMCON** — a trust ladder of profiles, climbed on evidence. Each rung *up* is a
**widening**, and widening is deliberately outside the lattice: it is an administrative
rebind — a new binding epoch, requiring the admin handle, leaving an audit mark:

```js
tenants.new()      → std.profiles.probation   // strictest: no portals, tiny budgets
after 30 days clean + coverage report:
                   → std.profiles.standard    // widened by REVIEWED diff, new epoch
after 6 months + attested maker chain:
                   → std.profiles.trusted
```

**The point:** graduated trust becomes an auditable pipeline instead of a gut feeling —
and every loosening is structurally forced through the reviewed, epoch-marked gate.

---

## 25. The five-minute vendor evaluation

> **REAL CODE (v5.125): SHIPPED, in two reads instead of one report.** Static: `admit` refuses
> the first undeclared free name and `node.references(name)` / `node.callsites(name)`
> enumerate where a name is used, from bytecode, with lines. Dynamic: learn mode harvests
> every name reached for, with hit counts:
> ```js
> comcon.admit(sdk, { imports: [] })   // {certified:false, code:"E_ADMIT_FREENAME", reject:"free name not declared in imports: fetch"}
> comcon.pom(sdk).callsites("fetch")   // [{line, call:true, method:false}, …]
> comcon.mode("learn"); comcon.include(sdkSource)({}); nginx.tenantLearning().wants
> ```
> Tests: `t/comcon_admit.t`, `t/comcon_pom_callsites.t`, `t/comcon_include_learn.t`. Demos:
> `js_comcon_demos/D_Developers/D1`, `P_Platform_Teams/P2`. **v5.127:** the one static report
> is `comcon.std.evaluate(sdk, {declares: [...]})` — "requests 5 authorities; declared 1 of 5",
> with call sites and lines, nothing run (`t/comcon_std_evaluate.t`; demo `A_Auditors/A2`).

**Problem:** procurement asks: "what does this vendor SDK actually *do*?"

**With COMCON** — drop it in a fully closed cage and read the static harvest (a dry-run
admission: every free name must resolve, so the SDK's appetite is simply its
unresolved-name list):

```
$ comconctl evaluate ./vendor-sdk.js --policy std.profiles.closed
  static harvest: names demanded: network(4 hosts: 2 undocumented), fs, eval(!), Date
  verdict: requests 9 authorities; vendor docs mention 3
```

No execution required for the static report; a caged demo run adds the dynamic one.

**The point:** vendor claims become checkable in minutes — the SDK's *appetite* is
measured, not asserted.

---

## 26. Protocols with an enforced order

> **REAL CODE (v5.125): SHIPPED** for the capability kinds that exist — a socket's field
> reads and an outbound capability's `request`:
> ```js
> comcon.mediate(sock, comcon.protocol("address", "port*", "fd"))   // bare once, starred any number
> ```
> Order is enforced, completion is not (a fragment can simply return), the cursor is per
> wrapper and a violation does not advance it (`t/comcon_cap_protocol.t`; demo
> `js_comcon_demos/S_Security_Teams/S2`). A `ws` facet with `handshake`/`frames`/`close` is
> not a capability kind today (gap G-14).

**Problem:** streaming/WebSocket code that sends frames before the handshake, or
writes after close — whole bug classes are just *wrong order*.

**With COMCON** — the sequence is policy:

```js
expose(host, "tenant", ws.facet(protocol("handshake", "frames*", "close")));
```

```js
ws.send(frame);        // ✗ before handshake completed — denied at the interface
ws.handshake(hdrs);    // ✓ state advances; frames now legal
```

**The point:** "correct sequence of operations" is enforced like types — the API's
manual becomes its mechanics.

---

## 27. The cluster is a tree too

> **REAL CODE (v5.125): NOT BUILT** (gap G-15: an open design question — the kernel semantics
> is single-runtime). What crosses workers today is data through `nginx.shared`: a shared
> binding's `{epoch, source}` (`comcon.bindShared`, `t/comcon_pom_fanout.t`) and the fleet
> posture (`t/comcon_mode_fanout.t`); each worker reconciles lazily and recompiles in its own
> compartment.

**Problem:** nginx master + N workers + helper processes — inter-worker messages are
today's wild west.

**With COMCON** — the master is the root fragment; workers are its children; **a
message is an export** like any other:

```js
workers.broadcast(routes_update);     // ✓ master → workers: granted
worker[3].send(worker[5], payload);   // only if a communication edge exists — else denied
```

The same lattice that cages tenants inside a worker governs traffic *between* workers —
one model from a single property access up to cluster topology.

**The point:** cluster-internal trust stops being implicit. Same physics at every scale.

---

## 28. Undo for production

> **REAL CODE (v5.125): SHIPPED.** Every live binding is epoch-versioned with a bounded
> rollback history; a snapshot is a quotation; a config change is applied by hash and rolled
> back by the record it returned:
> ```js
> var h = comcon.bindAt(site, comcon.quote(v1), { imports: [] });
> h.replace(comcon.quote(v2));  h.epoch();  h.rollback();  h.describe();
> ops.snapshot("acme").source;  ops.rollback("acme");
> var applied = comcon.std.config.apply(plan, node, { confirm: [...] });  comcon.std.config.rollback(applied, node);
> ```
> Tests: `t/comcon_pom_mutate.t` (500 replacements, flat heap), `t/comcon_std_ops.t`,
> `t/comcon_config_instance.t`. Demos: `js_comcon_demos/O_Operators/O1`, `L_Live_Ops/L1`.

**Problem:** the config/tenant change was fine — until an hour later it wasn't.

**With COMCON** — bindings and subtrees are **epoch-versioned** (every administrative
change is a new epoch; the old one stays rollback-able until retired), and tenant state
is snapshot-able via app-registered serializers:

```
$ comconctl snapshot tenants/acme --label pre-campaign
$ ... an hour of trouble ...
$ comconctl rollback tenants/acme pre-campaign     # epoch revert; transient state drains
```

**The point:** "roll it back" is an epoch operation with a label, not an archaeology
session.

---

## 29. Documentation that can't lie

> **REAL CODE (v5.125): PARTIAL.** The registry the docs would be generated from exists and
> is checked against the code: `nginx.describe()` / `nginx.describeType(cls)` for every COM
> member with its safety class, `comcon.std.describe()` for which contract field is enforced
> by what (and which words are absent), `node.describe()` for a program view's read ops,
> `ops.trustReport().bindings[i].ops` per binding. **v5.127:** the per-tenant rendering is
> `comcon.std.docs.render(name, fragment)` / `ops.docs(name)` — a projection of the contract
> the binding carries, through the kernel's own grant translation, with the live epoch
> (`t/comcon_std_docs.t`; demo `A_Auditors/A3`). Tests: `t/js_com_describe.t`,
> `t/comcon_std_lib.t`, `t/comcon_v9_pom_describe.t`.

**Problem:** tenant docs say one thing; the deployed reality says another.

**With COMCON** — a tenant's docs are *generated from its effective policy* (the
`describe()` registry — every operation, its writability, its cost class):

```
$ comconctl docs acme
  # ACME — your available API (derived from your grants, epoch 2026-07-08)
  http.get(path: "/api/*")           — 5ms budget/request
  db.query(ParameterizedQuery only)  — table: orders (read)
  ...
```

If it's in the docs, it works; if it works, it's in the docs — both are projections of
the same descriptors.

**The point:** the reference manual is a *query*, perpetually current, per tenant.

---

## 30. The intern-proof deploy

> **REAL CODE (v5.125): SHIPPED.**
> ```js
> var feature = comcon.include(recommendV2, {
>     imports: [], grants: { catalog: comcon.mediate(srv, comcon.routes("/catalog/*")) },
>     meter: comcon.meter({ timeoutMs: 2 }) });
> ```
> The fragment's worst day is bounded by that contract: no `orders`, no `nginx`, no disk —
> unreachable, not reviewed. Demos: `js_comcon_demos/P_Platform_Teams/P1`, `P3`.

**Problem:** a junior dev's first change ships to production. Everyone holds their
breath.

**With COMCON** — the change lands in a fragment whose authority *is* the blast radius:

```js
const feature = env();
grant(feature, "catalog", readOnly(catalog));
include("./features/recommend-v2.js",
        acme.profiles.feature(feature, { exposeTo: ["renderer"],
                                         budgets: { cpu: "2ms" } }));
```

The worst possible bug in that fragment can mis-recommend products. It cannot touch
orders, payments, other tenants, or the disk — not by review, by *reachability*.

**The point:** code review checks quality; the cage bounds catastrophe. Breathing
normally resumes.

---

## 31. Authority with office hours

> **REAL CODE (v5.125): SHIPPED.**
> ```js
> var migrate = comcon.mediate(comcon.mediate(cap,
>     comcon.window({ days: "Sat", from: "02:00", to: "04:00" })),          // UTC
>     comcon.cosign({ key: "schema-migration", quorum: 2, within: 900, as: principal }));
> ```
> Outside the window: `cap.window`; inside, the first attempt records a consent and is denied
> (`cap.cosign`), the second distinct principal's attempt runs. Tests: `t/comcon_cap_window.t`,
> `t/comcon_cap_cosign.t`. Demo: `js_comcon_demos/S_Security_Teams/S2`.

**Problem:** risky operations (schema migrations, cache flushes) should only happen in
maintenance windows, with a second pair of eyes.

**With COMCON** — schedule and co-sign are mediations on the grant:

```js
grant(ops, "migrate",
      mediate(db.migrate, window("Sat 02:00–04:00 UTC"), cosign(["sre-lead"])));
```

Outside the window the capability *doesn't exist*; inside it, exercise requires the
co-signature. No process document — descriptor arithmetic.

**The point:** change-management policy becomes engine-enforced, not calendar-enforced.

---

## 32. The parallel universe (deception for defense)

> **REAL CODE (v5.125): NOT BUILT** (gap G-03, COW views). The containment that exists is
> posture and budget: the suspect binding can be re-included with `onViolation: "audit"` to
> watch, or its slot tombstoned (`ops.remove`) to stop it, and its denials are counted per
> gate meanwhile.

**Problem:** a tenant behaves suspiciously. Kill it and you lose the forensics; let it
run and you risk the platform.

**With COMCON** — COW views make a *shadow world* cheap:

```
$ comconctl shadow-world tenants/suspect --writes sandboxed --reads live
```

The suspect keeps running against live-looking reads while every write lands in a
private overlay nobody else sees. Forensics watches through the (opaque-respecting)
console.

**The point:** containment without tipping off — the suspect executes in a universe
that diverged the moment you got suspicious.

---

## 33. Selling compute on your edge

> **REAL CODE (v5.125): PARTIAL.** The isolation to sell exists (a slot is a fragment with
> `meter`), the counters exist for memory (`comcon.memStatus(f)`: invocations, retained,
> refused) and the compiled tier is real (`comcon.aotStatus(f)`; `objs_jit`). A CPU-time meter
> and invoice-grade counters are not built (gap G-07). Demos: `js_comcon_demos/P3`, `D2`.

**Problem:** partners want to run logic on your edge nginx fleet. Revenue opportunity;
terrifying operationally.

**With COMCON** — a rented slot is a caged fragment; **budgets are the meter**:

```js
include("market/partner-x.js", std.profiles.marketplace({
    budgets: { cpu: "2ms/req", memory: "8MB" },     // enforcement AND billing input
    grants:  { req: http.readonly_facet() }, exposeTo: ["responder"],
}));
```

The same counters that stop a runaway partner produce the invoice line items. And the
compiled tier is what makes the economics work: a typed marketplace fragment lowered to
C serves at near-stock throughput (`PERFORMANCE.md`), so the isolation you *sell*
doesn't eat the margin you *charge*.

**The point:** multi-tenancy strong enough to *sell* — the security model doubles as
the billing model.

---

## 34. Platform upgrades without hostage tenants

> **REAL CODE (v5.125): NOT BUILT as a version word** (gap G-17). What carries it in practice:
> grants are per fragment, so two tenants can hold two differently-mediated facets of the
> same node, and a library dependency is pinned by hash per fragment (`deps`), so a straggler
> keeps its pinned copy while the fleet moves.

**Problem:** js_com v2 ships breaking changes; tenant Y can't migrate this quarter.
Today that blocks the whole fleet's upgrade.

**With COMCON** — API version is part of the grant:

```js
grant(acme, "js_com", platform.api("v2"));
grant(y,    "js_com", platform.api("v1-compat"));   // pinned, deprecated
```

`comconctl diff` lists exactly which tenants still hold v1 grants — the migration's
burn-down chart is a query.

**The point:** the platform moves at the platform's pace; stragglers are pinned,
visible, and contained — not blocking.

---

## 35. The log that can't be un-written

> **REAL CODE (v5.125): NOT BUILT** (gap G-18: no log facet; `allow`/`redact` masks exist only
> on socket and server capabilities). The engine's own denial log is written by the host
> side, never by a fragment: 100 full records then a 1/100 sample, counters exact
> (`t/comcon_include_denial_log.t`).

**Problem:** audit logs are only as trustworthy as the code that *could* rewrite them.

**With COMCON** — append-only is a capability shape:

```js
grant(everyone, "audit", log.facet({ append: true, read: false, truncate: NEVER }));
```

No fragment on the platform — including the host program itself, if so configured —
holds a delete/rewrite capability on the audit stream; verification lives with an
external reader holding the read grant.

**The point:** log integrity by construction: the writer provably cannot read or
rewrite what it wrote.

---

## 36. The whole config is a program (finally, safely)

> **REAL CODE (v5.125): SHIPPED for the tenant half, PARTIAL for the builder.** The tenant's
> config is a proposal in a declarative sub-language, reviewed against the typed registry
> (members, arity), applied all-or-nothing and rolled back by hash:
> ```js
> var plan = comcon.std.config.review("acme.root('/srv/acme'); acme.proxy.pass('http://acme_backend');",
>     { type: "NginxLocation", root: "acme", allow: ["root", "proxy.*"], allowClass: ["safe"] });
> comcon.std.config.apply(plan, acmeLocation, { confirm: ["acme.proxy.pass"] });
> ```
> `"use comcon: name";` anchors are inert, queryable attributes (`t/comcon_pom_anchors.t`).
> The host's own stage-0 program (`root.js`) is trusted host JS; a builder profile that
> denies clock/RNG/I/O is not built (gap G-19). Tests: `t/comcon_config_instance.t`,
> `t/comcon_review_calls.t`. Demo: `js_comcon_demos/O_Operators/O1`.

**Problem:** `nginx.conf` templating grew into a fragile generator zoo — because config
*wants* to be a program, and raw programmability was too dangerous to grant.

**With COMCON** — config is a policy-governed program, and determinism is literal:
clock, RNG, and I/O are capabilities the builder profile simply doesn't grant:

```js
"use comcon: config_builder";        // anchor; the policy lives with the host
for (const region of regions)
    server({ listen: region.port, tls: certs.for(region), routes: standard(region) });
```

No clock, no randomness, no I/O ⇒ byte-reproducible builds, diffable output, testable
infra — and the generator provably can't do anything *but* emit config. (This is
js_com/pilgrim's config-as-COM story with the missing safety layer added.) And in v4
the symmetry runs all the way down: the *emitted config* is itself an admitted
fragment — sentences of a restricted, typed config grammar, checked against the same
schema that types the JS API (scenarios 46–47).

**The point:** infrastructure-as-code without the "what else can the code do?" anxiety
— and config that is governed like code, because it *is* code (bound to an empty
environment).

---

## 37. The audit is a query, not an interview

> **REAL CODE (v5.125): PARTIAL.** The report is a library verb over resources the session was
> handed — nothing ambient:
> ```js
> var ops = comcon.std.ops({ log: nginx.tenantDenials, learn: nginx.tenantLearning,
>                            mode: comcon.mode, bindings: true });
> ops.trustReport()   // {bindings: [{name, epoch, ops: [...]}], enforcedBy: [{field, by, effect}]}
> ops.bindings();  ops.denials();  comcon.std.describe()
> ```
> `describe()` names the two resources with no host spelling (`provenance`, `signing`) and
> reports the verbs they would enable as withheld, so the gap is checkable rather than
> invisible (gap G-16). Tests: `t/comcon_std_ops.t`. Demo: `js_comcon_demos/A_Auditors/A1`.

**Problem:** the annual security audit: weeks of interviews, spreadsheets, and hope.

**With COMCON** — the evidence pack is generated, and the first line is now a
*computation with a theorem behind it* (authority = the transitive closure `A*` of
grants from the root — SEMANTICS.md):

```
$ comconctl trust-report --scope tenants/acme
  authority:    A*(root grants) — reachability, machine-derived
  provenance:   maker chains — every policy & fragment, signed, dated, pinned by hash
  verification: allow/deny suites: 100% pass; declarative coverage: 96% verified static
  incidents:    denials last 90d: 12 (all deny-suite-known classes)
  delegations:  3 active grant chains (longest: 2 hops, all attenuated)
```

Every line is derived from descriptors and signatures the engine already maintains —
the auditor can re-run any of it.

**The point:** compliance stops being testimony and becomes reproducible computation.
*"Show me who can touch what"* has an answer that is exact, current, and checkable.

---

*Part I: `SHOWCASE.md` (1–7) · Part II: `SHOWCASE17.md` (8–17) · Part IV:
`SHOWCASE45.md` (38–45) · Design: `FOUNDATION.md`.*
