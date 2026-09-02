# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

This workspace integrates [QuickJS](https://github.com/bellard/quickjs) (a lightweight ES2020+ JavaScript engine) into [NGINX](https://github.com/nginx/nginx) to enable dynamic NGINX configuration via JavaScript. The two directories are separate upstream git repositories cloned as-is; integration code will be added as a new NGINX module.

## Repository Layout

```
src/, auto/, …   # the nginx source tree — the repo root IS the nginx tree
src/js/          # the pilgrim JS/COM/COMCON module (--add-module)
quickjs/         # the JS engine, VENDORED IN-TREE (2026-09-01)
```

**quickjs is vendored into this repo** (subdir `quickjs/`), not a sibling checkout —
so the engine (pilgrim patches + maxim's COMCON JIT) and its consumer co-evolve
atomically and a clone is self-contained. It was brought in with `git subtree`
(`--prefix=quickjs --squash`, base = the bellard tree at VERSION 2025-09-13 + patches).
Build: `make -C quickjs libquickjs.a` then configure nginx with
`-Iquickjs -Lquickjs -lquickjs` (see the project memory build recipe).

### Updating the vendored engine

Three cases, by how much reconciliation is involved:

1. **Our own engine changes** (the common case: the COMCON JIT glue, M-SES hardening,
   bugfixes we author). **Just edit `quickjs/…` in place and commit to pilgrim.** No
   special mechanism — that atomicity is the whole point of vendoring: an engine change
   and the `src/js` change that needs it land in one commit.

2. **A specific upstream bugfix** (a bellard CVE, a maxim fix). Small/single → apply the
   patch directly to `quickjs/` and commit. A batch →
   `git subtree pull --prefix=quickjs <upstream-repo> <ref> --squash`; expect conflicts
   where the fix touches lines our patches changed, resolve once.

3. **A major rebase** (new bellard base, or maxim finalization — the next known event, to
   clear maxim's ~200 test262 fails before COMCON C7). **Do NOT reconcile inside
   pilgrim.** Do the 3-way merge (new base + pilgrim patches + maxim JIT) in a separate
   *engine workbench* checkout that has bellard and maxim as real remotes with full
   history; produce one clean updated branch; then `git subtree pull --squash` **that
   branch** into pilgrim. Pilgrim only ever receives an already-merged result.

**The single integration fork (LIVE 2026-09-01):** `github.com/nginxinc/pilgrim-quickjs`
(private; local checkout `../pilgrim-quickjs/`) is the one clean lineage — remotes
`bellard` + `maxim`, branch `jit` (= maxim/jit) and default branch `pilgrim` (= `jit` +
our COMCON/host delta). Pilgrim's `quickjs/` was cut over to it via `git subtree pull`
(the `quickjs-fork` remote → the GitHub URL; squash anchor `34665804f`). **Always
`git subtree pull --prefix=quickjs quickjs-fork pilgrim --squash` — never hand-sync maxim
again;** gate every pull on SR-2 + full regression (see the project memory). Divergence
analysis + the maxim-advance / bellard-CVE / cutover workflows are in the fork's
`INTEGRATION.md`.

## Build Commands

### NGINX

```bash
# Install build dependencies (Debian/Ubuntu)
sudo apt install gcc make libpcre3-dev zlib1g-dev libssl-dev

# Configure (run from nginx/)
cd nginx && auto/configure

# Build; binary produced at nginx/objs/nginx
make -C nginx

# Install to /usr/local/nginx/
sudo make -C nginx install

# Run
sudo /usr/local/nginx/sbin/nginx

# Test a running instance
curl localhost
```

Common `auto/configure` flags (see `nginx/auto/configure` or https://nginx.org/en/docs/configure.html):
- `--with-debug` — enable debug logging
- `--add-module=<path>` — include a static module
- `--with-http_ssl_module` — enable SSL/TLS

### QuickJS

```bash
# Build qjs (REPL) and qjsc (bytecode compiler)
make -C quickjs

# Run the test suite
make -C quickjs test

# Run test262 conformance suite (requires test262 checkout)
make -C quickjs test2

# Install to /usr/local
sudo make -C quickjs install
```

QuickJS sanitizer builds: uncomment `CONFIG_ASAN`, `CONFIG_MSAN`, or `CONFIG_UBSAN` in `quickjs/Makefile`.

## Tests

Three complementary test suites live at the repo root.  Each targets a
different failure class; none replaces the others.

---

### `./t` — Functional correctness tests

Perl tests built on [Test::Nginx](https://github.com/nginx/nginx-tests).
Each file starts its own nginx instance, runs focused behavioral assertions,
then stops nginx.  Catches regressions in request handling, COM semantics,
SharedWorker behaviour, directive parsing, etc.

**One-time setup** (provides `Test::Nginx` to all three suites):

```bash
git clone --depth=1 https://github.com/nginx/nginx-tests.git /tmp/nginx-tests
cp -r /tmp/nginx-tests/lib t/lib
```

**Run:**

```bash
# Absolute path to the binary is required
TEST_NGINX_BINARY=$(pwd)/objs/nginx prove -v t/

# Single file
TEST_NGINX_BINARY=$(pwd)/objs/nginx prove -v t/js_shared_worker.t
```

> **Never** use `-j` parallel execution — port contention causes flaky
> failures when multiple nginx instances race for the same ports.

---

### `./t_stress` — Stress and leak tests

Two families of tests that apply sustained repeated load to detect leaks.
All helpers live in `t_stress/lib/`; every test does `chdir($FindBin::Bin)`
so `use lib 'lib'` resolves to `t_stress/lib/` automatically.

**Run:**

```bash
TEST_NGINX_BINARY=$(pwd)/objs/nginx prove -v t_stress/
```

#### COM steady-state leak tests (`com_*.t`)

Single nginx instance per file.  A JS request handler runs **N iterations**
of COM accessor calls in one HTTP request and reports the JS heap delta via
`nginx.jsMemUsage()` (before) → `nginx.gc()` → `nginx.jsMemUsage()` (after).
After an explicit GC pass, only truly leaked objects (refcount > 0 with no
JS reference) remain in the delta.  Assertion threshold: **< 32 KB over
10 000 iterations**.

Catches per-call JS value leaks: wrapper objects not freed, registry arrays
that grow without bound, array getters that accumulate temporary objects.
Found and fixed a real leak: `location.handler = fn` was appending to
`__ngx_handlers__[]` on every replacement without releasing the old closure.

**Shared helper:** `t_stress/lib/ComStress.pm`
— `run_stress($t, $path, $n)` and `assert_flat($d, $n, $label)`.

**Adding a test:** follow the pattern in any existing file.  The stress
handler must call `nginx.gc()` before the second `nginx.jsMemUsage()` so
GC-eligible garbage is collected before asserting.

| File | What it stresses | Iters |
|---|---|---|
| `com_upstream_weight.t` | `peers[0].weight` setter full chain | 10 000 |
| `com_handler_replace.t` | `location.handler` replacement | 5 000 |
| `com_limit_req.t` | `limitReq.limits[0].burst` full chain | 10 000 |
| `com_enumerate_servers.t` | `nginx.http.servers[]` read | 10 000 |
| `com_enumerate_locations.t` | `server.locations[]` read | 10 000 |
| `com_upstream_peer_list.t` | `upstream.peers[]` + getters | 10 000 |
| `com_proxy_access.t` | `location.proxy` wrapper + getters | 10 000 |
| `com_proxy_set_header.t` | `proxy.setHeader[]` array getter | 10 000 |
| `com_ssl_access.t` | `server.ssl` wrapper + `protocols[]` | 10 000 |
| `com_limit_conn.t` | `location.limitConn` wrapper | 10 000 |
| `com_upstreams_array.t` | `nginx.http.upstreams[]` enumeration | 10 000 |

#### Reload lifecycle leak tests (`sighup_*.t`)

Detects leaks in the nginx reload path: JS runtime teardown,
SharedWorker thread retirement, fd cleanup in `exit_master`/`exit_process`.
The suite sends N=20 SIGHUP signals to a single nginx instance and asserts
that the master process RSS and open fd count stay flat across cycles.

**Shared helper:** `t_stress/lib/ReloadHarness.pm`
— `reload_nginx($t)` (SIGHUP + polls until old workers exit),
`rss_kb($pid)`, `fd_count($pid)`, `assert_rss_stable()`,
`assert_fd_stable()`.

Thresholds: RSS < 4 MB total growth; fd delta ≤ 3 (socket fds are exact
— any leaked socketpair end shows up immediately).

| File | Config | Reloads | What it catches |
|---|---|---|---|
| `sighup_baseline.t` | Plain nginx, no JS | 20 | Harness noise floor |
| `sighup_js_source.t` | `js_source` + COM reads | 20 | JS runtime init/destroy leak |
| `sighup_sw.t` | `js_source` + `new SharedWorker` | 20 | SW pthread + socketpair fd leak |
| `sighup_sw_memfd.t` | SW + worker-created SABs | 20 | memfd fd (SCM_RIGHTS cleanup) |
| `sighup_handlers.t` | `js_source` with `location.handler` | 20 | JSValue handler lifecycle |

Found and fixed a real leak: `bcast_fds[i][1]` (master's copy of the
worker-read broadcast socketpair end) was not closed on reload, leaking
1 fd per worker per SIGHUP cycle (detected by `sighup_js_source.t` and
`sighup_handlers.t`, 40 fd delta over 20 reloads with 2 workers).

## Performance Benchmarks

The `t_performance/` directory benchmarks Pilgrim vs an njs-enabled nginx
across three HTTP handler scenarios.

**Run:**

```bash
# One session (Pilgrim only)
bash t_performance/run.sh

# With njs comparison, 4 sessions × 10 runs for stable averages
bash t_performance/run.sh \
    --njs=t_performance/njs-nginx/objs/nginx \
    --sessions=4 --runs=10 --duration=15
```

`run.sh` builds wrk automatically if not in PATH (clones into `/tmp/wrk-src/`).

**Representative results** (WSL2, 8 vCPU / 6 schedulable, 4 workers, 50 conn,
4-session grand average, outlier-filtered):

| Scenario | Pilgrim | njs | Pilgrim lead |
|---|---|---|---|
| baseline (return 200, no JS) | 158K | 160K | tied |
| empty (minimal JS handler) | 135K | 124K | +9% |
| headers (JS + object access) | 119K | 109K | +9% |

Pilgrim JS overhead vs baseline: **~15%** (njs: ~23%).

**WSL2 pitfall**: `networkingMode=mirrored` + `firewall=true` in `.wslconfig`
routes loopback through Windows Defender Firewall → adds ~2ms/request → ~7×
throughput drop. Keep `.wslconfig` in NAT mode for benchmarking.

See `t_performance/README.md` for full options, CPU pinning notes, and
interpretation guidance.

## NGINX Architecture

NGINX runs as a master process (reads config, manages workers) + N worker processes (handle requests). All I/O is non-blocking and event-driven (epoll on Linux).

**Key source areas in `nginx/src/`:**

| Directory | Purpose |
|---|---|
| `core/` | Master process, config parsing, memory pools, data structures |
| `event/` | Event loop abstraction (epoll, kqueue, IOCP), timer management, QUIC |
| `http/` | HTTP/1.x, HTTP/2, HTTP/3 request lifecycle, variables, upstream |
| `http/modules/` | 70+ built-in HTTP modules (proxy, gzip, SSL, auth, cache, etc.) |
| `stream/` | TCP/UDP stream proxy |
| `mail/` | IMAP, POP3, SMTP proxy |
| `os/unix/` | POSIX signals, processes, sockets, file I/O, memory mapping |

**Adding a new module:** implement `ngx_module_t`, `ngx_command_t[]`, and handler functions, then reference it with `--add-module=` at configure time.

## QuickJS Architecture

| File | Purpose |
|---|---|
| `quickjs/quickjs.c` | Core JS engine (bytecode compiler + VM, ~1.8 MB) |
| `quickjs/quickjs.h` | Public C API |
| `quickjs/quickjs-libc.c` | Standard library (timers, file I/O, workers) |
| `quickjs/qjs.c` | CLI REPL |
| `quickjs/qjsc.c` | Bytecode compiler (`qjsc`) |
| `quickjs/libregexp.c` | Regex engine |
| `quickjs/libunicode.c` | Unicode tables |

To embed QuickJS: include `quickjs.h`, link against `libquickjs.a`, create a `JSRuntime` then one or more `JSContext` instances.

## NGINX Commit Message Style

Subject line: ≤67 characters, use area prefix + colon (e.g., `Core:`, `Upstream:`, `QUIC:`, `SSL:`, `HTTP/2:`).
Body lines: ≤76 characters.
