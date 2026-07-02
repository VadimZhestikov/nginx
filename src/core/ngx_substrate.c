
/*
 * Copyright (C) nginx JS contributors
 *
 * mirror thread 2 — substrate interchange, phase 2.1.
 * POSIX reference backend + the active-substrate global. See ngx_substrate.h.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * POSIX backend: the data-movement vtable is the platform's ngx_os_io, which
 * ngx_os_specific_init() fills in (e.g. ngx_linux_io) before any connection is
 * accepted. &ngx_os_io is a link-time constant, so static initialisation is
 * safe; the vtable's contents are resolved by the time accept/connect read it.
 */
static ngx_substrate_t  ngx_posix_substrate = {
    "posix",
    &ngx_os_io
};


ngx_substrate_t  *ngx_substrate = &ngx_posix_substrate;
