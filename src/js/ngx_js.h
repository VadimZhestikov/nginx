
/*
 * Copyright (C) nginx JS contributors
 */

#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/* Forward declaration for SharedWorker state list */
struct ngx_js_sw_state_s;
typedef struct ngx_js_sw_state_s ngx_js_sw_state_t;

/*
 * Per-cycle configuration owned by ngx_js_module (NGX_CORE_MODULE).
 * Allocated in cycle->pool via create_conf; populated by js_source
 * directives during ngx_conf_parse(), executed by init_conf().
 */
typedef struct {
    ngx_array_t         sources;               /* ngx_str_t: paths from js_source        */
    JSRuntime          *rt;                    /* master-process QuickJS runtime          */
    JSContext          *ctx;                   /* master-process QuickJS context          */
    void               *worker;               /* ngx_js_worker_t* after fork             */
    ngx_js_sw_state_t  *sw_list;              /* linked list of SharedWorker states      */
    size_t              worker_memory_limit;   /* 0 = no limit; read from JS after eval  */
    size_t              worker_request_timeout;/* ms; 0 = no limit; interrupt on timeout */
} ngx_js_conf_t;


/*
 * Saved state for one suspended async nginx request.
 * Allocated in r->pool; freed automatically when the request pool is torn down.
 */
struct ngx_http_request_s;

typedef struct {
    struct ngx_http_request_s  *r;
    JSValue                     req_obj;   /* DupValue'd from content handler */
    JSValue                     promise;   /* outer handler Promise */
} ngx_js_async_ctx_t;


/*
 * Per-worker JS runtime created in init_process().
 * Workers never share a JSRuntime — QuickJS is not thread-safe.
 */
typedef struct {
    JSRuntime           *rt;
    JSContext           *ctx;
    ngx_js_async_ctx_t  *async_pending;      /* NULL or one suspended request  */
    ngx_js_sw_state_t   *local_sw_list;      /* dynamic SWs created post-fork  */
    uint64_t             request_deadline_ms; /* 0 = none; CLOCK_MONOTONIC ms   */
} ngx_js_worker_t;


/*
 * Per-location JS handler config owned by ngx_js_http_module.
 * handler_idx == -1 means no JS handler is set for this location.
 * Otherwise it is an index into the global __ngx_handlers__ array
 * that was populated by location.handler = <function> assignments
 * during the config phase.
 */
typedef struct {
    ngx_int_t  handler_idx;
} ngx_js_loc_conf_t;


extern ngx_module_t  ngx_js_module;
extern ngx_module_t  ngx_js_http_module;


/* COM initialisation — installs nginx.* into ctx's global object */
ngx_int_t  ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle);

/*
 * config.write(text) — feeds config text back into ngx_conf_parse() via a
 * temporary file.  ctx's opaque must be a valid ngx_conf_t*.  Usable from
 * both js_preprocess (core-level) and js_init_http (http-level) handlers.
 */
JSValue    ngx_js_config_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);

/* Read a file into a NUL-terminated cycle->pool buffer. */
u_char    *ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path,
    size_t *len);

/* Log a pending JS exception to the NGINX error log, then clear it */
void       ngx_js_log_exception(JSContext *ctx, ngx_log_t *log);

/*
 * Compile and execute a JS file as an ES module.  Handles the compile-only
 * → JS_EvalFunction → drain-jobs → promise-rejection-check sequence that
 * QuickJS requires for module-mode evaluation.
 * Returns NGX_CONF_OK or NGX_CONF_ERROR (exception already logged).
 */
char      *ngx_js_eval_module(JSContext *ctx, JSRuntime *rt,
    const u_char *src, size_t src_len, const u_char *filename,
    ngx_log_t *log);

/* Register NginxRequest class in rt (called once per new runtime) */
ngx_int_t  ngx_js_request_register_class(JSRuntime *rt);

/* Install shared NginxRequest prototype in ctx (called once per new context) */
ngx_int_t  ngx_js_request_install_proto(JSContext *ctx);

/* Install shared NginxPendingServer prototype in ctx */
ngx_int_t  ngx_js_pending_server_install_proto(JSContext *ctx);

/* Content-phase handler; installed in clcf->handler by the JS setter */
struct ngx_http_request_s;
ngx_int_t  ngx_js_content_handler(struct ngx_http_request_s *r);

/*
 * Inspect the promise of a suspended async request and finalize it if
 * the promise has settled.  Called from the timer handler in ngx_js_com.c.
 */
void ngx_js_async_check(ngx_js_worker_t *w);


/*
 * ------------------------------------------------------------------ *
 * Shared-memory SharedArrayBuffer infrastructure                       *
 * ------------------------------------------------------------------ *
 *
 * All SABs created by any JS runtime in this module use
 * mmap(MAP_SHARED|MAP_ANONYMOUS).  Because the mapping is established
 * before fork(), the same physical pages are visible at the same
 * virtual address in the master process AND in every nginx worker
 * process, making cross-process pointer passing correct.
 *
 * SABs created in a worker process after fork() have NGX_JS_SAB_SHARED
 * clear; the SharedWorker pipe layer rejects them with a TypeError so
 * the user gets a clear error instead of a SIGSEGV.
 *
 * Header layout (total 16 bytes; buf[] is at offset 16, 8-byte aligned):
 *
 *   offset  0  int      ref_count   (atomic)
 *   offset  4  uint32_t flags
 *   offset  8  uint32_t size        payload bytes
 *   offset 12  uint32_t _pad
 *   offset 16  uint64_t buf[]       SAB data (pointer passed to QuickJS)
 */

#define NGX_JS_SAB_SHARED  0x01u   /* mmap'd before fork — valid in all processes */
#define NGX_JS_SAB_MEMFD   0x02u   /* memfd-backed — fd tracked in per-process table */

typedef struct {
    int      ref_count;
    uint32_t flags;
    uint32_t size;
    uint32_t _pad;
    uint64_t buf[0];
} ngx_js_sab_hdr_t;


void  ngx_js_sab_dup(void *opaque, void *ptr);
void  ngx_js_sab_free(void *opaque, void *ptr);
void *ngx_js_sab_alloc(void *opaque, size_t size);

/*
 * Return the memfd fd for a worker-created SAB, or -1 if not memfd.
 * Used by the channel layer to pass the fd via SCM_RIGHTS.
 */
int   ngx_js_sab_get_fd(void *ptr);

/*
 * Register a memfd SAB mapping received from another process.
 * ptr  — data pointer (buf[] address in THIS process's mapping)
 * fd   — the local memfd fd (dup'd from SCM_RIGHTS)
 * size — payload bytes (== ngx_js_sab_hdr_t.size)
 * The mapping starts with local_refs=1; the caller releases this ref
 * after JS_ReadObject has taken its own ref via sab_dup.
 */
void  ngx_js_sab_register_memfd(void *ptr, int fd, size_t size);

extern const JSSharedArrayBufferFunctions  ngx_js_sab_funcs;

/*
 * Set to 1 inside an SW thread so ngx_js_sab_alloc uses memfd even
 * when ngx_process == NGX_PROCESS_MASTER.
 */
extern __thread int  ngx_js_sw_thread_active;


#endif /* _NGX_JS_H_INCLUDED_ */
