# COMCON in nginx — Look & Feel: Scenarios 1–7 (v3)

> **Status: illustrative, not normative.** This document conveys *intent and feel* to a
> general audience. Every code sample uses **hypothetical syntax** — names and APIs will
> change. The design lives in `FOUNDATION.md`; the formal guarantees in `SEMANTICS.md`.
> Reworked for v3: environments and grants instead of `imports:` blobs, `include` =
> parse∘admit∘bind, anchors/queries instead of inline policy literals, and the
> guarantees quoted here are now theorems, not intentions.

**COMCON in one paragraph:** nginx gains the ability to run JavaScript from *many
parties that don't trust each other* — tenants, vendors, contractors, AI — inside one
process, each piece of code caged by a *policy*. A policy states what a piece of code
may see, do, and say — and the engine enforces it, at compile time and at run time.
Policies are attached by the host (by query or by inert anchor), never by the code
being caged; one axiom — **no operation mints authority** — makes every inner policy
provably no more permissive than its outer one. And the discipline compiles away: a
fully-typed static policy lowers to C measured at **~96% of stock nginx**, while the
same policy interpreted runs at ~28% (`PERFORMANCE.md`).

---

## 1. Two tenants, one nginx — and neither can name the other

**Problem:** you want ACME's and Globex's request-processing scripts on the same nginx,
without a whisper of data or interference between them.

**Today:** separate nginx instances or "be careful" code review.

**With COMCON** — `nginx.conf` stays classic; one new directive:

```nginx
http {
    comcon_load /etc/nginx/comcon/root.js;
    ...
}
```

`root.js` is an ordinary stage-0 program the host runs with the root environment
(tenants never see it):

```js
import std    from "comcon:std-policies";   // a governed policy library
import js_com from "js_com";                 // host caps arrive as grants, never globals

// Each tenant is grafted in under a cage the HOST builds:
const acme = env();                                        // deny-by-default: empty
grant(acme, "http", mediate(js_com.http, routes("/acme/*")));   // a facet, not the API

include("/etc/nginx/tenants/acme/main.js",
        std.profiles.tenant(acme),          // include = parse ∘ admit ∘ bind
        { profile: "restrictive" });

const globex = env();
grant(globex, "http", mediate(js_com.http, routes("/globex/*")));
include("/etc/nginx/tenants/globex/main.js", std.profiles.tenant(globex),
        { profile: "restrictive" });
```

**The point:** the tenant writes plain JavaScript; the *host* attaches the cage at the
include. Inside a fragment, a name either resolves in its environment or is a
compile-time error — there is no ambient fallback. ACME's code cannot even *name*
Globex's objects: they are not merely forbidden, they are unresolvable.

---

## 2. Third-party libraries: caged on entry, revocable on CVE day

**Problem:** a tenant uses a popular utility library. Next month it ships a compromised
update (supply-chain attack), or a CVE lands.

**Today:** the library runs with the full authority of whoever calls it. Patch,
redeploy, pray.

**With COMCON:**

```js
// Computation only — no I/O, no globals, no eval, no network. And PINNED:
include("./vendor/magic-utils.js",
        std.profiles.pure_library({ exposeTo: ["acme/**"] }),
        { pin: "sha256:9f2c…" });     // a silently-changed file is refused at admission
```

CVE day — one command, no restart, no redeploy:

```
$ comconctl revoke --grant 'acme/vendor/magic-utils' --cascade
```

Revocation is a mediation flag flipping to *deny* — narrowing to zero, always
lattice-safe. Grant chains carry provenance, so everything the library ever delegated
onward dies with it; calls into it become clean policy denials the tenant can handle.

**The point:** a library runs with what *we granted*, not with what *it wants* — and
the grant is a switch we hold at run time. (The pin story continues in scenario 40.)

---

## 3. SQL injection: not detected — *unwritable*

**Problem:** the single most common web vulnerability class for thirty years.

**Today:** scanners, WAF signatures, code review, hope.

**With COMCON** — the database facet granted to tenants admits only the
*parameterized-query* grammar production (a policy defines its fragment's *language*):

```js
grant(acme, "db", js_com.db.facet({ language: "sql",
                                    syntax_allowed: ["ParameterizedQuery"] }));
```

Tenant code:

```js
db.query("SELECT * FROM users WHERE id = ?", [req.params.id]);   // ✓ compiles

db.query("SELECT * FROM users WHERE id = " + req.params.id);     // ✗ compile-time denial:
// DENIED by policy 'db-parameterized' (installed by host) at acme/orders.js:12
//   — production 'string-built query' is not an admissible sentence of this interface
```

**The point:** the vulnerable sentence is not *scanned for* — it **cannot be written**.
A whole vulnerability class removed by grammar, not by vigilance.

---

## 4. Header smuggling / response splitting: values become grammar, not string paste

**Problem:** user-controlled data flows into an HTTP header; a stray `\r\n` becomes a
second, attacker-authored response header.

**Today:** escaping discipline, and every team reinvents it.

**With COMCON** — crossing into a header context requires being a *complete, valid
symbol of the header grammar*, optionally with a verifier:

```js
resp.headers.set("X-Rate-Limit",
    header_value.number({ min: 0, max: 1_000_000 }).from(user_input));
    // not a number in range → denial; CRLF has no way in — there is no
    // "raw string paste" operation on this interface at all
```

**The point:** "remember to escape" is replaced by "the interface only accepts
well-formed values." Injection needs a hole; the hole is no longer part of the language.

---

## 5. Onboarding 5,000 lines of legacy script — without reading them

**Problem:** a years-old analytics script must move under policy. Nobody knows what it
actually touches.

**Today:** archaeology, or an over-broad policy that protects nothing.

**With COMCON** — learning mode, riding the declared failure semantics of the binding
(`onViolation: "audit"` — cage on, nothing blocked, denials logged):

```
$ comconctl learn acme/legacy-analytics --window 48h
  harvest: 14 imports, 3 exports, 41 grammar productions, 0 dynamic-eval uses

$ comconctl propose acme/legacy-analytics
  → candidate policy (declarative): grants { http.get, json.parse, log.info, ... }
  → allow-suite generated: 1,214 recorded cases
  → coverage: 91% of code paths — WARNING: error handlers never exercised

$ comconctl shadow acme/legacy-analytics      # bind with onViolation: "audit"
  ... one week, zero would-be denials ...

$ comconctl enforce acme/legacy-analytics     # after human review: onViolation: "deny"
```

**The point:** the cage is *derived from observed behavior*, comes with its own
regression tests, is trialed in shadow, and a human approves the final grant. Never
auto-deployed — learning describes, people prescribe.

---

## 6. AI as operator and author — safe by asymmetry

**Problem:** you want AI to help run the platform and write tenant code/policies,
without betting the platform on the AI being right.

**Today:** either no AI, or AI with your credentials.

**With COMCON** — the AI is just another caged actor. An ops session:

```js
const session = repl.open({
    actor:  "ai-assistant",
    lease:  "15m",                              // expires by itself
    policy: std.profiles.rel({                  // REL: a REPL with no "print secrets"
        read: ["metrics/**"], write: [], disclose: "handles_only",
    }),
});
```

An AI-proposed policy change arrives as a reviewable diff:

```
$ comconctl diff proposals/ai-tighten-acme.policy
  - grants.http: *
  + grants.http: { get: "/api/*" }     # narrowing only — verified monotone, auto-safe
```

**The point:** an AI-written policy can *deny too much* (an outage, caught in shadow
mode) but structurally **cannot grant too much** (never a breach) — that is the
No-Amplification theorem wearing work clothes. The general rule for every AI actor
here: **capabilities bound the damage; tests only bound the correctness.**

---

## 7. Secrets the code can use but never see

**Problem:** a tenant script needs an API key to call a backend — and API keys leak:
into logs, error messages, analytics, exceptions.

**Today:** audits of every log statement.

**With COMCON** — the key is granted as an *opaque* value with exactly one exit:

```js
grant(acme, "api_key",
      opaque.str(vault.get("acme-backend-key"), { pass_to: ["js_com.http.auth"] }));
```

Tenant code:

```js
http.auth(api_key);          // ✓ the one granted sink — works
console.log(api_key);        // ✗ denied: opaque values cannot be printed
const s = "" + api_key;      // ✗ denied: cannot concatenate into a plain string
JSON.stringify({api_key});   // ✗ denied: cannot be serialized
```

**The point:** exfiltration paths aren't *audited* — they **don't exist**. The code can
exercise the secret's power without ever holding its bytes.

---

## Epilogue: the cage is also the accelerator — now with numbers

A policy is a set of *declared guarantees*, and guarantees are what optimizers crave:
frozen intrinsics mean inline caches never invalidate (faster than stock JS); a
fragment that forbids `eval` skips whole categories of bookkeeping; and a
fully-described, typed fragment is a closed world that compiles (via **maxim**) to C.
This is no longer an argument: the same policy measured **96% of stock nginx compiled
vs 28% interpreted**, with a control proving the entire gap is the interpreter
(`PERFORMANCE.md`). **The narrower the policy, the faster the code** — security and
performance are the same declaration, read twice.

---

*Part II: `SHOWCASE17.md` (8–17) · Part III: `SHOWCASE37.md` (18–37) ·
Part IV (new in v3): `SHOWCASE45.md` (38–45) · Design: `FOUNDATION.md`.*
