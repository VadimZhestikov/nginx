# COMCON in nginx — Look & Feel, Part V: Scenarios 46–47 (v5.0)

> **Status: illustrative, not normative.** These two scenarios showcase the v4 symmetry
> correction: configuration is a governed language instance like code — with an
> admission hinge (typed config surface) and quotation-based proposals. Design:
> `FOUNDATION.md` §2a; work item: `ROADMAP.md` M-CFG.
> **Since v5.126 every scenario opens with a `REAL CODE` block:** what the shipped tree does today for that scenario, the tests that pin it, and the gap id (`SHOWCASE-gaps.md`) where the sample and the tree differ. The samples below it are the original hypothetical syntax, kept as written.

---

## 46. Typed config: the tenant edits nginx configuration — safely

> **REAL CODE (v5.125): SHIPPED, in the config language's JS form.** The slice is a policy —
> a subtree, a path allow-list, the safety classes that may apply unasked — and the
> proposal is config-shaped sentences typed against the same registry that types the JS API:
> ```js
> var POLICY = { type: "NginxLocation", root: "acme", allow: ["root", "alias", "proxy.*"], allowClass: ["safe"] };
> var plan = comcon.std.config.review("acme.root('/srv/acme'); acme.proxy.pass('http://x');", POLICY);
> plan.ok; plan.hash; plan.ops; plan.refused    // [{path, why}] — the E_CFG_PRODUCTION of the sample
> comcon.reviewCalls("loc.addLocationTypo('/x')", { loc: loc })   // refused: unknown member, at admission
> ```
> `nginx.conf` syntax itself is not the proposal grammar (gap G-23). Tests:
> `t/comcon_config_instance.t`, `t/comcon_review_calls.t`, `t/comcon_declarative.t`. Demo:
> `js_comcon_demos/O_Operators/O1`.

**Problem:** tenants always end up needing config changes — a port, a location block, a
timeout. Today that's a ticket to your team, because handing a tenant *any* config
access means handing them *all* of it: `nginx.conf` has no notion of "your slice."

**Today:** ticket queues — or templating systems that regex-check tenant snippets and
hope.

**With COMCON v4** — a tenant's config fragment is a **sentence of a restricted, typed
config grammar**, admitted exactly like code. The host grants a config *language
slice*, not file access:

```js
// host: what config ACME may even EXPRESS — grammar + types from the same
// schema that types the JS API (the M2 registry, read in its second role):
admitConfig("tenants/acme", {
    productions: ["server", "location", "listen", "error_page", "gzip"],
    types:       { listen: ports(8440, 8449), location: routes("/acme/*") },
});
```

```nginx
# the tenant writes ordinary nginx config — inside the granted grammar:
server {
    listen 8443;                       # ✓ typed: in the granted port range
    location /acme/api/ { gzip on; }   # ✓ granted productions, granted namespace
}
```

```
$ comconctl admit-config acme ./acme.conf
  DENIED  E_CFG_PRODUCTION  proxy_pass at acme.conf:7
    why:  'proxy_pass' is not an admissible production of your config grammar
    hint: upstream selection is available via your granted `backend` facet
```

The dangerous sentence is not *reviewed for* — it **cannot be admitted**, the same way
scenario 3 made string-built SQL unwritable. And the whole loop is the familiar one:
admit under `audit`, watch, enforce; every applied change is an epoch; rollback is a
label.

**The point:** config stops being all-or-nothing. A tenant edits their configuration
in the config language itself — inside a grammar-and-types cage derived from the same
schema as everything else. Tickets become admissions.

---

## 47. Propose the config you can't apply

> **REAL CODE (v5.125): SHIPPED.** A proposal never applies itself; the operator reviews a
> diff, applies with explicit confirmation of guarded classes, and the record is the rollback:
> ```js
> var plan = comcon.std.config.review(proposalText, POLICY);        // typecheck, hash, ops, refused
> comcon.std.config.diff(plan, acme);                               // what would change
> var applied = comcon.std.config.apply(plan, acme, { confirm: ["acme.proxy.pass"] });   // all-or-nothing
> comcon.std.config.rollback(applied, acme);
> ```
> Tests: `t/comcon_config_instance.t`. Demo: `js_comcon_demos/O_Operators/O1`.

**Problem:** the tenant knows what change they need — more ports, a new upstream — but
that change exceeds their slice. Today: a ticket in prose, re-typed by an operator,
with transcription errors and no audit trail from wish to change.

**Today:** "please add…" → copy-paste → hope it's what they meant.

**With COMCON v4** — the tenant writes the *exact* config they want as a **proposal**:
inert text, provably carrying zero authority (a quotation of a config subtree — the
same mechanism as scenario 39's policy generator, worn by data):

```
$ comconctl propose-config acme ./blackfriday.conf --reason "expected 10x traffic"
  proposal #88 filed: touches [listen ×4, upstream 'acme-burst'] — EXCEEDS tenant grant
  → routed to operator review (proposals never apply themselves)
```

The operator — the *realizer*, holding the authority the tenant lacks — reviews a
config diff, not prose; admission re-checks grammar, types, and pins; applying it is a
new epoch of ACME's subtree:

```
$ comconctl realize proposal:88 --after admit
  epoch 23 → 24 on tenants/acme     (epoch 23 retained for rollback)
```

Nothing was re-typed, the wish and the change are byte-identical, the audit trail is
the proposal itself — and an unrealized proposal is inert forever. (Old friends will
recognize the snapshot/rollback console: it was this mechanism all along — snapshots
are quotations of config subtrees.)

**The point:** "asking for a change" and "making a change" become the same artifact
separated by one authority check. Tenants propose in the config language; operators
realize with a diff review; the lattice guarantees a proposal can never smuggle its own
approval.

---

*Part I: `SHOWCASE.md` (1–7) · Part II: `SHOWCASE17.md` (8–17) · Part III:
`SHOWCASE37.md` (18–37) · Part IV: `SHOWCASE45.md` (38–45) · Design: `FOUNDATION.md` ·
Guarantees: `SEMANTICS.md` · Numbers: `PERFORMANCE.md`.*
