# D2 — The compiled tier gives the same answers

**Audience:** developers and architects deciding whether to run tenant
fragments on the compiled tier, and what that changes.

**In one sentence:** the same fragment, bound by the same host program, runs
interpreted on `objs/nginx` and lowered to C on `objs_jit/nginx`; the answers
are byte-identical and the deadline fires on both.

## Run

```bash
bash test.sh                       # 8 checks; runs BOTH binaries in turn
# by hand, interpreter:
../../../objs/nginx -p . -c nginx.conf;     curl 'http://127.0.0.1:8211/run?token=a.b.c'; ../../../objs/nginx -p . -c nginx.conf -s stop
# by hand, compiled:
../../../objs_jit/nginx -p . -c nginx.conf; curl 'http://127.0.0.1:8211/run?token=a.b.c'; ../../../objs_jit/nginx -p . -c nginx.conf -s stop
```

The compiled build is the one `t/tools/gate.sh --configure` produces as
`objs_jit` (same source, `-DCONFIG_JIT`, linked with `-ldl -lpthread`).

## What you see

```json
objs/nginx      {"tier":{"token":{"jit":false,"functions":0,"compiled":0}, …},
                 "answer":{"token":{"parts":3,"hash":"…","ok":…},"scan":{"len":…,"acc":…,"bad":1}},
                 "gate":{"spin":"interrupted","ms":100}}
objs_jit/nginx  {"tier":{"token":{"jit":true,"functions":1,"compiled":1}, …},
                 "answer": <identical>,
                 "gate":{"spin":"interrupted","ms":100}}
```

## What the tier changes, and what it does not

- **Nothing in the host program.** `comcon.include` compiles at include time
  when the binary can; `comcon.aotStatus(f)` reports which tier a fragment is
  on. Host JS itself is never compiled (the JIT is inert for it).
- **Not the answers.** SR-2, the faithfulness gate: compiled == interpreted,
  asserted by `t/comcon_include_faithfulness.t` over 77 shapes with the spec's
  values written out, and fuzzed by `t/tools/jit-diff-fuzz.py`.
- **Not the gates.** The deadline is polled on loop back-edges in lowered C,
  the memory allowance and the retained cap are charged at the boundary, and
  an abort is uncatchable on both tiers (finding F16 closed that).
- **The cost.** Measured on this box: a byte scan 11.72 → 1.34 ns/byte, a
  token check 55 → 34 ns/char (`PERFORMANCE.md` §2f); the remaining gap to a
  C bound is the engine's per-character read, which is where the compiler
  track closed.

## Where to read more

- `PERFORMANCE.md` §2f; `ASSURANCE.md` G7.5, G7.18, G7.20, G7.23.
- Tests: `t/comcon_include_faithfulness.t`, `t/comcon_compiled_resource_gates.t`, `t/comcon_jit_uncatchable.t`.
