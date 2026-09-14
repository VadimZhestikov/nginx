# The COMCON User's Manual — DRAFT

> **Status: working-backwards artifact — NOT the shipped surface (v5.35).** This manual is
> written *as if COMCON shipped*, to test the design from the user's chair. Its concrete
> surface — `comcon_load`, the `comconctl` CLI verbs, and the tenant framing — is
> **hypothetical and does not match the build.** What actually ships: `nginx.conf` gains only
> `js_source root.js;`; the root script drives everything through the `comcon` operators
> (`env`/`grant`/`mediate`/`admit`/`include`/`mode`) over the live COM (`nginx.*`), and binds a
> confined handler with `location.handler = req => {…}`. The `js_tenant_*` directives this manual
> and the older increment docs assume have been **removed** (INCREMENT_CONVERGE.md). Read this
> for design *feel*; read `OPERATOR_API.md` + `INCREMENT_MCFG.md` + `INCREMENT_CONVERGE.md` for
> the real API. Every genuinely-undecided item is marked **[TBD]** (Appendix B). Design:
> `FOUNDATION.md` · guarantees: `SEMANTICS.md` · numbers: `PERFORMANCE.md`.

COMCON is used by people wearing three different hats. This manual is organized by hat:

- **The Host** (platform operator): enables COMCON in nginx, onboards tenants, runs the
  fleet. → chapters 2, 5.
- **The Tenant developer**: writes ordinary JavaScript that happens to run in a cage.
  → chapter 3.
- **The Policy author** (security engineer / platform team): writes the cages.
  → chapter 4.

One person may wear all three; the *system* never confuses them — each hat is itself a
caged actor with its own grants.

---

## 0. Why confinement (read `DOCTRINE.md`)

Before the how, the *why*: "is this untrusted code safe?" is undecidable (Rice's theorem), so
scanning-and-patching is an endless treadmill. COMCON instead **confines** — it changes the question
from the undecidable "will it do harm?" to the decidable "what authority does it hold and what can it
express?" — and concentrates correctness on the finite TCB. The full doctrine (four pillars, honest
residuals, the finite-effort win) is `DOCTRINE.md`; this manual is the *how* that serves it.

## 1. The five-minute mental model

Four ideas, in dependency order:

1. **Your program is a tree of fragments** (modules, functions, blocks). COMCON sees
   the same tree you see in your editor.
2. **Every fragment runs inside an environment** — a plain list of names it may use.
   A name is either in the list or the fragment *cannot mention it*. There are no
   globals, no ambient anything. Absent means unresolvable, not forbidden-but-visible.
3. **Names are bound to capabilities** — values that can actually do things (fetch,
   query, read a config node), usually *narrowed*: not "the network," but "GET to
   api.partner.com"; not "the config," but "read-only, your subtree."
4. **One law: nothing can grant itself more.** You can always give away less than you
   have — never more. Every guarantee in this manual is that law wearing a different
   costume. (It is a proved theorem, not a convention — `SEMANTICS.md` §3.)

And one unification worth knowing from day one: **your config is the same kind of
thing as your code.** A config fragment is a "program" with an (almost) empty
environment — same tree, same admission gate, same epochs, same rollback. That is why
everything in this manual comes in matching pairs: code fragments and config fragments
are onboarded, cage-checked, proposed, and rolled back with the same verbs (§3.6).

And one economic fact: **the cage costs what stays undecided until runtime.** A policy
the compiler can fully resolve costs ~nothing — measured at ~96% of stock nginx
throughput vs ~28% for the same policy interpreted. Chapter 6 tells you how to stay on
the cheap end.

---

## 2. Quick start (the Host hat)

### 2.1 Enable

```nginx
# nginx.conf — everything else stays exactly as it was
http {
    comcon_load /etc/nginx/comcon/root.js;
}
```

### 2.2 Scaffold

```
$ comconctl init /etc/nginx/comcon
  created: root.js            (host root program — the root of trust)
           profiles/          (starter profiles: tenant, pure_library, forensics)
           tenants/           (empty)
  NOTE: root.js runs with FULL authority. Keep it short. Keep it reviewed.
```

**[TBD-1]** What the shipped default root grants by default (the "secure out of the
box vs useful out of the box" line) is an undecided product decision.

### 2.3 First tenant, first cage

```js
// root.js (excerpt)
import std    from "comcon:std-policies";
import js_com from "js_com";

const acme = env();
grant(acme, "http", mediate(js_com.http, routes("/acme/*")));
grant(acme, "log",  mediate(host.log,    rateLimit(10)));

include("tenants/acme/main.js", std.profiles.tenant(acme),
        { profile: "restrictive", onViolation: "audit" });   // observe first
```

```
$ nginx -s reload && curl localhost/acme/hello
  hello from acme                                # tenant code just runs

$ comconctl denials acme --last 1h
  (none)                                          # cage fits; tighten it:
$ comconctl enforce acme                          # onViolation: audit → deny
```

That is the entire loop you will repeat forever: **grant little → watch denials →
adjust → enforce.** Everything else in this manual is elaboration.

---

## 3. The Tenant handbook (the code's-eye view)

### 3.1 What is different inside the cage

Your code is normal JavaScript with a shorter horizon:

| You write | What happens |
|---|---|
| `http.get("/api/x")` | works — `http` is in your environment |
| `require("fs")` / `import fs` | **compile error** — imports resolve only to granted modules |
| `globalThis.leak = x` | **compile error** — there is no ambient global object |
| `eval(s)`, `new Function(s)` | **denied by default** — dynamic code is a grant like any other |
| `new Worker(...)`, `new SharedWorker(...)` | **denied by default** — creating executors is a grant (guests cannot spawn actors) |
| `/regex/` in strict tenant profiles | **denied where profiled** — use `pattern { … }` instead (bounded ⇒ no ReDoS) |
| `Object.prototype.x = y` | **denied** — the world's floor is frozen (for everyone, always) |

Rule of thumb: *if you can name it, you may use it; if you may not use it, you cannot
name it.* There is no "visible but forbidden."

### 3.2 Reading a denial

Every denial — compile-time or runtime — has the same anatomy:

```
DENIED  E_CAP_UNRESOLVED                        ← stable, machine-parsable code
  what:   fetch("https://evil.example/…")       ← the operation, redacted as needed
  where:  tenants/acme/analytics.js:41          ← your source position
  policy: net-allowlist  (installed by: host, epoch 12)   ← whose rule, which version
  why:    host "evil.example" is not in allowHosts
  hint:   your grant covers: api.partner.com    ← what WOULD work
```

**That block is the record's designed shape, not today's output.** What exists now:
exact per-code counters (`nginx.tenantDenials()`), an error-log line naming the op, the
object, the mode and the running total (under the TM-1 quota + sampling), and — for a
refusal — a thrown `Error` carrying `.code` and a source position
(`at <comcon-fragment>:LINE:COL`). The `policy:` and `hint:` fields have no
implementation yet; do not write CI against them.

The same record, sign-flipped, is what learning mode logs and what your deny-suite
asserts against. Codes are stable across releases — pin your CI to codes, not to
message text.

**Two axes, and you need both.** A **denial** names a gate that fired while your
policy was *running*; a **refusal** names why your policy was never *admitted*. They
fail differently and you fix them differently, so they are separate sets:

| | what it means | where you read it | today's set |
|---|---|---|---|
| **denial** | a gate denied an operation at request time | `nginx.tenantDenials().byOp` (exact counters per code) | `sock.listener`, `listener.read`, `listener.serverByName`, `enum.sockets`, `sock.mutate`, `budget.uses`, `cap.expired`, `out.host`, `out.drain`, `cap.window` |
| **refusal** | the fragment was not admitted — it never ran | `e.code` on the throw; `code` on an `admit()` verdict; `comcon.refusalCodes()` enumerates the set | `E_ADMIT_ARG`, `E_ADMIT_NOTBYTECODE`, `E_ADMIT_SOURCE`, `E_ADMIT_DYNCODE`, `E_ADMIT_FREENAME`, `E_ADMIT_INTRINSIC`, `E_ADMIT_SCHEMA`, `E_ADMIT_TEST`, `E_ADMIT_CONTRACT`, `E_ADMIT_DEP`, `E_CAP_GRANT`, `E_CAP_FLAVOR`, `E_CAP_ESCALATE`, `E_PIN_IDENTITY`, `E_EPOCH_STALE` |

A refusal carries its code three ways: as `.code` on the thrown `Error` (**assert on
this one**), bracketed at the end of the message so your error log is greppable
(`… free name not declared in imports: nginx [E_ADMIT_FREENAME]`), and as `code` on
the verdict object `admit()` returns. The prose beside it may be reworded in any
release; the code may not.

```js
try {
    comcon.include(src, contract);
} catch (e) {
    if (e.code === 'E_ADMIT_FREENAME') { /* add the name to imports */ }
}
```

**Both sets are frozen by a test, not by a promise.** Every code above carries a probe
in `t/tools/golden-denials.js`, and `t/comcon_v12_denial_codes.t` provokes each one and
compares what fires against what is frozen (V12) — so a renamed code breaks this
project's own suite before it breaks your CI. A code cannot be added to the runtime
without a probe or a written reason it is unreachable.

**[TBD-2] — RESOLVED (v5.70), and what that leaves.** Every refusal the platform makes
about a fragment or a capability now carries a code, including the capability layer's own
two: **`E_CAP_FLAVOR`** (a mediation flavor outside the closed vocabulary — the refusal
that closed a fail-open, where a typo used to mean FULL authority) and
**`E_CAP_ESCALATE`** (a composition that cannot be *shown* to narrow: a routes glob or a
budget with no computable meet, a meet that widened, a realization env that is not a
sub-map of the realizer's — one code, because it is one rule).

Two things deliberately have **no** code, and that is the answer rather than a gap.
`E_BUDGET_*` is empty because running out of a `uses` budget is a **denial**
(`budget.uses`) — a gate refusing at request time — not an admission refusal. And the
deadline abort has no code of ours at all: it is the engine's interrupt, and there is no
refusal of ours at that point to label. What still has only message text is the argument
checking of library calls (`query: empty selector`, `std.config.apply: arg0 must be a
plan`), and that is on purpose: the fix for those is to fix the call, and coding them
would invite CI to pin to our argument checks.

### 3.3 Your documentation is generated — and cannot lie

```
$ comconctl docs acme
  # ACME — your available API (derived from your grants, epoch 12)
  http.get(path: "/acme/*")      cost: IC-cached   budget: 5ms/request
  log.info(msg)                  cost: O(1)        rate: 10/s
```

If it's listed, it works; if it works, it's listed. When you need more, you don't edit
config — you file a **grant request** (§3.7).

### 3.4 Developing locally, without the host

You do not need a platform to develop against a cage:

```
$ comconctl dev --env acme.envspec --tests ./suite.js
  emulated environment: http (double), log (double)   clock/RNG: denied
  suite: 1,214 / 1,214 ✓          determinism: reproducible run hash 9c41…
```

`comconctl dev` runs your fragment against **capability doubles** generated from the
same environment spec the host will bind — the emulator and production admission are
the same gate (`admit`), so "works on my machine" and "admitted by the host" are the
same predicate. **[TBD-3]** The doubles' fidelity contract (record/replay? schema-only
stubs?) is undesigned — this is the tenant-side SDK the plan currently lacks.

### 3.4a Using npm dependencies *(v5.1)*

Your dependencies come along — each one caged, pinned, and admitted like everything
else:

```
$ comconctl install                      # reads your lockfile
  lodash@4.17.21    → pure_library cage, candidate policy from static harvest
                      (imports: nothing; exports_to: you)   pin: sha256:…
  zod@3.23.8        → pure_library cage                     pin: sha256:…
  left-pad@1.3.0    → WARNING: requests eval — denied by pure_library; see denial #12
```

Every dependency is a child fragment under a `pure_library` policy generated from its
own static harvest; pins are your lockfile hashes, so a hijacked update is refused at
admission (scenario 40); transitive dependencies are cages inside cages. A library
runs with what *you* grant it — never with what it requests.

### 3.5 Budgets

Your cage meters CPU, memory, and compile episodes. Exhaustion is a *catchable* error
in your own error channel (`E_BUDGET_CPU` …) — handle it or fail cleanly; you cannot
ignore it and you cannot starve your neighbors. **[TBD-4]** Unit semantics (wall vs
CPU time, per-request vs per-episode accounting) are stated in your generated docs and
are not yet finally specified here.

### 3.6 Writing config, not just code *(v4)*

Your slice of the server's configuration is yours to edit — as **config sentences**,
not tickets. Your config fragment is admitted exactly like your code: a restricted
grammar (which directives you may write at all) plus types (which values they may
take), both derived from the same schema that generates your API docs:

```nginx
# tenants/acme/acme.conf — a config FRAGMENT; admitted, then grafted
server {
    listen 8443;                     # ✓ in your granted port range
    location /acme/api/ { ... }      # ✓ inside your route namespace
}
```

```
$ comconctl admit-config acme ./acme.conf
  DENIED  E_CFG_PRODUCTION   proxy_pass at acme.conf:7
    why:  directive 'proxy_pass' is not in your admissible grammar
    hint: your upstreams are reachable via the granted `backend` facet
```

For changes above your authority, **propose** them — a proposal is inert text (it
carries no authority at all) that an operator reviews and applies; applied proposals
become a new epoch of your subtree, rollback-able like everything else:

```
$ comconctl propose-config acme ./wish.conf --reason "black friday scale-up"
```

*(v5.0)* When an operator later realizes your proposal, it does **not** run with the
operator's full authority: realization is **least-authority** — your proposal carries
a manifest of the names it mentions, the operator reviews exactly that list, and the
realized program can touch the reviewed intersection and nothing else. A proposal
cannot be a trojan for the operator's session.

### 3.7 Asking for more

```
$ comconctl request acme 'grants.http.post: "/acme/api/*"' --reason "checkout v2"
  → request #312 filed: a WIDENING (requires host review — widenings always do)
```

Widening is never self-service (the one law). The host reviews a descriptor diff;
approval lands as a new epoch of your binding; your generated docs update themselves.

---

## 4. The Policy author's handbook

### 4.1 The vocabulary you actually use

You will rarely touch the four kernel operators directly. You compose the standard
library:

| Combinator | Meaning |
|---|---|
| `routes("/acme/*")`, `allowHosts([...])` | scope a facet to a slice |
| `readOnly()`, `redact("bodies")` | weaken what a handle shows |
| `uses(1)`, `ttl("10m")`, `window("Sat 02:00-04:00")`, `cosign([...])` | grants with a fuse / office hours |
| `rateLimit(n)`, `budgets({...})` | metering |
| `opaque.str({pass_to: [...]})` | usable-but-unreadable values |
| `protocol("handshake", "frames*", "close")` | enforced operation order |
| `stone(value)` | deep-freeze plain *data* once — membrane-free sharing across boundaries (membranes for authority, stone for data) |
| `pattern { 1-32 (alpha, digit, "-") }` | readable, composable validation — bounded quantifiers ⇒ ReDoS impossible; static patterns compile through |
| `wasm.admit(path, {imports, budgets, pin})` *(v5.3)* | admit a foreign (non-JS-born) module: its imports are your grants, its fuel your budgets; hot modules ingest via wasm2c into the native funnel (scenario 50) |

Everything above is itself governed code from `comcon:std-policies` — the library
cannot leak the raw authority it wraps (facet pattern). If you find yourself needing a
raw kernel operator in an application policy, treat it as a design smell.

### 4.2 Choosing a binding: query, anchor, or include

| You have | Use | Example |
|---|---|---|
| Code you cannot edit (vendor, legacy) | **query** | `bind(e, pom.query("callsites(fetch) within module('vendor/**')"))` |
| Code written for the platform | **anchor** | `"use comcon: checkout";` in the code, policy elsewhere |
| Code you are grafting in whole | **include** | `include(path, policy, {pin, profile, onViolation})` |

Always **pin** third-party includes (`pin: "sha256:…"`): a silently changed file is
then refused at admission while the previously reviewed version keeps serving.

### 4.3 The two profiles — declare which one you are

- **restrictive**: your policy only denies/attenuates. The target program remains a
  correct standalone JS program. *Hardening existing code must stay here.*
- **adaptive**: your policy transforms (rewrites, injects protocol). The program now
  depends on COMCON. Legal, powerful — and must be declared, so "runs without COMCON"
  stays a checkable claim fleet-wide.

### 4.4 Failure modes — choose per rule, not per platform

`reject-at-compile` (default for authored code) · `deny-at-runtime` · `audit-only`
(rollout mode: log what *would* be denied) · `attenuate-silently` (degrade, for
compat). The standard rollout is always: **audit → read the would-deny report →
enforce.**

### 4.5 Composition: don't coordinate, just bind

Two teams, one file? Bind both policies. They compose by **meet** (intersection),
order-independent — there is no "loaded last wins" to misconfigure. A fleet-wide
lockdown overlay is the same property used at 3 a.m.: applying it can only ever narrow,
so the panic button is provably safe to hit.

### 4.6 Testing a policy

A policy ships with two suites, and the denial schema is your assertion language:

- **allow-suite** (liveness): intended programs still run — *the cage admits the animal.*
- **deny-suite** (safety): attacks are rejected **with the specific expected denial**
  (`expectDeny("E_CAP_UNRESOLVED", at: "…")`) — a syntax error must never masquerade
  as enforcement.

Budget your paranoia asymmetrically: over-restriction announces itself (tenants
complain); under-restriction is silent — spend the deny-suite and fuzzing there.

### 4.7 Contracts: hiring code

For generated or contracted fragments, write the **contract** first:
`(environment, natural-language spec, tests)`. Admission runs the tests inside the
sandbox against doubles with clock/RNG/IO denied. Then remember which guarantee is
which: **tests bound correctness; the environment bounds damage.** Review the cage
(a dozen lines you wrote), not the ten thousand lines you didn't.

---

## 5. Operations guide (the Host hat, day 2+)

One fact frames this whole chapter *(v4.1)*: there is no management plane. Every verb
below runs as an admitted episode in your operator session, under your environment —
so everything in chapters 3–4 (denials, budgets, mediations, audit) applies to *you*
while you operate.

### 5.1 The lifecycle verbs

```
learn → propose → shadow → enforce        # onboarding brownfield code
diff → shadow → enforce                   # tightening an existing cage
admit-config / propose-config → realize   # config fragments: same gate, same epochs (v4)
snapshot → (trouble) → rollback           # epochs make undo a label
revoke --cascade                          # CVE day: one command, no restart
```

### 5.2 Live changes and epochs

Every administrative change (widening, rewrite, profile swap) is a new **epoch** of
the binding; the previous epoch stays serving until the new one is coherent on all
workers, and stays *rollback-able* until retired. Live code patches ride the same
machinery: admitted at the door, broadcast to workers, the patched node briefly runs
at interpreted speed until re-compiled, then the fleet switches together. No dropped
requests; a bad patch is `rollback <epoch>` away.

### 5.3 Incident kit

- `comconctl apply-overlay '**' std.postures.lockdown` — provably-safe crouch.
- `comconctl attach worker:N --profile forensics` — read-only console; payloads appear
  as opaque handles; constant-response-time (no timing leak).
- `comconctl shadow-world tenants/suspect` — suspect keeps running against
  live-looking reads; every write lands in a private overlay; forensics watches.
- `comconctl trust-report --scope tenants/X` — the audit evidence pack, regenerable.

### 5.4 Upgrades

Engine upgrades version the grammar enumeration; a policy targeting productions that
changed fails **at admission, loudly, before serving** — never silently re-interprets.
Platform API upgrades are per-tenant grants (`platform.api("v2")` vs `"v1-compat"`);
the migration burn-down is `comconctl diff --holders v1-compat`. **[TBD-5]** The
compatibility window policy (how many grammar versions admission supports) is a
product decision, not yet made.

---

## 6. Performance guide (one page)

- **Stay declarative, stay typed** → your policy compiles away; the measured ceiling is
  ~96% of stock nginx. Dynamic constructs (`any`, computed access, runtime hooks) pull
  toward the interpreted floor (~28%).
- **Stay in the typed core** — the compilable profile is a deliberately small JS
  (no `this`, no classes/`new`/prototype tricks, no coercion — a Misty-like core);
  code outside it still runs, on the interpreted path.
- **Free:** anchors; frozen intrinsics (often *faster* — ICs never invalidate); grants
  never exercised; policy metadata (∝ policies, not objects).
- **Reload-time, not request-time:** admission, meet-composition, hashing, tests.
- **Cheap:** descriptor checks at boundaries (IC-cached); metering.
- **Pay attention:** membranes with *dynamic* predicates on hot paths (a JS call per
  op) — prefer static predicates (they compile to an inline check); heavy cross-tenant
  object sharing (COW/IC integration is the platform's known hot spot).
- **Transient:** a live-rewritten node runs interpreted until re-AOT — plan hot-patch
  windows for hot tenants.
- Your per-operation price is printed in your own generated docs (cost classes:
  O(1) / IC-cached / JS-call).

---

## 7. Reference

### 7.1 nginx directives

| Directive | Purpose |
|---|---|
| `comcon_load <path>` | run the host root program at startup (root of trust) |
| **[TBD-6]** `comcon_state_dir`, reload semantics knobs | undecided |

### 7.2 `comconctl` verbs (consolidated)

*(v4.1)* **comconctl is a shell, not a privileged tool.** Each verb below is a library
program (`std.ops`) executed as an admitted episode in *your* session, over *your*
granted capabilities — there is no management API behind it. Consequences you can rely
on: an operator's session is auditable like any tenant (`trust-report` covers it);
office-hours/cosign mediations apply to admin verbs; CI and AI operators use the same
gate; and anyone can rebuild any subset of this CLI from the capabilities they hold —
you get exactly your slice of the verbs, never more.

`init` · `learn` / `propose` / `shadow` / `enforce` · `denials` · `diff` · `docs
[--json]` *(v5.1: the machine-readable schema/denial feed — what AI generators and CI
consume; same data as the human docs)* · `dev` · `install` *(v5.1: the dependency
workflow, §3.4a)* · `request` · `admit-config` / `propose-config` *(v4)* · `evaluate` ·
`revoke [--cascade]` · `remove-subtree` · `snapshot` / `rollback` · `rewrite` ·
`apply-overlay` · `attach` · `shadow-world` · `trust-report` · `export` · `optimize`

### 7.3 Policy file anatomy (annotated)

```js
// acme.policy.js — a stage-0 program; runs under the policy-for-policies
import std from "comcon:std-policies";

export default function (ctx) {              // ctx: what the HOST granted THIS policy
  const e = env();                           // 1. an empty environment (zero authority)
  grant(e, "http", mediate(ctx.js_com.http,  // 2. grants: only what ctx holds
                           routes("/acme/*")));
  return policy({                            // 3. the reified policy value (a CLOSURE —
    env: e,                                  //    it carries caps; see quotations for
    profile: "restrictive",                  //    the propose-don't-hold variant)
    onViolation: "deny",
    contract: { tests: "./acme.suite.js" },
  });
}
```

### 7.4 Denial record fields

`code` (stable) · `what` (operation, redacted) · `where` (file:line, fragment path) ·
`policy` (name, installer, epoch) · `why` (human) · `hint` (what would work) — one
schema for denials, learning records (sign-flipped), and deny-suite assertions.

### 7.5 Selector language

`module('glob')` · `callsites(name)` · `exports(fragment)` · `anchors('name')` ·
`within(...)` combinator. **[TBD-7]** Full grammar is an M2.5 deliverable; treat this
list as the working subset the examples use.

*Implemented as of D5b-2 (2026-09-12):* `module` · `function` · `*` · `name(glob)` ·
`within` (D2), and over a `cst()` view `block` · `stmt` · `expr` · `call(glob)` ·
`type(glob)` · `anchors(glob)` · `line(N)` · `line(N-M)`. Factors AND within a term.
Quoting a glob is optional — this section's `anchors('name')` and a bare
`anchors(name)` are the same selector, because a reader following the manual must not
get a silent zero-match. Still TBD: `exports(fragment)`, and `callsites(name)` as a
*selector* (it exists as the method `node.callsites(name)`; the selector spelling is
`call(glob)`).

### 7.6 Glossary

**fragment** a governed subtree of the program · **environment** the complete name→
capability list a fragment sees · **capability** an unforgeable value that can do
something · **facet** a capability wrapping narrower behavior around authority it
hides · **quotation** a policy/code *description* — provably carries no authority ·
**anchor** an inert source marker naming a policy attachment site · **pin** a content
hash a policy is locked to · **epoch** one version of a binding; the undo unit ·
**profile** restrictive (deny-only) or adaptive (transforming) · **contract**
(environment, spec, tests) — the admission gate for a fragment · **stone** a
deep-frozen plain-data value, safe to share without a membrane · **pattern** the
bounded, composable validation form that replaces regex in strict profiles ·
**config fragment** *(v4)* your slice of configuration as admissible sentences — code's
sibling: same tree, same gate, same epochs (data is code bound to an empty environment)
· **proposal** *(v4)* inert config/policy text carrying no authority; takes effect only
when an operator with the authority realizes it · **episode** *(v4.1)* one admitted run
of a stage-0 program — every comconctl verb, REPL input, and policy execution is one;
the unit of administration, budgeting, and audit.

---

## Appendix A. What this manual deliberately oversells

Written as if shipped; in reality (2026-08): the kernel, semantics, and POM are
specified (docs-v3); M1 measured the performance endpoints; **nothing else exists**.
The `comconctl` verb set is a design target consolidated from the showcases.

## Appendix B. The [TBD] harvest — decisions this manual forced into the open

1. **[TBD-1] Default-root contents** — the secure-vs-useful out-of-box line.
2. **[TBD-2] Denial code taxonomy — ✅ FULLY RESOLVED 2026-09-12 (v5.62 + v5.70).** Two
   axes, both frozen by V12: DENIAL codes (run-time gates,
   `nginx.tenantDenials().byOp`, six of them incl. `budget.uses`) and fifteen REFUSAL
   codes (admission and the capability layer, `e.code` + `comcon.refusalCodes()`) — see
   §3.2. The two families that remain EMPTY are answers, not omissions: `E_BUDGET_*`
   because budget exhaustion is a denial rather than a refusal, and the deadline abort
   because it is the engine's interrupt with no refusal of ours to label.
3. **[TBD-3] `comconctl dev` / tenant SDK** — capability doubles, fidelity contract,
   emulator ≡ admission. *Not in the plan at all before this manual.*
4. **[TBD-4] Budget unit semantics** — wall vs CPU, per-request vs per-episode.
5. **[TBD-5] Grammar-version compatibility window** on engine upgrades.
6. **[TBD-6] nginx integration knobs** — state dir, reload semantics surface.
7. **[TBD-7] Selector grammar** (already promoted to M2.5; the manual adds the
   requirement that examples and LSP share one working subset).
8. **Terminology freeze** — this manual had to pick words (cage/binding/epoch/facet);
   the glossary should become normative at M2.5 before more docs accrete synonyms.
9. **[TBD-8]** *(v4)* **Config-production enumeration** — which nginx directives are
   admissible grammar for tenant config fragments, and their typed value domains
   (feeds the dual-role M2 schema; the config-side p_symbols).
10. **[TBD-9]** *(v4)* **Config value-conflict rule** — rights meet by intersection,
    but two admitted fragments may still *state* conflicting values for one mergeable
    directive; what counts as agreement/priority at admission is undecided.
