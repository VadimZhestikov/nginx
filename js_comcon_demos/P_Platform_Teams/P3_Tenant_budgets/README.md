# P3 — Tenant budgets: time, a burst, a leak

**Audience:** platform teams who need one bad tenant to cost that tenant, and
nobody else.

**In one sentence:** every fragment runs under a deadline, a per-call
allocation allowance and a retained-memory cap, each a word in `meter`; a
tenant that hits one is refused, the worker lives, and the neighbour is served.

## Run

```bash
bash test.sh                       # 8 checks
curl http://127.0.0.1:8202/budgets
```

## What you see

```json
{"spin":      {"threw":"InternalError: interrupted","ms":100},     <- 100 ms deadline, uncatchable inside
 "burst":     {"threw":"InternalError: out of memory"},            <- 1 MB per call
 "burstAgain":{"threw":"InternalError: out of memory"},            <- clean allowance each call
 "leak":      {"served":16,"refused":24,"code":"E_MEM_RETAINED",
               "status":{"retained":1048576,"invocations":16,"refused":24,"cap":1048576}},
 "good":      {"returned":"still served"}}
```

| bound | word | default | what it bounds |
|---|---|---|---|
| deadline | `timeoutMs` | 5 s, never unbounded | wall-clock per invocation; aborts through compiled code too |
| burst | `memoryBytes` | 16 MB | what one invocation may allocate |
| leak | `retainedBytes` | 8 MB | what the fragment holds across calls; charged per call, refused past the cap |

`comcon.memStatus(fragment)` reads the account on the host: retained bytes,
invocations, refusals, the cap in force. A refused leaker stays refused until
its epoch is replaced (the slot is freed and the memory returns) or its
contract raises the cap.

## Why this shape

The deadline is enforced in C at the invocation boundary, so calling the
internals directly cannot skip it, and a fragment cannot catch its own abort:
`try { while(true){} } catch (e) {}` still ends. The burst allowance narrows
the runtime limit for the duration of one call, so growth in that window is
attributable to that fragment. The retained cap is the leak half of the same
question, corrected for cycles at O(1) per call.

## Where to read more

- `OPERATOR_API.md` §3 (the meter words), §8c (host JS is bounded too).
- Assurance: `ASSURANCE.md` G6.6, G7.22; findings F2, F6.
- Tests: `t/comcon_fragment_deadline.t`, `t/comcon_fragment_memory.t`, `t/comcon_retained_memory.t`.
