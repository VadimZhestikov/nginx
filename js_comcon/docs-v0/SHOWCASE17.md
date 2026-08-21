# COMCON in nginx — Look & Feel, Part II: Scenarios 8–17

> **Status: illustrative, not normative.** Continuation of `SHOWCASE.md` (scenarios 1–7).
> All syntax is **hypothetical** — it conveys intent and feel for a general audience;
> the design itself lives in `FOUNDATION.md`.

---

## 8. Resellers: your tenant becomes a host — cages nest for free

**Problem:** ACME (your tenant) resells to *its own* customers. ACME wants to cage them;
you don't want to know or care.

**Today:** either you manage everyone's isolation, or nobody does.

**With COMCON** — a tenant is code; a host is code; they are the same kind of code. ACME
simply does to its customers what you did to ACME:

```js
// inside ACME's own main.js — no involvement from the platform team:
include js "acme/customers/bobs-flowers"
    from("./customers/bobs-flowers.js")
    policy(acme_policies.customer({ routes: "/acme/bobs/*" }));
```

The laws of physics hold automatically: everything ACME grants its customer is **at most
what ACME itself holds** — a sub-tenant can never be more powerful than its reseller. No
platform-team review needed for that guarantee; the engine enforces it.

**The point:** multi-level tenancy is not a feature we built — it falls out of the model.
Every tenant is a potential host, and authority can only shrink downward.

---

## 9. Partners that compute together without seeing each other

**Problem:** two tenants — a retailer and a payments provider — must cooperate per
request, but each considers its logic and data proprietary.

**Today:** a REST hop between separate deployments, latency, and a contract nobody can
enforce technically.

**With COMCON** — a negotiated exchange, both sides consenting, in-process:

```js
// Retailer's policy — what it agrees to SHOW:
exports_to: { "payments-pro": { order_total: "read", basket: "opaque" } }

// Payments provider's policy — what it agrees to ACCEPT:
imports:    { retailer: { order_total: "read" } }     // basket refused: don't want the liability
```

The retailer never sees the provider's risk model; the provider never sees the basket
contents. Each side's *refusal* is as binding as each side's grant.

**The point:** cooperation without disclosure, enforced by the engine — the technical form
of a data-processing agreement. "Mutual protection" is the product, not a slogan.

---

## 10. The noisy neighbor, silenced by arithmetic

**Problem:** one tenant's accidental infinite loop or memory balloon takes down every
tenant on the worker.

**Today:** process-level limits at best — one bad tenant still starves its neighbors
inside the process.

**With COMCON** — budgets are part of the cage:

```js
policy(std.profiles.tenant({
    budgets: { cpu: "5ms/request", memory: "16MB", compile: "50ms" },
    on_exhausted: "catchable",       // the TENANT may handle it gracefully...
}))                                  // ...but cannot ignore it — the meter is the engine's
```

A runaway tenant gets a clean, catchable "budget exhausted" in *its own* error channel;
its neighbors never notice anything happened.

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

Each policy admits only its format's productions — a "JSON" that contains a function call
simply fails to parse, because under `policies.json` a function call is not a sentence.

**The point:** we stop *writing* parsers and start *deriving* them. Hardening effort
concentrates on one engine instead of scattering across N validators.

---

## 12. Capabilities with a fuse: one-shot, leased, expiring

**Problem:** a password-reset action, a one-time payment approval, a contractor who needs
access "just for the afternoon."

**Today:** tokens that outlive their purpose, and cleanup jobs that forget.

**With COMCON** — expiry is a property of the grant, not a chore:

```js
grant(reset_password, { to: session, uses: 1 });                  // dies on first use
grant(approve_payment, { to: partner, uses: 1, ttl: "10m" });     // whichever comes first
grant(debug_console,   { to: contractor, ttl: "4h",
                         delegable: "no", attenuated: "read_only" });
```

Nothing to revoke later — the fuse burns by itself. And for the emergency case, everything
remains revocable *now*, cascade included.

**The point:** authority with a built-in end. The default question flips from "who still
has access?" to "nothing outlives its purpose — prove otherwise."

---

## 13. Open-heart surgery: debugging production without bleeding secrets

**Problem:** an incident on a live worker. You need to look inside *now* — but a debug
console on production is traditionally a master key.

**Today:** either no introspection, or an SSH session that sees everything.

**With COMCON** — the debug console is just another caged actor:

```
$ comconctl attach worker:3 --as sre-oncall --profile std.profiles.forensics
# forensics profile: read-only; tenant payloads visible as opaque handles;
# constant-response-time REL — the session cannot leak by timing either
comcon(worker:3)> tenants.acme.stats.requests_1m
  42_117
comcon(worker:3)> tenants.acme.last_request.body
  [opaque:handle #a91f]        // you see THAT it is, not WHAT it is
```

**The point:** production introspection stops being a trust decision about the human and
becomes a policy decision about the session. Dev-tools for a running nginx — with the
engine holding the scalpel.

---

## 14. Config changes that rehearse before they perform

**Problem:** a policy or config tightening looks right, but production traffic is the only
honest reviewer.

**Today:** deploy and watch the error rate.

**With COMCON** — every policy change can run in **shadow** first, on real traffic:

```
$ comconctl diff v2/acme.policy
  - imports.http: { get: "/api/*", post: "/api/*" }
  + imports.http: { get: "/api/*" }                   # POST removed — narrowing, auto-safe

$ comconctl shadow acme --candidate v2/acme.policy
  ... 24h of live traffic ...
  would-deny: 3 events, all from cron-job 'legacy-sync' (POST /api/flush)

$ comconctl enforce acme v2/acme.policy --after-fixing legacy-sync
```

The diff *predicted* the only breakage; shadow *confirmed* it on real traffic; enforcement
happened with zero surprises.

**The point:** policy changes get the safety workflow code always had — diff, dry-run,
canary — because policies *are* code.

---

## 15. The iRule that moved in without renovating

**Problem:** years of BIG-IP iRules (Tcl) embody real business logic; rewriting them all
in JavaScript is the reason migrations stall.

**Today:** big-bang rewrites, or two platforms forever.

**With COMCON** — the fragment's *language* is part of its policy:

```js
include tcl "legacy/rule-blacklist"
    from("./irules/blacklist.tcl")
    policy(std.profiles.irule({           // the Tcl interpreter runs INSIDE the cage
        imports:    { HTTP: js_com.http.facet({ read: ["uri", "headers"] }) },
        exports_to: ["router"],
        budgets:    { cpu: "1ms/request" },
    }));
```

The iRule runs unmodified — but caged exactly like a JS tenant: same grants, same budgets,
same revocation switch, same audit trail.

**The point:** migration becomes *adoption*, not rewrite. Legacy code doesn't block the
new security model — it gets enrolled in it, file by file.

---

## 16. One shared table, a thousand private views

**Problem:** tenants and plugins all consult the shared routing table — and each wants a
few private tweaks. Copying the table per tenant explodes memory; sharing it mutable is an
incident waiting to happen.

**Today:** pick your poison — copies or lock discipline.

**With COMCON** — copy-on-write views are the native sharing model:

```js
imports: { routes: shared.routing_table.view({ base: "read", overlay: "private" }) }
```

```js
routes.get("/api/*");            // reads the shared truth — zero copy
routes.set("/api/v2/*", myPool); // ✓ lands in MY overlay — invisible to everyone else
```

A thousand tenants pay memory only for the entries they actually changed; the shared base
stays immutable under everyone's feet — same object, different views, by the engine.

**The point:** "personalized views of shared state" without copies and without locks —
the memory bill is proportional to actual disagreement.

---

## 17. Code that travels with its cage

**Problem:** the same validation/personalization logic is needed at nginx *and* in the
browser (or an edge node) — today that's two implementations drifting apart.

**Today:** duplicate code, or trust the client.

**With COMCON** — a fragment is code **plus policy plus tests plus signature** (its
manifest). That whole unit is what ships:

```
$ comconctl export acme/validators --target service-worker --sign
  → validators.frag  (code + policy + allow/deny suites + maker chain)
```

The receiving side — browser ServiceWorker, edge nginx, another data center — admits the
fragment through the same pipeline as any tenant: verify signature → compile under the
*local* host's policy → run caged. The traveling code never gets more authority than the
destination grants, no matter what it was granted at home.

**The point:** mobile code stops being scary when the cage travels with it — and the
destination always holds the narrower key. One implementation, many placements,
provable containment everywhere.

---

*Part I (scenarios 1–7): `SHOWCASE.md`. Design: `FOUNDATION.md`.*
