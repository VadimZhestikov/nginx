# COMCON C6 — compiled-tier benchmark (interpreted vs AOT-compiled)

The confined tenant handler, run **interpreted** (`objs/nginx`) vs **AOT-compiled to native
C** (`objs_jit/nginx`, COMCON C5), serving under load. Both run the *same* fragment; the
compiled build lowers it via server-AOT at load (`js_comcon_aot_compile`). Reproduce with
`run.sh` (needs both builds + wrk).

## Setup

WSL2; 1 nginx worker pinned (`worker_cpu_affinity 0010`), wrk on CPUs 4–7 (`taskset`),
`wrk -t2 -c50`, 3s warmup + 10s measured, `access_log off`, keepalive. Loopback. Two runs
shown (reproducible).

## Results (Requests/sec)

| Handler | Interpreted | AOT-compiled | Speedup |
|---|---:|---:|---:|
| **realistic** (uri split + object + `JSON.stringify`) | ~186 K | ~181 K | ~1.0× (tied) |
| **compute** (hot 20 000-iter typed-int loop) | ~5.3 K | ~72 K | **~13.5×** |

(run 1: realistic 189K→186K, compute 5376→71932 = 13.4×; run 2: 183K→177K, 5286→72283 =
13.7×.)

## Reading it

The result matches the tier's design profile exactly:

- **Compute-heavy JS → ~13.5×.** The hot typed-int loop is where maxim's fast paths (typed
  `int` locals, inline arith, no interpreter dispatch) dominate. A confined tenant doing
  real work — transforms, scoring, parsing, math — gets a large win.
- **I/O / builtin-bound handler → tied.** The "realistic" handler's time is nginx + HTTP +
  C builtins (`split`, `JSON`); JS execution is a small slice, so compiling it neither helps
  nor meaningfully hurts (the ~3% dip is within run-to-run noise + install overhead). This is
  the known "~15% JS overhead vs baseline" regime — compilation targets *that* slice, which
  is small here.

## Takeaways for the roadmap

- The compiled confined tier delivers its value **where JS is the bottleneck**; it is not a
  free speedup for thin routing handlers (and doesn't need to be — those are already fast).
- **C5.1 (partial-eval inline gate checks)** would help the *reach-gated* path, not the raw
  compute loop measured here; its value shows up for handlers that call gated host functions
  in a hot path — a separate microbenchmark to build when C5.1 lands.
- Confinement is unchanged and free in both tiers (erasure — see `t/comcon_lowering.t`):
  this speedup carries **no** loss of the A1 gates / denial accounting.
