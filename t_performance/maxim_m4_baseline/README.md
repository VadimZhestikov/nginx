# M4 gate — re-measure M1 against the C5 baseline

**Status: harness ready, NOT yet measured.** No number here is the answer.

## The question

M1 ([`../maxim_m1/RESULTS.md`](../maxim_m1/RESULTS.md)) passed the gate for the
typed-policy compiler on **"hand-written C is 3.44× the interpreted mirror"**.

That figure is stale in a way that matters. It compared against the
**interpreter**, and C5 has shipped since — tenant JS already runs
JIT-compiled. So the question that actually decides M4/M5 is not

> how much faster than the interpreter is compiled C?

but

> **how much headroom is left above the JIT?**

If the JIT already sits near the hand-C ceiling, a typed compiler reclaims
little and M4/M5 should not be built. That is a multi-week decision currently
resting on a number nobody has measured.

## Arms

All serve M1's identical "count + tag" policy: increment a shared counter, read
`x-tenant`, emit two response headers, return `ok\n`.

| arm | what it is | why |
|---|---|---|
| `floor` | no JS at all, `return 200` | the machine's ceiling. M1 put hand-C at 90–96% of this, so it doubles as a proxy for the compiler ceiling |
| `directives` | the header part in pure nginx directives | M1's control |
| `jit` | `mirror.js` + `maxim_mirror_app.js` on the JIT build | **the new number — the C5 baseline** |
| `interp` | same JS on a genuinely non-JIT engine | optional; needs the libquickjs toggle |
| `handc` | M1's hand-written C module | optional; needs its own build |

**The `directives` arm does strictly less work than the JS arms** — nginx
directives cannot do the shared counter. M1 had the same asymmetry and used it
the same way. Do not read `directives` vs `jit` as apples to apples.

## Running it

```bash
bash run.sh                 # floor + directives + jit
bash run.sh --dry-run       # start/probe each arm, no load  (safe any time)
bash run.sh --handc=PATH --interp=PATH
```

It **refuses to measure unless the box is idle** (absolute load ≤ 0.5, no
process over 50% CPU). Override with `--force-busy` only if you intend to throw
the result away.

## Three deliberate guards

1. **Engine verification.** It reports what each binary actually contains
   (`nm | grep ' T js_jit_'`) rather than trusting the builddir name. `objs/nginx`
   links the JIT whenever `libquickjs.a` was built `CONFIG_JIT=y`, so a binary
   labelled "interpreter" may be nothing of the sort — and an arm labelled
   `interp` would then measure the JIT and invert the conclusion. If `--interp`
   is given a JIT-linked binary the harness refuses outright.
2. **Work verification.** Every arm's probe response must match a required
   pattern (`x-count`, `x-tenant-seen`). A policy that has silently stopped
   running looks exactly like a very fast policy; this makes that a refusal
   instead of a flattering number.
3. **Idle verification.** See above. Load average *scaled by CPU count* is the
   wrong instrument and this harness had it wrong first: one saturated core on
   a 16-CPU box is ~6% by that measure, which sails past a 25% threshold while
   being more than enough to move throughput.

## Measured 2026-09-08 on an idle box — AND THE `jit` ARM IS MISLABELLED

```
floor        1,190,964 req/s   100%
directives   1,138,510 req/s    96%
jit            468,693 req/s    39%     -> 2.54x "headroom"
```

**This does NOT answer the C5-baseline question, because the JIT never engaged.**

Checked after the fact, with `QJS_JIT_KEEP_C=1` so every JIT compilation leaves
its generated C behind:

| | new `/tmp/qjs_jit_*.c` |
|---|---|
| standalone `qjs`, hot loop | 1 |
| standalone `qjs -m` (module mode, as nginx loads host JS) | 1 |
| **nginx, ~4.8M handler invocations at 480k req/s** | **0** |

So the mechanism works, and nginx is the difference. The engine's automatic
path (`JS_CallInternal` -> `js_jit_fb_inc_count` -> threshold 100 ->
`js_jit_queue_gcc`) only ENQUEUES; `quickjs-jit.h` documents that
`js_jit_drain()` must be awaited and an install step called from the main
thread afterwards — and **nothing in `src/js` ever calls `js_jit_drain()` or
any `js_jit_*` install function**. `jit_no_compile` is set on enqueue, so a
function is never retried either.

COMCON C5's server-AOT (`js_comcon_aot_compile`) is invoked for admitted
`comcon.include` fragments ONLY. Host JS loaded via `js_source` — all of
mirror, every `location.handler` — is not covered.

**Consequence for the gate:** the number above is headroom above the
INTERPRETER, which is what M1 already measured. Treat it as a rough
corroboration of M1 (2.54x here vs 3.09x on M1's loopback setup, different box
and generator), NOT as the C5 baseline.

Whether the C5 baseline question is even well-posed for host-JS policies is
now open: if host JS never runs JIT-compiled, M1's figure is not stale for that
path at all.

Not yet established: whether the queued jobs are compiled-but-never-installed
or never compiled. Zero `.c` files points at the latter, but that was not
chased down.

## When the box is free

1. `bash run.sh` — the three default arms.
2. Add `--handc=` for the real ceiling rather than the floor proxy; the module
   and its addon `config` are in `../maxim_m1/`.
3. Add `--interp=` for context on what the JIT itself bought, after building a
   non-JIT engine.
4. Record the outcome here and update the gate decision in
   `[[design-comcon-maxim-typed-policy]]`.
