# COMCON in nginx — Look & Feel, Part II: Scenarios 8–17 (v5.0)

> **Status: illustrative, not normative.** Continuation of `SHOWCASE.md` (1–7). All
> syntax is **hypothetical**; the design lives in `FOUNDATION.md` / `SEMANTICS.md`.
> **Since v5.126 every scenario opens with a `REAL CODE` block:** what the shipped tree does today for that scenario, the tests that pin it, and the gap id (`SHOWCASE-gaps.md`) where the sample and the tree differ. The samples below it are the original hypothetical syntax, kept as written.

---

## 8. Resellers: your tenant becomes a host — cages nest, and now authoring does too

> **REAL CODE (v5.125): SHIPPED** — the banner below records the measurement and the build.
> Runnable: `js_comcon_demos/P_Platform_Teams/P4_Reseller_authors_subfragments` (a reseller
> admits two sub-fragments, narrows a copied socket wrapper, is refused on widening and on
> the third slot).

> **MEASURED 2026-09-14 (v5.98), then BUILT 2026-09-15 (v5.106–v5.107).** The first
> measurement said the heading was half true: ATTENUATION nested without limit (a host
> could chain `allow(port,address)` → `allow(port)` → `uses()` three levels deep), but
> AUTHORING did not nest at all — `comcon` read `undefined` inside a fragment, and the
> S6 gate asserted that as CLOSED, by design. The code sample this section used to show
> was the half that did not exist.
>
> It exists now, and not the way the sample wrote it. The kernel operators are still
> withheld from a fragment (INCREMENT_MLIB §4's "raw operators withheld" stands); what a
> fragment can hold is ONE mediated capability, `comcon.author({subFragments: N})`, with
> ONE operation, `author.include(source, contract)`. The contract carries `imports`
> (mandatory: admission is not optional for a sub-tenant), `grants` — the reseller's OWN
> wrappers, copied — and `attenuate` — plain data words (`allow`, `redact`,
> `ttlSeconds`), because a fragment has no `comcon.*` producers. Pinned by
> `t/comcon_author_basic.t`, `t/comcon_author_regrant.t` and the depth-2 battery
> `t/comcon_author_depth2_gate.t` (every S6 probe answers at depth 2 as at depth 1).

**Problem:** ACME (your tenant) resells to *its own* customers. ACME wants to cage
them; you don't want to know or care.

**Today:** either you manage everyone's isolation, or nobody does.

**With COMCON** — a tenant is code; a host is code; they are the same kind of code.
The platform grants ACME an author capability once; from then on ACME does to its
customers what you did to ACME:

```js
// the platform, once: ACME may hold up to 50 sub-tenants at once (drop one, get the slot back)
var acme = comcon.include(acmeSource, {
    imports: [],
    grants: { http:   comcon.mediate(srv, comcon.routes('/acme/*')),
              sock:   comcon.mediate(sock, comcon.allow(['address', 'port'])),
              author: comcon.author({ subFragments: 50 }) } });

// inside ACME's own fragment — no involvement from the platform team:
var bobs = author.include(bobsSource, {
    imports: [],
    grants:   { http: http, sock: sock },            // ACME's own slice, copied
    attenuate: { sock: { redact: ['port'], ttlSeconds: 3600 } } });   // and narrowed
```

Two honest limits of the built thing. A route facet has no meet on the host either, so
`http` reaches Bob's Flowers as ACME holds it (`/acme/*`), not as `/acme/bobs/*` — the
narrowing ACME can express is the socket's fields and the lifetime. And a sub-tenant of
Bob's is not a thing: the depth is capped at two, and an author capability is not itself
re-grantable.

The laws of physics hold automatically — and they are literally a theorem
(No-Amplification, `SEMANTICS.md` §3, now with the nested step written down): everything
ACME grants its customer is a COPY of something ACME held, narrowed — never a fresh
wrapper minted from a handle, so a sub-tenant of a reseller whose socket was closed
holds a stale copy, not a laundered live one. A sub-tenant can never be more powerful
than its reseller. Nobody has to review for that; it is not checkable behavior, it is
*unexpressible* behavior.

**The point:** multi-level tenancy is not a feature we built — it falls out of the
model. Every tenant is a potential host, and authority only shrinks downward. (For the
day ACME wants to *propose* more than it holds, see scenario 39.)

---

## 9. Partners that compute together without seeing each other

> **REAL CODE (v5.125): NOT BUILT** (gap G-06: there are no `expose`/`accept` communication
> edges between fragments). The nearest shipped shape is host-brokered: both parties are
> fragments of one host, which passes JSON between them and grants each a mediated
> capability the other never holds; a reseller can also author a partner as a sub-fragment
> under copies of its own wrappers, narrowed (`t/comcon_author_regrant.t`):
> ```js
> var risk  = comcon.include(paymentsSource, { imports: [] });
> var shop  = comcon.include(retailerSource, { imports: [] });
> var total = shop({ phase: "total", basket: basket }).order_total;   // the basket stays here
> var ok    = risk({ order_total: total });                           // the provider sees a number
> ```

**Problem:** two tenants — a retailer and a payments provider — must cooperate per
request, but each considers its logic and data proprietary.

**Today:** a REST hop between separate deployments, latency, and a contract nobody can
enforce technically.

**With COMCON** — a negotiated exchange, both sides consenting, in-process:

```js
// Retailer's policy — what it agrees to SHOW:
expose(retailer, "payments-pro", { order_total: "read", basket: "opaque" });

// Payments provider's policy — what it agrees to ACCEPT:
accept(paymentsPro, "retailer", { order_total: "read" });   // basket refused:
                                                            // an unwanted grant is a liability
```

The retailer never sees the provider's risk model; the provider never sees the basket.
Each side's *refusal* is as binding as each side's grant — communication edges need
both signatures.

**The point:** cooperation without disclosure, enforced by the engine — the technical
form of a data-processing agreement. "Mutual protection" is the product, not a slogan.

---

## 10. The noisy neighbor, silenced by arithmetic

> **REAL CODE (v5.125): SHIPPED**, with the consequence fixed by the engine rather than the
> policy: a deadline abort is *uncatchable inside the fragment* (a tenant cannot catch its own
> deadline — finding F16), an allowance overrun is an ordinary exception, and a retained-memory
> overrun refuses the next call:
> ```js
> comcon.include(src, { imports: [], meter: comcon.meter({ timeoutMs: 5, memoryBytes: 16777216,
>                                                          retainedBytes: 8388608 }) });
> comcon.memStatus(f)        // {retained, invocations, refused, cap}
> ```
> Tests: `t/comcon_fragment_deadline.t`, `t/comcon_fragment_memory.t`,
> `t/comcon_retained_memory.t`, `t/comcon_compiled_resource_gates.t`. Demo:
> `js_comcon_demos/P_Platform_Teams/P3`. Not built: `cpu` and `compile` units, the `gas`
> instruction count (gap G-07).

**Problem:** one tenant's accidental infinite loop or memory balloon takes down every
tenant on the worker.

**Today:** process-level limits at best — one bad tenant still starves its neighbors
inside the process.

**With COMCON** — budgets are metering mediations, part of the cage:

```js
std.profiles.tenant(acme, {
    budgets: { cpu: "5ms/request", memory: "16MB", compile: "50ms" },
    on_exhausted: "catchable",     // the TENANT may handle it gracefully...
})                                 // ...but cannot ignore it — the meter is the engine's
```

A runaway tenant gets a clean, catchable "budget exhausted" in *its own* error channel;
its neighbors never notice anything happened. (Engine substrate: the gas/interrupt
machinery of `HARDENING.md` S5.)

**The point:** fairness is not scheduling folklore — it's a per-tenant meter with a
policy-defined consequence.

---

## 11. One parser to rule every format

> **REAL CODE (v5.125): NOT BUILT** (gap G-02). One derived parser exists —
> `comcon.reviewDeclarative(text)` accepts exactly the declarative sub-language of JS
> (fluent call chains, literal arguments) and returns a diffable descriptor table — and it is
> the checker under `comcon.std.config.review` (`t/comcon_declarative.t`,
> `t/comcon_config_instance.t`). JSON is an intrinsic a fragment may name in `imports`.

**Problem:** every config/format in the stack — JSON, YAML-ish files, header values,
custom DSLs — has its own hand-written parser and its own CVE history.

**Today:** N parsers, N validators, N sets of bugs.

**With COMCON** — one engine, N policies. A format *is* a policy on the grammar:

```js
const config  = eval(text, policies.json);              // JSON.parse ≡ eval under json_policy
const routes  = eval(text, policies.routes_dsl);        // custom DSL = a stricter policy
const retry   = eval(text, policies.number({min:0,max:10}));  // one header value
```

Each policy admits only its format's productions — a "JSON" that contains a function
call simply fails to parse, because under `policies.json` a function call is not a
sentence.

**The point:** we stop *writing* parsers and start *deriving* them. Hardening effort
concentrates on one engine instead of scattering across N validators.

---

## 12. Capabilities with a fuse: one-shot, leased, expiring

> **REAL CODE (v5.125): SHIPPED, with one difference in shape.** The fuse words compose and
> only narrow:
> ```js
> comcon.mediate(reset,   comcon.protocol("request"))                    // one-shot: after the
>                                                                          //   last step the
>                                                                          //   conversation is over
> comcon.mediate(approve, comcon.ttl(600))                               // expires; MIN composes
> comcon.mediate(comcon.mediate(console, comcon.allow(["address","port"])), comcon.ttl(14400))
> comcon.mediate(cap, comcon.uses("approve:acme", 1, 600))               // a fleet-wide budget
> ```
> `uses(n)` is a *fleet-wide, keyed, fixed-window* budget rather than a per-wrapper counter;
> the per-wrapper one-shot is `protocol` with a single bare step. `delegable("no")` is
> structural: an author descriptor is not re-grantable and every re-grant is a copy.
> Tests: `t/comcon_cap_ttl.t`, `t/comcon_budget_uses.t`, `t/comcon_cap_protocol.t`. Demos:
> `js_comcon_demos/S_Security_Teams/S1`, `S2`.

**Problem:** a password-reset action, a one-time payment approval, a contractor who
needs access "just for the afternoon."

**Today:** tokens that outlive their purpose, and cleanup jobs that forget.

**With COMCON** — expiry is a mediation on the grant, not a chore:

```js
grant(session,    "reset_password",  mediate(reset,   uses(1)));           // dies on first use
grant(partner,    "approve_payment", mediate(approve, uses(1), ttl("10m"))); // whichever first
grant(contractor, "debug_console",   mediate(console, ttl("4h"),
                                             delegable("no"), readOnly()));
```

Nothing to revoke later — the fuse burns by itself. And for the emergency case,
everything remains revocable *now*, cascade included.

**The point:** authority with a built-in end. The default question flips from "who
still has access?" to "nothing outlives its purpose — prove otherwise."

---

## 13. Open-heart surgery: debugging production without bleeding secrets

> **REAL CODE (v5.125): NOT BUILT** (gap G-04: no `attach`, no REPL over a worker, no
> forensics profile). What an operator can read today is data, per worker, through host
> handlers: `nginx.tenantDenials()`, `nginx.tenantLearning()`, `comcon.memStatus(f)`,
> `comcon.aotStatus(f)`, `ops.trustReport()`, and a fragment's program tree through
> `comcon.pom(f)`, whose reads return quotations and whose `binding` is redacted by default
> (`t/comcon_pom_nodeview.t`). Demo: `js_comcon_demos/O_Operators/O2`.

**Problem:** an incident on a live worker. You need to look inside *now* — but a debug
console on production is traditionally a master key.

**Today:** either no introspection, or an SSH session that sees everything.

**With COMCON** — the debug console is just another caged actor, holding *redacted*
handles (mediate's redact flavor: you see interfaces and facts, never bodies or
payloads):

```
$ comconctl attach worker:3 --as sre-oncall --profile std.profiles.forensics
# forensics profile: read-only; tenant payloads visible as opaque handles;
# constant-response-time REL — the session cannot leak by timing either
comcon(worker:3)> tenants.acme.stats.requests_1m
  42_117
comcon(worker:3)> tenants.acme.last_request.body
  [opaque:handle #a91f]        // you see THAT it is, not WHAT it is
```

**The point:** production introspection stops being a trust decision about the human
and becomes a policy decision about the session. Dev-tools for a running nginx — with
the engine holding the scalpel.

---

## 14. Config changes that rehearse before they perform

> **REAL CODE (v5.125): SHIPPED for the rollout, PARTIAL for the diff.** Shadow is a
> per-binding word or a fleet switch that reaches every worker; the denial counters survive
> the switch, which is what makes the evidence usable:
> ```js
> var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode });
> ops.shadow();   ops.denials();   ops.enforce();          // audit → read → enforce
> var candidate = comcon.include(src, { …, onViolation: "audit" });   // one binding shadowed
> ```
> A *config* proposal has a real diff (`comcon.std.config.diff(plan, node)`), and **since
> v5.127 so does a policy:** `comcon.std.policy.diff(current, candidate)` answers `narrowing`
> (auto-safe), `widening`, `unchanged` or `incomparable` with every change named, and
> `ops.wouldDeny(binding)` is the would-deny list of one shadowed binding. Tests:
> `t/comcon_std_policy_diff.t`, `t/comcon_would_deny.t`, `t/comcon_std_ops.t`,
> `t/comcon_posture.t`. Demos: `js_comcon_demos/O_Operators/O3`, `P_Platform_Teams/P2`.

**Problem:** a policy or config tightening looks right, but production traffic is the
only honest reviewer.

**Today:** deploy and watch the error rate.

**With COMCON** — failure semantics are declared per rule, so every change can run in
**shadow** first (`onViolation: "audit"`), on real traffic:

```
$ comconctl diff v2/acme.policy
  - grants.http: { get: "/api/*", post: "/api/*" }
  + grants.http: { get: "/api/*" }                   # POST removed — narrowing, auto-safe

$ comconctl shadow acme --candidate v2/acme.policy
  ... 24h of live traffic ...
  would-deny: 3 events, all from cron-job 'legacy-sync' (POST /api/flush)

$ comconctl enforce acme v2/acme.policy --after-fixing legacy-sync
```

The diff *predicted* the only breakage; shadow *confirmed* it on real traffic;
enforcement happened with zero surprises.

**The point:** policy changes get the safety workflow code always had — diff, dry-run,
canary — because policies *are* code.

---

## 15. The iRule that moved in without renovating

> **REAL CODE (v5.125): NOT BUILT** (gap G-08: a fragment's language is JavaScript; Tcl and
> WASM are M9 / stage-2 design). The cage an iRule would get is the one every fragment gets
> today: `comcon.include(src, { grants, meter })`.

**Problem:** years of BIG-IP iRules (Tcl) embody real business logic; rewriting them
all in JavaScript is the reason migrations stall.

**Today:** big-bang rewrites, or two platforms forever.

**With COMCON** — the fragment's *language* is a parameter of the policed include:

```js
include("./irules/blacklist.tcl",
        std.profiles.irule({                 // the Tcl interpreter runs INSIDE the cage
            grants:   { HTTP: js_com.http.facet({ read: ["uri", "headers"] }) },
            exposeTo: ["router"],
            budgets:  { cpu: "1ms/request" },
        }),
        { language: "tcl", profile: "restrictive" });
```

The iRule runs unmodified — but caged exactly like a JS tenant: same grants, same
budgets, same revocation switch, same audit trail.

**The point:** migration becomes *adoption*, not rewrite. Legacy code doesn't block the
new security model — it gets enrolled in it, file by file.

---

## 16. One shared table, a thousand private views

> **REAL CODE (v5.125): NOT BUILT** (gap G-03: COW views need the opaque/COW engine substrate,
> unscheduled). The shared truth a fragment can read is a route facet over a server node
> (`comcon.mediate(srv, comcon.routes("/acme/*"))` → `paths()`, `allowed(p)`), and the
> fleet-wide store is `nginx.shared` on the host side; neither has a private overlay.

**Problem:** tenants and plugins all consult the shared routing table — and each wants
a few private tweaks. Copying the table per tenant explodes memory; sharing it mutable
is an incident waiting to happen.

**Today:** pick your poison — copies or lock discipline.

**With COMCON** — copy-on-write views are the native sharing model:

```js
grant(acme, "routes", shared.routing_table.view({ base: "read", overlay: "private" }));
```

```js
routes.get("/api/*");            // reads the shared truth — zero copy
routes.set("/api/v2/*", myPool); // ✓ lands in MY overlay — invisible to everyone else
```

A thousand tenants pay memory only for the entries they actually changed; the shared
base stays immutable under everyone's feet — same object identity, different views, by
the engine.

**The point:** "personalized views of shared state" without copies and without locks —
the memory bill is proportional to actual disagreement.

---

## 17. Code that travels with its cage

> **REAL CODE (v5.125): PARTIAL.** The traveling artifact's *properties* exist: a quotation
> is cap-free by construction (a function or a capability in its splices is refused at
> `quote()` time), a fragment is pinned by `identity`, a dependency by SHA-256, and the only
> thing that crosses between workers in a live rebind is source text
> (`t/comcon_pom_splice.t`, `t/comcon_include_admit.t`, `t/comcon_pom_fanout.t`):
> ```js
> var q = comcon.quote(validatorSource, { LIMITS: { max: 50 } });   // inert, frozen, cap-free
> var f = comcon.realize(q, { imports: [], identity: pin }, localEnv);   // authority enters HERE
> ```
> Not built: the signed manifest, `export`, and a ServiceWorker target (gap G-09).

**Problem:** the same validation/personalization logic is needed at nginx *and* in the
browser (or an edge node) — today that's two implementations drifting apart.

**Today:** duplicate code, or trust the client.

**With COMCON** — a fragment is code **plus policy plus tests plus signature** (its
manifest), and it ships as a **quotation** — which is *provably authority-free in
transit* (quotations are cap-free by construction; there is nothing inside to steal or
smuggle):

```
$ comconctl export acme/validators --target service-worker --sign
  → validators.frag  (quotation + policy + allow/deny suites + maker chain + content hash)
```

The receiving side — browser ServiceWorker, edge nginx, another data center — admits
the fragment through the same pipeline as any tenant: verify signature and **pin
(content hash)** → `admit` under the *local* host's contract → bind caged. Authority
enters only at the destination's `grant`s; the traveling code carries none of its own.

**The point:** mobile code stops being scary when the cage travels with it — and the
destination always holds the narrower key. One implementation, many placements,
provable containment everywhere.

---

*Part I: `SHOWCASE.md` (1–7) · Part III: `SHOWCASE37.md` (18–37) · Part IV:
`SHOWCASE45.md` (38–45) · Design: `FOUNDATION.md`.*
