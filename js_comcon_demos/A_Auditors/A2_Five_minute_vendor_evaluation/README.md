# A2 — The five-minute vendor evaluation

**Audience:** procurement and security reviewers asked "what does this SDK
actually do?"

**In one sentence:** `comcon.std.evaluate(fn)` reads the whole free-name
manifest of a module in one static pass, classifies each name, lists its call
sites with line numbers, flags dynamic code, and reports the remainder against
what the vendor declared; nothing is executed.

## Run

```bash
bash test.sh                       # 9 checks
curl http://127.0.0.1:8217/evaluate
curl http://127.0.0.1:8217/paste
```

## What you see

```json
{"names":[{"name":"Date","kind":"authority","calls":1,"lines":[4],"references":1},
          {"name":"JSON","kind":"intrinsic",…},
          {"name":"fetch","kind":"authority","calls":2,"lines":[2,3],"references":2},
          {"name":"nginx","kind":"authority","calls":1,…}, …],
 "authorities":["Date","buildUrl","createSocket","fetch","nginx"],
 "undocumented":["Date","buildUrl","createSocket","nginx"],
 "dynamicCode":false,"admissible":true,
 "contract":{"imports":["Date","buildUrl","createSocket","fetch","nginx"]},
 "verdict":"requests 5 authorities (…); declared 1 of 5"}
```

## Why it can be trusted

- The free-name walk is the **same C collector admission uses**; the report
  and the gate cannot disagree about what a fragment names.
- Call sites come from **bytecode** (D5a), so a nested argument
  (`fetch(buildUrl(id))`) is still attributed to `fetch`.
- A pasted **source** is parsed by the vendored parser first and accepted only
  as exactly one function expression; `function(){}; evil()` is refused, and
  `evil()` never ran (the test asserts the marker).
- `Date` is an authority here, not an intrinsic: the compartment's allowance
  does not include a clock, which is what makes admission tests reproducible.

## Where to read more

- SHOWCASE §25 (`REAL CODE`); `SHOWCASE-gaps.md` G-13 (closed v5.127).
- Tests: `t/comcon_std_evaluate.t`, `t/comcon_pom_callsites.t`, `t/comcon_admit.t`.
