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

## Preliminary observation — CONTAMINATED, not a result

Before guard 3 was tightened, a run completed on a box with a test262 sweep
occupying a core:

```
floor        1,073,404 req/s   100%
directives   1,016,366 req/s    95%
jit            447,715 req/s    42%     → 2.40× headroom to the ceiling
```

**Do not cite this.** It was taken on a loaded box, on WSL2 loopback, with one
policy shape, and the absolute figures are implausibly high for real traffic
(short keepalive responses over loopback flatter everything). It is recorded
only so it is not rediscovered and mistaken for a measurement.

If it survives a clean run, the direction it points is that meaningful headroom
remains above the JIT and M1's conclusion still stands — but that is a
hypothesis to test, not a finding.

## When the box is free

1. `bash run.sh` — the three default arms.
2. Add `--handc=` for the real ceiling rather than the floor proxy; the module
   and its addon `config` are in `../maxim_m1/`.
3. Add `--interp=` for context on what the JIT itself bought, after building a
   non-JIT engine.
4. Record the outcome here and update the gate decision in
   `[[design-comcon-maxim-typed-policy]]`.
