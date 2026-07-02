
/*
 * Copyright (C) nginx JS contributors
 *
 * mirror thread 2 — substrate interchange, phase 2.1.
 * POSIX reference backend + the active-substrate global. See ngx_substrate.h.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/* POSIX connection-teardown ops: thin delegates to the socket syscalls. */

static ngx_int_t
ngx_posix_substrate_close(ngx_socket_t fd)
{
    return ngx_close_socket(fd);
}


static ngx_int_t
ngx_posix_substrate_shutdown(ngx_socket_t fd, int how)
{
    return ngx_shutdown_socket(fd, how);
}


/*
 * POSIX backend: the data-movement vtable is the platform's ngx_os_io, which
 * ngx_os_specific_init() fills in (e.g. ngx_linux_io) before any connection is
 * accepted. &ngx_os_io is a link-time constant, so static initialisation is
 * safe; the vtable's contents are resolved by the time accept/connect read it.
 */
static ngx_substrate_t  ngx_posix_substrate = {
    "posix",
    &ngx_os_io,
    ngx_posix_substrate_close,
    ngx_posix_substrate_shutdown
};


ngx_substrate_t  *ngx_substrate = &ngx_posix_substrate;
