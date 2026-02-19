  Worker-created SAB → SharedWorker via memfd

  ngx_js.h — added NGX_JS_SAB_MEMFD flag and declarations for ngx_js_sab_get_fd / ngx_js_sab_register_memfd.

  ngx_js_module.c — memfd SAB allocator:
  - Workers now use memfd_create(2) + mmap(MAP_SHARED, fd) instead of MAP_ANONYMOUS for post-fork SABs
  - Per-process fd table (64 slots, mutex) tracks {ptr, fd, size, local_refs} for memfd SABs
  - sab_dup/sab_free check the fd table first; pre-fork SABs still use the shared atomic ref_count

  ngx_js_sw.c — channel infrastructure refactor:
  - Replaced pipe() inbox/outbox pairs with a single AF_UNIX SOCK_SEQPACKET socketpair per worker (sw_fd + worker_fd) — sockets support SCM_RIGHTS
  - New 16-byte header: [type, data_len, n_sabs, n_memfds]
  - channel_send: classifies SABs as pre-fork (transit dup + VA in body) or memfd (fd via SCM_RIGHTS + sender VA for patching)
  - channel_recv: receives memfd fds via SCM_RIGHTS, mmaps each to a local VA, patches the serialised buffer (replacing sender VA with local VA), registers in fd table
  - Removed the NGX_JS_SAB_SHARED restriction — any SAB can now cross the process boundary
  - Dynamic SW manager now returns one fd (worker_fd) per reply instead of two

  t/js_sw_worker_sab.t — 6 tests verifying worker-created SABs reach the SharedWorker correctly, including the true shared-memory mutation test.
