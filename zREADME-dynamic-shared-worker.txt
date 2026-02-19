  Dynamic SharedWorker creation from worker handlers

  new SharedWorker(url) now works from nginx worker request handlers, not just js_include init code. The SW thread still runs in the master process.

  Architecture

  Pre-fork: ngx_js_sw_manager_start() (called at end of ngx_js_init_conf) creates:
  - A AF_UNIX SOCK_SEQPACKET socketpair — workers write requests, master reads them
  - A termination pipe
  - A ngx_js_sw_manager_thread pthread in the master

  Worker requests a new SW:
  1. Creates a private socketpair for the reply
  2. Sends [url_len][worker_idx][url] + SCM_RIGHTS{reply_fd} to sw_cmd_fds[1]
  3. Blocking recvmsg on the reply socket receives {inbox.wfd, outbox.rfd}
  4. Builds a one-channel stub ngx_js_sw_state_t, stores in w->local_sw_list

  Manager (master):
  - Finds or creates the full N-channel ngx_js_sw_state_t, starts SW pthread
  - Sends the requesting worker's channel fds back via SCM_RIGHTS

  New ngx_js_sw_opaque_t.local_wi field: (ngx_uint_t)-1 = static SW (use ngx_worker), 0 = stub (always use slot 0). All three methods (postMessage, onmessage getter/setter, activate) use sw_wi(op)
  instead of ngx_worker directly.

  Cleanup: exit_process frees w->local_sw_list stubs; exit_master signals the manager via term pipe and joins it before tearing down sw_list.
