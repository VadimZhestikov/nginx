# COMCON — The Typed Schema (C2)

*The machine-readable type surface a policy is written against — the input to the typed
profile (C3, the front-end type-checks fragments against it) and the erasure oracle for
lowering (C5). This document is the normative description; the machine-readable form is
`../schema/tenant-env.schema.json`. First cut: the small, real **tenant environment**;
the host COM surface (typed from the `describe()` registry) is a later extension (§4).*

---

## 1. What it is, and why it is data

A COMCON policy is admitted against a **contract**, and the contract's type half is *this
schema*. Two consumers:

- **C3 (the typed-profile front-end)** type-checks a fragment: every free name must
  resolve in the bound environment *and* be used at its declared type. Types "only reject
  (at admission) and accelerate (at T2)" — erasure soundness.
- **C5 (lowering)** reads the same types to emit unboxed C (an `int` field unboxes; a
  `string` is a rope handle) — and, crucially, the erasure guarantee means the interpreted
  and compiled tiers behave identically with types ignored.

The schema is *data*, not code, for the same reason the `describe()` registry is data:
one source of truth, machine-consumable, and checkable against reality (drift is a test
failure, not a latent bug — §3).

## 2. The tenant environment (the real surface today)

A confined tenant is **deny-by-default**: its environment is exactly the names below, plus
any host grants. `tenant-env.schema.json` gives the full machine form; the shape:

- **`report(msg: string): undefined`** — the one always-granted capability (log a line).
- **`onRequest(handler: (Request) => Response): undefined`** — register the request
  handler; its return value *is* the response.
- **`Request = { method, uri, args: string, headers: Record<string,string> }`** — plain
  request *data*, a copy, no capability.
- **`Response = string | { status?: int, body?: string, headers?: OutHeaders }`** — the
  tenant's *entire* authority over the response. `OutHeaders` carries the SR-1 guards as
  type domains (token names; no CR/LF; framing/hop-by-hop names rejected; bounded size
  and count).
- **Host grants** (optional, via `nginx.grantToTenant`) — e.g. a **`Socket`** whose
  scalar reads are typed and whose reach edges (`.listener`) and mutators
  (`close`/`broadcast`) are marked **gated** (the A1 compartment checks): the type says
  *what the tenant holds*; the gate says *what it may reach*.

## 3. The numeric discipline (V1) — and how the schema carries it

JS doubles are normative in **both** tiers. The typed integer `int` is a safe-integer
refinement (`|x| ≤ 2⁵³−1`, `Number.isSafeInteger`). The schema encodes the load-bearing
rule as a `numeric` block and per-field `domain`s:

> **No host op may expose a numeric domain past the safe-integer range** — **ms** not ns
> for timestamps, scaled units for large quantities, **strings/opaque handles for true
> 64-bit ids**. Enforced per schema row.

The tenant surface today only carries small ints (`status`, `port`, `fd`), all safely in
range — but the rule is stated now so the first granted clock/counter cannot violate it.

**Grounded, not aspirational.** `t/comcon_schema_conformance.t` runs a tenant that probes
every schema entry against the *running* surface (each field's presence and JS type, the
`int`s as `Number.isSafeInteger`, the `Response` object by returning one, `.listener`
gated to null cross-compartment). If the implementation ever drifts from the schema, the
test fails — the `describe ⊇ reality` discipline, applied to the schema.

## 4. What is deferred (the honest edge)

- **The host COM surface.** A *host*-authored policy that is granted COM access needs the
  large typed surface — and that comes from the `describe()` registry (the reality-check
  finding: the `type` column already exists; M2/S4 add the read-only-getter rows for the
  reach paths). That is a data extension of this schema, not a new mechanism; it lands
  with the S4 registry walk.
- **Schema hashing.** C4's fragment artifact pins a `schema-hash`; the artifact identity
  `H(source ∥ schema-version)` (SPEC §8) uses this file's `version`. Wiring the hash into
  admission is C4.
- **The type grammar itself.** The `kind` vocabulary here (`object`/`record`/`union`/
  `opaque`/`function`/`method`) is the minimum for the tenant surface; C3 fixes the full
  type language (the Misty-core subset it checks).
