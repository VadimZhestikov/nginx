# S1 — The attenuation vocabulary

**Audience:** security and compliance teams who need to say precisely what a
piece of untrusted code may do, and read back what it tried.

**In one sentence:** a capability is narrowed by words (`allow`, `redact`,
`revoke`, `ttl`, `uses`, and more), the words compose and only ever narrow, and
each gate that fires leaves a stable denial code.

## Run

```bash
bash test.sh                       # 10 checks
curl http://127.0.0.1:8204/arms
```

## What you see

One probe text, run against the same socket behind six different membranes:

| arm | mediation | the probe sees |
|---|---|---|
| `raw` | none | address, port, fd; `listener` is **null** (the reach edge into the config tree is closed cross-compartment) |
| `allow` | `allow(['port'])` | only `port` |
| `redact` | `redact(['address'])` | everything but `address` |
| `leased` | `ttl(1)` | nothing, after one second (`cap.expired`) |
| `metered` | `uses('s1-demo', 2, 60)` | two reads, then denied (`budget.uses`) |
| `stacked` | `allow` ∘ `ttl(3600)` ∘ `uses(100/60s)` | address and port, still working |
| `revoked` | `revoke()` | the name `s` does not exist |

and the denial counters after: `sock.listener`, `cap.expired`, `budget.uses`.

## The whole vocabulary (ten words)

`allow` / `redact` say **which** operations exist; `uses` says **how often**;
`ttl` and `window` say **when**; `cosign` says **by whom**; `protocol` says
**in what order**; `allowHosts` says **where to** (outbound); `routes` says
**which subtree** (COM nodes); `revoke` says nothing at all. Each composes
with the others by a meet that can only narrow: masks AND, lifetimes MIN,
budgets must be identical (two budgets are not ordered, so a different one is
refused rather than guessed).

**Nothing is defaulted.** A `ttl` of zero, a budget without a limit, an empty
glob: each is refused at `mediate()` with `E_CAP_FLAVOR`. The one direction a
mediation may never take is toward more authority.

**Denials are codes, not text.** `nginx.tenantDenials().byOp` counts each gate
by a name frozen in `t/tools/golden-denials.js`; pin your CI to codes.

## Where to read more

- `OPERATOR_API.md` §2 (`mediate`), §8b, §8d, §8f.
- Tests: `t/comcon_mediate.t`, `t/comcon_cap_ttl.t`, `t/comcon_budget_uses.t`, `t/comcon_v12_denial_codes.t`.
- Next: **S2** for `cosign` and `protocol`, **S3** for `allowHosts`.
