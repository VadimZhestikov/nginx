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

## ATTEMPTED 2026-09-11 — REFUSED, and the refusal is the finding

AOT-A landed ([[aot-a-host-js-load-time-compile]]), so the `jit-aot` arm finally
compiles for real: `app_aot.js` now compiles `mirror.attach` (7 functions,
including the per-request dispatcher `attach` hands to `location.addHook`), the
table method and the three policy handlers — **11 walked, 11 installed, 1.7s**,
logged per-root so the arm cannot post a number having compiled nothing.

The run was then **refused by a new fourth guard**, and correctly:

```
floor 111-118k req/s at ~840us latency, nginx workers ~50% CPU (205% of 400%)
vs   1,190,964 req/s from this same harness on this same box on 2026-09-08
```

The box is **latency-bound, not CPU-bound** — nginx has half its capacity idle.
Cause: `.wslconfig` `networkingMode=mirrored` + `firewall=true`, which routes
loopback through Windows Defender Firewall. That pitfall is documented in
CLAUDE.md, and was still missed, because nothing checked.

Why it matters more than "the numbers are smaller": when a large fixed
per-request cost sits outside nginx, every arm pays it and **all ratios compress
toward 1.0**. The harness duly printed `headroom 1.18x` and
`READS AS: little left to reclaim — M4/M5 hard to justify`. That is a multi-week
decision, and it would have been drawn from a network setting.

**To get the real number:** set `.wslconfig` to NAT mode, `wsl --shutdown`,
re-run. The harness now refuses until floor latency is under 250us
(`FLOOR_LAT_MAX_US` to override).

## Four deliberate guards

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
4. **Bottleneck verification** (added 2026-09-11). Comparing arms says something
   about the policy only if NGINX is what limits throughput. The floor arm's
   latency is checked against 250us; loopback on an unencumbered box is tens of
   microseconds. Guards 1-3 all passed while the box was in a regime that makes
   every ratio meaningless — being right about the engine, the work and the
   idleness is not the same as measuring the right thing.

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

**ROOT CAUSE, established 2026-09-08: the JIT is inert in nginx workers by
design, and the engine says so.**

`js_jit_init()` starts the GCC worker thread, but a pthread does not survive
`fork()`. So `jit_atfork_child()` zeroes `jit_worker.started` in the child, and
every enqueue/drain path is guarded by `if (!jit_worker.started) return;`.
nginx's master creates the JS runtime and workers fork from it, so no worker
can compile anything. From `quickjs-jit.c`:

> the child runs pure interpreter with no JIT. This makes a forking host (e.g.
> nginx: master starts the thread, workers fork) safe. **Real per-worker JIT
> activation (start a worker-local thread + wire install) is a later step**;
> here the goal is only "forked workers don't hang".

This is not a defect discovered here — it is a known unfinished step in the
JIT integration.

Confirmed by experiment. `nginx.jitCompile(fn)` was added to invoke exactly the
C5 path (`js_jit_compile_all` + `js_jit_drain` + `js_jit_install_results`)
on host-JS functions at load, and an `app_aot.js` / `jit-aot` arm added:

```
floor        1,189,296 req/s   100%
directives   1,146,772 req/s    96%
jit            464,108 req/s    39%
jit-aot        456,190 req/s    38%    <- AOT invoked; indistinguishable
```

`jitCompile` returned `true` for all three functions and generated **zero** C.
That `true` is not a lie so much as a weak claim: `js_comcon_aot_compile()`
returns 0 as soon as the argument is a bytecode function, so it means
"eligible", never "compiled".

### C5 was the open question — and C5 is REAL. Tested 2026-09-08.

The worry was that if the AOT path is inert in workers, COMCON C5's server-AOT
might be doing nothing either, and C6's ~13.5x compute figure with it. It is
not. C5 works, and the difference is exactly WHEN the compile happens:

`comcon.include` runs at config load (stage-0) in the master, **before** the
daemon and worker forks, while the GCC thread is still alive. The resulting
`.so` is `dlopen`ed and `jit_func` installed there, and both survive the forks.
Host JS gets no such call, and the automatic per-call path is inert in workers.

Verified two independent ways with a compute-bearing fragment
(`comcon.include` of a 2000-iteration loop, invoked per request):

1. **It compiles.** One new generated C file at load, and the log line
   `js comcon: include fragment lowered to native C (COMCON C5 server-AOT)`.
2. **It is measurably faster.** Same config, same fragment, the only difference
   being that `objs` is built without `-DCONFIG_JIT` so the AOT call is
   preprocessed out:

   | build | AOT log lines | req/s |
   |---|--:|--:|
   | `objs_jit` (AOT active) | 2 | **183,016** |
   | `objs` (AOT compiled out) | 0 | 127,115 |

   **1.44x end-to-end.** Not 13.5x, and not in tension with it: C6 measured
   COMPUTE, whereas this is whole-request throughput where nginx's own HTTP
   path dominates and the fragment is a small slice.

So the picture is clean, and the gap is specific:

| path | compiled? |
|---|---|
| `comcon.include` fragments (the confined tier) | **yes**, AOT at load |
| host JS via `js_source` — mirror, every `location.handler` | **no** |

The `jit-aot` arm is kept as the reproduction: if per-worker JIT activation
lands, it should diverge from `jit`. Today it does not.

## When the box is free

1. `bash run.sh` — the three default arms.
2. Add `--handc=` for the real ceiling rather than the floor proxy; the module
   and its addon `config` are in `../maxim_m1/`.
3. Add `--interp=` for context on what the JIT itself bought, after building a
   non-JIT engine.
4. Record the outcome here and update the gate decision in
   `[[design-comcon-maxim-typed-policy]]`.
