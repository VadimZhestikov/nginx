# mirror — Thread 2, Path B: pilgrim `ngx_substrate` DPDK backend (scoping)

Status: **scoping / design only.** Path A (Step 2.2) proved *F-Stack's own* nginx
serves HTTP over DPDK in the KVM guest. Path B is the real goal: make **our
pilgrim nginx** (nginx 1.29 + the JS module + mirror + the `ngx_substrate` seam)
serve over DPDK — ideally *through the seam*, so a future TMM backend slots into
the same vtable. This doc scopes that, grounded in a recon of F-Stack's actual
nginx patch (we built it in the guest).

## 0. Definition of done

Pilgrim `objs/nginx` accepts and serves a real HTTP request over the DPDK data
plane (F-Stack userspace TCP), and the mirror `example` rules run over it — with
the DPDK integration expressed behind `ngx_substrate` as far as practical.
Functional only; performance is a later bare-metal exercise.

## 1. Recon: how F-Stack *actually* integrates into nginx

F-Stack's `app/nginx-1.28.0` is **not** a clean library behind a vtable — it is a
**source fork** of nginx. Concretely (from the guest):

- **~13 core files patched** + new modules: `ngx_ff_module.c`,
  `ngx_ff_host_event_module.c`, patched `ngx_epoll_module.c`; touches
  `nginx.c`, `ngx_connection.c`, `ngx_cycle.h`, `ngx_event.c`,
  `ngx_event_connect.c`, `ngx_process_cycle.c`, `ngx_http_core_module.c`,
  `ngx_http_proxy_module.c`, `ngx_stream_core_module.c`, `ngx_stream_proxy_module.c`,
  `ngx_mail_core_module.c`.
- **Syscall-interception shim (`ngx_ff_module.c`):** wrappers named like the libc
  calls (`socket/accept/bind/listen/recv/send/close/setsockopt/ioctl/epoll_*`)
  that branch on **`is_fstack_fd(fd)`** → dispatch to `ff_*` for F-Stack fds, else
  the real kernel syscall. F-Stack fds live in a distinguished numeric range. This
  is how they handle **fd duality without editing every call site** — they
  intercept at the call level.
- **Loop inversion:** `ngx_process_cycle.c` calls
  `ff_run(ngx_worker_process_cycle_loop, cycle)` — **F-Stack owns the worker main
  loop**; nginx's cycle body becomes F-Stack's per-tick callback. Per worker,
  `ff_mod_init(fstack_conf, proc_id=worker_idx, proc_type)`.
- **Dual event handling:** `ngx_ff_host_event_module.c` is a *second* event module
  for **kernel** fds (channel, timers, control), running alongside F-Stack's epoll
  for **data-plane** fds. nginx ends up with two readiness sources.

**Key takeaway:** F-Stack's integration philosophy (syscall shim + loop inversion +
dual event) is *different* from our `ngx_substrate` vtable philosophy. Path B has
to reconcile the two.

## 2. The three hard problems (now confirmed, not speculative)

1. **Loop inversion.** nginx wants `for(;;) ngx_process_events_and_timers()`;
   F-Stack wants to own the loop via `ff_run()`. F-Stack resolves it by making
   nginx's cycle the `ff_run` callback. Any clean-seam design still has to cede the
   top-level loop to F-Stack (it drives NIC RX/TX + the FreeBSD stack tick).
2. **fd duality.** `ngx_connection_t.fd` is an `int`; F-Stack fds are a separate
   space. nginx (and pilgrim's JS module) call `getsockopt/setsockopt/ioctl/fcntl/
   epoll_*` on `c->fd` in many places. Our seam routes recv/send/close/shutdown
   (2.1a/2.1b) but NOT accept/listen/event-registration/sockopt — exactly the
   pieces F-Stack's shim covers by fd-dispatch.
3. **Listener + init timing.** Listen sockets are created in the master at config
   time (`socket/bind/listen`), but `ff_init`/`ff_mod_init` runs *per worker* after
   fork. F-Stack defers/redoes listener creation on the F-Stack stack in the
   worker. Our master-creates-listeners model must adapt.

## 3. Two strategies + recommendation

- **Strategy 1 — adopt F-Stack's shim.** Port F-Stack's ~13-file patch onto
  pilgrim's nginx. Fast route to "pilgrim serves over DPDK." But it's invasive,
  and it largely *bypasses* our `ngx_substrate` seam (the shim intercepts I/O by
  fd, not via the vtable) — so it doesn't validate the seam as the convergence
  point.
- **Strategy 2 — clean seam.** Grow `ngx_substrate` into a full backend vtable
  (add `accept`, `listen`/`open_listener`, socket-options, and pair it with a
  DPDK **event module** doing `ff_epoll_*` + the F-Stack pump), implement each via
  `ff_*`. Architecturally clean and reusable (TMM later), but it is essentially
  re-deriving F-Stack's integration behind our abstraction — the most work.

- **Recommended: hybrid, in this order.**
  1. **Strategy 1 first** to get pilgrim actually serving over DPDK (prove the
     environment + surface pilgrim-specific breakage early).
  2. **Then refactor** the fd-dispatch + I/O points to route through
     `ngx_substrate` (Strategy 2), demonstrating the seam can *express* the DPDK
     backend and leaving the shim only where the vtable can't reach (loop
     inversion, listener timing). This yields the reusable seam without paying its
     full cost up front — the same "prove it works, then make it clean" pattern
     the whole project has used.

## 4. Pilgrim-specific coexistence risks (bigger than for stock nginx)

Pilgrim is not stock nginx; these are the real danger zones:

- **SharedWorker pthreads + AF_UNIX socketpairs + memfd.** pilgrim runs long-lived
  pthreads doing blocking `recvmsg` on **kernel** fds (per-worker socketpairs,
  SCM_RIGHTS memfds). F-Stack is lcore/run-to-completion and doesn't expect extra
  app threads; those fds must stay on the kernel path (host event module / real
  syscalls via `is_fstack_fd` returning false). Risk: thread-affinity and F-Stack's
  per-lcore assumptions vs pilgrim's threads.
- **JS async (`nginx.setTimeout`, fetch, async handlers).** These ride nginx
  timers + the event loop; under loop inversion they must still fire from the
  cycle callback. `r.fetch` opens **kernel** upstream sockets today — those would
  go kernel-path unless also moved to F-Stack (mirror.kv over DPDK is future).
- **Timers / channel / signals.** nginx's inter-process channel and timer fds are
  kernel fds → host event module.
- **The mirror `example` uses `reuseport`, 2 workers, upstreams, stream, TLS.**
  Each is a surface: reuseport + F-Stack, upstream connect over which stack, stream
  module (also patched by F-Stack), `onClientHello` (SSL over F-Stack fds).

## 5. Reconciliation with the existing seam

- 2.1a bound `c->recv/send/*_chain` from `ngx_substrate->io`; 2.1b added
  `close/shutdown`. A DPDK backend would set `io` = `{ff_recv, ff_send, ...}` and
  `close` = `ff_close` — **but only for F-Stack fds**. So the vtable needs the
  same `is_fstack_fd`-style dispatch, OR the connection must carry a "which
  substrate" tag (cleaner: `c->substrate` pointer per connection, set at accept by
  whichever listener accepted it). The per-connection-substrate idea is the clean
  generalization and worth adopting in the refactor (B.4).
- Event readiness stays a *module* (as decided in 2.1b): a `ngx_dpdk_event_module`
  doing `ff_epoll_*`, selected when the DPDK backend is active — plus the host
  event module for kernel fds.

## 6. Phased sub-plan

| Sub-phase | Deliverable | Risk |
|---|---|---|
| **B.0** | Extract F-Stack's exact nginx diff vs stock 1.28 (we have the tree); catalogue every patch hunk + why | low |
| **B.1** | Apply the F-Stack integration to **pilgrim** nginx 1.29 (shim + loop inversion + dual event + ff_mod_init), JS module DISABLED first — get bare pilgrim-core serving over DPDK | **high** (1.28→1.29 patch drift; build) |
| **B.2** | Re-enable the JS module; get pilgrim (JS) serving a static response over DPDK; fix SharedWorker/kernel-fd coexistence | **high** (pilgrim threads vs F-Stack) |
| **B.3** | Run the mirror `example` rules over DPDK (HTTP hooks, table, persist); triage each feature | med |
| **B.4** | Refactor: route the shim's I/O + accept through `ngx_substrate` (per-connection substrate tag); DPDK event module | med |
| **B.5** | Doc the seam as the convergence point; leave loop-inversion/listener-init as documented backend responsibilities | low |

Each sub-phase is a checkpoint. B.1 and B.2 are the make-or-break, high-risk steps
(they may surface that pilgrim's threading model needs real rework under F-Stack).

## 7. Effort, risks, non-goals

- **Effort:** multi-session, easily the largest single piece of the mirror project.
  B.1–B.2 alone are substantial C integration + debugging.
- **Top risks:** (a) 1.28→1.29 patch drift; (b) pilgrim SharedWorker pthreads vs
  F-Stack's lcore model; (c) F-Stack DPDK 24.11 vs pilgrim's build/link (two DPDKs:
  apt 23.11 + F-Stack 24.11 already coexist — must link the right one);
  (d) debugging a run-to-completion inverted loop is harder (no clean gdb over the
  poll loop; rely on logs).
- **Non-goals:** performance numbers (bare metal only); upstreaming; full pilgrim
  feature parity over DPDK (TLS, stream, fetch-over-DPDK are later); mirror.kv over
  DPDK.

## 8. Environment

Dev/test = the KVM guest on `192.168.0.114` (`~/mirror-build/vm`), NIC on
`vfio-pci` no-IOMMU, F-Stack DPDK 24.11 + libfstack installed, reachable via the
SLIRP hostfwd trick (see DESIGN.md / memory). Perf → bare-metal PCIe/virtio host.

## 9. Recommended first concrete step

**B.0** — produce the F-Stack-vs-stock-1.28 diff and a hunk-by-hunk catalogue.
It's low-risk, needs no pilgrim changes, and turns B.1 from "port a fork blind"
into "apply N catalogued, understood hunks." Everything after B.0 is gated on
accepting the effort/risk of B.1–B.2.

---
*Companion to `DESIGN.md` (thread-2 overview + phases 2.1a/2.1b) and
`CAPABILITIES.md` (threads 1+3). Path B is design-only until B.1 is opened.*
