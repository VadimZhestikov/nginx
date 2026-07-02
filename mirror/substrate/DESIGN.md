# mirror — Thread 2: Substrate Interchange (design doc, phase 2.x)

Status: **phases 2.1a + 2.1b landed** (per-connection I/O + teardown seam); the
rest is design-only. This scopes the substrate-interchange track, deliberately
targeting the ~90% of thread 2 that needs **no BIG-IP / TMM source access**, and
quarantining the part that does.

> **Phase 2.1a (done):** `src/core/ngx_substrate.{h,c}` defines `ngx_substrate_t`
> + the global `ngx_substrate`, with a POSIX reference backend whose `io` is the
> live `ngx_os_io`. `ngx_event_accept.c` and `ngx_event_connect.c` bind a
> connection's `recv`/`send`/chain ops from `ngx_substrate->io` instead of the
> hardcoded `ngx_recv`/`ngx_send` globals. Byte-identical for POSIX; wired into
> `auto/sources` + `ngx_core.h`.
>
> **Phase 2.1b (done):** the vtable grew connection-teardown ops
> `close(fd)` + `shutdown(fd, how)` (POSIX → `ngx_close_socket` /
> `ngx_shutdown_socket`). The central per-connection close in
> `ngx_close_connection` (`ngx_connection.c` — every connection, inbound and
> upstream) and the HTTP write-`shutdown` (`ngx_http_request.c`) now route through
> the substrate. So the seam now owns both halves of the per-connection
> data-plane: **I/O (2.1a) + teardown (2.1b)**. Verified: full `t/` green + mirror
> `example` 54/54.
>
> **Reclassified / deferred (was "2.1b" in the original table):**
> - **Event readiness (`ngx_event_actions`)** is *already* pluggable via nginx's
>   event-module mechanism (epoll/kqueue/… are selectable modules). A DPDK/TMM
>   backend adds an **event module** (à la the epoll module), not a `ngx_substrate`
>   hook — so this is out of the substrate vtable by design.
> - **`accept()`** is entangled with platform `accept4` detection (a mutable
>   `static use_accept4` + an `ENOSYS`-fallback retry loop in `ngx_event_accept`).
>   A real backend delivers connections from its own event source and replaces
>   that path wholesale, so routing it through a generic POSIX-identity hook now
>   buys nothing; folded into the backend work (§2.2).
> - **Listener creation** (`ngx_open_listening_sockets`) and broad **socket-option**
>   call-sites are setup-time and platform-heavy; deferred to the backend phase.

## 0. TL;DR

- Thread 2 = decouple the data-plane **engine** from its **I/O substrate**, both
  directions: nginx-on-TMM, and BIG-IP-on-OS-sockets.
- We do **not** need BIG-IP sources for the enabling work. The plan:
  1. **Carve a substrate seam** in nginx (it nearly exists already — `ngx_os_io_t`
     + `ngx_event_actions_t` + a handful of `ngx_connection_t` fields).
  2. **Keep POSIX/epoll as the reference backend** behind that seam (no behaviour
     change; the whole `t/` suite must stay green).
  3. **Bring up a second, genuinely non-socket backend — DPDK + a userspace TCP
     stack (F-Stack-class)** — as a *stand-in for a TMM-class substrate*, exactly
     as `mirror.kv` used an `nginx.shared`-backed HTTP service to stand in for
     Redis in phase 17.
  4. **Write a black-box conformance suite** (from a BIG-IP VE observed as a black
     box + public docs) that every backend must pass.
- What genuinely needs F5 access — binding TMM's real primitives, hardware
  offload, bug-for-bug data-plane parity — becomes a *later, access-gated backend*
  behind the same seam, not a prerequisite.

## 1. Why this is tractable without their sources

**BIG-IP-on-OS-sockets (direction b) is half-done already.** Its *programmability*
layer — the iRules event/command model on OS sockets — **is mirror** (threads 1+3,
phases 1–18). What remains of (b) is data-plane feature parity, which is again a
substrate question, not a "read their code" question.

**nginx-on-TMM (direction a)** reduces to: *can nginx's event core talk to a
substrate that is not BSD sockets?* That is answered entirely within our tree by
defining the seam and proving it against a non-socket backend. TMM itself is, from
public material (F5 patents, DevCentral, CMP/TMM architecture talks), a
**userspace, kernel-bypass, run-to-completion** stack — and the open analog of
that is DPDK + userspace TCP. So a DPDK backend is a faithful *architectural*
proxy for TMM; the TMM-specific binding is a later swap.

## 2. The seam — what nginx already gives us

nginx isolates OS I/O far better than most servers. Two structs are the seam:

**Data movement — `ngx_os_io_t` (`src/os/unix/ngx_os.h`), bound per-connection as
`c->recv`, `c->send`, `c->recv_chain`, `c->send_chain`, `c->udp_*`:**

```c
typedef struct {
    ngx_recv_pt        recv;         // (c, buf, size) -> ssize_t
    ngx_recv_chain_pt  recv_chain;   // (c, chain, limit)
    ngx_recv_pt        udp_recv;
    ngx_send_pt        send;
    ngx_send_pt        udp_send;
    ngx_send_chain_pt  udp_send_chain;
    ngx_send_chain_pt  send_chain;
    ngx_uint_t         flags;
} ngx_os_io_t;
```

**Readiness / notification — `ngx_event_actions_t` (`src/event/ngx_event.h`),
the event-module interface epoll/kqueue/... implement:**

```c
typedef struct {
    ngx_int_t (*add)(ev, event, flags);
    ngx_int_t (*del)(ev, event, flags);
    ngx_int_t (*enable)(ev, event, flags);
    ngx_int_t (*disable)(ev, event, flags);
    ngx_int_t (*add_conn)(ngx_connection_t *c);
    ngx_int_t (*del_conn)(ngx_connection_t *c, flags);
    ngx_int_t (*notify)(ngx_event_handler_pt handler);
    ngx_int_t (*process_events)(cycle, timer, flags);
    ngx_int_t (*init)(cycle, timer);
    void      (*done)(cycle);
} ngx_event_actions_t;
```

Between them these already express **read/write bytes**, **readiness events**,
and **cross-thread wakeup** (`notify`). A substrate is essentially: *a listen/
accept source + an `ngx_os_io_t` + an `ngx_event_actions_t` + a buffer-ownership
policy.*

### 2.1 Where the abstraction leaks (the real work)

The seam is not clean today; nginx assumes a POSIX **fd** in many places:

- `ngx_connection_t.fd` is an `int` and is passed to `getsockopt`, `setsockopt`,
  `ngx_nonblocking`, `ngx_shutdown_socket`, `ngx_close_socket`, `sendfile`,
  `ngx_socket`, `bind`/`listen` directly (not via the seam).
- Listeners (`ngx_listening_t`) create/own real sockets; `ngx_event_accept`
  calls `accept4()`.
- TLS (`ngx_ssl_*`) uses `SSL_set_fd` / BIO-over-fd.
- `sendfile`, `ngx_output_chain`, `ngx_linux_sendfile_chain` assume kernel fds.
- Blocking/one-off syscalls: resolver, `ngx_open_file`, unix-domain control.

**Phase-2.1 deliverable is precisely: make these go through a substrate handle
instead of a raw fd**, with the POSIX backend keeping today's behaviour byte-for-
byte. The design target is an opaque `ngx_conn_handle_t` (fd for POSIX; a
`{lcore, flow-id}` or stack-object pointer for DPDK/TMM) plus a `substrate` vtable:

```
ngx_substrate_t {
    // lifecycle
    init(cycle) / done(cycle)
    // listeners
    open_listener(addr, opts) -> listener_handle
    accept(listener_handle) -> conn_handle (+ peer/local addr)
    // per-connection I/O  (== ngx_os_io_t, but keyed by conn_handle)
    recv / recv_chain / send / send_chain / udp_*
    // readiness (== ngx_event_actions_t)
    event_add/del/add_conn/del_conn/notify/process_events
    // teardown + socket-option shims
    shutdown(conn_handle, how) / close(conn_handle)
    getopt/setopt(conn_handle, opt, ...)   // TCP_NODELAY, SO_*, etc.
    // capability flags: sendfile?, zerocopy?, tls-offload?, so_reuseport?
    caps
}
```

`ngx_os_io` and `ngx_event_actions` become the POSIX *implementation* of this,
so the refactor is mostly "route fd-touching call sites through `substrate->…`."

## 3. Backends

### 3.1 Backend #1 — POSIX/epoll (reference)
The existing code, moved behind the seam. Success = **zero behaviour change**,
full `t/`, `t_stress`, and `mirror/example` suites stay green. This backend is the
executable definition of "correct" for the conformance suite (§4).

### 3.2 Backend #2 — DPDK + userspace TCP (the TMM stand-in)
Purpose: prove the seam supports a **non-socket, kernel-bypass, run-to-completion**
substrate, and measure the performance thesis that motivates TMM — with fully
open components.

Candidate stacks:

| Option | Notes |
|---|---|
| **F-Stack** | DPDK + FreeBSD TCP/IP; already ships an *nginx-on-DPDK* port. `ff_socket/ff_epoll/...` mirror POSIX → smallest seam impedance. **Recommended starting point.** |
| mTCP | DPDK + custom TCP; epoll-like API; research-grade. |
| VPP + TLDK | Feature-rich, heavier integration surface. |
| Seastar | C++/shard-per-core; largest rewrite. |

Integration questions the doc flags for phase 2.2:
- **Execution model.** DPDK is poll-mode / run-to-completion per **lcore**; nginx
  is one **worker** per core with an event loop. Map worker↔lcore 1:1; `process_
  events` drives `ff_epoll_wait` (F-Stack) instead of `epoll_wait`.
- **fd space.** F-Stack fds are a separate namespace from kernel fds — this is
  exactly why the `conn_handle` opacity in §2.1 matters. Kernel fds (log files,
  signals, the resolver, unix control socket) must still use the OS; only
  data-plane connections use the substrate. The seam must let both coexist.
- **TLS.** Terminate over a memory BIO fed by substrate recv/send (not `SSL_set_
  fd`), since there is no kernel fd. (pilgrim's `onClientHello` work already
  touches the SSL layer — reuse that seam.)
- **`sendfile`/zerocopy.** Not available the same way; `caps.sendfile=0` → fall
  back to `send_chain`. The capability flags exist for exactly this.
- **Buffers.** DPDK mbufs vs nginx `ngx_buf_t`/`ngx_chain_t` ownership. Define the
  copy/borrow boundary in `recv_chain`/`send_chain`.

**Build/runtime reality (called out honestly):** DPDK needs hugepages, a bound
NIC (or virtio/`--vdev` in a VM), and root; it **will not run under the current
WSL2 test rig**. Phase 2.2 requires a real Linux host/VM with DPDK. This track is
materially heavier than the pure-JS phases and touches `auto/configure` + link
flags.

### 3.3 Backend #3 — TMM (future, access-gated)
Same vtable, backed by TMM's real I/O primitives / plugin ABI + hardware offload
(ePVA/FPGA, CMP/RSS). Needs an F5 SDK/partnership. **Not a prerequisite** for
2.1–2.4; it slots in once access exists, and the conformance suite (§4) is how we
know it behaves.

## 4. Conformance spec (contract-first, black-box)

To converge two engines without seeing one's source, define the substrate's
**observable behavioural contract** and make every backend pass it.

Derived from: a **BIG-IP Virtual Edition** (free trial) observed as a *black box*
+ public docs — never their source.

Contract dimensions (each becomes test cases):
- **Flow lifecycle:** accept → establish → data → half-close → close; the exact
  event a rule sees at each edge (ties to mirror's onClientAccept/Data/Close).
- **Byte semantics:** partial reads/writes, short sends, `EAGAIN`/would-block
  equivalents, coalescing, message boundaries for UDP/datagram.
- **Backpressure / flow control:** writable-readiness, send-buffer-full behaviour.
- **Ordering & atomicity:** per-connection ordering guarantees; what `notify`
  wakeups guarantee.
- **Error taxonomy:** reset vs graceful close vs timeout, surfaced uniformly.
- **Timers:** resolution and firing semantics used by nginx event timers.
- **Addressing:** peer/local addr, PROXY-protocol, source NAT visibility.

**Harness design:**
1. **Substrate unit tests** — drive the vtable directly with a loopback flow,
   asserting each contract dimension. Run per backend.
2. **Integration conformance** — run the *existing* `mirror/example/test.sh` (54
   checks) unchanged against each backend. If mirror rules behave identically on
   POSIX and DPDK, the seam is real. This reuses everything we already built as a
   substrate-agnostic acceptance test — the payoff of having mirror sit *above*
   the seam.

## 5. Fit with mirror / pilgrim

The seam sits **below** the JS layer. mirror rules, the transpiler, `table`,
`persist`, `mirror.kv` — all unchanged — run on any backend. That is the thesis of
the whole project stated precisely: **one programmability layer (mirror), swappable
substrate (POSIX / DPDK / TMM).** Threads 1+3 gave us the top half; thread 2 makes
the bottom half pluggable, and the two meet at `ngx_substrate_t`.

## 6. Risks / unknowns

- **fd assumptions are pervasive** in nginx and third-party modules — the 2.1
  refactor is the risky part (breadth, not depth). Mitigation: POSIX backend must
  stay byte-identical; land it behind a compile-time default so mainline is
  untouched until proven.
- **DPDK build/run friction** (hugepages, NIC binding, no WSL2) — needs a
  dedicated lab host; scope 2.2 accordingly.
- **Performance-claim validity** — kernel-bypass wins are workload- and
  NIC-dependent; measure, don't assume.
- **Upstreaming** — a substrate seam is invasive; treat as a long-lived branch /
  RFC, not a drop-in.
- **Access/licensing for TMM** — F5-internal; out of engineering's control.

## 7. Phased plan

| Phase | Deliverable | Needs BIG-IP? | Runs on WSL2? |
|---|---|---|---|
| **2.1a** ✅ | `ngx_substrate_t` + POSIX backend; accept/connect bind I/O from `ngx_substrate->io`; green (no behaviour change) | no | yes |
| **2.1b** ✅ | vtable + `close`/`shutdown`; central connection-close + HTTP write-shutdown routed through it; green | no | yes |
| ~~listener/accept/event-actions routing~~ | reclassified: event loop already pluggable via event modules; accept/listener fold into the backend (2.2) | — | — |
| **2.2** | DPDK/F-Stack backend behind the seam; nginx serves over it on a lab host | no | **no** (real Linux + DPDK) |
| **2.3** | Conformance suite (substrate unit tests + mirror/example run per backend); POSIX≡DPDK | no (VE black-box + docs) | 2.1 part yes |
| **2.4** | TMM backend | **yes (F5 SDK/partnership)** | n/a |

**Path A (F-Stack's own nginx over DPDK) is proven** in the KVM guest; the pilgrim
DPDK-backend integration (**Path B**) is scoped separately in
[`PATH_B_DPDK.md`](PATH_B_DPDK.md) — grounded in a recon of F-Stack's actual nginx
patch (syscall shim + loop inversion + dual event modules).

Recommended first concrete step: **phase 2.1** — it's in-tree, WSL2-friendly,
regression-guarded by the existing suites, and unlocks everything after it.

## 8. Explicitly out of scope here

- Any TMM source, ABI, or reverse-engineering.
- Hardware-offload emulation.
- Data-plane feature parity with BIG-IP modules (AFM/ASM/etc.).
- Committing to upstream nginx.

---
*Companion to `mirror/CAPABILITIES.md` (threads 1+3). This doc is thread 2, and is
design-only until a phase 2.1 branch is opened.*
