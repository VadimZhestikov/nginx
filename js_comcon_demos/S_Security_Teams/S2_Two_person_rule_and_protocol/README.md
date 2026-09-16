# S2 — The two-person rule, and enforced operation order

**Audience:** security teams with operations that need a second pair of hands
(key rotation, a production switch) and capabilities that must be used in a
fixed order.

**In one sentence:** `cosign` makes an operation run only after a quorum of
distinct principals has attempted it, and `protocol` makes a capability's
operations legal only in the declared order.

## Run

```bash
bash test.sh                       # 11 checks
curl 'http://127.0.0.1:8205/attempt?who=alice'
curl 'http://127.0.0.1:8205/attempt?who=alice'   # still one consent
curl 'http://127.0.0.1:8205/attempt?who=bob'     # quorum met: the intent is queued
curl  http://127.0.0.1:8205/protocol
```

## What you see

```
alice  -> {"result":"denied",  "queued":0,"denials":{"cap.cosign":1}}   denied; consent recorded
alice  -> {"result":"denied",  "queued":0, ...}                          same principal: no second vote
bob    -> {"result":"recorded","queued":1, ...}                          quorum of 2 met; the host has the intent
```

```json
{"inOrder":   ["address","port","port","fd"],   <- protocol('address','port*','fd')
 "skipStar":  ["address","fd"],                 <- a starred step may happen zero times
 "outOfOrder":["-","address"],                  <- fd first is denied; the cursor stays, address still works
 "oneShot":   ["fd","-","-"]}                   <- protocol('fd'): after the last step the conversation is over
```

## The rules, as shown

- **There is no `approve()` verb.** Calling the gated operation records the
  caller's principal; the second operator simply retries. A `cap.cosign`
  denial is the one denial with a side effect: it means "go find a colleague".
- **`as` is written on the trusted side.** COMCON does not authenticate; the
  host asserts the principal (an mTLS subject, a verified JWT), so a fragment
  holds exactly one identity and casts exactly one vote. Distinctness is
  structural, not checked.
- **The record is a set of principals**, fleet-wide in `nginx.shared`, anchored
  at the first signature and expiring after `within` seconds.
- **`protocol` enforces order, not completion.** "You cannot take the fd before
  you have looked at the address" is checkable at the call; "you must
  eventually close" is not, and the word does not pretend otherwise.
- **The cursor is per wrapper**, and a violation does not advance it.

## Where to read more

- `OPERATOR_API.md` §8g (`cosign`), §8h (`protocol`).
- Tests: `t/comcon_cap_cosign.t`, `t/comcon_cap_protocol.t`.
