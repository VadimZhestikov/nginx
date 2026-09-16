# P4 — A reseller authors sub-fragments

**Audience:** platform teams whose tenants have tenants: an agency that runs
policies for its own customers, a marketplace vendor that ships plug-ins.

**In one sentence:** the host grants a tenant `author({subFragments: N})`, and
the tenant runs the same admission pipeline on its own sub-fragments, handing
down only copies of what it holds, narrowed further, never wider.

## Run

```bash
bash test.sh                       # 7 checks
curl http://127.0.0.1:8203/reseller
```

## What you see

```json
{"limit":2, "used0":0,
 "sub1":{"addr":"string","port":"undefined"},   <- the reseller's wrapper, port redacted on the way down
 "used1":1,
 "escalate":"E_CAP_ESCALATE",                   <- asking for `fd` it never had
 "namesParent":"E_ADMIT_FREENAME",              <- a sub-fragment cannot name `author`
 "sub2":42, "used2":2,
 "third":"E_AUTHOR_LIMIT"}                      <- two is the host's number
```

## The rules, as the demo shows them

- **Not a mediation, wraps nothing.** `author()` is a descriptor the host's
  `include` grants; the far side is an object whose only verb is `include`.
  It cannot be mediated or re-granted.
- **The contract is the host contract's subset a less-trusted author may
  write:** `imports` (mandatory), `grants`, `attenuate`, `tests`, `timeoutMs`,
  `memoryBytes`. Posture words (`onViolation`, `profile`), `identity`, `deps`
  and `meter` are refused: the posture is the parent's.
- **Copy, then narrow.** A grant must be a wrapper the reseller itself holds;
  the child gets a copy with the owner changed. `attenuate` can move two
  things downward: the field mask and the expiry. A subset of the parent's
  mask is fine; a superset is `E_CAP_ESCALATE`.
- **The count is live.** A callable is an object with a finalizer; dropping
  the last reference refunds the slot. A reseller that authors per request
  and drops the result spends nothing lasting.
- **What crosses is text.** JSON in, JSON out, synchronous; an exception
  arrives as a message and a string code, never an object.

## Where to read more

- `OPERATOR_API.md` §8j (`author`).
- Assurance: G7.16 (re-grants copy, never re-wrap), finding F16.
- Tests: `t/comcon_author_basic.t`, `t/comcon_author_regrant.t`, `t/comcon_author_depth2_gate.t`.
