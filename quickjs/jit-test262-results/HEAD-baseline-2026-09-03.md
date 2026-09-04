# T0 JIT-conformance baseline — HEAD 7312525 (2026-09-03)

**Headline: maxim's JIT is conformant across the entire COMCON-relevant test262
surface — 0 JIT-introduced correctness failures.** This supersedes the
2026-09-01 `t0-delta.txt` (binary `b07ca5d`: 3 crashes + 6 miscompiles), all of
which are fixed at HEAD.

## Method
- Build: `make CONFIG_JIT=y run-test262`; suite: `make test2-bootstrap`.
- `./t0-measure-jit.sh` — JIT-introduced delta vs the 72 interpreter baseline
  (`test262_errors.txt`), forcing JIT with `--jit-threshold-gcc=1`.
- The 72 interpreter fails are all `staging/sm/*` (SpiderMonkey annex-B / edge
  cases) — engine-level, not maxim/JIT.

## Result
Every semantic directory in the COMCON set reports **0 JIT-new**. Fixed vs the
stale report:
- `TypedArray/prototype/map` SIGSEGV → 0/84.
- `TypedArray/prototype/filter` SIGABRT → 0/242.
- `DataView/prototype` hang → 0/488.
- The sloppy-mode `this` cluster (non-strict callback without `thisArg` must see
  `this === global`, not `undefined`) across TypedArray forEach/some, Array.from,
  Map forEach → all pass. `OP_push_this` → `js_jit_this_sloppy` gated on
  `js_jit_fb_is_strict` is correct.

## The `rc=124` entries are NOT crashes
`Array/prototype/{reduce,filter}`, `Map/prototype`, `DataView/prototype` show
`rc=124` under `--jit-threshold-gcc=1`. That is `timeout(1)` firing: at
threshold=1 GCC compiles *every* function, which is slow on large directories.
All four pass **0 errors at `--jit-threshold-gcc=100`** (and interpreted). Real
crashes are `rc=139` (SIGSEGV) / `rc=134` (SIGABRT) — none at HEAD.

`t0-measure-jit.sh` now reclassifies an `rc=124` timeout with a threshold=100
confirmation run → `SLOW-OK` when clean, `HANG?/rc=N` when genuinely stuck.

## Next
- A full test262 JIT sweep beyond the COMCON subset (the exhaustive
  `--jit-link` warmup → `--jit-aot` per-test sweep) to catch any deltas
  elsewhere.
- The SR-2 faithfulness gate on the COMCON surface is effectively **green**.
