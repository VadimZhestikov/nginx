# COMCON in nginx — Look & Feel, Part IV: Scenarios 38–45 (v4)

> **Status: illustrative, not normative.** These eight scenarios showcase capabilities
> that exist only in the v3 architecture: intensional targeting, closure/quotation,
> pin-by-hash, live-rewrite epochs, the program-tree symmetry, compile-through, contract
> admission, and meet-composition. Syntax remains **hypothetical**; design in
> `FOUNDATION.md`, guarantees in `SEMANTICS.md`, numbers in `PERFORMANCE.md`.

---

## 38. Harden code you will never touch

**Problem:** three hundred vendor and legacy files are already in production. You may
not edit them — not one line, not even to add a marker.

**Today:** wrappers around some entry points, and hope about the rest.

**With COMCON** — policies target by **query**, not by annotation. The selector
language reaches *inside* code you don't own:

```js
// every call site of fetch, anywhere under vendor/ — no file is touched:
const sites = pom.query("callsites(fetch) within module('vendor/**')");

bind(env_with({ fetch: mediate(net.fetch, allowHosts(["api.partner.com"])) }),
     sites,
     { profile: "restrictive", onViolation: "audit" });   // observe first, deny later
```

The vendor files remain byte-identical — they still run anywhere, with or without
COMCON (restrictive profile: the policy only ever *removes* behavior). Rollout is the
standard two-step: audit-only for a week, then deny.

**The point:** annotation-based security dies at the first file you can't edit.
Query-based binding means *ownership of the code is not a precondition for governing
it.*

---

## 39. The generator that proposes more than it holds

**Problem:** your tenant-policy generator (a program!) should draft per-tenant policies
including things the generator itself must never have — say, metrics-write access. But
whoever *drafts* usually must *hold*, and giving the generator everything it may ever
mention makes it the juiciest target on the platform.

**Today:** the generator runs with union-of-everything privileges. One compromise =
every tenant's keys.

**With COMCON** — the generator emits **quotations**: descriptions of policies, which
are inert data with *provably zero authority inside* (the constructor rejects
capability values — deeply):

```js
// generator (tightly caged itself — it holds ~nothing). What it emits is ordinary
// policy-JS, just QUOTED — same language as everything else, not yet bound:
export default tenants.map(t => quote`
  const e = env();
  grant(e, "limit",   ratelimit.makeLimiter({ keyPrefix: "rl:${t.id}:", rps: ${t.rps} }));
  grant(e, "metrics", host.metrics.scoped("${t.id}"));  // generator does NOT hold host.metrics!
  bind(e, pom.query("module('tenants/${t.id}.js')"), { profile: "restrictive" });
`);
```

```js
// ops (the realizer — holds the real authority) applies the corporate contract, then:
for (const q of proposals) realize(admit(q, corpContract));   // caps drawn from OPS, not
                                                              // from the generator
```

The generator can *mention* `host.metrics` — mentioning is text. It can never *test
against* it, *smuggle* it (cap-free rule), or make anything happen (unrealized
quotations are inert forever). The tenant ends up bounded by *ops*, while the
generator stayed bounded by almost nothing.

**The point:** proposing and holding are finally different things. Drafts can be
ambitious; only realizers spend authority — and the split is one machine-checkable bit.

---

## 40. The silent update that wasn't

**Problem:** Friday: security reviews `vendor/checkout.js` and approves its policy.
Saturday: the vendor's pipeline ships a "patch" to the same path.

**Today:** Monday's incident review.

**With COMCON** — policies *target* by name but **pin by content hash**:

```js
include("./vendor/checkout.js", vendorPolicy,
        { pin: "sha256:4be1…" });     // the hash of what was actually reviewed
```

Saturday's file hashes differently — admission refuses it:

```
DENIED at admission: vendor/checkout.js is sha256:9c07…,
  policy is pinned to sha256:4be1… (reviewed 2026-08-14 by secteam)
  → refusing to govern unreviewed content; previous epoch still serving
```

The old, reviewed version keeps serving (epochs, scenario 41); the new one queues for
re-review. Nothing silent happened.

**The point:** "the policy applies to the file" quietly assumed the file stays what
you reviewed. Pinning makes that assumption a checked fact — supply-chain drift becomes
a clean, loud admission failure.

---

## 41. Patching a hot function at noon

**Problem:** a pricing bug in the busiest tenant's hottest function — the one that was
AOT-compiled to native code. Fixing it "requires a reload" (say the old rules), and
noon is not reload time.

**Today:** wait for the window, eat the bug for six hours.

**With COMCON** — the program tree is live, and code mutation has the same machinery as
config mutation (epochs, coherent fan-out, rollback):

```
$ comconctl rewrite acme/pricing.js#computeDiscount --from fix-1042.frag
  admit: types ✓  caps ✓  allow-suite 1,214/1,214 ✓        (same gate as any fragment)
  class F (fan-out): broadcasting epoch 41 → 4 workers
    node dirty → serving from bytecode fallback…            (~interpreted speed, this node only)
  re-AOT complete: epoch 42 live on all workers             (native speed restored)
  epoch 41 retained for rollback
```

No dropped request, never stop-the-world: during the window only the rewritten function
runs at interpreted speed, everything else stays native; then the workers switch epochs
*together* (a half-updated fleet is the failure this machinery exists to prevent). Bad
fix? `rollback epoch 41` — one command.

**The point:** live code patching with the same guarantees as live config patching —
admitted at the door, coherent across workers, reversible by epoch. The compiled world
doesn't take away the dynamic world's superpower.

---

## 42. The plugin that audits itself

**Problem:** every tenant dashboard ("what can I do? what did I use? what's my
coverage?") is host-written, drifts, and sees too much.

**Today:** a privileged internal service that everyone hopes is right.

**With COMCON** — the running program and a policy are the same kind of thing: both can
hold (attenuated) handles on the program tree. So the dashboard is just *a tenant
fragment holding a read handle scoped to that tenant's own subtree*:

```js
// granted to acme's dashboard fragment — nothing else:
grant(dash, "self", mediate(pom, subtreeOnly("tenants/acme/"), redact("bodies")));
```

```js
// inside the dashboard — ordinary caged code, introspecting its own world:
self.query("functions within module('**')").length      // its OWN code inventory
self.describe()                                          // its OWN effective grants
self.binding.epoch                                       // which policy version cages it
```

It physically cannot render a neighbor's dashboard: the neighbor's subtree is outside
`reach(self)` — not hidden by an `if`, unreachable by construction.

**The point:** introspection stops being a privileged service and becomes an ordinary
grant. The base/meta symmetry pays a practical dividend: *self-service observability
with tenant-shaped blinders, built from the same four operators as everything else.*

---

## 43. The policy that vanished at compile time

**Problem:** everyone knows how this movie ends: add a security layer, watch the
latency graph, remove the security layer.

**Today:** security vs. performance, pick one.

**With COMCON** — enforcement moves to the earliest stage that can decide it, and for a
fully static policy that stage is the *compiler*. The membrane from scenario 38:

```js
mediate(net.fetch, allowHosts(["api.partner.com"]))   // looks like a runtime wrapper…
```

…is a constant predicate, so maxim lowers it into the generated C as roughly:

```c
if (strncmp(url, "https://api.partner.com/", 24) != 0) return deny(...);
```

one string compare — not a JS call, not a policy interpreter, not anything. Measured on
real hardware, the full "count + tag" policy: **compiled 96% of stock nginx;
interpreted 28%; a no-policy control at parity** — the entire gap was the interpreter,
and the interpreter is what compile-through deletes (`PERFORMANCE.md`).

**The point:** the cage costs what remains *undecided* at compile time. Write static
policies and the answer to "what does the security layer cost?" is: *approximately one
`strncmp`.*

---

## 44. Hiring code, not trusting it

**Problem:** you want an AI (or a contractor, same thing here) to write a whole
fragment — and you want to plug it in the way you'd hire a person: job description,
interview, probation. Not "read every line and pray."

**Today:** read every line. Pray anyway.

**With COMCON** — a fragment is hired against a **contract** = (capability environment,
natural-language spec, tests) — one artifact read by three parties: the human states
intent, the generator generates against it, the machine verifies against it:

```js
include(ai.generate(spec),                       // or a contractor's tarball — same door
        env_with({ catalog: readOnly(catalog) }), // the JOB'S authority, decided up front
        {
          spec:  "Rank products by relevance for the query; never return >50 items.",
          tests: "./recommend.suite.js",          // run IN the cage, clock/RNG/IO denied
          pin:   true,                            // hash-locked once admitted
        });
```

Admission is the interview: types check, every referenced name must exist in the
granted environment, and the tests run *inside the sandbox against capability doubles
with nondeterminism denied* — reproducible, and with **zero blast radius** (a
malicious "test run" has nothing real to touch). Then the two guarantees split
cleanly: **tests bound correctness; capabilities bound damage.** If the spec was
misunderstood, you get bad rankings — never touched orders, never exfiltrated data:
the fragment's worst day is bounded by an `env` you wrote before it existed.

**The point:** the review question flips from "is this code good?" (unknowable at AI
volume) to "is this cage right?" (a dozen lines you wrote yourself).

---

## 45. Two departments, one file

**Problem:** corporate security requires "no network except the allowlist" on
everything; the payments team requires PCI rules on `payments/**`. Both policies claim
the same files. Who wins? In every config system ever: whoever loads last.

**Today:** ordering bugs promoted to security bugs.

**With COMCON** — multiple bindings on one node compose by **meet**: intersection of
authority, order-independent, built into `bind` itself:

```js
bind(corpSecurity,  pom.query("module('**')"));            // security team, fleet-wide
bind(pciRules,      pom.query("module('payments/**')"));   // payments team, their subtree
// loaded in either order — same result: payments/** runs under corp ∧ pci
```

An operation inside `payments/**` must pass *both* policies; a name must be granted by
*both* to resolve. There is no "last writer wins," no priority integer to misconfigure,
no merge function to review — `∧` is the whole story, and it can only ever narrow
(scenario 21's lockdown overlay is this same property worn as a panic button).

**The point:** independent teams govern overlapping code without coordinating load
order — composition is commutative, so organizational structure stops being a race
condition.

---

*Part I: `SHOWCASE.md` (1–7) · Part II: `SHOWCASE17.md` (8–17) · Part III:
`SHOWCASE37.md` (18–37) · Design: `FOUNDATION.md` · Guarantees: `SEMANTICS.md` ·
Numbers: `PERFORMANCE.md`.*
