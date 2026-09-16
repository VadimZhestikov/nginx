# P2 — Observe first, then enforce

**Audience:** platform teams rolling a new tenant policy into a fleet that is
already enforcing everyone else's.

**In one sentence:** the fleet starts in learn mode so a tenant written against
more than it was given is harvested rather than killed; the posture (`audit` or
`deny`) is a word in each binding's contract that wins whatever the fleet is
in; when the environment is settled, one call flips the fleet to enforce.

## Run

```bash
bash test.sh                       # 12 checks
# by hand:
../../../objs/nginx -p . -c nginx.conf; sleep 1.5
curl http://127.0.0.1:8201/postures            # fleet: learn
curl http://127.0.0.1:8201/learn               # the greedy tenant, harvested
curl 'http://127.0.0.1:8201/mode?set=enforce'  # the flip
curl http://127.0.0.1:8201/postures            # the no-word binding now denies
kill -QUIT $(cat logs/nginx.pid)
```

## What you see

```json
{"fleet":"learn",
 "shadow": {"addressType":"string",   "portType":"number"},     <- audit: logged, ALLOWED
 "enforce":{"addressType":"undefined","portType":"undefined"},  <- deny, whatever the fleet is in
 "inherit":{"addressType":"string",   "portType":"number"},     <- follows the fleet (learn allows)
 "denials":{"cap.expired":N, ...}}
```

The three results come from **the same tenant text**, bound three times over a
socket capability whose one-second lease has expired. Under `onViolation:
"audit"` the expired read is logged and allowed; under `"deny"` it is denied;
with no word the binding inherits the fleet. After `/mode?set=enforce` the
no-word binding denies and the other two are unchanged.

`/learn` runs a tenant that reaches for `nginx.http.addServer`, `createSocket`
and `fetch`. In learn mode nothing is fatal: the reaches are harvested into
`nginx.tenantLearning()` with hit counts, which is the raw material for the
contract you write next (`js_com_demos/COMCON_onboard` turns that record into
a paste-ready stub).

## Why per binding

`comcon.mode('audit')` switches the whole worker. Shadowing one tenant's new
policy that way also stops enforcing every other tenant's, which is a strictly
worse posture than the one you are trying to reach. The binding's own word
wins in both directions: a `deny` binding enforces while the fleet is in
audit, and an `audit` binding is shadowed while the fleet enforces. The
override is applied in C for one invocation and restored afterwards, even when
the fragment throws.

## Where to read more

- `OPERATOR_API.md` §8i (`onViolation` and `profile`), §8d (`ttl`).
- Tests: `t/comcon_posture.t`, `t/comcon_include_learn.t`.
