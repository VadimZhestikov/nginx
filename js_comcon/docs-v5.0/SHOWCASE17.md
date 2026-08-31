# COMCON in nginx — Look & Feel, Part II: Scenarios 8–17 (v5.0)

> **Status: illustrative, not normative.** Continuation of `SHOWCASE.md` (1–7). All
> syntax is **hypothetical**; the design lives in `FOUNDATION.md` / `SEMANTICS.md`.

---

## 8. Resellers: your tenant becomes a host — cages nest for free

**Problem:** ACME (your tenant) resells to *its own* customers. ACME wants to cage
them; you don't want to know or care.

**Today:** either you manage everyone's isolation, or nobody does.

**With COMCON** — a tenant is code; a host is code; they are the same kind of code.
ACME simply does to its customers what you did to ACME:

```js
// inside ACME's own main.js — no involvement from the platform team:
const bobs = env();
grant(bobs, "http", mediate(acmeHttp, routes("/acme/bobs/*")));   // a slice of ACME's own slice
include("./customers/bobs-flowers.js", acme_policies.customer(bobs));
```

The laws of physics hold automatically — and in v3 they are literally a theorem
(No-Amplification, `SEMANTICS.md` §3): everything ACME grants its customer was
evaluated from ACME's own environment, so a sub-tenant can never be more powerful than
its reseller. Nobody has to review for that; it is not checkable behavior, it is
*unexpressible* behavior.

**The point:** multi-level tenancy is not a feature we built — it falls out of the
model. Every tenant is a potential host, and authority only shrinks downward. (For the
day ACME wants to *propose* more than it holds, see scenario 39.)

---

## 9. Partners that compute together without seeing each other

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
