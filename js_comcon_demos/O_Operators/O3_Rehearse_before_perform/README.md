# O3 — A policy change rehearses before it performs

**Audience:** operators rolling a tightened policy onto one tenant while
every other tenant stays as it is.

**In one sentence:** the diff predicts (narrowing, auto-safe; or widening),
the candidate runs shadowed on live traffic beside the current binding, its
own would-deny rows say what enforce would refuse (four of six requests, here,
to a budget the current policy never had), and the flip is the same text with
one word changed.

## Run

```bash
bash test.sh                       # 12 checks
curl http://127.0.0.1:8216/diff
for i in 1 2 3; do curl -s http://127.0.0.1:8216/t; echo; done
curl http://127.0.0.1:8216/rehearsal
curl http://127.0.0.1:8216/perform
```

## What you see

```json
/diff       {"candidate":{"verdict":"narrowing","autoSafe":true,"changes":[
               {"path":"grants.s.ttlSeconds","to":3600,"direction":"narrowing"},
               {"path":"grants.s.budget","to":{"key":"acme:port","limit":2,"window":3600},"direction":"narrowing"},
               {"path":"meter.timeoutMs","from":200,"to":100,"direction":"narrowing"}]},
             "looser":{"verdict":"widening","autoSafe":false,"changes":[…"checkRequest"…]}}
/t          {"served":{"seen":["port"]},"shadowed":{"seen":["port"]}}        <- audit logs and allows
/rehearsal  {"live":{"posture":"deny","observing":false,"total":0,"events":[]},
             "candidate":{"posture":"audit","observing":true,"total":4,"events":[{"op":"budget.uses","n":4}]}}
/perform    {"enforced":{"seen":[]},"denials":{"posture":"deny","observing":false,…}}
```

## The three verbs

- **`comcon.std.policy.diff(before, after)`** compares two contracts axis by
  axis in the lattice the kernel uses: masks by inclusion, lifetimes and
  deadlines by size, budgets by limit under the same key, quorums up and
  windows down, globs and protocols by identity only. `narrowing` is
  auto-safe (nothing gained authority); `incomparable` is what two globs
  are, never guessed. The reading is the kernel's own grant translation,
  not a second one.
- **`onViolation: "audit"` on one binding** shadows the candidate while every
  other binding stays enforced (demo **P2**).
- **`ops.wouldDeny(binding)`** reads the binding's *own* denial rows (the
  gates that fired while it ran, attributed to it by the invoke) against its
  posture: under audit they are what enforce would have refused. A mask is not
  a gate (a redacted field is simply absent, never counted), so what a
  rehearsal observes is the gated vocabulary: budgets, leases, windows,
  cosignatures, protocols, reach edges, outbound globs.

## Where to read more

- SHOWCASE §14 and §5 (`REAL CODE`); `SHOWCASE-gaps.md` G-05 (closed v5.127).
- Tests: `t/comcon_std_policy_diff.t`, `t/comcon_would_deny.t`, `t/comcon_posture.t`.
