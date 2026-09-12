# INCREMENT — M-LIB: the standard policy library — scope

**Status:** 🚧 STARTED 2026-09-12 (docs v5.51). **Step 1 ✅** — `comcon.std` with
`profiles.tenant` / `profiles.pure_library` / `describe()`, and the closed mediation
vocabulary that step 1 had to fix first. `std.ops`, further profiles, and the posture
vocabulary are NOT built (see §4 for why each is absent rather than pending).

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
  `imports []`. `imports` is *present*, which is what switches admission on, so **any** free
  name refuses the fragment. The strongest fully-enforced profile, and the right default for
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
