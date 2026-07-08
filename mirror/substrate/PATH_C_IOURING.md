# mirror — Thread 2, Path C: io_uring `ngx_substrate` backend (scoping)

Status: **scoping / design only.** Path B proved the seam's *generality* against a
kernel-bypass stack (pilgrim + mirror over F-Stack/DPDK, functional in the KVM
guest; bare-metal numbers blocked on hardware). Path C targets the middle rung
the thread has not evaluated: **io_uring** — kernel-native completion-based I/O.
No fork, no root, no lcore model, no fd-namespace split; runs on the existing
WSL2 and n6 bare-metal rigs today. This doc scopes an io_uring backend behind
`ngx_substrate`, grounded in the landed 2.1a/2.1b seam and the Path B findings.

## 0. Definition of done

Pilgrim `objs/nginx` serves real traffic with (a) an `ngx_uring_module` event
module selected by `use uring;` and (b) a `"uring"` substrate moving plaintext
connection bytes in completion mode — with the full `t/` suite and the mirror
`example` (54 checks) green under both, and an A/B row in `t_performance`
(epoll vs uring on n6 bare metal). Unlike Path B, **performance is in scope**:
the whole point of this rung is numbers on hardware we already have.

## 1. Why Path C exists (in Path B's own terms)

The B.0 catalogue measured what F-Stack costs: 53 files, +1895 LOC, and the
three pieces `PATH_B_FINDINGS.md` (branch `mirror-dpdk-b1`) concluded *cannot*
be expressed through the seam — the `is_fstack_fd` syscall shim, the `ff_run`
loop inversion, and the dual event module for kernel fds. io_uring has none of
them:

- **No fd duality.** io_uring operates on ordinary kernel fds. SharedWorker
  AF_UNIX socketpairs, memfd SABs, the resolver, logs, the channel — everything
  that forced F-Stack's host event module — works natively. Catalogue buckets
  A and G evaporate; the pilgrim coexistence risks of PATH_B §4 do not exist.
- **No loop inversion.** The worker keeps its loop; `io_uring_enter` replaces
  `epoll_wait` inside `process_events`. Bucket B evaporates.
- **Runs on existing rigs.** Path B's Table C is blocked on a cable, sudo, and
  a dubious `bnx2x` PMD. **Path C's Table C is unblocked now** with the
  existing `t_performance` harness on n6.

And the design-debt argument: io_uring's provided-buffer rings pose the *same*
buffer-ownership question DESIGN.md §3.2 flags for DPDK mbufs ("define the
copy/borrow boundary in `recv_chain`/`send_chain`"). Path C answers it once,
cheaply testable, and DPDK/TMM backends inherit the contract.

## 2. The impedance mismatch, stated precisely

nginx is **pull**: readiness fires → the handler calls `c->recv(c, buf, size)`
with the buffer it wants filled, *now*. io_uring's payoff mode is **push**: a
recv SQE carries a buffer pinned at submission; the CQE says it is already
full. Everything in the plan follows from two consequences:

1. **Poll mode is cheap and compatible.** io_uring emulates epoll via multishot
   `IORING_OP_POLL_ADD`; `ngx_os_io` and all callers stay untouched. Modest
   win, near-zero risk — the scaffolding phase.
2. **Completion mode changes buffer lifetime.** A submitted buffer must outlive
   the request that submitted it:
   - *Recv:* the kernel fills a buffer the caller didn't choose (provided-buffer
     ring). v1 delivers by bounce-copy into the caller's buffer, preserving the
     `c->recv` / `NGX_AGAIN` contract byte-for-byte; the copy is removed later
     by the borrow API (§5, phase U.4).
   - *Send/teardown:* an in-flight SQE references request-pool memory, and
     `ngx_close_connection` frees `c` synchronously into the free list. A CQE
     landing after that is a use-after-free. **2.1b already routes close through
     the substrate** — exactly the hook needed: `close` becomes submit
     `ASYNC_CANCEL` + defer reclamation of a generation-counted per-connection
     state object (the SQE `user_data`) until all in-flight CQEs are reaped.
     This is the substrate-level analog of the `r->count` finalize discipline;
     build the debug invariant checker in from day one.

**TLS bypasses the seam.** `ngx_ssl_recv` → `SSL_read` → OpenSSL's own `read()`
on the fd; `c->recv` never moves TLS bytes. Completion-mode TLS needs the
memory-BIO rework DESIGN.md §3.2 already prescribes for DPDK. Crucially, the
per-connection substrate tag (B.4) defers this: **TLS connections stay on the
poll path while plaintext goes completion-mode — mixed per connection, already
architecturally supported.**

## 3. What `ngx_substrate_t` must grow

Today's vtable (`ngx_substrate.h`) is `{name, io, close, shutdown}`. Path C
needs, consistent with the header's own `NOT YET ROUTED` list and the §2.1
target vtable in DESIGN.md:

- `init(cycle)` / `done(cycle)` — rings are **per worker, created post-fork**
  (rings do not survive fork); maps exactly onto event-module `init`.
- `caps` flags — `sendfile` (none in io_uring; use `IORING_OP_SPLICE` or fall
  back to `send_chain`), `zerocopy_send` (`SEND_ZC`), `fixed_files`.
- An async-aware `close` **contract** — signature can stay `close(fd)`, but
  document "reclamation may be deferred" in the header now, before a second
  backend assumes synchronous close.

Notably **not** needed: the opaque `conn_handle_t` generality — io_uring keys
everything by real fds. That is why this backend is an order of magnitude
lighter than Path B while still exercising the seam's hard parts.

## 4. Kernel/feature baseline

Target **kernel ≥ 6.1**: multishot accept (5.19), multishot recv +
`IORING_REGISTER_PBUF_RING` (5.19/6.0), `SEND_ZC` (6.0), `DEFER_TASKRUN` +
single-issuer (6.1). All features **probed at startup**, per-feature fallback
(multishot recv absent → single-shot resubmit), POSIX default untouched.
io_uring is disabled in many hardened/container environments
(`io_uring_disabled` sysctl; Docker's default seccomp) — Path C can only ever
be an opt-in backend, same posture DESIGN.md §6 takes for DPDK ("mainline
untouched until proven"). SQPOLL is out of scope: it burns a dedicated core
and fights the worker-per-core model.

## 5. Phased sub-plan

| Sub-phase | Deliverable | Risk | WSL2? |
|---|---|---|---|
| **U.0** | Feature probe + runtime selection plumbing; `use uring;` accepted; POSIX fallback path proven | low | yes |
| **U.1** | **Event module, poll mode**: `ngx_uring_module` implements `ngx_event_actions_t` via multishot `POLL_ADD`; `notify` via `MSG_RING`/eventfd; nginx rbtree timers kept (min-timeout passed to `io_uring_enter`). I/O stays `ngx_os_io`; sendfile + TLS untouched. ~600–900 LOC (epoll module ≈ 1100). Full `t/` + mirror 54/54 green | low | yes |
| **U.2** | **Completion-mode recv** behind a `"uring"` substrate: multishot recv + per-worker provided-buffer ring (e.g. 1024×16 KB ≈ 16 MB, directive-tunable), bounce-copy delivery preserving the `c->recv` contract. Plaintext connections only (per-conn tag); TLS stays on U.1 path | med | yes |
| **U.3** | **Completion-mode send + multishot accept** + the cancel-on-close reclamation object (§2). `SEND_ZC` only where buffer lifetime is provable. Accept folds into the event module — consistent with 2.1b's reclassification of `accept()` into the backend | med-high | yes |
| **U.4** | **The borrow API**: lend/return semantics on the `recv_chain`/`send_chain` seam — removes the U.2 copy for pass-through proxying, and **is the same contract DPDK mbufs / TMM need** (answers DESIGN.md §3.2 once) | high | yes |
| **U.5** (opt) | File-I/O ops (`READ`/`SPLICE`) replacing thread-pool AIO for static serving; orthogonal track | med | yes |

Each sub-phase is a checkpoint; U.1 is the 2.1a of this path — behavior-
identical, regression-guarded, unlocks everything after it.

## 6. Measurement plan (the point of the path)

Rerun the existing `t_performance` ladder (stock / njs / pilgrim / mirror ×
h1 / h2 / https) on **n6 bare metal**, `use epoll` vs `use uring` (U.1) vs
completion mode (U.2/U.3). No new hardware, no cabling, no PMD.

Priced honestly (DESIGN.md §6: "measure, don't assume"):

- **U.1 poll mode:** small — batched poll rearms, single digits. Its value is
  scaffolding + a clean A/B rig.
- **U.2/U.3 completion mode:** public epoll-vs-uring proxy/echo results with
  `DEFER_TASKRUN` + single-issuer cluster around **+10–30%** on small-message
  high-connection keepalive workloads; parity or worse on large transfers
  where the bounce memcpy dominates (until U.4). Post-Spectre syscall pricing
  is what makes batching pay.
- **Where it won't help:** CPU-bound TLS handshakes, gzip, JS execution — a
  different recommendation's territory (thread-pool/private-key offload).
- The VM caveat from `RESULTS_baremetal_vm.md` applies unchanged: virtio
  ceilings mask the delta; only bare-metal rows count.

## 7. Risks

- **Stale-CQE use-after-free** — the one genuinely dangerous class; mitigated
  by the generation-counted state object + debug invariant checker, and by the
  fact that teardown already routes through the seam (2.1b was the
  prerequisite, and it is done).
- **Kernel/feature sprawl** — startup probe + per-feature degradation.
- **Deployment veto** — io_uring's LPE CVE history means some security
  postures disable it wholesale; opt-in backend, POSIX default, forever.
- **TLS memory-BIO rework** — real C work, but shared with the DPDK/TMM path;
  do it once at U.3+, not as an io_uring special.
- **Buffer-ring memory accounting** — per-worker ring is a new fixed cost;
  directive-tunable, and must be sized against `worker_connections`.

## 8. Non-goals

- Replacing epoll as default — POSIX/epoll stays the reference backend (§3.1
  of DESIGN.md) indefinitely.
- kTLS/SSL offload interplay (later, with the memory-BIO work).
- Upstreaming (same posture as the seam itself: long-lived branch / RFC).
- Windows/kqueue analogs.

## 9. Recommended first concrete step

**U.1.** It is in-tree, WSL2-friendly, regression-guarded by the existing
suites, produces the A/B measurement rig for free, and — like 2.1a before it —
is a behavior-identical commit that unlocks every later phase. U.0's probe
plumbing falls out of writing it.

---
*Companion to `DESIGN.md` (thread-2 overview, phases 2.1a/2.1b),
`PATH_B_DPDK.md` / `PATH_B_B0_CATALOGUE.md` (DPDK path), and the Path B
findings on branch `mirror-dpdk-b1`. Path C is design-only until U.1 is
opened.*
