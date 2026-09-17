# COMCON in nginx — Look & Feel: Scenarios 1–7 (v5.0)

> **Status: illustrative, not normative.** This document conveys *intent and feel* to a
> general audience. Every code sample uses **hypothetical syntax** — names and APIs will
> change. The design lives in `FOUNDATION.md`; the formal guarantees in `SEMANTICS.md`.
> Reworked for v3: environments and grants instead of `imports:` blobs, `include` =
> parse∘admit∘bind, anchors/queries instead of inline policy literals, and the
> guarantees quoted here are now theorems, not intentions.
> **Since v5.126 every scenario opens with a `REAL CODE` block:** what the shipped tree does today for that scenario, the tests that pin it, and the gap id (`SHOWCASE-gaps.md`) where the sample and the tree differ. The samples below it are the original hypothetical syntax, kept as written.

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

> **REAL CODE (v5.125): SHIPPED, spelled differently.** There is no `comcon_load` directive
> — `nginx.conf` gains only `js_source root.js;` and the root program does the rest. The
> route facet is a real mediation over a COM server node, and the tenant profile derives
> `imports` from the environment so the manifest and the grants cannot drift:
> ```js
> var srv = nginx.http.servers[0];
> var acme = comcon.grant(comcon.env(), "http", comcon.mediate(srv, comcon.routes("/acme/*")));
> var acmeFrag = comcon.include(acmeSource, comcon.std.profiles.tenant(acme));   // parse∘admit∘bind
> locs.find(function (l) { return l.path === "/acme"; }).handler = function (req) {
>     var o = acmeFrag({ uri: req.uri }); req.respond(o.status, o.headers, o.body); };
> ```
> Inside, `http.paths()` / `http.allowed("/acme/x")` answer for the facet's glob and nothing
> else; a free name is refused at config load (`E_ADMIT_FREENAME`). Two tenants share one
> compartment context with frozen intrinsics, and `t/comcon_cross_identity.t` plants on every
> shared object and reads next door to assert no channel. Tests: `t/comcon_com_facet.t`,
> `t/comcon_std_lib.t`, `t/comcon_include_deny.t`. Demo: `js_comcon_demos/P_Platform_Teams/P1`.

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

> **REAL CODE (v5.125): PARTIAL.** The pin half is real, at two levels: a fragment is pinned
> by `identity` (H(H(source)‖schema), refused on mismatch) and a pure-library dependency by
> its SHA-256, loaded as a per-fragment closure parameter, never onto a shared global:
> ```js
> var f = comcon.include(src, comcon.std.profiles.pure_library());          // computation only
> var g = comcon.include(src, { imports: ["lib"],
>     deps: [{ name: "lib", path: "/etc/nginx/vendor/magic-utils.js", sha256: "9f2c…" }] });
> var h = comcon.include(src, { imports: [], identity: "<sha256 pin of what was reviewed>" });
> ```
> Tests: `t/comcon_include_deps.t`, `t/comcon_include_admit.t`. **The CVE-day half is built
> since v5.129 (G-01 closed), in a different spelling:** the switch is on the *grant*, and it
> is held by the operator session, not a CLI:
> ```js
> ops.withdraw("acme", "lib", { confirm: "acme" });   // {revoked: ["lib"], delegated: 3}
> ```
> Every wrapper the library was granted, and every copy it re-granted onward, answers
> `cap.revoked` from the next call — in every posture, no reload, no redeploy — and the
> tenant's calls into it are denials it can handle. A `replace()` does not lift it; nothing
> does but a new admission. `t/comcon_revoke.t`, demo `S_Security_Teams/S5`. The
> `comconctl` spelling stays not built (G-26).

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

> **REAL CODE (v5.125): NOT BUILT** (gap G-02: there is no `db` facet and no grammar-valued
> interface). Two shipped pieces carry the *principle* — "the dangerous sentence cannot be
> written" — on other surfaces: a **stone splice** binds producer data into a quotation as a
> JSON literal, so a spliced string can never become code (the parameterized-SQL defence,
> `t/comcon_pom_splice.t`), and `comcon.reviewDeclarative(source)` is a sound rejecter that
> admits only straight-line fluent call chains with literal arguments — no operators, no
> string building (`t/comcon_declarative.t`):
> ```js
> var q = comcon.quote("function(){ return db_query('SELECT * FROM users WHERE id = ?', [ID]); }",
>                      { ID: userInput });        // ID crosses as data, escaped, never text
> comcon.reviewDeclarative("acme.proxy.pass('http://acme_backend')").declarative   // true
> comcon.reviewDeclarative("route(1 + 2)")                                         // throws: not declarative
> ```

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

> **REAL CODE (v5.125): PARTIAL.** The hole is closed, but by a drop rather than a grammar: a
> response header a fragment returns crosses the boundary as data, and a value carrying CRLF
> is dropped — the second header never exists (`t/comcon_include_headers.t`; demo
> `js_comcon_demos/P_Platform_Teams/P1`, which returns `"ok\r\nX-Evil: pwned"` and shows no
> `X-Evil`). `header_value.number(...)`, `pattern { … }` and the regex-denying profile are not
> built (gap G-02):
> ```js
> var h = comcon.include("function(req){ return { status: 200,"
>   + " headers: { 'X-Tag': req.headers['x-tag'], 'X-Try': 'ok\\r\\nX-Evil: 1' }, body: 'ok' }; }",
>   { imports: [], checkRequest: true });
> // the host: req.respond(o.status, o.headers, o.body) — 'X-Try' is dropped, X-Evil never sent
> ```

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

resp.headers.set("X-Request-Tag",
    header_value.match(pattern { 1-32 (alpha, digit, "-") }).from(user_input));
    // richer shapes use PATTERNS, not regex: named, composable, and with
    // bounded quantifiers — so catastrophic backtracking (ReDoS) is
    // impossible by construction. Strict tenant profiles deny the regex
    // engine entirely and grant patterns instead — one more vulnerability
    // class removed by grammar, exactly like SQL injection above.
```

**The point:** "remember to escape" is replaced by "the interface only accepts
well-formed values." Injection needs a hole; the hole is no longer part of the language.

---

## 5. Onboarding 5,000 lines of legacy script — without reading them

> **REAL CODE (v5.125): SHIPPED, without the CLI.** Learn mode is a fleet posture that
> harvests every withheld name a fragment reaches for, with hit counts; shadow is
> `onViolation: "audit"` per binding or `ops.shadow()` for the fleet; enforce is the last
> word. The generated stub is a plain library program:
> ```js
> comcon.mode("learn");                                    // before the FIRST include
> var legacy = comcon.include(legacySource);               // written against too much host
> …                                                        // traffic
> nginx.tenantLearning()          // {mode, wants: [{path: "nginx.http.addServer", hits: 41}, …]}
> var shadow  = comcon.include(legacySource, { imports: [...], grants: g, onViolation: "audit" });
> var enforce = comcon.include(legacySource, { imports: [...], grants: g, onViolation: "deny" });
> ```
> `js_com_demos/COMCON_onboard/onboard.js` turns the harvest into a paste-ready grant stub.
> Tests: `t/comcon_include_learn.t`, `t/comcon_posture.t`, `t/comcon_std_ops.t`. Demo:
> `js_comcon_demos/P_Platform_Teams/P2`. **v5.127:** the per-binding would-deny report is
> `ops.wouldDeny(f)` (demo `O_Operators/O3`). **v5.130 (G-05 closed):** the allow-suite and
> its coverage, in a different spelling:
> ```js
> ops.record("acme");                 // …traffic…
> ops.suite("acme")                   // {recorded: 1214, distinct: 388, cases: [...], unstable: [...]}
> ops.coverage("acme")                // {functions: {total: 11, called: 10, percent: 91, uncalled: [{name: "onError", line: 40}]}, ...}
> ops.guard("acme");                  // the next rebind must answer every recorded case the same
> ```
> Coverage is by function entered, not code path; the compiled tier says `exact: false`.
> `t/comcon_std_suite.t`, demo `O_Operators/O4`. The `comconctl` spelling stays G-26.

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

> **REAL CODE (v5.125): PARTIAL.** The asymmetry is real for what the AI *writes*: a proposal
> is a quotation, provably cap-free, realized only under the realizer's authority and only
> within its declared manifest; a config proposal is reviewed as a diff and applied with
> explicit confirmation of guarded classes:
> ```js
> var q = comcon.quote(aiProposalSource);                         // inert; carries nothing
> var f = comcon.realize(q, { imports: ["JSON"], profile: "declarative" }, opsEnv);
> var plan = comcon.std.config.review(aiConfigProposal, POLICY);   // typed against the registry
> comcon.std.config.diff(plan, node);                              // the reviewable diff
> ```
> A leased operator session is `comcon.std.sessions({sessions: nginx.shared}).grant(principal,
> {imports, ttl: 900})`. Tests: `t/comcon_realize.t`, `t/comcon_config_instance.t`,
> `t/comcon_std_sessions.t`. Demo: `js_comcon_demos/O_Operators/O1`. **v5.127:** the
> monotonicity check over policy diffs is `comcon.std.policy.diff(before, after)` — `narrowing`
> is auto-safe, anything else is not (demo `O_Operators/O3`). Not built: the REL console (gap G-04).

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

> **REAL CODE (v5.125): NOT BUILT, by decision.** `opaque.*` is an engine-substrate question,
> never a mediation, and is on ROADMAP's canonical NOT BUILT list (gap G-03; scenarios 7, 16,
> 19, 32 share it). What ships is *field-level* hiding on a capability: a fragment can
> exercise a socket's `port` while `address` is unreadable — hidden, not printed as
> `[opaque]`:
> ```js
> grants: { s: comcon.mediate(sock, comcon.redact(["address"])) }   // typeof s.address → "undefined"
> ```
> and a session descriptor can never carry a capability at all (`t/comcon_std_sessions.t`).

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
Part IV: `SHOWCASE45.md` (38–45) · Part V (v4): `SHOWCASE47.md` (46–47) ·
Part VI (v4.2): `SHOWCASE49.md` (48–49) · Part VII (v5.2): `SHOWCASE50.md` (50) ·
Design: `FOUNDATION.md`.*
