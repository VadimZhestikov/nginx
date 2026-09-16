# O2 — The denials dashboard, and the switch

**Audience:** operators and SREs who run the fleet and get paged when a gate
fires.

**In one sentence:** every gate that fires leaves a stable code in a counter
the host can read, every reason a fragment was not admitted is a stable code
on the error, and one switch turns the fleet from denying to observing.

## Run

```bash
bash test.sh                       # 14 checks
curl http://127.0.0.1:8209/dash
curl http://127.0.0.1:8209/probe                # denied
curl 'http://127.0.0.1:8209/mode?set=audit'
curl http://127.0.0.1:8209/probe                # allowed, still counted
curl 'http://127.0.0.1:8209/mode?set=enforce'
```

## What you see

```json
{"mode":"enforce","total":0,"fired":{},
 "denialCodes": ["sock.listener","listener.read","listener.serverByName","enum.sockets","sock.mutate",
                 "budget.uses","cap.expired","out.host","out.drain","cap.window","cap.cosign",
                 "cap.protocol","cap.owner"],
 "refusalCodes":["E_ADMIT_ARG","E_ADMIT_FREENAME","E_ADMIT_DYNCODE","E_ADMIT_TEST", …,
                 "E_CAP_FLAVOR","E_CAP_ESCALATE","E_AUTHOR_LIMIT","E_MEM_RETAINED", …]}
```

Then the same probe: `"denied"` under enforce, `"allowed (8259)"` under audit,
counted either way.

## Two axes, kept apart

| axis | when | where to read | pin in CI as |
|---|---|---|---|
| **denial** | a gate fired while the fragment was running | `nginx.tenantDenials().byOp` | `sock.listener`, `cap.expired`, … |
| **refusal** | the fragment was never admitted | `e.code` on the thrown error; `comcon.refusalCodes()` | `E_ADMIT_FREENAME`, `E_CAP_FLAVOR`, … |

"My policy tripped `sock.listener` on request 41" and "my policy will not
load" are different failures with different fixes. Both sets are frozen in
`t/tools/golden-denials.js`, and a code cannot be added to the engine without
a row there.

## The switch

`comcon.mode('audit')` makes every gate on the worker log and allow, so a
limit can be watched before it is switched on. It is fleet-level and per
worker; for one tenant at a time use the binding's own `onViolation` word
(demo **P2**). An unknown mode is refused rather than read as something. The
counters are per worker (`nginx.workerId`): a dashboard across a fleet sums
them or reads each.

## Where to read more

- `OPERATOR_API.md` §8i; `MANUAL.md` §3.2 (codes are the tenants' CI contract).
- Tests: `t/comcon_v12_denial_codes.t`, `t/comcon_mode_fanout.t`.
