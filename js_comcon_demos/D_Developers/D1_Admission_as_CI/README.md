# D1 — Admission as a CI step

**Audience:** developers writing fragments, and the people who review them.

**In one sentence:** `comcon.admit(fn, contract)` returns a verdict with a
stable code without running the function, the contract's tests run against
the compiled fragment inside the compartment, and a fragment that fails
either never reaches a request.

## Run

```bash
bash test.sh                       # 11 checks
curl http://127.0.0.1:8210/admit
```

## What you see

```json
{"clean":    {"certified":true},
 "freeName": {"certified":false,"code":"E_ADMIT_FREENAME","reject":"free name not declared in imports: nginx"},
 "declared": {"certified":true},
 "dynCode":  {"certified":false,"code":"E_ADMIT_DYNCODE", "reject":"dynamic-code …"},
 "notFn":    {"certified":false,"code":"E_ADMIT_ARG",     "reject":"admit: arg0 must be a function"},
 "testPass": {"admitted":true,"result":42},
 "testFail": {"admitted":false,"code":"E_ADMIT_TEST","why":"…want 999…"},
 "testReach":{"admitted":false, …},                       <- the test reached for nginx; there is none
 "fields":   {"admitted":true,"result":"…"},
 "fieldsNo": {"admitted":false, …},                       <- req.bogus: no such Request field
 "refusalCodes":["E_ADMIT_ARG","E_ADMIT_FREENAME", …]}
```

## The three gates, in order

1. **Structural.** Every free name must be in `imports`; `eval`, `Function`
   and friends are refused; the argument must be a function (or source text
   for `include`). Nothing is executed.
2. **Tests.** `contract.tests` is a function (or its text) given the compiled
   fragment. It runs *in the confined compartment*, so a test cannot reach the
   host either; a thrown error refuses admission with `E_ADMIT_TEST`.
3. **The request shape.** With `checkRequest: true`, every `req.<field>` the
   fragment reads must be a field a Request actually has (the shape is
   sealed); a typo or an invented field is refused at admission, not
   discovered on request 41.

`comcon.include` runs the same gates and throws with the same `e.code`.
`comcon.refusalCodes()` enumerates the closed set, frozen alongside the
denial codes in `t/tools/golden-denials.js`; pin CI to codes, never to
message text.

## Where to read more

- `OPERATOR_API.md` §2 (`admit`), §3.
- Tests: `t/comcon_admit.t`, `t/comcon_admit_tests.t`, `t/comcon_include_contract_fuzz.t`.
- Next: **D2** shows the compiled tier giving the same answers.
