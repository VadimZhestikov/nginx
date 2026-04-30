
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_module — NGX_CORE_MODULE that embeds QuickJS into NGINX.
 *
 * Lifecycle:
 *   create_conf  — allocate ngx_js_conf_t in cycle->pool
 *   [ngx_conf_parse runs; js_source directives populate jcf->sources;
 *    js_preprocess directives run immediately and may inject config text]
 *   init_conf    — create JSRuntime/JSContext, install COM, eval scripts
 *   init_process — each worker inherits jcf->rt/ctx directly (fork COW);
 *                  no new runtime, no re-evaluation, no I/O
 *   exit_process — free worker's private copy of the runtime
 *   exit_master  — free master's original runtime
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_socket.h"
#include "ngx_js_sw.h"
#include "ngx_js_listener.h"


/* ------------------------------------------------------------------ */
/* Shared-memory SAB allocator (see ngx_js.h for design rationale)    */
/* ------------------------------------------------------------------ */

/*
 * Per-process table of memfd-backed SABs.
 *
 * Workers create SABs post-fork using memfd_create so the backing memory
 * can be passed to the master (SharedWorker thread) via SCM_RIGHTS.
 * Each process tracks its own {data-pointer → memfd fd} mapping.
 * local_refs counts JS references within THIS process only.
 */

#ifdef SYS_memfd_create
# ifndef MFD_CLOEXEC
#  define MFD_CLOEXEC  1U
# endif
static int
ngx_memfd_create(const char *name, unsigned int flags)
{
    return (int) syscall(SYS_memfd_create, name, flags);
}
# define NGX_JS_HAVE_MEMFD  1
#endif


#define NGX_JS_SAB_FD_TABLE_MAX  64

typedef struct {
    void     *ptr;        /* SAB data pointer (buf[]) in this process */
    int       fd;         /* memfd fd; -1 = slot unused */
    size_t    size;       /* payload bytes */
    uint32_t  local_refs; /* ref count within this process */
} ngx_js_sab_fd_entry_t;

static ngx_js_sab_fd_entry_t  ngx_js_sab_fd_table[NGX_JS_SAB_FD_TABLE_MAX];
static pthread_mutex_t         ngx_js_sab_fd_lock = PTHREAD_MUTEX_INITIALIZER;
static int                     ngx_js_sab_fd_initialized;


static void
ngx_js_atfork_prepare(void)
{
    pthread_mutex_lock(&ngx_js_sab_fd_lock);
}


static void
ngx_js_atfork_parent(void)
{
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);
}


static void
ngx_js_atfork_child(void)
{
    /* Re-init: child inherited a locked mutex but no thread holds it */
    pthread_mutex_init(&ngx_js_sab_fd_lock, NULL);
}


static void
ngx_js_sab_fd_table_init(void)
{
    int  i;

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        ngx_js_sab_fd_table[i].fd = -1;
    }

    ngx_js_sab_fd_initialized = 1;
}


/* Find entry index by ptr; caller must hold the lock. */
static int
ngx_js_sab_fd_find(void *ptr)
{
    int  i;

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd >= 0
            && ngx_js_sab_fd_table[i].ptr == ptr)
        {
            return i;
        }
    }

    return -1;
}


void *
ngx_js_sab_alloc(void *opaque, size_t size)
{
    ngx_js_sab_hdr_t  *hdr;
    size_t             total;
#ifdef NGX_JS_HAVE_MEMFD
    int                fd, i;
#endif

    total = sizeof(ngx_js_sab_hdr_t) + size;

    if ((ngx_process == NGX_PROCESS_MASTER
         || ngx_process == NGX_PROCESS_SINGLE)
        && !ngx_js_sw_thread_active)
    {
        /* Pre-fork master main thread: MAP_SHARED|MAP_ANONYMOUS — same VA
         * in all workers.  SW threads (post-fork, master process) fall
         * through to memfd so the fd can be passed via SCM_RIGHTS. */
        hdr = mmap(NULL, total, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (hdr == MAP_FAILED) {
            return NULL;
        }

        hdr->ref_count = 1;
        hdr->flags     = NGX_JS_SAB_SHARED;
        hdr->size      = (uint32_t) size;
        hdr->_pad      = 0;

        return (void *) hdr->buf;
    }

#ifdef NGX_JS_HAVE_MEMFD
    /* Worker (post-fork): use memfd so the fd can be passed via SCM_RIGHTS */
    fd = ngx_memfd_create("ngx_js_sab", MFD_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }

    if (ftruncate(fd, (off_t) total) < 0) {
        close(fd);
        return NULL;
    }

    hdr = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (hdr == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    hdr->ref_count = 0;   /* not used for memfd SABs; tracked in fd table */
    hdr->flags     = NGX_JS_SAB_MEMFD;
    hdr->size      = (uint32_t) size;
    hdr->_pad      = 0;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd < 0) {
            ngx_js_sab_fd_table[i].ptr        = (void *) hdr->buf;
            ngx_js_sab_fd_table[i].fd         = fd;
            ngx_js_sab_fd_table[i].size        = size;
            ngx_js_sab_fd_table[i].local_refs  = 1;
            pthread_mutex_unlock(&ngx_js_sab_fd_lock);
            return (void *) hdr->buf;
        }
    }

    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Table full */
    munmap(hdr, total);
    close(fd);
    return NULL;

#else
    /* No memfd support: fall back to MAP_SHARED|MAP_ANONYMOUS (no cross-process) */
    hdr = mmap(NULL, total, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (hdr == MAP_FAILED) {
        return NULL;
    }

    hdr->ref_count = 1;
    hdr->flags     = 0;
    hdr->size      = (uint32_t) size;
    hdr->_pad      = 0;

    return (void *) hdr->buf;
#endif
}


void
ngx_js_sab_dup(void *opaque, void *ptr)
{
    ngx_js_sab_hdr_t  *hdr;
    int                idx;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    if (idx >= 0) {
        ngx_js_sab_fd_table[idx].local_refs++;
        pthread_mutex_unlock(&ngx_js_sab_fd_lock);
        return;
    }
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Pre-fork SAB: use shared atomic ref count */
    hdr = (ngx_js_sab_hdr_t *) ptr - 1;
    __atomic_fetch_add(&hdr->ref_count, 1, __ATOMIC_SEQ_CST);
}


void
ngx_js_sab_free(void *opaque, void *ptr)
{
    ngx_js_sab_hdr_t  *hdr;
    size_t             total;
    int                idx, fd;
    size_t             size;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    if (idx >= 0) {
        ngx_js_sab_fd_table[idx].local_refs--;
        if (ngx_js_sab_fd_table[idx].local_refs > 0) {
            pthread_mutex_unlock(&ngx_js_sab_fd_lock);
            return;
        }

        fd   = ngx_js_sab_fd_table[idx].fd;
        size = ngx_js_sab_fd_table[idx].size;
        ngx_js_sab_fd_table[idx].fd  = -1;
        ngx_js_sab_fd_table[idx].ptr = NULL;
        pthread_mutex_unlock(&ngx_js_sab_fd_lock);

        hdr   = (ngx_js_sab_hdr_t *) ptr - 1;
        total = sizeof(ngx_js_sab_hdr_t) + size;
        munmap(hdr, total);
        close(fd);
        return;
    }
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Pre-fork SAB: free when shared ref count reaches zero */
    hdr = (ngx_js_sab_hdr_t *) ptr - 1;
    if (__atomic_fetch_add(&hdr->ref_count, -1, __ATOMIC_SEQ_CST) != 1) {
        return;
    }

    total = sizeof(ngx_js_sab_hdr_t) + hdr->size;
    munmap(hdr, total);
}


int
ngx_js_sab_get_fd(void *ptr)
{
    int  idx, fd;

    if (!ngx_js_sab_fd_initialized) {
        return -1;
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    fd  = (idx >= 0) ? ngx_js_sab_fd_table[idx].fd : -1;
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    return fd;
}


void
ngx_js_sab_register_memfd(void *ptr, int fd, size_t size)
{
    int  i;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd < 0) {
            ngx_js_sab_fd_table[i].ptr        = ptr;
            ngx_js_sab_fd_table[i].fd         = fd;
            ngx_js_sab_fd_table[i].size        = size;
            ngx_js_sab_fd_table[i].local_refs  = 1;
            break;
        }
    }

    pthread_mutex_unlock(&ngx_js_sab_fd_lock);
}


const JSSharedArrayBufferFunctions  ngx_js_sab_funcs = {
    ngx_js_sab_alloc,
    ngx_js_sab_free,
    ngx_js_sab_dup,
    NULL,
};


static void *ngx_js_create_conf(ngx_cycle_t *cycle);
static char *ngx_js_init_conf(ngx_cycle_t *cycle, void *conf);

static int       ngx_js_interrupt_handler(JSRuntime *rt, void *opaque);
static ngx_int_t ngx_js_init_module(ngx_cycle_t *cycle);
static ngx_int_t ngx_js_init_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_master(ngx_cycle_t *cycle);
static void      ngx_js_bcast_recv_handler(ngx_event_t *ev);
static void      ngx_js_handle_worker_load_plugin(ngx_socket_t fd,
    ngx_int_t payload_len);
static void      ngx_js_handle_worker_channel_msg(ngx_socket_t fd,
    ngx_int_t payload_len);
static void      ngx_js_handle_master_channel_msgs(ngx_cycle_t *cycle);
static void      ngx_js_dispatch_master_event(ngx_cycle_t *cycle,
    const char *event, ngx_pid_t pid, ngx_int_t slot, int status);

static char   *ngx_js_source(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char   *ngx_js_preprocess(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_js_commands[] = {

    /*
     * js_source /path/to/script.js;
     *
     * Loads and evaluates a JavaScript file once the full nginx.conf
     * has been parsed (in init_conf).  Relative paths are resolved
     * against the directory of nginx.conf.  Multiple directives are
     * executed in declaration order.
     */
    { ngx_string("js_source"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_js_source,
      0,
      0,
      NULL },

    /*
     * js_preprocess /path/to/script.js;
     *
     * JS config preprocessor — the script is evaluated immediately
     * when this directive is encountered during ngx_conf_parse().
     * The script receives a global `config` object with a single
     * method: config.write(text) feeds `text` back into the nginx
     * config parser as if it had appeared inline.  This lets JS
     * generate any nginx directives (including full http{}/server{}
     * blocks) from files, environment variables, or external sources.
     *
     * The JS runtime used here is short-lived and independent of the
     * runtime created by js_source/init_conf.  nginx.http.servers[]
     * and other post-parse COM objects are NOT available.
     *
     * Multiple js_preprocess directives are allowed; each runs in its
     * own isolated runtime.
     */
    { ngx_string("js_preprocess"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_js_preprocess,
      0,
      0,
      NULL },

    ngx_null_command
};


static ngx_core_module_t  ngx_js_module_ctx = {
    ngx_string("js"),
    ngx_js_create_conf,
    ngx_js_init_conf
};


ngx_module_t  ngx_js_module = {
    NGX_MODULE_V1,
    &ngx_js_module_ctx,                /* module context */
    ngx_js_commands,                   /* module directives */
    NGX_CORE_MODULE,                   /* module type */
    NULL,                              /* init master */
    ngx_js_init_module,                /* init module */
    ngx_js_init_process,               /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    ngx_js_exit_process,               /* exit process */
    ngx_js_exit_master,                /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_js_create_conf(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = ngx_pcalloc(cycle->pool, sizeof(ngx_js_conf_t));
    if (jcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&jcf->sources, cycle->pool, 4,
                       sizeof(ngx_str_t)) != NGX_OK)
    {
        return NULL;
    }

    /* rt, ctx, worker, sw_list are 0/NULL after pcalloc */
    jcf->master_handlers = JS_UNINITIALIZED;

    return jcf;
}


static char *
ngx_js_source(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_str_t      *value, *path;

    value = cf->args->elts;    /* value[0] = "js_source", value[1] = path */

    path = ngx_array_push(&jcf->sources);
    if (path == NULL) {
        return NGX_CONF_ERROR;
    }

    *path = value[1];

    /* Resolve relative path against the directory of nginx.conf */
    if (ngx_conf_full_name(cf->cycle, path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*
 * ngx_js_init_conf — called after ngx_conf_parse() completes.
 *
 * At this point every server{}, location{}, upstream{} in nginx.conf
 * has been parsed and its C config structs are fully populated.  We
 * create the master QuickJS runtime, install the COM, and evaluate
 * every js_source file.  Any mutations JS makes to COM objects
 * (Phases 2+) write directly into cycle-pool memory and are visible
 * to worker processes after fork().
 */
static char *
ngx_js_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_uint_t      i;
    ngx_str_t      *path;
    u_char         *src;
    size_t          src_len;

    if (jcf->sources.nelts == 0) {
        return NGX_CONF_OK;    /* nothing to do — pure static config */
    }

    /* ---- Create the master QuickJS runtime ---- */

    jcf->rt = JS_NewRuntime();
    if (jcf->rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    js_std_init_handlers(jcf->rt);
    JS_SetModuleLoaderFunc2(jcf->rt, NULL,
                            js_module_loader, js_module_check_attributes,
                            NULL);
    JS_SetSharedArrayBufferFunctions(jcf->rt, &ngx_js_sab_funcs);

    /* Limit memory to 64 MB for the config-phase runtime */
    JS_SetMemoryLimit(jcf->rt, 64 * 1024 * 1024);

    jcf->ctx = JS_NewContext(jcf->rt);
    if (jcf->ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewContext() failed");
        goto failed_rt;
    }

    /* ---- Register std and os built-in modules ---- */

    if (js_init_module_std(jcf->ctx, "std") == NULL
        || js_init_module_os(jcf->ctx, "os") == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: failed to register std/os modules");
        goto failed_ctx;
    }

    /* ---- Install nginx.* COM namespace ---- */

    if (ngx_js_com_init(jcf->ctx, cycle) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: COM initialisation failed");
        goto failed_ctx;
    }

    /* ---- Initialise master lifecycle event handler registry ---- */

    jcf->master_handlers = JS_NewObject(jcf->ctx);
    if (JS_IsException(jcf->master_handlers)) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: failed to create master_handlers object");
        goto failed_ctx;
    }

    /* ---- Evaluate each js_source file in declaration order ---- */

    path = jcf->sources.elts;

    for (i = 0; i < jcf->sources.nelts; i++) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "js: executing \"%V\"", &path[i]);

        src = ngx_js_read_file(cycle, &path[i], &src_len);
        if (src == NULL) {
            goto failed_ctx;
        }

        if (ngx_js_eval_module(jcf->ctx, jcf->rt,
                               src, src_len, path[i].data,
                               cycle->log)
            != NGX_CONF_OK)
        {
            goto failed_ctx;
        }
    }

    /*
     * The master runtime stays alive until exit_master().  Worker
     * processes inherit it via fork() (copy-on-write) and use their
     * private copies; init_process() just wires w->rt / w->ctx to it.
     *
     * Start the SharedWorker manager thread so that workers can call
     * new SharedWorker(url) from request handlers.
     */
    if (ngx_js_sw_manager_start(jcf, cycle) != NGX_OK) {
        goto failed_ctx;
    }

    /*
     * Phase 2/3 messaging uses the existing nginx channel socketpairs
     * (ngx_processes[i].channel[0/1]) created by ngx_spawn_process().
     * No extra fds needed here — just set the function pointer hooks in
     * ngx_js_init_module() once the processes are spawned.
     */

    /*
     * P17: all JS scripts have now been evaluated.  Any server.on('accept')
     * or server.addL4Filter() calls they made have been recorded in the
     * per-server ngx_js_http_srv_conf_t.  Override ls->handler on every
     * standard HTTP listen socket whose default server has JS hooks so that
     * ngx_js_srv_accept_handler fires instead of ngx_http_init_connection.
     */
    ngx_js_srv_install_accept_hooks(cycle);

    return NGX_CONF_OK;

failed_ctx:
    JS_FreeContext(jcf->ctx);
    jcf->ctx = NULL;

failed_rt:
    js_std_free_handlers(jcf->rt);
    JS_FreeRuntime(jcf->rt);
    jcf->rt = NULL;

    return NGX_CONF_ERROR;
}


/*
 * ngx_js_interrupt_handler — polled by QuickJS every ~100 bytecodes.
 *
 * Returns 1 to abort JS execution when the per-request deadline has
 * passed.  The deadline (CLOCK_MONOTONIC milliseconds) is set in
 * ngx_js_content_handler() before JS_Call and cleared afterwards.
 * clock_gettime() is async-signal-safe and does not require the nginx
 * event loop to be running, so it fires even inside a tight JS loop.
 */
static int
ngx_js_interrupt_handler(JSRuntime *rt, void *opaque)
{
    ngx_js_worker_t  *w = opaque;
    struct timespec   ts;
    uint64_t          now_ms;

    if (w->request_deadline_ms == 0) {
        return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ms = (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;

    return now_ms >= w->request_deadline_ms ? 1 : 0;
}


/* ------------------------------------------------------------------ */
/* F4 — bcast fd event handler                                         */
/* ------------------------------------------------------------------ */

/*
 * Context stored in conn->data for the bcast fd nginx connection.
 * Allocated in cycle->pool so no explicit free is needed.
 */
typedef struct {
    ngx_js_worker_t  *w;
} ngx_js_bcast_ctx_t;


/*
 * Called by nginx event loop when bcast_fds[ngx_worker][1] becomes readable.
 * Drains all pending broadcast messages, creates per-worker socket state for
 * each received socket fd, and calls nginx.onSocket(sock) if registered.
 */
static void
ngx_js_bcast_recv_handler(ngx_event_t *ev)
{
    ngx_connection_t       *conn;
    ngx_js_bcast_ctx_t     *bctx;
    ngx_js_worker_t        *w;
    JSContext              *ctx, *job_ctx;
    uint8_t                 recv_body[NGX_JS_BCAST_MAX + 1];
    char                    cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct iovec            iov;
    struct msghdr           mh;
    ssize_t                 n;
    uint32_t               *hdr32;
    uint32_t                handle, addr_len;
    char                   *addr_ptr;
    int                     recv_fd;
    struct cmsghdr         *cmh;
    ngx_js_socket_state_t  *st;
    ngx_uint_t              i;
    const char             *colon;
    long                    port;
    JSValue                 global, nginx_obj, on_sock, sock_val, ret;

    conn = ev->data;
    bctx = conn->data;
    w    = bctx->w;
    ctx  = w->ctx;

    for ( ;; ) {
        iov.iov_base = recv_body;
        iov.iov_len  = sizeof(recv_body) - 1;  /* leave room for NUL */

        ngx_memzero(&mh, sizeof(mh));
        mh.msg_iov        = &iov;
        mh.msg_iovlen     = 1;
        mh.msg_control    = cmsg_buf;
        mh.msg_controllen = sizeof(cmsg_buf);

        n = recvmsg(conn->fd, &mh, MSG_DONTWAIT);
        if (n <= 0) {
            break;
        }

        if (n < 1 || (mh.msg_flags & MSG_TRUNC)) {
            continue;
        }

        /* Dispatch on the type byte (Phase 2 bcast format) */

        if (recv_body[0] == NGX_JS_BCAST_TYPE_SUSPEND) {
            (void) ngx_js_disable_accept_events((ngx_cycle_t *) ngx_cycle);
            (void) send(conn->fd, "\x01", 1, 0);  /* ack to manager */
            continue;
        }

        if (recv_body[0] == NGX_JS_BCAST_TYPE_RESUME) {
            (void) ngx_enable_accept_events((ngx_cycle_t *) ngx_cycle);
            (void) send(conn->fd, "\x01", 1, 0);  /* ack to manager */
            continue;
        }

        /* NGX_JS_BCAST_TYPE_SOCKET: socket delivery */

        if ((size_t) n < NGX_JS_BCAST_HDR) {
            continue;
        }

        hdr32    = (uint32_t *)(void *)(recv_body + 1);  /* skip type byte */
        handle   = hdr32[0];
        addr_len = hdr32[1];

        if (addr_len == 0 || addr_len > 63
            || (size_t) n < NGX_JS_BCAST_HDR + addr_len)
        {
            continue;
        }

        addr_ptr           = (char *) recv_body + NGX_JS_BCAST_HDR;
        addr_ptr[addr_len] = '\0';

        /* Extract socket fd from SCM_RIGHTS */
        recv_fd = -1;
        cmh = CMSG_FIRSTHDR(&mh);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&recv_fd, CMSG_DATA(cmh), sizeof(int));
        }

        if (recv_fd < 0) {
            continue;
        }

        if (handle >= NGX_JS_SOCKET_REG_MAX) {
            close(recv_fd);
            continue;
        }

        if (ngx_js_socket_reg[handle] != NULL) {
            /* Slot already occupied — log warning and discard */
            ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                          "js: bcast: handle %uD already occupied,"
                          " closing received fd", handle);
            close(recv_fd);
            continue;
        }

        /* Parse port from "host:port" */
        colon = strrchr(addr_ptr, ':');
        port  = (colon != NULL) ? strtol(colon + 1, NULL, 10) : 0;

        st = ngx_alloc(sizeof(ngx_js_socket_state_t), ngx_cycle->log);
        if (st == NULL) {
            close(recv_fd);
            continue;
        }

        st->fd          = recv_fd;
        st->port        = (uint16_t) port;
        st->in_listening = 0;
        ngx_cpystrn((u_char *) st->addr, (u_char *) addr_ptr,
                    sizeof(st->addr));

        ngx_js_socket_reg[handle] = st;

        /* Register in worker-local registry for cleanup */
        for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
            if (w->local_socket_reg[i] == NULL) {
                w->local_socket_reg[i] = st;
                break;
            }
        }

        /* Call nginx.onSocket(sock) if registered */
        global    = JS_GetGlobalObject(ctx);
        nginx_obj = JS_GetPropertyStr(ctx, global, "nginx");
        JS_FreeValue(ctx, global);

        if (!JS_IsException(nginx_obj) && !JS_IsUndefined(nginx_obj)) {
            on_sock = JS_GetPropertyStr(ctx, nginx_obj, "onSocket");
            JS_FreeValue(ctx, nginx_obj);

            if (JS_IsFunction(ctx, on_sock)) {
                sock_val = ngx_js_socket_wrap(ctx, handle);
                if (!JS_IsException(sock_val)) {
                    ret = JS_Call(ctx, on_sock, JS_UNDEFINED, 1, &sock_val);
                    JS_FreeValue(ctx, sock_val);
                    if (JS_IsException(ret)) {
                        ngx_js_log_exception(ctx, ngx_cycle->log);
                    }
                    JS_FreeValue(ctx, ret);
                }
            }

            JS_FreeValue(ctx, on_sock);

        } else {
            JS_FreeValue(ctx, nginx_obj);
        }
    }

    while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { }

    ngx_js_async_check(w);
    ngx_js_bf_async_check(w);
    ngx_js_sf_async_check(w);
}


/*
 * ngx_js_bcast_ensure_active — register the per-worker bcast event handler
 * in the nginx epoll/kqueue event loop.  Idempotent; no-op if already done
 * or if no bcast fd is available.
 *
 * Must only be called from inside the worker's event loop (e.g., from a
 * request content handler), after ngx_event_process_init() has run and
 * ngx_cycle->free_connections is non-NULL.
 */
void
ngx_js_bcast_ensure_active(ngx_js_worker_t *w)
{
    ngx_connection_t    *bcast_conn;
    ngx_js_bcast_ctx_t  *bcast_ctx;

    if (w == NULL || w->bcast_conn != NULL || w->bcast_fd < 0) {
        return;   /* already active, no fd, or no worker */
    }

    bcast_ctx = ngx_alloc(sizeof(ngx_js_bcast_ctx_t), ngx_cycle->log);
    if (bcast_ctx == NULL) {
        return;
    }

    bcast_ctx->w = w;

    bcast_conn = ngx_get_connection(w->bcast_fd, ngx_cycle->log);
    if (bcast_conn == NULL) {
        ngx_free(bcast_ctx);
        return;
    }

    bcast_conn->data          = bcast_ctx;
    bcast_conn->read->handler = ngx_js_bcast_recv_handler;
    bcast_conn->read->log     = ngx_cycle->log;

    if (ngx_add_event(bcast_conn->read, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_free_connection(bcast_conn);
        bcast_conn->fd = (ngx_socket_t) -1;
        ngx_free(bcast_ctx);
        return;
    }

    w->bcast_conn = bcast_conn;
}


/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Phase 2/3 — JS messaging over existing nginx channel socketpairs    */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_dispatch_msg — shared helper: deserialise payload and call all
 * handlers registered under `event` in jcf->master_handlers.
 * `extra_argc` / `extra_argv` are prepended before the data argument
 * (used for 'workerMessage' which passes slot as first arg).
 */
static void
ngx_js_dispatch_msg(JSContext *ctx, JSRuntime *rt, ngx_js_conf_t *jcf,
    const char *event, const uint8_t *buf, size_t payload_len,
    int extra_argc, JSValue *extra_argv)
{
    JSValue      msg, arr, len_val, fn, ret;
    uint32_t     i, len;
    JSValue      argv[4];
    int          argc;

    if (JS_IsUninitialized(jcf->master_handlers)) {
        return;
    }

    arr = JS_GetPropertyStr(ctx, jcf->master_handlers, event);
    if (!JS_IsArray(ctx, arr)) {
        JS_FreeValue(ctx, arr);
        return;
    }

    msg = JS_ReadObject(ctx, buf, payload_len, JS_READ_OBJ_REFERENCE);
    if (JS_IsException(msg)) {
        JSValue      exc;
        const char  *str;

        exc = JS_GetException(ctx);
        str = JS_ToCString(ctx, exc);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: channel msg: deserialize error: %s",
                      str ? str : "(null)");
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, arr);
        return;
    }

    /* Build call args: [extra_argv..., msg] */
    argc = 0;
    for (i = 0; i < (uint32_t) extra_argc; i++) {
        argv[argc++] = extra_argv[i];
    }
    argv[argc++] = msg;

    len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    for (i = 0; i < len; i++) {
        fn = JS_GetPropertyUint32(ctx, arr, i);
        if (JS_IsFunction(ctx, fn)) {
            ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
            if (JS_IsException(ret)) {
                JSValue      exc;
                const char  *str;

                exc = JS_GetException(ctx);
                str = JS_ToCString(ctx, exc);
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                              "js: nginx.on('%s') exception: %s",
                              event, str ? str : "(null)");
                JS_FreeCString(ctx, str);
                JS_FreeValue(ctx, exc);
            }
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, fn);
    }

    JS_FreeValue(ctx, msg);
    JS_FreeValue(ctx, arr);

    /* drain microtasks */
    {
        JSContext  *pctx;
        while (JS_ExecutePendingJob(rt, &pctx) > 0) { /* nothing */ }
    }
}


/*
 * Phase 2: called by ngx_channel_handler (via function pointer hook) when
 * the worker receives NGX_CMD_JS_MESSAGE on its channel fd.
 * fd        — the channel fd (ngx_channel, = channel[1], O_NONBLOCK)
 * payload_len — bytes of JS-serialized data that follow in the stream
 */
static void
ngx_js_handle_worker_channel_msg(ngx_socket_t fd, ngx_int_t payload_len)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;
    static uint8_t    buf[NGX_JS_MSG_MAX];
    ssize_t           n;
    ngx_int_t         total;

    if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: worker channel msg: invalid payload_len %i",
                      payload_len);
        return;
    }

    /* Read payload — loop to handle partial reads on O_NONBLOCK stream */
    total = 0;
    while (total < payload_len) {
        n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
        if (n > 0) {
            total += (ngx_int_t) n;
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "js: worker channel msg: recv() failed");
            return;
        }
        /* EAGAIN: yield briefly — payload should be in buffer already */
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return;
    }

    w = jcf->worker;
    if (w == NULL) {
        return;
    }

    ngx_js_dispatch_msg(w->ctx, w->rt, jcf, "message",
                        buf, (size_t) total, 0, NULL);
}


/*
 * P16: called in this worker when master sends NGX_CMD_JS_LOAD_PLUGIN.
 * Payload format: "absolute_dir\0config_json\0".
 * Reads the payload, then calls ngx_js_load_plugin to evaluate the plugin
 * in this worker's runtime.
 */
static void
ngx_js_handle_worker_load_plugin(ngx_socket_t fd, ngx_int_t payload_len)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;
    static uint8_t    buf[NGX_JS_MSG_MAX];
    ssize_t           n;
    ngx_int_t         total;
    const char       *dir, *config_json;
    size_t            dir_len;

    if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: load_plugin: invalid payload_len %i", payload_len);
        return;
    }

    total = 0;
    while (total < payload_len) {
        n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
        if (n > 0) {
            total += (ngx_int_t) n;
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "js: load_plugin: recv() failed");
            return;
        }
    }

    /* Ensure NUL-termination */
    buf[total] = '\0';

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL) {
        return;
    }

    w = jcf->worker;
    if (w == NULL || w->ctx == NULL) {
        return;
    }

    /* Parse two NUL-terminated strings from payload */
    dir        = (const char *) buf;
    dir_len    = strnlen(dir, (size_t) total);
    config_json = (dir_len + 1 < (size_t) total)
                  ? (const char *) buf + dir_len + 1
                  : NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "js: P16: worker loading plugin from broadcast: %s", dir);

    (void) ngx_js_load_plugin(w->ctx, w->rt, (ngx_cycle_t *) ngx_cycle,
                              dir, config_json);
}


/*
 * Phase 3: called by the master loop after SIGIO to drain worker→master
 * JS messages from all active channel[0] fds.
 * Performs non-blocking reads; ignores EAGAIN/unknown commands.
 */
static void
ngx_js_handle_master_channel_msgs(ngx_cycle_t *cycle)
{
    ngx_js_conf_t   *jcf;
    ngx_channel_t    ch;
    static uint8_t   buf[NGX_JS_MSG_MAX];
    ngx_int_t        i, payload_len, total;
    ssize_t          n;
    int              fd;
    JSValue          slot_val;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL
        || JS_IsUninitialized(jcf->master_handlers))
    {
        return;
    }

    for (i = 0; i < ngx_last_process; i++) {
        fd = ngx_processes[i].channel[0];
        if (fd < 0) {
            continue;
        }

        /* Non-blocking read of ngx_channel_t header */
        n = recv(fd, &ch, sizeof(ch), MSG_DONTWAIT);
        if (n <= 0) {
            continue;
        }

        if ((size_t) n < sizeof(ch)) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: short header read from slot %i", i);
            continue;
        }

        if (ch.command != NGX_CMD_JS_WORKER_MSG
            && ch.command != NGX_CMD_JS_USE_PLUGIN)
        {
            /* Not a JS message — log and skip (shouldn't happen) */
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: unexpected command %ui from slot %i",
                          ch.command, i);
            continue;
        }

        payload_len = (ngx_int_t) ch.fd;   /* repurposed field */
        if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: bad payload_len %i from slot %i",
                          payload_len, i);
            continue;
        }

        /* Read payload — use blocking recv since we're in sigsuspend loop */
        total = 0;
        while (total < payload_len) {
            n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
            if (n > 0) {
                total += (ngx_int_t) n;
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                              "js: master channel: recv() failed, slot %i", i);
                break;
            }
        }

        if (total < payload_len) {
            continue;
        }

        /*
         * P16: NGX_CMD_JS_USE_PLUGIN — worker requests that all OTHER
         * workers load the same plugin.  Broadcast via NGX_CMD_JS_LOAD_PLUGIN
         * to every worker except the one that sent the request (slot i).
         */
        if (ch.command == NGX_CMD_JS_USE_PLUGIN) {
            ngx_int_t  k;

            buf[total] = '\0';  /* safety NUL */
            ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "js: P16: master broadcasting plugin load from"
                           " worker %i: %s", i, buf);

            for (k = 0; k < ngx_last_process; k++) {
                if (k == i) {
                    continue;  /* skip the sender */
                }
                if (ngx_processes[k].channel[0] >= 0) {
                    ngx_js_channel_send(ngx_processes[k].channel[0],
                                       NGX_CMD_JS_LOAD_PLUGIN,
                                       (ngx_uint_t) k,
                                       buf, (size_t) total,
                                       cycle->log);
                }
            }
            continue;
        }

        /* Dispatch to nginx.on('workerMessage', fn(slot, data)) */
        slot_val = JS_NewInt32(jcf->ctx, (int32_t) i);
        ngx_js_dispatch_msg(jcf->ctx, jcf->rt, jcf, "workerMessage",
                            buf, (size_t) total, 1, &slot_val);
        JS_FreeValue(jcf->ctx, slot_val);
    }
}


/*
 * ngx_js_init_process — called in each worker after fork().
 *
 * After fork() every worker has a private copy-on-write image of the
 * master's address space, including its JSRuntime and JSContext.  Those
 * objects already contain the fully-evaluated JS environment (all
 * js_source scripts executed, COM installed, handler functions
 * registered in the global scope, all QuickJS classes registered).
 *
 * We simply point the worker's runtime handle at jcf->rt / jcf->ctx.
 * No new runtime, no re-evaluation, no file I/O.
 *
 * exit_process() frees the worker's private copy; exit_master() frees
 * the master's original — no double-free, no shared mutable state.
 */
static ngx_int_t
ngx_js_init_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    if (jcf->rt == NULL) {
        return NGX_OK;    /* no js_source directives */
    }

    w = ngx_pcalloc(cycle->pool, sizeof(ngx_js_worker_t));
    if (w == NULL) {
        return NGX_ERROR;
    }

    w->rt  = jcf->rt;
    w->ctx = jcf->ctx;

    /*
     * Measure the runtime's heap footprint right after fork.  This baseline
     * is stored in w->baseline_malloc_size so the content handler can compute
     * the absolute memory limit as baseline + nginx.workerMemoryLimit on each
     * request.  Both nginx.workerMemoryLimit and nginx.workerRequestTimeout
     * are read live from the JS property before every JS_Call so that request
     * handlers can reconfigure them at runtime.
     */
    {
        JSMemoryUsage  mu;

        JS_ComputeMemoryUsage(w->rt, &mu);
        w->baseline_malloc_size = (size_t) mu.malloc_size;
    }

    /*
     * Always install the interrupt handler so that request handlers can
     * set nginx.workerRequestTimeout at runtime and have it take effect
     * immediately.  The handler is a no-op when request_deadline_ms == 0.
     */
    JS_SetInterruptHandler(w->rt, ngx_js_interrupt_handler, w);

    /*
     * Overwrite the context opaque (set to cycle in ngx_js_com_init) with
     * the worker pointer so that JS C functions (e.g. nginx.setTimeout) can
     * retrieve the worker via JS_GetContextOpaque(ctx).
     */
    JS_SetContextOpaque(w->ctx, w);

    /* Update nginx.workerIdx to the actual 0-based worker index. */
    {
        JSValue  global, nginx_obj;

        global    = JS_GetGlobalObject(w->ctx);
        nginx_obj = JS_GetPropertyStr(w->ctx, global, "nginx");
        JS_FreeValue(w->ctx, global);

        if (!JS_IsException(nginx_obj) && !JS_IsUndefined(nginx_obj)) {
            JS_SetPropertyStr(w->ctx, nginx_obj, "workerIdx",
                              JS_NewInt32(w->ctx, (int32_t) ngx_worker));
            JS_FreeValue(w->ctx, nginx_obj);
        }
    }

    jcf->worker = w;

    /*
     * nginx.broadcast() callbacks are run by ngx_js_http_init_process()
     * (ngx_js_http_module, a later module), NOT here.  ngx_js_module is a
     * NGX_CORE_MODULE at a low index, so ngx_event_process_init() (which
     * calls ngx_event_timer_init and initialises free_connections) has not
     * yet run at this point.  Any nginx.setTimeout() or SharedWorker
     * postMessage() called from a broadcast callback therefore needs a live
     * event loop — see ngx_js_http_module.c:ngx_js_http_init_process.
     *
     * F4: store the per-worker bcast fd but do NOT register the event
     * here.  ngx_event_process_init (which initialises free_connections)
     * runs AFTER ngx_js_init_process, so ngx_get_connection would fail.
     * Lazy activation is done on the first request via
     * ngx_js_bcast_ensure_active().
     */
    w->bcast_fd   = ngx_js_sw_get_bcast_fd((ngx_uint_t) ngx_worker);
    w->bcast_conn = NULL;

    /*
     * Phase 2/3 messaging uses the existing nginx channel (ngx_channel).
     * NGX_CMD_JS_MESSAGE is handled by ngx_channel_handler via the
     * ngx_js_worker_channel_msg hook — no extra setup needed here.
     */

    return NGX_OK;
}


static void
ngx_js_exit_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t         *jcf;
    ngx_js_worker_t       *w;
    ngx_uint_t             i;
    ngx_js_socket_state_t *st;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    w = jcf->worker;
    if (w == NULL) {
        return;
    }

    /*
     * F3: close any worker-local sockets that were never activated.
     * Activated sockets (in_listening == 1) are nginx's responsibility
     * — their fd lives in cycle->listening and nginx will close them.
     * Unactivated sockets are this worker's private resource; leak them
     * and the fd is lost until the process exits.
     */
    for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
        st = w->local_socket_reg[i];
        if (st == NULL) {
            continue;
        }

        if (!st->in_listening && st->fd >= 0) {
            (void) close(st->fd);
            st->fd = -1;
            ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "js: exit_process: closed worker-local socket %i",
                           (ngx_int_t) i);
        }

        ngx_free(st);
        w->local_socket_reg[i] = NULL;
    }

    /* F4: deregister bcast event handler */
    if (w->bcast_conn != NULL) {
        ngx_js_bcast_ctx_t  *bcast_ctx = w->bcast_conn->data;

        ngx_del_event(w->bcast_conn->read, NGX_READ_EVENT, 0);
        ngx_free_connection(w->bcast_conn);
        w->bcast_conn->fd = (ngx_socket_t) -1;
        w->bcast_conn     = NULL;

        if (bcast_ctx != NULL) {
            ngx_free(bcast_ctx);
        }
    }

    /*
     * Drain async-pending HTTP requests with 503 BEFORE closing SW sockets.
     *
     * On graceful shutdown (SIGQUIT) a suspended request waits for a SW reply.
     * If the SW thread dies before replying, the request hangs forever and the
     * worker never exits, deadlocking the whole shutdown sequence.
     *
     * Sending 503 here unblocks the request.  The drain must run while SW
     * channels are still open (before ngx_js_sw_exit_process) so that any
     * read event registered on a channel fd does not fire on a closed fd.
     *
     * ngx_js_async_drain_503 also frees the DupValue'd req_obj and promise,
     * satisfying the QuickJS "list_empty(&rt->gc_obj_list)" invariant.
     */
    if (w->ctx != NULL && w->async_pending != NULL) {
        ngx_js_async_drain_503(w);
        /* w->async_pending is now NULL */
    }

    ngx_js_sw_exit_process(cycle, jcf);

    /*
     * Drain body-filter, streaming-filter, and L4 pending entries.
     *
     * Like the async_pending drain above, this frees all DupValue'd
     * JSValues (satisfying the QuickJS "list_empty(&rt->gc_obj_list)"
     * invariant) and finalises the associated nginx connections so that
     * the "open socket left in connection" ALERT from
     * ngx_worker_process_exit() is not triggered on graceful shutdown.
     */
    if (w->ctx != NULL) {
        ngx_js_bf_pending_t  *bf_p, *bfnext;

        for (bf_p = w->bf_pending; bf_p != NULL; bf_p = bfnext) {
            bfnext = bf_p->next;

            ngx_log_error(NGX_LOG_WARN, bf_p->r->connection->log, 0,
                          "js: drain body-filter request with error on worker exit");

            if (!JS_IsUndefined(bf_p->gen)) {
                JS_FreeValue(w->ctx, bf_p->gen);
            }
            JS_FreeValue(w->ctx, bf_p->promise);

            /*
             * Response headers were already sent (the 200 OK went out before
             * the body-filter suspended).  We cannot send a new status line, so
             * just close the connection via NGX_DONE (count-- → 0 → close).
             * Setting c->error prevents the keepalive path from recycling it.
             */
            bf_p->r->connection->error = 1;
            ngx_http_finalize_request(bf_p->r, NGX_DONE);
        }
        w->bf_pending = NULL;

        {
            ngx_js_sf_pending_t  *sf_p, *sfnext;

            for (sf_p = w->sf_pending; sf_p != NULL; sf_p = sfnext) {
                sfnext = sf_p->next;

                ngx_log_error(NGX_LOG_WARN, sf_p->r->connection->log, 0,
                              "js: drain streaming-filter request with error"
                              " on worker exit");

                JS_FreeValue(w->ctx, sf_p->promise);

                sf_p->r->connection->error = 1;
                ngx_http_finalize_request(sf_p->r, NGX_DONE);
            }
            w->sf_pending = NULL;
        }

        ngx_js_l4_drain_exit(w);
        ngx_js_repl_drain_exit(w);
    }

    if (w->ctx) {
        /*
         * master_handlers is a GC-tracked JSValue held in the shared jcf.
         * Workers do not own it (only the master registers/fires it), but
         * they inherited a COW copy of the JS heap.  Each process has its
         * own private heap copy after the first write, so freeing it here
         * is safe — every process decrements its own ref_count independently.
         * This must happen before JS_FreeContext to avoid the QuickJS
         * "list_empty(&rt->gc_obj_list)" assertion on JS_FreeRuntime.
         */
        if (!JS_IsUninitialized(jcf->master_handlers)) {
            JS_FreeValue(w->ctx, jcf->master_handlers);
        }

        JS_FreeContext(w->ctx);
        w->ctx = NULL;
    }

    if (w->rt) {
        js_std_free_handlers(w->rt);
        JS_FreeRuntime(w->rt);
        w->rt = NULL;
    }
}


/*
 * ngx_js_init_module — called by ngx_init_cycle() after config parse.
 * Sets the global hook pointer so ngx_process_cycle.c can fire JS events
 * without including any JS headers.
 */

/* Defined in ngx_process_cycle.c — no JS headers needed there. */
extern void  (*ngx_js_master_event)(ngx_cycle_t *cycle, const char *event,
    ngx_pid_t pid, ngx_int_t slot, int status);
extern void  (*ngx_js_worker_channel_msg)(ngx_socket_t fd,
    ngx_int_t payload_len);
extern void  (*ngx_js_master_channel_msg)(ngx_cycle_t *cycle);
extern void  (*ngx_js_worker_load_plugin)(ngx_socket_t fd,    /* P16 */
    ngx_int_t payload_len);
extern void  (*ngx_js_sw_threads_start)(ngx_cycle_t *cycle);

static ngx_int_t
ngx_js_init_module(ngx_cycle_t *cycle)
{
    ngx_js_conf_t      *old_jcf, *new_jcf;
    static ngx_uint_t   atfork_registered;

    if (!atfork_registered) {
        pthread_atfork(ngx_js_atfork_prepare,
                       ngx_js_atfork_parent,
                       ngx_js_atfork_child);
        atfork_registered = 1;
    }

    /*
     * On reload: ngx_cycle still points to the OLD cycle here (nginx updates
     * ngx_cycle after init_module returns).  If the old cycle had JS scripts,
     * stop its SW threads and free its runtime now — before new workers fork.
     */
    if (ngx_cycle != NULL
        && ngx_cycle->conf_ctx != NULL
        && ngx_cycle != cycle)
    {
        old_jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                                  ngx_js_module);
        if (old_jcf != NULL) {

            /* Stop old SW threads; manager is a singleton, leave it running */
            ngx_js_sw_retire_threads(old_jcf);

            /* Redirect manager's dynamic-create list to the new config */
            new_jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                      ngx_js_module);
            if (new_jcf != NULL) {
                ngx_js_sw_update_mgr_jcf(new_jcf);
            }

            /* Free old master JS runtime (SW threads are joined — safe) */
            if (old_jcf->ctx != NULL) {
                if (!JS_IsUninitialized(old_jcf->master_handlers)) {
                    JS_FreeValue(old_jcf->ctx, old_jcf->master_handlers);
                    old_jcf->master_handlers = JS_UNINITIALIZED;
                }
                JS_FreeContext(old_jcf->ctx);
                old_jcf->ctx = NULL;
            }

            if (old_jcf->rt != NULL) {
                js_std_free_handlers(old_jcf->rt);
                JS_FreeRuntime(old_jcf->rt);
                old_jcf->rt = NULL;
            }
        }
    }

    /* Wire up master supervisory-loop hooks. */
    ngx_js_master_event       = ngx_js_dispatch_master_event;
    ngx_js_worker_channel_msg = ngx_js_handle_worker_channel_msg;
    ngx_js_master_channel_msg = ngx_js_handle_master_channel_msgs;
    /* P16: broadcast plugin-load from worker to this worker */
    ngx_js_worker_load_plugin = ngx_js_handle_worker_load_plugin;
    /* Start SW pthreads after daemonisation (not during init_conf) */
    ngx_js_sw_threads_start   = ngx_js_sw_threads_start_deferred;

    return NGX_OK;
}


/*
 * ngx_js_dispatch_master_event — invoked from the master supervisory loop
 * at key lifecycle points.  Looks up all JS handlers registered via
 * nginx.on(event, fn) and calls them synchronously.
 *
 * For 'workerSpawned': argv = [pid, slot]
 * For 'workerExited':  argv = [pid, slot, status]
 * For all others:      argv = []
 */
static void
ngx_js_dispatch_master_event(ngx_cycle_t *cycle, const char *event,
    ngx_pid_t pid, ngx_int_t slot, int status)
{
    ngx_js_conf_t  *jcf;
    JSContext      *ctx;
    JSValue         arr, len_val, fn, ret;
    JSValue         argv[3];
    uint32_t        i, len;
    int             argc;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return;
    }

    if (JS_IsUninitialized(jcf->master_handlers)) {
        return;
    }

    ctx = jcf->ctx;

    arr = JS_GetPropertyStr(ctx, jcf->master_handlers, event);
    if (JS_IsUndefined(arr) || !JS_IsArray(ctx, arr)) {
        JS_FreeValue(ctx, arr);
        return;
    }

    len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    if (strcmp(event, "workerSpawned") == 0) {
        argv[0] = JS_NewInt64(ctx, (int64_t) pid);
        argv[1] = JS_NewInt32(ctx, (int32_t) slot);
        argc = 2;

    } else if (strcmp(event, "workerExited") == 0) {
        argv[0] = JS_NewInt64(ctx, (int64_t) pid);
        argv[1] = JS_NewInt32(ctx, (int32_t) slot);
        argv[2] = JS_NewInt32(ctx, status);
        argc = 3;

    } else {
        argc = 0;
    }

    for (i = 0; i < len; i++) {
        fn = JS_GetPropertyUint32(ctx, arr, i);

        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            continue;
        }

        ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argc ? argv : NULL);

        if (JS_IsException(ret)) {
            JSValue  exc;
            const char  *str;

            exc = JS_GetException(ctx);
            str = JS_ToCString(ctx, exc);
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "nginx.on('%s') handler exception: %s",
                          event, str ? str : "(null)");
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }

        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, fn);
    }

    /* free argv values */
    for (i = 0; i < (uint32_t) argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }

    JS_FreeValue(ctx, arr);

    /* drain any microtasks the handlers may have enqueued */
    {
        JSContext  *pctx;
        while (JS_ExecutePendingJob(jcf->rt, &pctx) > 0) { /* nothing */ }
    }
}


static void
ngx_js_exit_master(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    ngx_js_sw_exit_master(jcf);

    if (jcf->ctx) {
        if (!JS_IsUninitialized(jcf->master_handlers)) {
            JS_FreeValue(jcf->ctx, jcf->master_handlers);
            jcf->master_handlers = JS_UNINITIALIZED;
        }
        JS_FreeContext(jcf->ctx);
        jcf->ctx = NULL;
    }

    if (jcf->rt) {
        js_std_free_handlers(jcf->rt);
        JS_FreeRuntime(jcf->rt);
        jcf->rt = NULL;
    }
}


/*
 * config.write(text) — called from a js_preprocess script.
 *
 * Writes `text` to a temporary file then calls ngx_conf_parse()
 * recursively so that the generated text is treated as if it
 * appeared inline in nginx.conf.  Using a real file (rather than an
 * in-memory buffer) is required so that nested block directives
 * (http{}, server{}, etc.) are parsed correctly: ngx_conf_parse()
 * uses a non-invalid fd to distinguish parse_block from parse_param,
 * and only the file path gives that guarantee.
 *
 * The temporary file is unlinked before this function returns,
 * regardless of parse success or failure.
 */
JSValue
ngx_js_config_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_conf_t   *cf;
    const char   *text;
    size_t        len;
    char          tmppath[] = "/tmp/ngx_js_pp_XXXXXX";
    ngx_str_t     tmpstr;
    int           fd;
    ssize_t       n;
    char         *rv;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "config.write(text): argument required");
    }

    cf = JS_GetContextOpaque(ctx);
    if (cf == NULL) {
        return JS_ThrowInternalError(ctx, "config.write: no conf context");
    }

    text = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!text) {
        return JS_EXCEPTION;
    }

    fd = mkstemp(tmppath);
    if (fd == -1) {
        JS_FreeCString(ctx, text);
        return JS_ThrowInternalError(ctx, "config.write: mkstemp failed");
    }

    n = write(fd, text, len);
    close(fd);

    JS_FreeCString(ctx, text);

    if ((size_t) n != len) {
        unlink(tmppath);
        return JS_ThrowInternalError(ctx, "config.write: write failed");
    }

    tmpstr.data = (u_char *) tmppath;
    tmpstr.len  = ngx_strlen(tmppath);

    rv = ngx_conf_parse(cf, &tmpstr);

    unlink(tmppath);

    if (rv != NGX_CONF_OK) {
        return JS_ThrowInternalError(ctx,
                                     "config.write: config parse failed");
    }

    return JS_UNDEFINED;
}


/*
 * ngx_js_preprocess — directive handler for js_preprocess.
 *
 * Creates a short-lived JSRuntime, installs a global `config` object
 * exposing config.write(), evaluates the named script, then frees the
 * runtime.  The cf pointer is stored as the context opaque so that
 * config.write() can call ngx_conf_parse() directly.
 */
static char *
ngx_js_preprocess(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t   *value, path;
    u_char      *src;
    size_t       src_len;
    JSRuntime   *rt;
    JSContext   *ctx;
    JSValue      global, config_obj;
    char        *rv;

    value = cf->args->elts;
    path  = value[1];

    if (ngx_conf_full_name(cf->cycle, &path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    src = ngx_js_read_file(cf->cycle, &path, &src_len);
    if (src == NULL) {
        return NGX_CONF_ERROR;
    }

    rt = JS_NewRuntime();
    if (rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    js_std_init_handlers(rt);
    JS_SetModuleLoaderFunc2(rt, NULL,
                            js_module_loader, js_module_check_attributes,
                            NULL);
    JS_SetSharedArrayBufferFunctions(rt, &ngx_js_sab_funcs);

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewContext() failed");
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    if (js_init_module_std(ctx, "std") == NULL
        || js_init_module_os(ctx, "os") == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: failed to register std/os modules");
        JS_FreeContext(ctx);
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    /*
     * Store cf so that config.write() can retrieve it via
     * JS_GetContextOpaque() and call ngx_conf_parse().
     */
    JS_SetContextOpaque(ctx, cf);

    /* Install global `config` object with write() method */
    global     = JS_GetGlobalObject(ctx);
    config_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, config_obj, "write",
                      JS_NewCFunction(ctx, ngx_js_config_write, "write", 1));
    JS_SetPropertyStr(ctx, global, "config", config_obj);
    JS_FreeValue(ctx, global);

    rv = ngx_js_eval_module(ctx, rt, src, src_len, path.data, cf->log);

    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);

    return rv;
}


/*
 * Read an entire file into a NUL-terminated cycle->pool buffer.
 * Returns the buffer on success, NULL on error.
 */
u_char *
ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path, size_t *out_len)
{
    ngx_fd_t         fd;
    ngx_file_t       file;
    ngx_file_info_t  fi;
    u_char          *buf;
    size_t           size;
    ssize_t          n;

    fd = ngx_open_file(path->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_open_file_n " \"%V\" failed", path);
        return NULL;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.fd   = fd;
    file.name = *path;
    file.log  = cycle->log;

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_fd_info_n " \"%V\" failed", path);
        ngx_close_file(fd);
        return NULL;
    }

    size = ngx_file_size(&fi);

    buf = ngx_palloc(cycle->pool, size + 1);
    if (buf == NULL) {
        ngx_close_file(fd);
        return NULL;
    }

    n = ngx_read_file(&file, buf, size, 0);

    ngx_close_file(fd);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: read \"%V\" failed", path);
        return NULL;
    }

    buf[n]   = '\0';
    *out_len = (size_t) n;

    return buf;
}
