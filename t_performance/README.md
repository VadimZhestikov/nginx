# Performance Benchmarks — Pilgrim vs njs

Measures HTTP handler throughput for three scenarios on both engines:

| Scenario | What it measures |
|---|---|
| `baseline` | `return 200 "ok\n"` — no JS per request; raw nginx ceiling |
| `empty` | Minimal JS handler — JS entry/exit overhead only |
| `headers` | JS handler that reads all request headers; adds property-access cost |

## Quick start

```bash
# From the repo root — wrk is built automatically if not in PATH
bash t_performance/run.sh --njs=t_performance/njs-nginx/objs/nginx

# Recommended: 10 runs × 15s + 15s warmup for stable averages
bash t_performance/run.sh \
    --njs=t_performance/njs-nginx/objs/nginx \
    --runs=10 --duration=15 --warmup=15

# Multiple sessions (averages across sessions reduce WSL2 burst-credit noise)
bash t_performance/run.sh \
    --njs=t_performance/njs-nginx/objs/nginx \
    --sessions=4 --runs=10 --duration=15
```

`run.sh` builds wrk from https://github.com/wg/wrk into `/tmp/wrk-src/`
when `wrk` is not in PATH. Needs `git`, `make`, `gcc`, `libssl-dev`.

## Options

### run.sh

```
--sessions=N   run bench.sh N times in sequence  (default: 1)
```

All other options are forwarded to bench.sh.

### bench.sh

```
--pilgrim=PATH      Pilgrim nginx binary   (default: ../objs/nginx)
--njs=PATH          njs nginx binary       (default: njs tests skipped)
--duration=N        wrk test duration, s   (default: 30)
--connections=N     concurrent connections (default: 50)
--threads=N         wrk threads            (default: 4)
--warmup=N          warmup before each run (default: 15)
--runs=N            runs per scenario      (default: 1)
--cooldown=N        pause between runs, s  (default: 2)
--server-cpus=LIST  taskset for nginx      (opt-in; see CPU notes below)
--client-cpus=LIST  taskset for wrk        (opt-in; see CPU notes below)

Environment: PILGRIM_NGINX, NJS_NGINX, WRK — same as the corresponding flags.
```

## Outlier filtering

`bench.sh` automatically drops wrk timing glitches from each scenario's stats.
A run is an outlier if its req/s falls outside `[0.3×median, 2.0×median]` for
that scenario. Kept runs appear in the `└─ avg=…` summary line; dropped runs
show as `[N outlier(s) dropped]`.

Raw samples (including outliers) are always written to `results.txt` for
manual inspection.

## Ports used

| Port | Engine |
|---|---|
| 9001 | Pilgrim |
| 9011 | njs |

## File layout

```
t_performance/
  run.sh            one-shot wrapper: builds wrk if needed, then runs bench.sh
  bench.sh          main benchmark script
  build-njs.sh      one-time helper to build an njs-enabled nginx binary
  pilgrim/
    nginx.conf      4 workers, reuseport, access_log off
    handler.js      installs /empty and /headers handlers via nginx.broadcast()
  njs/
    nginx.conf      4 workers, reuseport, access_log off, js_import
    handler.js      empty() and headers() exported functions
  njs-nginx/        built by build-njs.sh (git-ignored)
  results.txt       last bench.sh run raw data (git-ignored)
  results_s*.txt    saved per-session raw data (git-ignored)
```

## CPU pinning notes

**Do not pin nginx and wrk to different CPU sets on loopback benchmarks.**
When nginx runs on CPUs A–B and wrk on isolated CPUs C–D, every HTTP response
requires a cross-CPU wakeup (IPI) across the domain boundary. At ~120K req/s
this cuts throughput by 3–4× vs letting both processes share all CPUs.

`--server-cpus` / `--client-cpus` are provided for setups where nginx and wrk
share the same isolated CPU set (e.g. both pinned to `isolcpus=2,3,4,5` with
`worker_processes 4`).

**WSL2 mirrored networking warning:** `networkingMode=mirrored` + `firewall=true`
in `.wslconfig` routes WSL2 loopback traffic through Windows Defender Firewall,
adding ~2 ms per request and reducing throughput by ~7×. Use NAT mode (the
default) for loopback benchmarks.

## Sample results (WSL2, 8 vCPU / 6 schedulable, 4 workers, 50 conn)

4-session grand averages (10 runs × 15s each, outlier-filtered):

```
  scenario                        Pilgrim       njs    Pilgrim lead
  baseline  (return 200, no JS)   158,000   160,000       tied
  empty     (minimal JS handler)  135,000   124,000        +9%
  headers   (JS + object access)  119,000   109,000        +9%

  JS overhead vs baseline:
    Pilgrim  ~15%
    njs      ~23%
```

**Key findings:**

1. **Pilgrim has lower JS overhead** (15% vs 23%). The runtime is inherited via
   COW fork and the worker opaque is pre-wired — no per-request engine setup.
   njs clones its engine context on every `js_content` call.

2. **Pilgrim leads njs on both JS scenarios** (~9% on both). The gap is stable
   across sessions (cv 5–9%).

3. **Baselines are tied** (within WSL2 session-to-session variance). The njs
   binary includes more compiled-in modules, which roughly offsets its lighter
   per-request JS dispatch cost on the baseline.

4. **15s warmup** allows QuickJS JIT to settle before measurement begins;
   shorter warmups inflate njs variance by ~30%.

## Interpreting overhead percentages

**JS overhead** = `(1 − handler_rps / baseline_rps) × 100%`

15% overhead means 85% of the nginx ceiling is preserved after adding a JS
handler. The remaining 15% is the per-request fixed cost: entering the JS
runtime, allocating the NginxRequest wrapper, dispatching the JS function,
running a GC increment, and returning the response.

**headers vs empty delta** measures `r.headers` / `r.headersIn` property-access
cost on top of the empty-handler baseline.

## Notes on fairness

Both binaries use nginx's default `-O` optimisation (no `-O0`, no ASAN).
The njs library is compiled with `-O -g`. For a production-grade comparison,
build both with identical `--with-cc-opt="-O2"`.
