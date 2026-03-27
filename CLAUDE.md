# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

This workspace integrates [QuickJS](https://github.com/bellard/quickjs) (a lightweight ES2020+ JavaScript engine) into [NGINX](https://github.com/nginx/nginx) to enable dynamic NGINX configuration via JavaScript. The two directories are separate upstream git repositories cloned as-is; integration code will be added as a new NGINX module.

## Repository Layout

```
nginx/      # Official nginx source (upstream: nginx/nginx)
quickjs/    # QuickJS JS engine (upstream: bellard/quickjs)
```

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

## Testing NGINX Changes

The NGINX test suite lives in a separate repository:

```bash
git clone https://github.com/nginx/nginx-tests.git
# Follow the nginx-tests README to run individual tests
```

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
