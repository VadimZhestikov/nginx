# INCREMENT — M-LIB: the standard policy library — scope

**Status:** 🚧 IN PROGRESS 2026-09-12 (docs v5.52). **Step 1 ✅** — `comcon.std` with
`profiles.tenant` / `profiles.pure_library` / `describe()`, and the closed mediation
vocabulary that step 1 had to fix first. **Step 2 ✅** — `std.ops`: administration as
library code (§6), which found and fixed a silently-inert runtime `comcon.mode()`.
Further profiles and the posture vocabulary are NOT built (see §4).

## 1. Why this is the next increment

ROADMAP M-LIB: **"the user-facing surface is not the kernel but the combinators."**
Increment D closed the confinement track, which means the kernel is finished — and
finished is not the same as usable. Before step 1 there were **20 kernel operators and no
`std.*` at all**, while `MANUAL.md` was written as-if-shipped against
`std.profiles.tenant(acme)`, `std.postures.lockdown` and `std.ops`.

The gap is not ergonomic decoration. A correct `include()` call requires four contract
fields to agree with an environment built by three other operators, and getting the
agreement wrong fails in two opposite directions: omit a granted name from `imports` and
admission refuses the fragment (fail-closed but baffling); list a name that is not granted
and the fragment sees `undefined` at runtime. **A profile makes that agreement once.**

## 2. The one rule for what may go in a profile

**Only fields the kernel actually ENFORCES.** `MANUAL.md`'s `{profile:"restrictive",
onViolation:"audit"}` and `std.postures.*` are not shipped because *nothing reads them* —
`realize()` knows only `profile:'declarative'`. A posture assembled from ignored keys would
read like a policy and do nothing, which is worse than its absence: it would be believed,
and by exactly the reader who is least able to check.

`comcon.std.describe()` is the honesty surface: per contract field, **what enforces it**,
plus an `absent` list naming the vocabulary that is deliberately not here.

| field | enforced by | effect |
|---|---|---|
| `imports` | admit free-name gate (C3) | refuse |
| `grants` | include: caps re-wrapped compartment-native | authority |
| `checkRequest` | admit request-field predicate | refuse |
| `meter` | worker request deadline | interrupt |
| `identity` | C4 artifact pin (sha256) | refuse |
| `tests` | admit test phase, run in the compartment | refuse |
| `deps` | pinned dep eval, bound as closure params | authority |

## 3. What step 1 shipped

- **`std.profiles.tenant(env, opts)`** — an untrusted fragment. `grants` come from the env
  and **`imports` is DERIVED from it** (`Object.keys(env.grants)`), so the manifest cannot
  drift from the grants. Admission on, request-field checks on, and **bounded by default**
  (100ms): the audit's §3 gap list names *"host JS unbounded by default"* as accepted
  residual risk, and a tenant profile inheriting only the 5s fragment ceiling would repeat
  it deliberately. `opts` overrides `meter` and supplies `identity` / `tests` / `deps`.
- **`std.profiles.pure_library(opts)`** — a cap-free computation: `grants {}` and
  `imports []`. `imports` is *present*, which is what switches admission on, so **any
  undeclared HOST name refuses the fragment** — while language intrinsics need no
  declaration (the C3 intrinsics allowance, v5.54), so such a fragment can actually compute
  with `JSON` and `Object` rather than arithmetic alone. Before that decision it could not,
  which made this profile stricter than "cap-free" suggests; `Date` and `Math` still have to
  be declared. **`pure_library({intrinsics: []})`** goes the other way and is the strictest
  contract expressible — no free names at all, not even language values. It is *not* the
  default: tightening a shipped profile silently would break fragments already computing
  with `JSON`. The strongest fully-enforced profile, and the right default for
  third-party code that should only compute.
- **`std.describe()`**, **`std.version`** (`comcon-std-1`), frozen namespace and frozen
  contracts.

### The fail-open step 1 had to close first

A library generates mediation descriptors mechanically, so their failure mode is the
library's failure mode. `include()`'s flavor translation fell through to its default
`{kind:0, mask:FULL}`, so a descriptor the enforcement layer does not implement
(`allowHosts`) or a one-letter typo (`redcat` for `redact`) **granted the capability in
full** — a misspelling that *widened* authority. Measured before fixing: the fragment read
`s.address` as a string through both, where `redact()` hid it.

The vocabulary is now **closed** (`revoke`, `redact`, `allow`, `routes`), refused at
`mediate()` — stage 0, at the producer — and `mediate()` **snapshots** the descriptor,
because validating at `mediate()` and reading at `include()` is a time-of-check/time-of-use
gap that reopened the same hole from the other end:

```js
var it = redact(['address']); var m = mediate(sock, it);
it.flavor = 'redcat';                 // used to reach the translation unknown
```

`include()`'s fall-through still throws, and is deliberately kept although no test can
reach it: what it guards is **drift** between two lists of the same closed vocabulary, and
the fall-through decides whether that mistake means REFUSE or FULL AUTHORITY.

`t/comcon_std_lib.t` (19), seven negative controls.

## 4. Not built — and why each is absent rather than pending

- **`std.postures.*` / `onViolation` / `profile:'restrictive'`** — nothing enforces them.
  They arrive with the enforcement, not before it.
- **`allowHosts` / `uses` / `ttl` / `window` / `cosign` / `protocol` / `opaque.*`** — each
  needs C-side enforcement (the current membrane is a socket field mask or a route glob).
  Shipping them as descriptors would be shipping policy that does nothing.
- **`std.ops`** (the comconctl verbs as library programs over ops-resource caps,
  FOUNDATION §8a) — not started; the natural step 2, and it needs the ops-resource caps
  first.
- **"raw operators withheld"** — the governance half. Today `comcon.*` is HOST_ROOT-only, so
  a library user *is* the operator; withholding requires a second compartment for library
  consumers, which is its own increment.
- **Interceptor certification criteria** — ROADMAP calls interceptors TCB-adjacent because
  they close over capabilities. **In this implementation they do not:** the four flavors are
  inert descriptors interpreted in C, never functions. The criteria become necessary if a
  function-valued interceptor is ever introduced; the closed vocabulary is what keeps that
  door shut for now.

## 5. Verification

`t/comcon_std_lib.t` asserts the profiles end-to-end (a mediated tenant reads `port` and not
`address`; a pure library computes and is refused any free name), that `imports` is derived
from the env, that a tenant is bounded by default, that `describe()` names both the enforced
fields and the absent vocabulary, and the whole closed-vocabulary story including the TOCTOU.
Seven controls, each reverting one decision and confirming the named assertion fails.

## 6. Step 2 — `std.ops`: administration as library code

FOUNDATION §8a: **there is no management plane.** comconctl is a shell, not a tool; every
verb is an ordinary library program over the four kernel operators plus the **ops-resource
capabilities**, so v2 §9.3's *"the tooling never needs a backdoor"* is **derived** rather than
asserted. `std.ops` is that derivation, made executable.

```js
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode,
                           learn: nginx.tenantLearning, bindings: true });
```

**A session takes its resources as arguments and reaches for no ambient authority.** A verb
whose resource was not passed is **absent from the session**, not present-and-throwing, so
"what can this session do" is answered by `Object.keys(ops)` rather than by reading the
implementation. A session given nothing has only `describe()` — the no-backdoor property,
visible.

### The third closed enumeration, made checkable

All seven ops resources of §8a are enumerated, **including the two with no host spelling**:

| resource | host spelling |
|---|---|
| `log` — denial/observation log | `nginx.tenantDenials()` |
| `learn` — learning recorder | `nginx.tenantLearning()` |
| `mode` — audit/enforce/learn switch | `comcon.mode()` |
| `bindings` — binding/epoch store | the session's own record of `bindAt` handles |
| `broadcast` — class-F channel | `nginx.shared` (via `bindShared`) |
| `provenance` — grant-chain registry | **none** |
| `signing` — signing key | **none** |

`host: null` is what makes those two gaps *checkable* instead of invisible, and the verbs
needing them (`revoke`, `cosign`/office-hours) are reported as withheld **with the reason**.
`describe()` walks the same table the session is built from, so the enumeration cannot drift
from reality (ROADMAP §12 **V7**: generated, never maintained).

### The fifteen verbs, and what they decompose to

`denials` · `learn` (reads over the report caps) · `shadow`/`enforce`/`learnMode` (the
audit-first rollout = `comcon.mode`) · `register`/`bindings`/`snapshot`/`rebind`/`rollback`/
`remove`/`revive` (the binding-epoch store = `bindAt` handles; **snapshot = quote**) ·
`rewrite` (§38 in one verb: `harden` + rebind) · `trustReport` · `describe`.

- **`remove` is class X** (POM.md §3) and is guarded the way that class demands:
  snapshot-first **plus a confirmation that NAMES the binding**, so a `{confirm:true}` pasted
  from another call cannot remove the wrong one.
- **`shadow`/`enforce` read the mode BACK** — through the denial report when the session holds
  it, which is a different path from the setter — so a verb cannot report a switch that did
  not happen.

### The defect step 2 found

`comcon.mode()` wrote `jcf->tenant_mode`, but the mode that **gates** is a static set once by
`ngx_js_compartment_policy_init()` at the end of config load. So `mode()` took effect during
the host eval and was **silently inert at request time** — exactly when an operator runs it.
An operator calling `enforce()` on a running server got "ok" and kept **auditing**: still
allowing what they believed they had begun denying. It surfaced because `std.ops` reads the
mode back through the denial report and the two disagreed.

Fixed with `ngx_js_compartment_mode_set()`, which switches the effective mode **without
resetting the counters** — switching audit → enforce must not destroy the audit evidence that
justified the switch. `comcon.mode()` now also returns the effective mode name, so a caller
can verify rather than trust. **The switch is PER PROCESS**: there is no fleet-wide mode
fan-out (that wants the class-F transport, as `bindShared` does), and `std.ops` reports the
scope rather than implying otherwise.

`t/comcon_std_ops.t` (26) asserts the rollout as **enforcement**, not as a label: the same A1
reach probe returns `null` (denied) under `enforce` and a real listener under `shadow`,
switched at request time — plus six negative controls.
