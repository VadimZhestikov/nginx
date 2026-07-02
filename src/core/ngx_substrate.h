
/*
 * Copyright (C) nginx JS contributors
 *
 * mirror thread 2 — substrate interchange, phase 2.1.
 *
 * A pluggable I/O-substrate seam: the single place a data-plane backend
 * (POSIX sockets today; DPDK / userspace-TCP or TMM later) supplies the
 * per-connection I/O vtable. It sits BELOW the JS / mirror layer, so mirror
 * rules run unchanged regardless of which substrate moves the bytes — that is
 * the whole point of the "one programmability layer, swappable substrate"
 * thesis (see mirror/substrate/DESIGN.md).
 *
 * PHASE 2.1 SCOPE (this file): the data-movement vtable only, with a POSIX
 * reference backend that delegates to the existing ngx_os_io. Accept and
 * connect bind a connection's recv, send and chain ops from ngx_substrate->io
 * instead of the hardcoded ngx_recv/ngx_send globals. Behaviour is byte-identical
 * for the POSIX backend (its io == &ngx_os_io).
 *
 * NOT YET ROUTED (phase 2.1b+): listener creation, accept() itself, the
 * event-readiness vtable (ngx_event_actions), fd lifecycle (close/shutdown) and
 * socket-option shims. Those still go straight to POSIX; see the DESIGN doc for
 * the ngx_substrate_t vtable they will grow into.
 */

#ifndef _NGX_SUBSTRATE_H_INCLUDED_
#define _NGX_SUBSTRATE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_substrate_s  ngx_substrate_t;

struct ngx_substrate_s {
    const char   *name;      /* "posix", "dpdk", "tmm", ... */
    ngx_os_io_t  *io;        /* per-connection I/O vtable (recv/send/chains) */

    /* connection-teardown lifecycle (phase 2.1b). fd-based for now — POSIX
     * delegates to ngx_close_socket / ngx_shutdown_socket. The signature will
     * grow to take the connection/handle when a non-fd backend arrives. */
    ngx_int_t   (*close)(ngx_socket_t fd);
    ngx_int_t   (*shutdown)(ngx_socket_t fd, int how);
};


/*
 * The active substrate. Defaults to the POSIX backend; a future backend would
 * install itself here at startup, before any connection is accepted.
 */
extern ngx_substrate_t  *ngx_substrate;


#endif /* _NGX_SUBSTRATE_H_INCLUDED_ */
