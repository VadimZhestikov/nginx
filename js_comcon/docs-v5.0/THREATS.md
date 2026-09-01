# COMCON — Threat Model (v5.4)

*The security-completeness check the four review passes structurally couldn't do: not
"is this mechanism right?" but "against a structured adversary list, is the set of
mitigations complete?" Each cell cites the mechanism that closes it (Principles, R/V/E/C
findings, S-phases) — making this the skeleton of the M8 assurance case (V15).
Two new findings came out of building it (TM-1, TM-2 — end of document).*

## Assets

**A1** host integrity (nginx process, engine, kernel operators) · **A2** other tenants'
data/secrets (confidentiality) · **A3** other tenants' behavior/config (integrity) ·
**A4** platform availability · **A5** the authority system itself (bindings, epochs,
signing key, provenance/grant chains) · **A6** audit/denial-log integrity · **A7**
secrets flowing through policies (opaque values).

## Adversaries × mitigations

### T1 — Malicious tenant (code author inside a cage)
*Capabilities:* writes arbitrary JS in its fragment; calls anything it can name.
*Blocked by:* deny-by-default NAME rule (nothing outside the env is even nameable);
possession axiom (no operation mints authority; monotone by construction, asserted at
admission — V4); frozen intrinsics + tamed portals (S1/S3); budgets both tiers (S5 +
R4 back-edge gas); grammar-valued facets make injection unwritable (scenarios 3/4);
opaque secrets unprintable/unspliceable (R2 corollary).
*Residual:* semantically hostile values within granted domains (Principle 9 caveat —
bounded by admission types + realizer review); covert/timing channels → T9.

### T2 — Compromised dependency (supply chain)
*Capabilities:* a hijacked npm update; a poisoned transitive dep.
*Blocked by:* per-dep `pure_library` cages with static-harvest policies (E1 install
workflow); **pin-by-hash** — the hijacked update is refused at admission, old epoch
serves (scenario 40, R7); transitive deps = cages in cages; cascade revocation on CVE
day (grant chains); library runs with what the consumer granted, never what it requests.
*Residual:* the platform's own supply chain (QuickJS upstream, wasm2c, GCC/TCC) →
vendored trees + V14 reproducible builds; trusting-trust accepted and named.

### T3 — Hostile or erroneous AI-generated fragment
*Blocked by:* contract admission (env + spec + tests, determinism denied — zero blast
radius at admission itself); the blast-radius property: **capabilities bound damage,
tests only bound correctness** — a misgenerated fragment cannot exceed an env written
before it existed; asymmetric failure for AI-written *policies* (deny too much, never
grant too much).
*Residual:* bad-but-in-cage behavior (wrong answers) — a correctness problem by design,
not a security one.

### T4 — Curious/malicious co-tenant (peer vs peer)
*Blocked by:* unresolvable peer names (scenario 1); mutual protection on communication
edges (both sides consent, refusal binding — scenario 9); per-tenant table-key
namespacing (baked into compiled C); COW views (writes invisible to peers); POM reach
(a tenant's handles cannot see a sibling's subtree — scenario 42).
*Residual:* contention/timing observation of shared resources, and data exfiltration
by a fragment that legitimately reads X and writes Y — **the named IFC gap**
(FOUNDATION §13.4, post-M9 track). This is the largest honest residual in the model.

### T5 — Compromised operator session
*Blocked by:* no management plane (Principle 10) — "administrative" is only the
session's env, so theft yields that env, not root; office-hours/cosign mediations on
dangerous verbs (scenario 31); least-authority realization (R6 — a proposal cannot
trojan the session); operator sessions audited like tenants (trust-report over
sessions); leases/TTL on session grants.
*Residual:* whatever the stolen env legitimately holds, within its windows — bounded,
audited, revocable. **Gap found: TM-2** (session identity → environment mapping is
unspecified; see below).

### T6 — Rogue mid-tier controller (a reseller/policy author gone bad)
*Blocked by:* monotonicity — it can only narrow within its own reach; it cannot widen
itself or touch ancestors/siblings (No-Amplification (c)); its grants to children are
bounded by its own holdings (scenario 8).
*Residual (accepted, documented):* availability-within-reach — it can black-hole its
*own* subtree (DoS-by-narrowing). Inside the trust model: a controller controls its
children.

### T7 — Foreign binary module (WASM-born)
*Blocked by:* validation = admit; imports = the entire world (born-bound, possession
enforced natively); fuel/memory caps; pin; hot-lane wasm2c with SFI preserved **and
back-edge gas + V6 CFG check on its emitted C (C3)**; generation-check revocation.
*Residual:* wasm2c correctness (in the TCB, named); module-internal flat trust domain
(by design — its authority granularity is its import surface).

### T8 — Engine/TCB attacker (memory-safety CVEs)
*Blocked by (reduced, not eliminated):* M-SES S1–S6 (frozen intrinsics, compartments,
tamed portals, facet audit, escape-probe gate); **R11** — the admission front-end named
as pre-sandbox attack surface (fuzzed, resource-limited, isolated parsing); ASAN/UBSAN
CI; upstream tracking; V3 executable-oracle differential testing.
*Residual (accepted, named):* capability discipline is not memory safety — a heap bug
in the C engine defeats everything above it. Defense-in-depth (per-tenant process
isolation) noted, not scoped. This is the model's second-largest honest residual.

### T9 — Side-channel observer / colluding tenants
*Blocked by (partially):* constant-response-time REL profile for the strictest
sessions (forensics, secrets-adjacent); opaque values close the *direct* readout.
*Residual (accepted, named):* timing/cache/contention channels between co-resident
tenants — with IFC (T4), the explicitly-deferred confidentiality axis.

### T10 — Request-level attacker (classic injection, from the network)
*Blocked by:* grammar-valued interfaces (SQL scenario 3, header CRLF scenario 4,
patterns w/o ReDoS); tenant cages bound the blast radius of any tenant-code bug the
attacker finds (scenario 30 — reachability, not review).
*Residual:* host ops not yet facet-ified — closed progressively by the M2+S4 registry
walk; complete when the walk is.

### T11 — Availability attacker (resource exhaustion)
*Blocked by:* budgets/gas on both tiers (S5 + R4 + C3); bounded grammars (patterns);
admission-time limits (R11); per-fragment blast radius; epoch machinery keeps rewrite
windows non-blocking (never stop-the-world).
*Residual:* **TM-1 found here** (denial-log flooding, below); coarse per-runtime
memory attribution (S5's honest deferral) until the substrate decision.

### T12 — Attacker of the authority system itself (A5/A6)
*Blocked by:* ops-resources are first-class caps (third enumeration — no backdoor,
checkable); signing key behind cosign/window mediations; append-only audit facet
(scenario 35: the writer provably cannot rewrite); provenance/grant chains feed cascade
revocation; binding store mutable only downward from inside (bind = meet), widening
administrative-only; schema-hash + content pins close both drift channels (V2, C11).
*Residual:* key compromise = its mediations' bounds (rotate = revoke + re-sign; rides
the same cascade machinery).

## The two new findings

**TM-1 — Denial-log flooding.** A tenant that *intentionally* triggers millions of
denials (a tight loop on a denied name) turns the denial log into a disk/IO exhaustion
vector and drowns the audit signal other tenants depend on. Mitigation (small, must be
in the schema design): **per-fragment denial-log quotas with sampling above quota** —
the record's counting stays exact (denial *counters* per code are cheap), full records
are sampled once a fragment exceeds its quota, and quota-exceeded is itself a reported
(and alertable) condition. Home: the M2.5 denial-schema deliverable.

**TM-2 — Session identity → environment mapping is unspecified.** Everything about
operator security assumes a session *has* an environment — but how an authenticated
principal (human, CI job, AI agent) is mapped to a granted environment (who
authenticates, where the identity→env table lives, how it is itself governed) is
host-integration work that no document owns. It rides the P19 admin-shell substrate.
Home: named as an increment-A integration deliverable (ROADMAP §13) — it must exist
before the first real operator session, i.e., before dogfood.

## Reading the residuals honestly

Three residuals are *accepted and named* rather than closed: engine memory safety
(T8 — reduced by M-SES, never eliminated), information flow/side channels between
co-resident tenants (T4/T9 — the post-M9 IFC track), and availability-within-reach for
controllers (T6 — inside the trust model). Everything else in the matrix cites a
specific closing mechanism. When a future review finds a threat with no cell, it goes
here first and gets a mechanism second — this document is the completeness ledger.

## SR-1 result (the A/B pre-C conformance review, 2026-09-01)

The first gate-review of the cadence (VERIFICATION.md). It audited the *implemented*
A/B capability logic against the claims above, within the current TCB assumption. Result:
the **primary control — deny-by-default environments — verified sound** (a grant-less
tenant has no nameable host authority, no I/O, inert learn-mode recorders, no
`nginx.shared`, pinned deps loaded before capabilities). It found one **HIGH** and three
**MEDIUM** defects in the *defense-in-depth* layer, **all now fixed** (commit
`66dfdb0dd`), each with a regression test (`t/comcon_sr1_regression.t`):

- **HIGH-1 (T1/T4)** — the request handler restored the compartment to HOST_ROOT
  *before* reading the tenant return value, so a `get status()` getter ran under
  HOST_ROOT and `may_reach` passed → a granted-socket tenant could walk the reach cycle
  from a getter. Fixed: `leave()` only after the response is inert C data.
- **MEDIUM-2** — framing/hop-by-hop response headers (`content-length`, …) passed the
  A3.1 guard → response smuggling. Fixed: dropped.
- **MEDIUM-3** — unbounded response header/body sizes. Fixed: per-response caps (the
  64MB runtime cap is the backstop; real budgets = S5, T11).
- **MEDIUM-4** — a granted socket's `close()`/`broadcast()` were ungated. Fixed: gated
  on `may_reach` (`sock.mutate` denial code).

Two findings **accepted/deferred, recorded here** (the ledger discipline):
- **LOW-5** — listener/server *mutators* (`addServer`, `addLocation`, …) carry no
  independent reach gate; they are reachable only by *holding* a foreign listener/server
  object, which the getters (now correctly gated under HIGH-1's fix) no longer hand out.
  Defense-in-depth-in-depth; revisit when POM-rewrite (increment D) widens who holds
  such objects.
- **LOW-6** — the tenant context is a full `JS_NewContext` (eval/Function/Proxy present),
  not `JS_NewContextRaw` + selective intrinsics. Within the TCB assumption these are not
  capability escapes (the global holds no host authority); dropping them is the S1/S3
  hardening lever, deferred to **M-SES**. (`Proxy` made HIGH-1 trivially exploitable, so
  HIGH-1's fix is what actually closed the exposure.)

Verdict: with HIGH-1 closed, the A/B capability logic is a sound foundation for
increment C, within the stated TCB assumption. The full adversarial pentest (engine
escapes) remains SR-3, after M-SES.
