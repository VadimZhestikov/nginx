# L2 — Fleet fan-out: one replace, four workers

**Audience:** live operations on a multi-worker nginx, where "the fix is in"
must mean every worker, and a half-updated fleet is the failure to prevent.

**In one sentence:** a shared binding keeps its current epoch and source in
`nginx.shared`; a replace on whichever worker took the control request reaches
every worker on its next request, and no request is ever served from a torn
state.

## Run

```bash
bash test.sh                       # 7 checks; nginx runs 4 workers
for i in 1 2 3 4 5 6; do curl -sD- http://127.0.0.1:8213/g | grep -E 'x-epoch|x-worker|version'; done
curl 'http://127.0.0.1:8213/ctl?op=patch'
for i in 1 2 3 4 5 6; do curl -sD- http://127.0.0.1:8213/g | grep -E 'x-epoch|x-worker|version'; done
```

## What you see

```
before   x-epoch: 0  x-worker: 2  {"version":"v1",...}     (workers 0..3 in any order)
patch    {"op":"patch","onWorker":1,"epoch":1}             <- ONE worker executed it
after    x-epoch: 1  x-worker: 3  {"version":"v2",...}     <- every worker, 24 of 24
remove   HTTP/1.1 410 on every worker ; revive -> 200 on every worker
```

## How it works

`comcon.bindShared(key, quotation, contract, onRequest)` is the multi-worker
spelling of `bindAt`. The handle's `replace(q)` writes `{epoch, source}` to
`nginx.shared`; `h.handler` reads the shared epoch on each request and, when it
is newer than the one this worker compiled, recompiles the source in *this*
worker's compartment and swaps before answering. There is no broadcast and
nothing but text crosses processes. The same lazy-pull carries the fleet
posture (`comcon.mode`), so `ops.enforce()` on one worker reaches all of them
(`t/comcon_mode_fanout.t`).

## Where to read more

- SHOWCASE §41 (`REAL CODE`); `INCREMENT_D.md` D4b; `POM.md` §4 (class F).
- Tests: `t/comcon_pom_fanout.t`, `t/comcon_mode_fanout.t`.
