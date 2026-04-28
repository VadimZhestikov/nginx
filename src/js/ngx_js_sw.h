
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_sw.h — SharedWorker class: long-lived JS thread in the master
 * process, reachable by all nginx worker processes via pre-allocated pipes.
 */

#ifndef _NGX_JS_SW_H_INCLUDED_
#define _NGX_JS_SW_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>
#include "ngx_js.h"


/* Channel message types (shared with ngx_js_worker.c for Worker-thread SW) */
#define NGX_JS_SW_MSG_DATA     0u
#define NGX_JS_SW_MSG_CONNECT  1u
#define NGX_JS_SW_MSG_TERM     2u

/* Manager command types (worker → manager via sw_cmd_fds) */
#define NGX_JS_MGR_CMD_CREATE_SOCKET     1u
#define NGX_JS_MGR_CMD_BROADCAST_SOCKET  2u
#define NGX_JS_MGR_CMD_SUSPEND_ACCEPT    3u
#define NGX_JS_MGR_CMD_RESUME_ACCEPT     4u

/*
 * Bcast message layout (master → worker via per-worker socketpair).
 * Every message begins with a 1-byte type discriminator:
 *
 *   Type 0x01 (SOCKET):  [type:u8][handle:u32][addr_len:u32][addr:bytes]
 *                        SCM_RIGHTS carries the socket fd.
 *   Type 0x02 (SUSPEND): [type:u8]   — disable acceptance, send ack
 *   Type 0x03 (RESUME):  [type:u8]   — re-enable acceptance, send ack
 */
#define NGX_JS_BCAST_TYPE_SOCKET   0x01u
#define NGX_JS_BCAST_TYPE_SUSPEND  0x02u
#define NGX_JS_BCAST_TYPE_RESUME   0x03u

/* Minimum header for a SOCKET-delivery bcast: type + handle + addr_len */
#define NGX_JS_BCAST_HDR  (1 + 2 * sizeof(uint32_t))
#define NGX_JS_BCAST_MAX  (NGX_JS_BCAST_HDR + 64)


/*
 * Install the global SharedWorker constructor into ctx.
 * Called once per process from ngx_js_com_init().
 */
ngx_int_t  ngx_js_sw_install(JSContext *ctx);


/*
 * Acquire a channel to a SharedWorker for the given URL.
 * May be called from any thread in a nginx worker process (including
 * JS Worker threads) — the call blocks until the master manager replies.
 * Sends the CONNECT sentinel on the returned fd.
 * Returns the worker_fd on success, -1 on failure.
 */
int  ngx_js_sw_acquire_channel(const char *url, size_t url_len,
    ngx_uint_t worker_idx, int *wake_fd_out);

/*
 * Send a DATA message on a SharedWorker channel fd.
 * Used by JS Worker threads to call sw.postMessage().
 * Takes ownership of buf and sab_tab (freed by channel_send internals).
 * wake_fd is the write end of the SW's wake pipe (from acquire_channel);
 * wi is the channel index (= ngx_worker) to encode in the wake byte.
 */
void  ngx_js_sw_wt_send(int worker_fd, int wake_fd, ngx_uint_t wi,
    uint8_t *buf, uint32_t len, uint8_t **sab_tab, uint32_t n_sabs);

/*
 * Receive the next message from a SharedWorker channel fd (non-blocking).
 * Returns 0 on success (caller owns *buf_out / *sab_tab_out),
 *        -1 if no message is available or on error.
 */
int  ngx_js_sw_wt_recv(int worker_fd, uint32_t *type_out,
    uint8_t **buf_out, uint32_t *len_out,
    uint8_t ***sab_tab_out, uint32_t *n_sabs_out);

/*
 * Release per-worker resources (epoll connection, on_message JSValue).
 * Called from ngx_js_exit_process() before JS_FreeContext().
 */
void  ngx_js_sw_exit_process(ngx_cycle_t *cycle, ngx_js_conf_t *jcf);

/*
 * Terminate all SW threads and free all SharedWorker state.
 * Called from ngx_js_exit_master() before JS_FreeContext().
 */
void  ngx_js_sw_exit_master(ngx_js_conf_t *jcf);

/*
 * Stop and join all SW pthreads in jcf->sw_list, free their state.
 * Does NOT touch the manager thread (singleton shared across reloads).
 * Called on reload from ngx_js_init_module() to clean up the old config's
 * threads and channel fds before the old JS runtime is freed.
 */
void  ngx_js_sw_retire_threads(ngx_js_conf_t *jcf);

/*
 * Redirect the manager thread's dynamic-SW creation list to a new jcf.
 * Called on reload so that new SharedWorker(url) requests from workers
 * attach to the current config's list, not the old (retired) one.
 */
void  ngx_js_sw_update_mgr_jcf(ngx_js_conf_t *jcf);

/*
 * Create the command socketpair and term pipe, then start the SW manager
 * thread.  The manager handles new SharedWorker(url) requests from worker
 * processes, creating SW threads in the master on demand.
 * Called once at the end of ngx_js_init_conf().
 */
ngx_int_t  ngx_js_sw_manager_start(ngx_js_conf_t *jcf,
    ngx_cycle_t *cycle);

/*
 * Start all deferred SW pthreads (those created via new SharedWorker(url)
 * during init_conf, which must not start threads before daemonisation).
 * Called via the ngx_js_sw_threads_start function pointer from
 * ngx_master_process_cycle / ngx_single_process_cycle, after daemonisation.
 */
void  ngx_js_sw_threads_start_deferred(ngx_cycle_t *cycle);

/*
 * Ask the manager thread (running in the master process) to create a
 * bound + listening TCP socket for the given "host:port" address string.
 * Blocks until the manager replies.
 * Returns the new socket fd on success, -1 on failure.
 * Only valid when called from a worker process after fork.
 */
int  ngx_js_socket_mgr_create(const char *addr_str, size_t addr_len);

/*
 * Broadcast socket handle to all other workers via per-worker bcast sockets.
 * The socket must already be in ngx_js_socket_reg[handle] in this worker.
 * Blocks until the manager confirms delivery to all workers.
 * Returns 0 on success, -1 on failure.
 * Only valid when called from a worker process after fork.
 */
int  ngx_js_socket_mgr_broadcast(uint32_t handle,
    const char *addr_str, size_t addr_len);

/*
 * Phase 2 — send a SUSPEND_ACCEPT or RESUME_ACCEPT command to the manager.
 * The manager broadcasts the corresponding bcast control message to all
 * workers, waits for their acks, then writes 1 byte on the returned fd.
 * Caller must register the returned fd with the nginx event system and
 * resolve the corresponding JS Promise when the fd becomes readable.
 * cmd_type must be NGX_JS_MGR_CMD_SUSPEND_ACCEPT or _RESUME_ACCEPT.
 * Returns the reply fd on success, -1 on failure.
 */
int  ngx_js_mgr_accept_control(uint32_t cmd_type);

/*
 * Activate this worker's channel for every static SharedWorker.
 * Idempotent; safe to call on every request (no-op after first call).
 * Must only be called from within the nginx event loop.
 */
void  ngx_js_sw_ensure_all_active(JSContext *ctx);

/*
 * Return the worker-readable end of the per-worker broadcast socketpair.
 * Used by ngx_js_init_process() to register the bcast event handler.
 * Returns -1 if the broadcast infrastructure is not available.
 */
int  ngx_js_sw_get_bcast_fd(ngx_uint_t wi);


#endif /* _NGX_JS_SW_H_INCLUDED_ */
