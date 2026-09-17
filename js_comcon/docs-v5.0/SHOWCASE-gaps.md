# SHOWCASE gaps — where the scenarios and the tree differ (v5.126, 2026-09-16; three closed at v5.127)

> **What this is.** The eight `SHOWCASE*.md` files were written in August 2026 as
> intent, in hypothetical syntax. On 2026-09-16 every one of their 53 scenario
> headings was annotated with a `REAL CODE` block: what the shipped tree (v5.125,
> pilgrim `ce9590c84`) does for that scenario, with the tests that pin it. This file
> collects the differences — each a gap id the blocks cite — so the scenario set
> can be read as an acceptance checklist (ROADMAP §5: "a milestone is showcase-true
> when its scenarios run as written").
>
> **How to read a row.** *Kind* says what closing the gap needs: **decision** (a
> mechanism could be built, nobody has decided what it should be), **substrate**
> (an engine capability the M-tracks do not build), **library** (a `std.*` or
> tooling program over shipped kernel operators), **design** (a named design
> increment not started), **honest limit** (an architectural fact the sample
> contradicts). *Home* is ROADMAP's earliest home for the cluster, where one is named.

## The scorecard

| status | scenarios |
|---|---|
| **SHIPPED** (runs as described, modulo names) | 1, 5, 8, 10, 12, 18, 26, 28, 30, 31, 39, 40, 41, 44, 47, 51, 51b |
| **SHIPPED in a different shape** (the property holds, the sample's spelling does not) | 14, 25, 36, 46 |
| **PARTIAL** (a real piece ships; a named piece does not) | 2, 4, 6, 17, 20, 22, 24, 29, 33, 37, 38, 42, 43, 45, 48, 49 |
| **NOT BUILT** | 3, 7, 9, 11, 13, 15, 16, 19, 21, 23, 27, 32, 34, 35, 50, 51c |

Seventeen of fifty-three run as written, twenty more hold their property in a
different spelling or in part, sixteen do not exist. Of the sixteen, four are one
substrate decision (`opaque.*`/COW: 7, 16, 19, 32), two are one design increment
(multi-language: 15, 50), and one is a posture nobody has decided (21).

## The gaps

| id | scenarios | the sample says | the tree has | what is missing | kind | home |
|---|---|---|---|---|---|---|
| **G-01** | 2, 20 | `comconctl revoke --cascade` over a provenance chain; offboarding follows delegations | **CLOSED v5.129:** `comcon.withdraw(f, name?)` / `ops.withdraw(name, grant, {confirm})` switch a grant off while the fragment runs; every granted wrapper holds a grant record and a re-grant's copy holds one under its parent's, so the cascade over a delegation chain is a pointer walk; `cap.revoked` is unconditional; a binding's revocation survives replace and rollback and fans out through the shared record. Demo `S_Security_Teams/S5`. | — (a copy is withdrawn through the grant it was copied from, not on its own; per worker for a raw fragment) | C mechanism + library | — |
| **G-02** | 3, 4, 11 | grammar-valued interfaces: a parameterized-only `db` facet, `header_value` grammars, `pattern{}` instead of regex, "one parser, N policies" | stone splices (data can never become code), `reviewDeclarative` as one sound rejecter, CRLF dropped at the header boundary | facets whose *language* is a policy; a pattern language; a `db` capability kind | design | M2.5–M4 (+M-LIB facets) |
| **G-03** | 7, 16, 19, 32, (18 overlay) | `opaque.str` values with named sinks; COW views and overlays; a shadow world | field-level `redact`/`allow` on socket and server capabilities; sessions never carry caps | the opaque/COW engine substrate | substrate, **unscheduled by decision** (ROADMAP §5 lesson 6; canonical NOT BUILT list) | none |
| **G-04** | 6, 13, 23 | a REL/forensics console attached to a worker; an AI session with redacted handles and opaque traffic | per-worker data through host handlers (`tenantDenials`, `tenantLearning`, `memStatus`, `aotStatus`, `trustReport`); NodeView reads are quotations, `binding` redacted | an attach/REPL surface; a forensics profile; body redaction on program handles | library (REPL exists on the js_com side; a COMCON-shaped session does not) | tooling verbs |
| **G-05** | 5, 6, 14 | allow-suite generation with coverage; a policy diff that reports "narrowing, auto-safe"; a would-deny event list | **CLOSED v5.127** for the diff and the would-deny list (`comcon.std.policy.diff`, `comcon.denials(f)` / `ops.wouldDeny(f)`, demo O3); **CLOSED v5.130** for the allow-suite: `comcon.std.suite` records a binding's (input, output) cases, emits them as a contract `tests` quotation, `guard` pins it so a rebind that answers differently is refused (`E_ADMIT_TEST`), `check` rehearses on the host, `coverage` names the functions never entered (function-level, from an engine entry counter; the native tier says `exact: false`). Demo `O_Operators/O4`. | — (function-level coverage, not code paths; answers, not effects) | library + one counter | — |
| **G-06** | 9 | `expose`/`accept` communication edges, both signatures required | host-brokered exchange (JSON between two fragments of one host); reseller copies narrowed | fragment-to-fragment edges as capabilities | design | mixed (ROADMAP §5) |
| **G-07** | 10, 33 | `cpu: "5ms/request"`, `compile: "50ms"`, budgets as billing counters | `timeoutMs` (wall clock), `memoryBytes` (burst), `retainedBytes` (leak), `memStatus` counts | a CPU-time unit, a `gas` instruction count (forward-declared), compile budgets, invoice-grade counters | design | S5-b (`meter({gas})`) |
| **G-08** | 15, 50 | Tcl iRules and WASM modules as fragments | JavaScript only | multi-language includes; the `wasm` facet and wasm2c lane | design | M9 / stage 2; M-LIB v5.2 (wasm) |
| **G-09** | 17, 23 | a signed manifest (quotation + policy + suites + maker chain + hash) that travels; `export` | quotations are cap-free by construction; `identity` and `deps.sha256` pins; only source text crosses between workers | signing, maker chains, an export tool, a ServiceWorker target; the `provenance` and `signing` ops resources (listed `host: null`) | library + decision | tooling verbs |
| **G-10** | 21, 45 | `std.postures.lockdown` applied fleet-wide; two bindings on one node meet | fleet `enforce`; mediation stacks meet on a capability (masks AND, lifetimes MIN, identical budgets compose, different refused) | what `lockdown` narrows to (a decision); a meet of two *contracts* on one fragment | **decision** (canonical NOT BUILT list) | — |
| **G-11** | 22 | an adaptive-profile transform at admission | `profile: "adaptive"` refused on purpose (`E_ADMIT_CONTRACT`); `harden`/`ops.rewrite` rewrite a quotation's matched sites, reviewable, installed as an epoch | a transforming profile — refused by decision so "runs standalone" stays falsifiable | decision | stage 2 |
| **G-12** | 24 | a trust ladder of profiles; widening requires the admin handle and leaves an epoch mark | two profiles (`tenant`, `pure_library`); widening is an ordinary `rebind`/`replace` epoch (host is trusted) | probation/standard/trusted profiles; an admin-handle gate on widening | library + decision | M6 |
| **G-13** | 25 | one static report of a module's whole appetite | **CLOSED v5.127:** `comcon.std.evaluate(fn | source, {declares})` — every free name from the admission collector, classified, with call sites and lines, the dynamic-code flag, the undocumented remainder, and the `imports` line a contract would need; a source is accepted only as one function expression and nothing runs. Demo `A_Auditors/A2`. | — | library | tooling verbs |
| **G-14** | 26 | `protocol` over a `ws` facet (`handshake`, `frames*`, `close`) | `protocol` over socket field reads and outbound `request` | more capability kinds with operations to order | design | M-LIB facets |
| **G-15** | 27 | workers as fragments; a message is an export; edges between workers | data through `nginx.shared` (shared bindings, fleet posture), lazy per-worker reconcile | cross-process kernel semantics | design (open question) | FOUNDATION open questions |
| **G-16** | 29, 37 | per-tenant generated docs; a trust report with provenance and signatures | **CLOSED v5.127 for the docs:** `comcon.std.docs.model/render` and `ops.docs(name)` — the manual as a projection of the contract a binding carries (`.contract`), through the grant translation the kernel enforces, with the live epoch. Demo `A_Auditors/A3`. | still open: provenance/signing resources for the trust report | library | M2 registry + tooling verbs |
| **G-17** | 34 | API version as a grant word (`platform.api("v1-compat")`) | per-fragment grants; per-fragment pinned deps | a version dimension on the COM facet | design | — |
| **G-18** | 35 | an append-only `log` facet (append, never read/truncate) | denial log written by the host side only; masks on socket/server caps | a log capability kind | design | M-LIB facets |
| **G-19** | 36 | a stage-0 builder profile that denies clock/RNG/I/O | the tenant's config proposals (declarative, typed, all-or-nothing); anchors; host `root.js` is trusted JS | determinism caps on the host's own stage-0 program | decision | — |
| **G-20** | 38, 42 | `bind(env, pom.query(...))` across modules; a fragment holding a scoped `pom` handle | queries, call sites, anchors, `harden` + epochs on the host; only socket/server caps cross into a compartment | binding a policy to a query result; program handles as grantable capabilities | design | increment D follow-on |
| **G-21** | 41 | re-AOT of a rewritten function while serving | **CLOSED v5.131:** a request-time epoch sends its wrapper text to the master, one detached helper compiles it compile-only and writes an index, every worker adopts the artifacts on its next request (`via: 'master'`); the interpreted epoch serves meanwhile; `unavailable` when it cannot happen. Demo `L_Live_Ops/L4`. | — (one helper at a time; sub-fragments not compiled this way) | C mechanism | — |
| **G-22** | 43 | a static mediation lowered to one `strncmp` | every gate holds on the compiled tier (SR-2, the fuzz, the resource gates); typed lowering measured (§2f) | membrane partial evaluation | design (M5 scope note); the compiler track is closed at M5.1c | M5–M7 |
| **G-23** | 46 | proposals in `nginx.conf` syntax | proposals as config-shaped JS sentences over a typed subtree | an `nginx.conf` grammar front-end for proposals | design | M-CFG follow-on |
| **G-24** | 48, 49 | type annotations, an admission type report, `E_TYPE_MISMATCH`, a HYBRID/`any` gradient | maxim's inference and the language's own types; `aotStatus` per fragment | the M3 typed profile front-end | design | M3/M4 typed IR (not started) |
| **G-25** | 51c | `includeAt` with `expose: {in, out}`: a hygienic text splice into the host's loop body | `harden` rewrites a quotation's sites and installs it as an epoch | structured POM splices; the anchor-fill link | design (folded into increment D) | D5 |
| **G-26** | 5, 6, 13, 14, 20, 21, 25, 28, 29, 37, 40, 41, 46, 47, 50 | a `comconctl` command line | every verb is a library program (`comcon.std.ops`, `std.config`, `std.sessions`) called from host JS; there is no management plane by design (FOUNDATION §8a) | a shell that wraps the verbs (a convenience; not a mechanism) | library | tooling |

## Where the tree is ahead of the scenarios

Written down so the checklist reads in both directions:

- **Budgets that bite on the compiled tier, uncatchable inside the fragment** (10): the
  samples assume a catchable "budget exhausted"; the tree makes a deadline abort uncatchable
  by the fragment (F16) because a tenant that could catch its own deadline could ignore it.
- **The retained-memory half of F2** (10, 33): a leak is charged per call and refused past a
  cap, corrected for cycles — none of the scenarios asked for it.
- **Every negative control automated** (37): the audit's "re-run any of it" is
  `bash t/tools/reviewer-pack.sh`, 34 controls verified.
- **The compiled tier fuzzed against the interpreter** (43, 48): `t/tools/jit-diff-fuzz.py`,
  a gate stage; it found F20 and F21.
- **Learn mode installs at the first include** (5): the compartment is created then, so
  `comcon.mode("learn")` must precede it — a rule the samples do not state.

## Closed since the register was written

| gap | closed | by |
|---|---|---|
| G-13 | v5.127 | `comcon.std.evaluate` (`t/comcon_std_evaluate.t`, demo A2) |
| G-05 (diff + would-deny) | v5.127 | `comcon.std.policy.diff`, `comcon.denials`, `ops.wouldDeny` (`t/comcon_std_policy_diff.t`, `t/comcon_would_deny.t`, demo O3) |
| G-16 (docs) | v5.127 | `comcon.std.docs`, `ops.docs` (`t/comcon_std_docs.t`, demo A3) |
| G-01 | v5.129 | `comcon.withdraw` / `comcon.withdrawn`, `h.withdraw`, `ops.withdraw` / `ops.withdrawn`, the `cap.revoked` code (`t/comcon_revoke.t`, the V12 row, control `revoke-not-checked.patch`, demo S5) |
| G-05 (allow-suite) | v5.130 | `comcon.std.suite` record/cases/tests/check/coverage, `h.guard`, `ops.record/suite/coverage/guard` (`t/comcon_std_suite.t`, control `suite-guard-inert.patch`, demo O4) |
| G-21 | v5.131 | a live epoch compiled by the master's helper and adopted by every worker; `aotStatus().via/pending/unavailable` (`t/comcon_aot_master.t`, control `aot-master-inert.patch`, demo L4) |

## The plan for what is left (2026-09-16, after v5.127)

Twenty-three gaps remain. Grouped by what closing each needs, cheapest evidence first.

**Wave 1 — library only, no C change, an afternoon each.**
- ~~G-05, the allow-suite generator~~ — **DONE v5.130** as `comcon.std.suite`: the pairs are
  recorded in the include result's own callable, emitted as a `tests` quotation, pinned by
  `guard`; coverage at function level from an engine entry counter. OPERATOR_API §8m,
  ASSURANCE G7.26.
- G-16, the rest: the `signing` ops resource (a host-held HMAC key descriptor; one C helper
  for HMAC-SHA256, since host JS has no crypto) and a maker chain `{who, when, hash}` appended
  at `register`/`rebind`; then `trustReport` carries provenance.
- G-26, the shell: `comconctl` as a script over a host-defined admin location written with
  `std.ops` — no directive, no new mechanism.
- G-12, the trust ladder: `probation`/`standard`/`trusted` profiles over shipped words, and a
  widening gate — `rebind` needs an explicit `{admin: true}`, recorded in `bindings()`.
- G-19, the builder profile: include the generator itself with `intrinsics` narrowed (no
  `Date`, no `Math.random`), which the allowance already supports.

**Wave 2 — one C mechanism each, with a maintained control patch.**
- ~~G-01, live revocation~~ — **DONE v5.129** as `comcon.withdraw`: a refcounted grant record per
  wrapper rather than a generation number (a copy's record points at its parent's, so the
  cascade is a walk and a chain outlives its holders); the verb is `withdraw` because
  `revoke()` is the flavour. OPERATOR_API §8l, ASSURANCE G7.25.
- G-14, `protocol` on the server facet and the author capability (operations already named).
- G-18, an append-only log capability kind: one operation, `append`, mediable by every word.
- G-07, a CPU-time meter: `getrusage` delta around the invoke, charged like retained bytes.
- ~~G-21, master-side compilation of a live epoch~~ — **DONE v5.131**, as the design below
  says, with two corrections found building it (workers forbid the compile thread; portable
  codegen for artifacts that cross processes). OPERATOR_API §8n, ASSURANCE G7.27.

**Wave 3 — a written design first.** G-20 (bind by query, grantable program handles), G-25
(`includeAt`/`expose`), G-23 (`nginx.conf`-syntax proposals), G-17 (an API version word),
G-06 (partner edges), G-24 (the typed front-end), G-04 (the REL console), G-09 (signed
manifests, after Wave 1's signing).

**By decision, not scheduled:** G-03, G-10, G-11, G-15, G-22, G-08, G-02.

**Recommended order:** ~~G-01~~ (done v5.129), ~~G-05~~ (done v5.130), ~~G-21~~ (done v5.131). Wave 1's remaining library gaps (G-16 signing, G-26, G-12, G-19) are next by cost.

### G-21: why a live epoch was not compiled in the master, and how it is now (built v5.131 as written below)

Not "cannot" — not built. What exists: a live replace writes `{epoch, source}` to
`nginx.shared` and every worker reconciles on its next request in its own compartment; the
compiled tier's `.so` cache is keyed by the bytecode hash with atomic rename, and a worker
that finds a cached artifact loads it; the master has the gcc thread and the cache directory.
Workers have neither and keep it that way (privilege, N compiles, a tenant's `replace()`
turning into a compile storm). What is missing: (1) a compile request from worker to master
(hash and epoch, on a socketpair or a shared queue), served by the helper thread the master
already runs; (2) the master compiles in the background exactly as at config time and writes
the `.so` under the hash; (3) workers keep serving the interpreted epoch and pick the artifact
up on reconcile — the same lazy pull, no broadcast; (4) two bounds: one compile in flight per
binding with a queue cap, and care with the master's signal handling (the shutdown hang
recorded at v5.127 is a master-side timing defect). The artifact must match the bytecode
exactly (the atom fixup table and the hash enforce it), and the master compiles but never
executes tenant code, as at config time. Two to three days with SR-2 rows and a control.

## Demos that came out of this pass

Four scenarios had shipped code and no demo, and each addresses an audience the first
twelve demos did not: **live operations / release engineering** (41, 28, 40: epochs and
rollback, fleet fan-out, pin-by-hash) and **auditors** (37, 38, 25: the trust report, the
query, the rewrite). They are `js_comcon_demos/L_Live_Ops/` and
`js_comcon_demos/A_Auditors/`.
