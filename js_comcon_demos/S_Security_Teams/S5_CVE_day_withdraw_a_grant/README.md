# S5 — CVE day: withdraw a grant while the tenant is serving

**Audience:** security teams, and anyone who has redeployed at 2 a.m. because a library
shipped a compromised update.

**The question:** a tenant's code uses a vendor library that we granted an outbound
capability. The library is compromised. Can we take *that* authority back — from the
tenant, from the library, from everything the tenant lent it to — without a reload, a
redeploy, or reading the tenant's code?

## What you see

```
$ bash test.sh
== 1. before: the tenant and the library it delegated to both reach the vendor ==
  ok  the tenant's own call reaches the vendor
  ok  the library's call, on the delegated copy, too
== 2. CVE day: withdraw the vendor grant, no reload, no redeploy ==
  ok  class X: the confirmation must name the binding
  ok  the grant is withdrawn, and one delegation was reached      {"revoked":["out"],"delegated":1}
  ok  the tenant's own call is denied
  ok  the library's copy died with it (the cascade)
  ok  the socket grant is untouched
== 3. no posture lifts it: audit still denies ==
== 4. the tenant ships a fix: the new epoch holds the same dead grant ==
== 5. what the auditor reads ==                                   `out` -- REVOKED: every use denies as cap.revoked
== 6. offboarding: the same verb with no grant name ==            {"revoked":["out","s","author"],"delegated":1}
```

## How it is shaped, and why

- **The grant is the switch.** `ops.withdraw("acme", "out", { confirm: "acme" })` flips the
  grant record the tenant's `out` wrapper points at. Every granted wrapper has one; a
  re-grant (the tenant's `author.include(..., { grants: { out: out } })`) gives the copy a
  record of its own *under* the original's. The gate walks up — so the library's copy dies
  with the tenant's grant, and `delegated: 1` says the withdrawal reached it.
- **`cap.revoked` is unconditional**, like `cap.owner`: the operator who withdrew a grant is
  exercising a policy, not observing one, so audit mode denies it too.
- **It sticks to the binding.** The tenant's `replace()` is admitted under the same contract
  and gets the same grants — and the same revocation. A rollback cannot lift it either.
  Nothing does but a new admission under a new contract, which is the reviewed path.
- **Class X, with a naming confirmation**, like `ops.remove`: a confirmation pasted from
  another call cannot withdraw the wrong tenant's grant.
- **The verb is `withdraw`**, not `revoke`, because `comcon.revoke()` is already the
  grant-time flavour (narrow to zero at admission). One name for two acts would let a
  mistaken call return a descriptor where a revocation was meant.
- **Offboarding** is the same verb with no grant name: every grant, and every delegation.

## Tests that pin this

`t/comcon_revoke.t`; the `cap.revoked` row in `t/comcon_v12_denial_codes.t`; the negative
control `t/tools/controls/revoke-not-checked.patch`. OPERATOR_API §8l; ASSURANCE G7.25.
