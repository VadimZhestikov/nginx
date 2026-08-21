# COMCON in nginx — Look & Feel: Seven Illustrative Scenarios

> **Status: illustrative, not normative.** This document exists to convey *intent and
> feel* to a general audience. Every code sample uses **hypothetical syntax** — names,
> directives, and APIs will change. The design itself lives in `FOUNDATION.md`; nothing
> here is part of it.

**COMCON in one paragraph:** nginx gains the ability to run JavaScript from *many parties
that don't trust each other* — tenants, vendors, contractors, AI — inside one process,
each piece of code caged by a *policy*. A policy states what a piece of code may see, do,
and say — and the JavaScript engine itself enforces it, at compile time and at run time.
Policies are attached by the host, never by the code being caged; an inner policy can
only ever be *stricter* than its outer one.

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

`root.js` (host-owned; tenants never see it):

```js
`comcon: set_default_policies(std.profiles.host_root)`

import std    from "comcon:std-policies";
import js_com from "js_com";

// Each tenant is grafted in under a cage the HOST defines:
include js "acme" from("/etc/nginx/tenants/acme/main.js") policy(std.profiles.tenant({
    globals:    "none",
    imports:    { http: js_com.http.facet({ routes: "/acme/*" }) },
    exports_to: [],                       // ACME exports to nobody
    eval:       "deny",
}));

include js "globex" from("/etc/nginx/tenants/globex/main.js") policy(std.profiles.tenant({
    imports:    { http: js_com.http.facet({ routes: "/globex/*" }) },
    ...
}));
```

**The point:** the tenant writes plain JavaScript; the *host* attaches the cage at the
include. ACME's code cannot even *name* Globex's objects — they are not merely
forbidden, they are invisible.

---

## 2. Third-party libraries: caged on entry, revocable on CVE day

**Problem:** a tenant uses a popular utility library. Next month it ships a compromised
update (supply-chain attack), or a CVE lands.

**Today:** the library runs with the full authority of whoever calls it. Patch, redeploy,
pray.

**With COMCON:**

```js
// Library gets computation only — no I/O, no globals, no eval, no network:
include js "acme/vendor/magic-utils" from("./vendor/magic-utils.js")
    policy(std.profiles.pure_library({ imports: {}, exports_to: ["acme/**"] }));
```

CVE day — one command, no restart, no redeploy:

```
$ comconctl revoke --grant 'acme/vendor/magic-utils' --cascade
```

Every capability the library ever held — including anything it delegated onward — dies
instantly; calls into it become clean policy denials the tenant can handle.

**The point:** a library runs with what *we granted*, not with what *it wants* — and the
grant is a switch we hold at run time.

---

## 3. SQL injection: not detected — *unwritable*

**Problem:** the single most common web vulnerability class for thirty years.

**Today:** scanners, WAF signatures, code review, hope.

**With COMCON** — the database facet granted to tenants admits only the
*parameterized-query* grammar production:

```js
imports: {
    db: js_com.db.facet({ language: "sql", syntax_allowed: ["ParameterizedQuery"] }),
}
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
second, attacker-authored response header (the classic nginx variable-injection family).

**Today:** escaping discipline, and every team reinvents it.

**With COMCON** — crossing into a header context requires being a *complete, valid symbol
of the header grammar*, optionally with a verifier:

```js
resp.headers.set("X-Rate-Limit",
    header_value.number({ min: 0, max: 1_000_000 }).from(user_input));
    // not a number in range → denial; CRLF has no way in — there is no
    // "raw string paste" operation on this interface at all
```

**The point:** "remember to escape" is replaced by "the interface only accepts well-formed
values." Injection needs a hole; the hole is no longer part of the language.

---

## 5. Onboarding 5,000 lines of legacy script — without reading them

**Problem:** a years-old analytics script must move under policy. Nobody knows what it
actually touches.

**Today:** archaeology, or an over-broad policy that protects nothing.

**With COMCON** — learning mode:

```
$ comconctl learn acme/legacy-analytics --window 48h
  harvest: 14 imports, 3 exports, 41 grammar productions, 0 dynamic-eval uses

$ comconctl propose acme/legacy-analytics
  → candidate policy (declarative): imports { http.get, json.parse, log.info, ... }
  → allow-suite generated: 1,214 recorded cases
  → coverage: 91% of code paths — WARNING: error handlers never exercised

$ comconctl shadow acme/legacy-analytics      # cage on, denials logged, nothing blocked
  ... one week, zero would-be denials ...

$ comconctl enforce acme/legacy-analytics     # after human review of the diff
```

**The point:** the cage is *derived from observed behavior*, comes with its own regression
tests, is trialed in shadow, and a human approves the final grant. Never auto-deployed —
learning describes, people prescribe.

---

## 6. AI as operator and author — safe by asymmetry

**Problem:** you want AI to help run the platform and write tenant code/policies, without
betting the platform on the AI being right.

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
  - imports.http: *
  + imports.http: { get: "/api/*" }     # narrowing only — verified monotone, auto-safe
```

**The point:** an AI-written policy can *deny too much* (an outage, caught in shadow mode)
but structurally **cannot grant too much** (never a breach). That asymmetry is what makes
industrializing AI operations sane.

---

## 7. Secrets the code can use but never see

**Problem:** a tenant script needs an API key to call a backend — and API keys leak: into
logs, error messages, analytics, exceptions.

**Today:** audits of every log statement.

**With COMCON** — the key is granted as an *opaque* value with exactly one exit:

```js
imports: {
    api_key: opaque.str(vault.get("acme-backend-key"), { pass_to: ["js_com.http.auth"] }),
}
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

## Epilogue: the cage is also the accelerator

Counterintuitive but true by design: a policy is a set of *declared guarantees*, and
guarantees are what optimizers crave. Frozen prototypes mean the engine's caches never
invalidate (faster than stock JavaScript); a fragment that forbids `eval` skips whole
categories of bookkeeping; and a fully-described fragment is a closed world that compiles
to C-grade native code (the follow-on **Maxim** project). **The narrower the policy, the
faster the code** — security and performance are the same declaration, read twice.
