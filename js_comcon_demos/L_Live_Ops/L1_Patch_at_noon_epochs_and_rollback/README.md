# L1 — Patching a hot function at noon: epochs and rollback

**Audience:** live operations and release engineering: the people who must fix
the busiest tenant's hottest function without a reload window.

**In one sentence:** a live binding is a sequence of epochs; a fix is admitted
like any fragment and swapped in as the next epoch, the previous one is
retained for a one-call rollback, and a site can be tombstoned and revived.

## Run

```bash
bash test.sh                       # 12 checks
curl -D- 'http://127.0.0.1:8212/price?qty=12'     # epoch 0, total 3000 (the bug)
curl 'http://127.0.0.1:8212/ctl?op=patch'
curl -D- 'http://127.0.0.1:8212/price?qty=12'     # epoch 1, total 2700 (the fix)
curl 'http://127.0.0.1:8212/ctl?op=rollback'
```

## What you see

```
x-epoch: 0   {"version":"v1","qty":12,"total":3000,"discount":0.1}   <- computed, never applied
op=patch     {"epoch":1}
x-epoch: 1   {"version":"v2","qty":12,"total":2700,"discount":0.1}
op=rollback  -> total 3000 again
op=remove    -> HTTP/1.1 410 gone ;  op=revive -> 200
op=stress    {"delta": <small>}     <- 300 replacements, the heap where it was
op=tier      {"aot":{"jit":…,"functions":…,"compiled":…}}
```

## How it works

`comcon.bindAt(site, quotation, contract)` admits the quotation and installs it
through a *site*: a function you write that wires the callable into
`location.handler` (the ordinary js_com setter; there is no second install
path). The handle is the epoch machine: `replace(q)` admits the new text and
swaps it in as epoch n+1, keeping the old one; `rollback()` restores it;
`remove()` tombstones the site; `revive()` brings it back; `describe()` lists
the ops with their safety classes. The rollback history is bounded and
superseded fragments are freed, which is what the stress step measures.

**Honest limit.** A new epoch built in a worker runs on the bytecode tier and
stays there: no compiler thread survives `fork()`. `comcon.aotStatus(f)` reports
that rather than claiming native, and the safety property still holds: the new
epoch's behaviour is what serves, never a stale native artefact
(`t/comcon_aot_epoch.t`). For a fleet of workers see **L2**.

## Where to read more

- SHOWCASE §28 and §41 (their `REAL CODE` blocks); `POM.md` §4; `INCREMENT_D.md` D4a.
- Tests: `t/comcon_pom_mutate.t`, `t/comcon_aot_epoch.t`.
