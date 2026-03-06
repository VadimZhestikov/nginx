
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_sw.c — SharedWorker class backed by a master-process pthread.
 *
 * new SharedWorker("script.js") in master (init_conf):
 *   - allocates N pipe-pairs (inbox + outbox per nginx worker)
 *   - spawns a pthread running the script with a fresh JSRuntime
 *
 * new SharedWorker("script.js") in a worker process:
 *   - looks up the existing state by URL
 *   - on first use per worker: activates the outbox.rfd in nginx epoll
 *     and sends a CONNECT sentinel to the SW thread via inbox
 *
 * SW thread receives CONNECT for channel[i] → fires onconnect({ports:[p]})
 * where p.postMessage(data) sends back to the nginx worker.
 *
 * Worker-side sw.postMessage(data) → serialise → write to inbox.wfd
 * SW thread receives data from inbox.rfd → calls port[i].onmessage({data:…})
 *
 * Worker-side sw.onmessage = fn → stored in worker_slots[i].on_message
 * nginx event on outbox.rfd → deserialise → call on_message({data:…})
 *
 * PIPE PROTOCOL (cross-process, direct write — NO in-memory queue)
 * ---------------------------------------------------------------
 * Each pipe carries binary-framed messages:
 *   [uint32_t type][uint32_t len][len bytes data]
 *
 * type values:
 *   NGX_JS_SW_MSG_DATA    (0) — serialised JS value
 *   NGX_JS_SW_MSG_CONNECT (1) — worker connects for the first time
 *   NGX_JS_SW_MSG_TERM    (2) — shutdown signal
 *
 * Writes use writev() so header+data land atomically (< PIPE_BUF = 4096).
 * Reads loop until EAGAIN.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <cutils.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_sw.h"


/* ------------------------------------------------------------------ */
/* Cross-process pipe (direct write, no in-memory queue)               */
/* ------------------------------------------------------------------ */

#define NGX_JS_SW_MSG_DATA     0u
#define NGX_JS_SW_MSG_CONNECT  1u
#define NGX_JS_SW_MSG_TERM     2u

typedef struct {
    int  rfd;
    int  wfd;
} ngx_js_sw_xpipe_t;


static ngx_int_t
xpipe_init(ngx_js_sw_xpipe_t *p)
{
    int  fds[2];

    if (pipe(fds) != 0) {
        return NGX_ERROR;
    }

    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0
        || fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0)
    {
        close(fds[0]);
        close(fds[1]);
        return NGX_ERROR;
    }

    p->rfd = fds[0];
    p->wfd = fds[1];

    return NGX_OK;
}


static void
xpipe_destroy(ngx_js_sw_xpipe_t *p)
{
    close(p->rfd);
    close(p->wfd);
}


/*
 * xpipe_send — write one framed message directly into the pipe.
 *
 * Message layout (total must be < PIPE_BUF = 4096 for atomic writev):
 *   [uint32_t type][uint32_t data_len][uint32_t n_sabs]   (12 bytes)
 *   [data_len bytes: serialised JS]
 *   [n_sabs × uint64_t: SAB data pointers]
 *
 * Ownership of buf and sab_tab is transferred; both are ngx_free()'d here.
 * For each SAB pointer, sab_dup() is called so the mapping stays alive
 * while the message travels through the pipe.
 */
static void
xpipe_send(ngx_js_sw_xpipe_t *p, uint32_t type,
           uint8_t *buf, uint32_t len,
           uint8_t **sab_tab, uint32_t n_sabs)
{
    uint32_t      hdr[3];
    uint64_t     *sab_ptrs;
    struct iovec  iov[3];
    int           niov;
    ssize_t       n;
    uint32_t      i;

    /* Increment SAB ref counts for in-transit ownership */
    for (i = 0; i < n_sabs; i++) {
        ngx_js_sab_dup(NULL, sab_tab[i]);
    }

    hdr[0] = type;
    hdr[1] = len;
    hdr[2] = n_sabs;

    iov[0].iov_base = hdr;
    iov[0].iov_len  = 12;
    niov = 1;

    if (buf != NULL && len > 0) {
        iov[niov].iov_base = buf;
        iov[niov].iov_len  = len;
        niov++;
    }

    if (n_sabs > 0) {
        sab_ptrs = ngx_alloc(n_sabs * sizeof(uint64_t), ngx_cycle->log);
        if (sab_ptrs != NULL) {
            for (i = 0; i < n_sabs; i++) {
                sab_ptrs[i] = (uint64_t)(uintptr_t) sab_tab[i];
            }
            iov[niov].iov_base = sab_ptrs;
            iov[niov].iov_len  = n_sabs * sizeof(uint64_t);
            niov++;
        }
    } else {
        sab_ptrs = NULL;
    }

    n = writev(p->wfd, iov, niov);
    (void) n;   /* O_NONBLOCK: failure logged elsewhere */

    if (sab_ptrs) {
        ngx_free(sab_ptrs);
    }
    ngx_free(sab_tab);

    if (buf) {
        ngx_free(buf);
    }
}


/*
 * xpipe_read_one — try to read one framed message from the pipe.
 *
 * Returns 0 on success, -1 on EAGAIN or error.
 * On success:
 *   *buf_out     — heap-allocated serialised JS data (caller ngx_free's it),
 *                  or NULL for zero-length/sentinel messages.
 *   *sab_tab_out — heap-allocated array of SAB data pointers (caller
 *                  ngx_free's it after calling sab_free for each entry).
 *   *n_sabs_out  — number of entries in *sab_tab_out.
 */
static int
xpipe_read_one(ngx_js_sw_xpipe_t *p, uint32_t *type_out,
               uint8_t **buf_out, uint32_t *len_out,
               uint8_t ***sab_tab_out, uint32_t *n_sabs_out)
{
    uint32_t   hdr[3];
    uint8_t   *buf;
    uint8_t  **sab_tab;
    uint64_t  *sab_ptrs;
    ssize_t    n;
    size_t     total;
    uint32_t   i;

    /* Read 12-byte header */
    n = read(p->rfd, hdr, 12);
    if (n <= 0) {
        return -1;    /* EAGAIN or EOF */
    }

    /* Handle partial header (rare but possible) */
    if ((size_t) n < 12) {
        uint8_t  *h = (uint8_t *) hdr;
        size_t    got = (size_t) n;

        while (got < 12) {
            n = read(p->rfd, h + got, 12 - got);
            if (n <= 0) {
                return -1;
            }
            got += (size_t) n;
        }
    }

    *type_out    = hdr[0];
    *len_out     = hdr[1];
    *buf_out     = NULL;
    *sab_tab_out = NULL;
    *n_sabs_out  = 0;

    /* Read serialised JS data */
    if (hdr[1] > 0) {
        buf = ngx_alloc((size_t) hdr[1], ngx_cycle->log);
        if (buf == NULL) {
            return -1;
        }

        total = 0;
        while (total < (size_t) hdr[1]) {
            n = read(p->rfd, buf + total, (size_t) hdr[1] - total);
            if (n <= 0) {
                ngx_free(buf);
                return -1;
            }
            total += (size_t) n;
        }

        *buf_out = buf;
    }

    /* Read SAB pointer table */
    if (hdr[2] > 0) {
        sab_ptrs = ngx_alloc((size_t) hdr[2] * sizeof(uint64_t),
                             ngx_cycle->log);
        if (sab_ptrs == NULL) {
            if (*buf_out) {
                ngx_free(*buf_out);
                *buf_out = NULL;
            }
            return -1;
        }

        total = 0;
        while (total < (size_t) hdr[2] * sizeof(uint64_t)) {
            n = read(p->rfd, (uint8_t *) sab_ptrs + total,
                     (size_t) hdr[2] * sizeof(uint64_t) - total);
            if (n <= 0) {
                ngx_free(sab_ptrs);
                if (*buf_out) {
                    ngx_free(*buf_out);
                    *buf_out = NULL;
                }
                return -1;
            }
            total += (size_t) n;
        }

        sab_tab = ngx_alloc((size_t) hdr[2] * sizeof(uint8_t *),
                            ngx_cycle->log);
        if (sab_tab == NULL) {
            ngx_free(sab_ptrs);
            if (*buf_out) {
                ngx_free(*buf_out);
                *buf_out = NULL;
            }
            return -1;
        }

        for (i = 0; i < hdr[2]; i++) {
            sab_tab[i] = (uint8_t *)(uintptr_t) sab_ptrs[i];
        }

        ngx_free(sab_ptrs);

        *sab_tab_out = sab_tab;
        *n_sabs_out  = hdr[2];
    }

    return 0;
}


/* ------------------------------------------------------------------ */
/* SharedWorker channel (one per nginx worker slot)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_js_sw_xpipe_t  inbox;    /* worker → SW  */
    ngx_js_sw_xpipe_t  outbox;   /* SW   → worker */
} ngx_js_sw_channel_t;


/* ------------------------------------------------------------------ */
/* SharedWorker state structs                                          */
/* ------------------------------------------------------------------ */

/* Per-worker slot; lives in master heap, COW-per-worker after fork */
typedef struct {
    ngx_connection_t  *conn;        /* nginx epoll for outbox.rfd; NULL = unactivated */
    JSValue            on_message;  /* worker's current onmessage handler */
    ngx_js_worker_t   *w;
} ngx_js_sw_worker_slot_t;


typedef struct ngx_js_sw_state_s  ngx_js_sw_state_t;
struct ngx_js_sw_state_s {
    pthread_t                 tid;
    char                     *url;
    char                     *script;
    ngx_uint_t                nchannels;
    ngx_js_sw_channel_t      *channels;
    ngx_js_sw_worker_slot_t  *worker_slots;
    ngx_js_sw_state_t        *next;
};


/* JS SharedWorker object opaque (worker side) */
typedef struct {
    ngx_js_sw_state_t  *state;
} ngx_js_sw_opaque_t;


/* nginx connection context for outbox.rfd */
typedef struct {
    ngx_js_sw_state_t  *state;
    ngx_uint_t          wi;
} ngx_js_sw_recv_ctx_t;


/* SW thread context (set as JS context opaque in SW thread) */
typedef struct {
    ngx_js_sw_state_t  *state;
    JSValue             on_connect;
    JSValue            *ports;      /* ports[nchannels], each JS MessagePort */
} ngx_js_sw_thread_ctx_t;


/* SW-side MessagePort opaque */
typedef struct {
    ngx_js_sw_state_t  *state;
    ngx_uint_t          wi;
    JSValue             on_message;
} ngx_js_sw_port_opaque_t;


/* ------------------------------------------------------------------ */
/* JS class IDs                                                        */
/* ------------------------------------------------------------------ */

static JSClassID  ngx_js_sw_class_id;
static JSClassID  ngx_js_sw_port_class_id;


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void *ngx_js_sw_thread(void *arg);
static ngx_int_t ngx_js_sw_activate(JSContext *ctx,
    ngx_js_sw_state_t *state, ngx_uint_t wi);


/* ------------------------------------------------------------------ */
/* SharedWorker JS class (worker side) — no JSValues                  */
/* ------------------------------------------------------------------ */

static void
ngx_js_sw_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_sw_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_sw_class_id);
    if (op) {
        ngx_free(op);
    }
}


static JSClassDef  ngx_js_sw_class = {
    "SharedWorker",
    .finalizer = ngx_js_sw_finalizer
};


/* ------------------------------------------------------------------ */
/* SW-side MessagePort JS class                                        */
/* ------------------------------------------------------------------ */

static void
ngx_js_sw_port_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_sw_port_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_sw_port_class_id);
    if (op) {
        JS_FreeValueRT(rt, op->on_message);
        ngx_free(op);
    }
}


static JSClassDef  ngx_js_sw_port_class = {
    "MessagePort",
    .finalizer = ngx_js_sw_port_finalizer
};


/*
 * port.postMessage(data) — SW thread → worker[wi]
 *
 * SharedArrayBuffers are passed by reference: the same mmap region
 * is visible to all processes (created before fork with MAP_SHARED).
 * The SAB data pointers are embedded in the serialised buffer by
 * JS_WriteObject2; we also send them as a separate table so the
 * receiver can release the in-transit reference after JS_ReadObject.
 */
static JSValue
ngx_js_sw_port_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_sw_port_opaque_t  *op;
    uint8_t                  *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t                    qjs_len, n_sabs;
    ngx_js_sab_hdr_t         *hdr;
    uint32_t                  i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_port_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "port.postMessage(data): data required");
    }

    qjs_buf = JS_WriteObject2(ctx, &qjs_len, argv[0],
                              JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                              &qjs_sab, &n_sabs);
    if (qjs_buf == NULL) {
        return JS_EXCEPTION;
    }

    /* Validate: all SABs must have been created before fork */
    for (i = 0; i < (uint32_t) n_sabs; i++) {
        hdr = (ngx_js_sab_hdr_t *) qjs_sab[i] - 1;
        if (!(hdr->flags & NGX_JS_SAB_SHARED)) {
            js_free(ctx, qjs_buf);
            js_free(ctx, qjs_sab);
            return JS_ThrowTypeError(ctx,
                "port.postMessage: SharedArrayBuffer was not created "
                "in master scope — cannot cross process boundary");
        }
    }

    buf = ngx_alloc(qjs_len, ngx_cycle->log);
    if (buf == NULL) {
        js_free(ctx, qjs_buf);
        js_free(ctx, qjs_sab);
        return JS_ThrowInternalError(ctx, "port.postMessage: alloc failed");
    }

    ngx_memcpy(buf, qjs_buf, qjs_len);
    js_free(ctx, qjs_buf);

    sab_tab = NULL;
    if (n_sabs > 0) {
        sab_tab = ngx_alloc(n_sabs * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            js_free(ctx, qjs_sab);
            ngx_free(buf);
            return JS_ThrowInternalError(ctx, "port.postMessage: alloc failed");
        }
        ngx_memcpy(sab_tab, qjs_sab, n_sabs * sizeof(uint8_t *));
    }
    js_free(ctx, qjs_sab);

    xpipe_send(&op->state->channels[op->wi].outbox,
               NGX_JS_SW_MSG_DATA, buf, (uint32_t) qjs_len,
               sab_tab, (uint32_t) n_sabs);

    return JS_UNDEFINED;
}


/* port.onmessage getter */
static JSValue
ngx_js_sw_port_get_onmessage(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_sw_port_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_port_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, op->on_message);
}


/* port.onmessage setter */
static JSValue
ngx_js_sw_port_set_onmessage(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_sw_port_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_port_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, op->on_message);
    op->on_message = JS_DupValue(ctx, val);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_sw_port_proto_funcs[] = {
    JS_CFUNC_DEF("postMessage", 1, ngx_js_sw_port_post_message),
    JS_CGETSET_MAGIC_DEF("onmessage",
                         ngx_js_sw_port_get_onmessage,
                         ngx_js_sw_port_set_onmessage, 0),
};


/* ------------------------------------------------------------------ */
/* SW thread global onconnect getter/setter                             */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_sw_get_onconnect(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_sw_thread_ctx_t  *tctx;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_UNDEFINED;
    }

    return JS_DupValue(ctx, tctx->on_connect);
}


static JSValue
ngx_js_sw_set_onconnect(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_sw_thread_ctx_t  *tctx;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_UNDEFINED;
    }

    JS_FreeValue(ctx, tctx->on_connect);
    tctx->on_connect = JS_DupValue(ctx, val);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_sw_global_props[] = {
    JS_CGETSET_MAGIC_DEF("onconnect",
                         ngx_js_sw_get_onconnect,
                         ngx_js_sw_set_onconnect, 0),
};


/* ------------------------------------------------------------------ */
/* Read script file (heap only, thread-safe)                            */
/* ------------------------------------------------------------------ */

static u_char *
sw_read_file(const char *path, size_t *out_len)
{
    int         fd;
    struct stat st;
    u_char     *buf;
    ssize_t     n;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }

    buf = ngx_alloc((size_t) st.st_size + 1, ngx_cycle->log);
    if (buf == NULL) {
        close(fd);
        return NULL;
    }

    n = read(fd, buf, (size_t) st.st_size);
    close(fd);

    if (n < 0) {
        ngx_free(buf);
        return NULL;
    }

    buf[n]   = '\0';
    *out_len = (size_t) n;

    return buf;
}


/* ------------------------------------------------------------------ */
/* SW thread entry                                                      */
/* ------------------------------------------------------------------ */

static void *
ngx_js_sw_thread(void *arg)
{
    ngx_js_sw_state_t       *state = arg;
    ngx_js_sw_thread_ctx_t  *tctx;
    JSRuntime               *rt;
    JSContext               *ctx, *job_ctx;
    JSValue                  global, result, data, event_obj, call_ret;
    JSValue                  port_obj, ports_arr, on_msg;
    ngx_js_sw_port_opaque_t *port_op;
    u_char                  *src;
    size_t                   src_len, i;
    struct pollfd           *pfds;
    uint32_t                 type, len, n_sabs, si;
    uint8_t                 *buf, **sab_tab;
    int                      terminate;
    ngx_uint_t               wi;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        return NULL;
    }

    js_std_init_handlers(rt);

    /*
     * The SW thread runs a blocking poll() loop, so it may use
     * Atomics.wait().  Install the shared SAB allocator so that
     * SABs created here are also MAP_SHARED (accessible to workers).
     */
    JS_SetCanBlock(rt, TRUE);
    JS_SetSharedArrayBufferFunctions(rt, &ngx_js_sab_funcs);

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NULL;
    }

    /* Register port class in this runtime */
    if (JS_NewClass(rt, ngx_js_sw_port_class_id,
                    &ngx_js_sw_port_class) < 0)
    {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return NULL;
    }

    tctx = ngx_alloc(sizeof(ngx_js_sw_thread_ctx_t), ngx_cycle->log);
    if (tctx == NULL) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return NULL;
    }

    tctx->state      = state;
    tctx->on_connect = JS_UNDEFINED;
    tctx->ports      = ngx_alloc(state->nchannels * sizeof(JSValue),
                                 ngx_cycle->log);
    if (tctx->ports == NULL) {
        ngx_free(tctx);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return NULL;
    }

    for (i = 0; i < state->nchannels; i++) {
        tctx->ports[i] = JS_UNDEFINED;
    }

    JS_SetContextOpaque(ctx, tctx);

    /* Install global onconnect getter/setter */
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyFunctionList(ctx, global,
                               ngx_js_sw_global_props,
                               countof(ngx_js_sw_global_props));
    JS_FreeValue(ctx, global);

    /* Read and evaluate the SW script */
    src = sw_read_file(state->script, &src_len);
    if (src == NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                      "js SharedWorker: failed to read \"%s\"",
                      state->script);
        goto done;
    }

    result = JS_Eval(ctx, (const char *) src, src_len,
                     state->script, JS_EVAL_TYPE_GLOBAL);
    ngx_free(src);

    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        JSValue str = JS_ToString(ctx, exc);
        const char *cstr = JS_ToCString(ctx, str);
        if (cstr) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                          "js SharedWorker eval exception: %s", cstr);
            JS_FreeCString(ctx, cstr);
        }
        JS_FreeValue(ctx, str);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, result);
        goto done;
    }

    JS_FreeValue(ctx, result);

    /* Set up poll fds for all inbox pipes */
    pfds = ngx_alloc(state->nchannels * sizeof(struct pollfd),
                     ngx_cycle->log);
    if (pfds == NULL) {
        goto done;
    }

    for (i = 0; i < state->nchannels; i++) {
        pfds[i].fd      = state->channels[i].inbox.rfd;
        pfds[i].events  = POLLIN;
        pfds[i].revents = 0;
    }

    /* Message loop */
    terminate = 0;

    for ( ;; ) {
        for (i = 0; i < state->nchannels; i++) {
            pfds[i].revents = 0;
        }

        if (poll(pfds, (nfds_t) state->nchannels, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (wi = 0; wi < state->nchannels; wi++) {
            if (!(pfds[wi].revents & POLLIN)) {
                continue;
            }

            /* Drain all messages from this inbox */
            while (xpipe_read_one(&state->channels[wi].inbox,
                                  &type, &buf, &len, &sab_tab, &n_sabs) == 0)
            {
                if (type == NGX_JS_SW_MSG_TERM) {
                    if (buf) {
                        ngx_free(buf);
                    }
                    ngx_free(sab_tab);
                    terminate = 1;
                    break;
                }

                if (type == NGX_JS_SW_MSG_CONNECT) {
                    ngx_free(sab_tab);
                    /* First connect from this worker → create port */
                    if (JS_IsUndefined(tctx->ports[wi])) {
                        port_op = ngx_alloc(
                            sizeof(ngx_js_sw_port_opaque_t),
                            ngx_cycle->log);
                        if (port_op == NULL) {
                            continue;
                        }

                        port_op->state      = state;
                        port_op->wi         = wi;
                        port_op->on_message = JS_UNDEFINED;

                        port_obj = JS_NewObjectClass(ctx,
                            ngx_js_sw_port_class_id);
                        if (JS_IsException(port_obj)) {
                            ngx_free(port_op);
                            continue;
                        }

                        JS_SetPropertyFunctionList(ctx, port_obj,
                            ngx_js_sw_port_proto_funcs,
                            countof(ngx_js_sw_port_proto_funcs));

                        JS_SetOpaque(port_obj, port_op);
                        tctx->ports[wi] = port_obj;
                    }

                    /* Fire onconnect({ports:[port]}) */
                    if (JS_IsFunction(ctx, tctx->on_connect)) {
                        ports_arr = JS_NewArray(ctx);
                        JS_SetPropertyUint32(ctx, ports_arr, 0,
                            JS_DupValue(ctx, tctx->ports[wi]));

                        event_obj = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, event_obj, "ports",
                                          ports_arr);

                        call_ret = JS_Call(ctx, tctx->on_connect,
                                           JS_UNDEFINED, 1, &event_obj);
                        if (JS_IsException(call_ret)) {
                            JSValue exc = JS_GetException(ctx);
                            JSValue str = JS_ToString(ctx, exc);
                            const char *cs = JS_ToCString(ctx, str);
                            if (cs) {
                                ngx_log_error(NGX_LOG_ERR,
                                    ngx_cycle->log, 0,
                                    "js SharedWorker onconnect: %s", cs);
                                JS_FreeCString(ctx, cs);
                            }
                            JS_FreeValue(ctx, str);
                            JS_FreeValue(ctx, exc);
                        }
                        JS_FreeValue(ctx, call_ret);
                        JS_FreeValue(ctx, event_obj);

                        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }
                    }

                    continue;
                }

                /* NGX_JS_SW_MSG_DATA */
                data = JS_ReadObject(ctx, buf, (size_t) len,
                                     JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);
                ngx_free(buf);

                /* Release in-transit SAB refs (JS_ReadObject took its own) */
                for (si = 0; si < n_sabs; si++) {
                    ngx_js_sab_free(NULL, sab_tab[si]);
                }
                ngx_free(sab_tab);

                if (JS_IsException(data)) {
                    JSValue exc = JS_GetException(ctx);
                    JS_FreeValue(ctx, exc);
                    continue;
                }

                if (!JS_IsUndefined(tctx->ports[wi])) {
                    port_op = JS_GetOpaque(tctx->ports[wi],
                                           ngx_js_sw_port_class_id);

                    if (port_op != NULL
                        && JS_IsFunction(ctx, port_op->on_message))
                    {
                        event_obj = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, event_obj, "data", data);

                        on_msg = JS_DupValue(ctx, port_op->on_message);
                        call_ret = JS_Call(ctx, on_msg,
                                           JS_UNDEFINED, 1, &event_obj);
                        JS_FreeValue(ctx, on_msg);

                        if (JS_IsException(call_ret)) {
                            JSValue exc = JS_GetException(ctx);
                            JSValue str = JS_ToString(ctx, exc);
                            const char *cs = JS_ToCString(ctx, str);
                            if (cs) {
                                ngx_log_error(NGX_LOG_ERR,
                                    ngx_cycle->log, 0,
                                    "js SharedWorker port msg: %s", cs);
                                JS_FreeCString(ctx, cs);
                            }
                            JS_FreeValue(ctx, str);
                            JS_FreeValue(ctx, exc);
                        }
                        JS_FreeValue(ctx, call_ret);
                        JS_FreeValue(ctx, event_obj);
                    } else {
                        JS_FreeValue(ctx, data);
                    }
                } else {
                    JS_FreeValue(ctx, data);
                }

                while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }
            }

            if (terminate) {
                break;
            }
        }

        if (terminate) {
            break;
        }
    }

    ngx_free(pfds);

done:
    JS_FreeValue(ctx, tctx->on_connect);
    for (i = 0; i < state->nchannels; i++) {
        JS_FreeValue(ctx, tctx->ports[i]);
    }
    ngx_free(tctx->ports);
    ngx_free(tctx);

    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);

    return NULL;
}


/* ------------------------------------------------------------------ */
/* Worker side: activate outbox.rfd in nginx epoll                     */
/* ------------------------------------------------------------------ */

static void ngx_js_sw_recv_handler(ngx_event_t *ev);


static ngx_int_t
ngx_js_sw_activate(JSContext *ctx, ngx_js_sw_state_t *state, ngx_uint_t wi)
{
    ngx_js_sw_worker_slot_t  *ws;
    ngx_js_sw_recv_ctx_t     *recv_ctx;
    ngx_connection_t         *conn;
    ngx_js_worker_t          *w;

    ws = &state->worker_slots[wi];

    if (ws->conn != NULL) {
        return NGX_OK;    /* already active */
    }

    w = JS_GetContextOpaque(ctx);

    recv_ctx = ngx_alloc(sizeof(ngx_js_sw_recv_ctx_t), ngx_cycle->log);
    if (recv_ctx == NULL) {
        return NGX_ERROR;
    }

    recv_ctx->state = state;
    recv_ctx->wi    = wi;

    conn = ngx_get_connection(state->channels[wi].outbox.rfd,
                              ngx_cycle->log);
    if (conn == NULL) {
        ngx_free(recv_ctx);
        return NGX_ERROR;
    }

    conn->data          = recv_ctx;
    conn->read->handler = ngx_js_sw_recv_handler;
    conn->read->log     = ngx_cycle->log;

    if (ngx_add_event(conn->read, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_free_connection(conn);
        conn->fd = (ngx_socket_t) -1;
        ngx_free(recv_ctx);
        return NGX_ERROR;
    }

    ws->conn = conn;
    ws->w    = w;

    /* Send CONNECT sentinel to SW thread */
    xpipe_send(&state->channels[wi].inbox, NGX_JS_SW_MSG_CONNECT,
               NULL, 0, NULL, 0);

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* nginx event: message arrived from SW thread                         */
/* ------------------------------------------------------------------ */

static void
ngx_js_sw_recv_handler(ngx_event_t *ev)
{
    ngx_connection_t          *conn;
    ngx_js_sw_recv_ctx_t      *recv_ctx;
    ngx_js_sw_state_t         *state;
    ngx_js_sw_worker_slot_t   *ws;
    ngx_js_worker_t           *w;
    JSContext                 *ctx, *job_ctx;
    JSValue                    data, event_obj, call_ret, on_msg;
    ngx_uint_t                 wi;
    uint32_t                   type, len, n_sabs, si;
    uint8_t                   *buf, **sab_tab;

    conn     = ev->data;
    recv_ctx = conn->data;
    state    = recv_ctx->state;
    wi       = recv_ctx->wi;
    ws       = &state->worker_slots[wi];
    w        = ws->w;
    ctx      = w->ctx;

    while (xpipe_read_one(&state->channels[wi].outbox,
                          &type, &buf, &len, &sab_tab, &n_sabs) == 0)
    {
        if (type != NGX_JS_SW_MSG_DATA) {
            if (buf) {
                ngx_free(buf);
            }
            ngx_free(sab_tab);
            continue;
        }

        data = JS_ReadObject(ctx, buf, (size_t) len,
                             JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);
        ngx_free(buf);

        /* Release in-transit SAB refs (JS_ReadObject took its own) */
        for (si = 0; si < n_sabs; si++) {
            ngx_js_sab_free(NULL, sab_tab[si]);
        }
        ngx_free(sab_tab);

        if (JS_IsException(data)) {
            JSValue exc = JS_GetException(ctx);
            JSValue str = JS_ToString(ctx, exc);
            const char *cstr = JS_ToCString(ctx, str);
            if (cstr) {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                              "js SharedWorker recv deserialize: %s",
                              cstr);
                JS_FreeCString(ctx, cstr);
            }
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, exc);
            continue;
        }

        event_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, event_obj, "data", data);

        if (JS_IsFunction(ctx, ws->on_message)) {
            on_msg   = JS_DupValue(ctx, ws->on_message);
            call_ret = JS_Call(ctx, on_msg, JS_UNDEFINED, 1, &event_obj);
            JS_FreeValue(ctx, on_msg);

            if (JS_IsException(call_ret)) {
                ngx_js_log_exception(ctx, ngx_cycle->log);
            }
            JS_FreeValue(ctx, call_ret);
        }

        JS_FreeValue(ctx, event_obj);
    }

    while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { }

    ngx_js_async_check(w);
}


/* ------------------------------------------------------------------ */
/* SharedWorker.prototype.postMessage (worker → SW thread)            */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_sw_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_sw_opaque_t  *op;
    uint8_t             *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t               qjs_len, n_sabs;
    ngx_js_sab_hdr_t    *hdr;
    ngx_uint_t           wi;
    uint32_t             i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage(data): data required");
    }

    wi = (ngx_uint_t) ngx_worker;

    if (wi >= op->state->nchannels) {
        return JS_ThrowInternalError(ctx,
                                     "postMessage: worker index out of range");
    }

    if (ngx_js_sw_activate(ctx, op->state, wi) != NGX_OK) {
        return JS_ThrowInternalError(ctx, "postMessage: activate failed");
    }

    qjs_buf = JS_WriteObject2(ctx, &qjs_len, argv[0],
                              JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                              &qjs_sab, &n_sabs);
    if (qjs_buf == NULL) {
        return JS_EXCEPTION;
    }

    /* Validate: all SABs must have been created before fork */
    for (i = 0; i < (uint32_t) n_sabs; i++) {
        hdr = (ngx_js_sab_hdr_t *) qjs_sab[i] - 1;
        if (!(hdr->flags & NGX_JS_SAB_SHARED)) {
            js_free(ctx, qjs_buf);
            js_free(ctx, qjs_sab);
            return JS_ThrowTypeError(ctx,
                "postMessage: SharedArrayBuffer was not created "
                "in master scope — cannot cross process boundary");
        }
    }

    buf = ngx_alloc(qjs_len, ngx_cycle->log);
    if (buf == NULL) {
        js_free(ctx, qjs_buf);
        js_free(ctx, qjs_sab);
        return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
    }

    ngx_memcpy(buf, qjs_buf, qjs_len);
    js_free(ctx, qjs_buf);

    sab_tab = NULL;
    if (n_sabs > 0) {
        sab_tab = ngx_alloc(n_sabs * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            js_free(ctx, qjs_sab);
            ngx_free(buf);
            return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
        }
        ngx_memcpy(sab_tab, qjs_sab, n_sabs * sizeof(uint8_t *));
    }
    js_free(ctx, qjs_sab);

    xpipe_send(&op->state->channels[wi].inbox,
               NGX_JS_SW_MSG_DATA, buf, (uint32_t) qjs_len,
               sab_tab, (uint32_t) n_sabs);

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* SharedWorker.prototype.onmessage getter/setter (worker side)       */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_sw_get_onmessage(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_sw_opaque_t  *op;
    ngx_uint_t           wi;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    wi = (ngx_uint_t) ngx_worker;
    if (wi >= op->state->nchannels) {
        return JS_UNDEFINED;
    }

    return JS_DupValue(ctx, op->state->worker_slots[wi].on_message);
}


static JSValue
ngx_js_sw_set_onmessage(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_sw_opaque_t       *op;
    ngx_js_sw_worker_slot_t  *ws;
    ngx_uint_t                wi;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    wi = (ngx_uint_t) ngx_worker;
    if (wi >= op->state->nchannels) {
        return JS_ThrowInternalError(ctx,
                                     "onmessage: worker index out of range");
    }

    ws = &op->state->worker_slots[wi];

    JS_FreeValue(ctx, ws->on_message);
    ws->on_message = JS_DupValue(ctx, val);

    /* Activate on first setter call (ensures connect is sent) */
    if (ws->conn == NULL) {
        ngx_js_sw_activate(ctx, op->state, wi);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_sw_proto_funcs[] = {
    JS_CFUNC_DEF("postMessage", 1, ngx_js_sw_post_message),
    JS_CGETSET_MAGIC_DEF("onmessage",
                         ngx_js_sw_get_onmessage,
                         ngx_js_sw_set_onmessage, 0),
};


/* ------------------------------------------------------------------ */
/* Build a JS SharedWorker object wrapping state                       */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_sw_make_object(JSContext *ctx, ngx_js_sw_state_t *state)
{
    ngx_js_sw_opaque_t  *opaque;
    JSValue              proto, obj;

    opaque = ngx_alloc(sizeof(ngx_js_sw_opaque_t), ngx_cycle->log);
    if (opaque == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "SharedWorker: opaque alloc failed");
    }

    opaque->state = state;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_sw_proto_funcs,
                               countof(ngx_js_sw_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_sw_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        ngx_free(opaque);
        return obj;
    }

    JS_SetOpaque(obj, opaque);

    return obj;
}


/* ------------------------------------------------------------------ */
/* new SharedWorker(url) constructor                                   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_sw_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    ngx_js_conf_t            *jcf;
    ngx_core_conf_t          *ccf;
    ngx_js_sw_state_t        *sw;
    volatile ngx_cycle_t     *c;
    const char               *url_cstr;
    size_t                    url_len;
    ngx_uint_t                nchannels, i;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "new SharedWorker(url): url required");
    }

    url_cstr = JS_ToCStringLen(ctx, &url_len, argv[0]);
    if (url_cstr == NULL) {
        return JS_EXCEPTION;
    }

    /*
     * During init_conf, the global ngx_cycle still points to the zero-
     * initialised init_cycle (conf_ctx == NULL) because ngx_cycle is only
     * assigned to the new cycle after ngx_init_cycle() returns.
     * In this case the JS context opaque is the new cycle (set by
     * ngx_js_com_init) and we must use it to reach conf_ctx.
     *
     * In worker / single-process mode (request handlers), ngx_cycle has
     * been updated to the live cycle and the context opaque holds
     * ngx_js_worker_t* — use ngx_cycle directly.
     */
    if (((volatile ngx_cycle_t *) ngx_cycle)->conf_ctx == NULL) {
        /* Config phase: opaque is the new ngx_cycle_t* */
        c = (volatile ngx_cycle_t *) JS_GetContextOpaque(ctx);
    } else {
        c = ngx_cycle;
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(c->conf_ctx, ngx_js_module);

    if (((volatile ngx_cycle_t *) ngx_cycle)->conf_ctx == NULL) {
        /* Config phase: create or look up SW */

        /* Look for existing SW with this URL */
        for (sw = jcf->sw_list; sw != NULL; sw = sw->next) {
            if (ngx_strcmp(sw->url, url_cstr) == 0) {
                JS_FreeCString(ctx, url_cstr);
                return ngx_js_sw_make_object(ctx, sw);
            }
        }

        /* Create new SW state */
        ccf = (ngx_core_conf_t *) ngx_get_conf(c->conf_ctx,
                                                ngx_core_module);
        nchannels = ccf->worker_processes;
        if (nchannels < 1) {
            nchannels = 1;
        }
        if (nchannels > NGX_MAX_PROCESSES) {
            nchannels = NGX_MAX_PROCESSES;
        }

        sw = ngx_alloc(sizeof(ngx_js_sw_state_t), c->log);
        if (sw == NULL) {
            JS_FreeCString(ctx, url_cstr);
            return JS_ThrowInternalError(ctx,
                                         "new SharedWorker: alloc failed");
        }

        ngx_memzero(sw, sizeof(ngx_js_sw_state_t));

        sw->url = ngx_alloc(url_len + 1, c->log);
        if (sw->url == NULL) {
            ngx_free(sw);
            JS_FreeCString(ctx, url_cstr);
            return JS_ThrowInternalError(ctx,
                                         "new SharedWorker: alloc failed");
        }

        ngx_memcpy(sw->url, url_cstr, url_len + 1);

        /* URL doubles as script path */
        sw->script = ngx_alloc(url_len + 1, c->log);
        if (sw->script == NULL) {
            ngx_free(sw->url);
            ngx_free(sw);
            JS_FreeCString(ctx, url_cstr);
            return JS_ThrowInternalError(ctx,
                                         "new SharedWorker: alloc failed");
        }

        ngx_memcpy(sw->script, url_cstr, url_len + 1);
        JS_FreeCString(ctx, url_cstr);

        sw->nchannels = nchannels;

        sw->channels = ngx_alloc(nchannels * sizeof(ngx_js_sw_channel_t),
                                 c->log);
        if (sw->channels == NULL) {
            ngx_free(sw->script);
            ngx_free(sw->url);
            ngx_free(sw);
            return JS_ThrowInternalError(ctx,
                                         "new SharedWorker: alloc failed");
        }

        sw->worker_slots = ngx_alloc(
            nchannels * sizeof(ngx_js_sw_worker_slot_t),
            c->log);
        if (sw->worker_slots == NULL) {
            ngx_free(sw->channels);
            ngx_free(sw->script);
            ngx_free(sw->url);
            ngx_free(sw);
            return JS_ThrowInternalError(ctx,
                                         "new SharedWorker: alloc failed");
        }

        for (i = 0; i < nchannels; i++) {
            if (xpipe_init(&sw->channels[i].inbox) != NGX_OK) {
                while (i-- > 0) {
                    xpipe_destroy(&sw->channels[i].inbox);
                    xpipe_destroy(&sw->channels[i].outbox);
                }
                ngx_free(sw->worker_slots);
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                return JS_ThrowInternalError(ctx,
                    "new SharedWorker: pipe_init failed");
            }

            if (xpipe_init(&sw->channels[i].outbox) != NGX_OK) {
                xpipe_destroy(&sw->channels[i].inbox);
                while (i-- > 0) {
                    xpipe_destroy(&sw->channels[i].inbox);
                    xpipe_destroy(&sw->channels[i].outbox);
                }
                ngx_free(sw->worker_slots);
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                return JS_ThrowInternalError(ctx,
                    "new SharedWorker: pipe_init failed");
            }

            sw->worker_slots[i].conn       = NULL;
            sw->worker_slots[i].on_message = JS_UNDEFINED;
            sw->worker_slots[i].w          = NULL;
        }

        if (pthread_create(&sw->tid, NULL, ngx_js_sw_thread, sw) != 0) {
            for (i = 0; i < nchannels; i++) {
                xpipe_destroy(&sw->channels[i].inbox);
                xpipe_destroy(&sw->channels[i].outbox);
            }
            ngx_free(sw->worker_slots);
            ngx_free(sw->channels);
            ngx_free(sw->script);
            ngx_free(sw->url);
            ngx_free(sw);
            return JS_ThrowInternalError(ctx,
                "new SharedWorker: pthread_create failed");
        }

        /* Prepend to registry */
        sw->next     = jcf->sw_list;
        jcf->sw_list = sw;

        return ngx_js_sw_make_object(ctx, sw);
    }

    /* Worker / single-process request phase: look up existing state */
    for (sw = jcf->sw_list; sw != NULL; sw = sw->next) {
        if (ngx_strcmp(sw->url, url_cstr) == 0) {
            JS_FreeCString(ctx, url_cstr);
            return ngx_js_sw_make_object(ctx, sw);
        }
    }

    JS_FreeCString(ctx, url_cstr);
    return JS_ThrowReferenceError(ctx,
        "SharedWorker: no SharedWorker with that URL "
        "(must be created in master scope first)");
}


/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_sw_install(JSContext *ctx)
{
    static ngx_uint_t  initialized;
    JSValue            global, ctor;

    if (!initialized) {
        JS_NewClassID(&ngx_js_sw_class_id);
        JS_NewClassID(&ngx_js_sw_port_class_id);
        initialized = 1;
    }

    if (JS_NewClass(JS_GetRuntime(ctx),
                    ngx_js_sw_class_id,
                    &ngx_js_sw_class) < 0)
    {
        return NGX_ERROR;
    }

    /* Register port class in the main runtime too (for consistency) */
    if (JS_NewClass(JS_GetRuntime(ctx),
                    ngx_js_sw_port_class_id,
                    &ngx_js_sw_port_class) < 0)
    {
        return NGX_ERROR;
    }

    global = JS_GetGlobalObject(ctx);

    ctor = JS_NewCFunction2(ctx, ngx_js_sw_ctor, "SharedWorker", 1,
                            JS_CFUNC_constructor, 0);

    JS_SetPropertyStr(ctx, global, "SharedWorker", ctor);

    JS_FreeValue(ctx, global);

    return NGX_OK;
}


void
ngx_js_sw_exit_process(ngx_cycle_t *cycle, ngx_js_conf_t *jcf)
{
    ngx_js_sw_state_t        *sw;
    ngx_js_sw_worker_slot_t  *ws;
    ngx_js_sw_recv_ctx_t     *recv_ctx;
    ngx_uint_t                wi;

    if (ngx_process != NGX_PROCESS_WORKER
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return;
    }

    wi = (ngx_uint_t) ngx_worker;

    for (sw = jcf->sw_list; sw != NULL; sw = sw->next) {
        if (wi >= sw->nchannels) {
            continue;
        }

        ws = &sw->worker_slots[wi];

        if (!JS_IsUndefined(ws->on_message)) {
            JS_FreeValue(jcf->ctx, ws->on_message);
            ws->on_message = JS_UNDEFINED;
        }

        if (ws->conn != NULL) {
            recv_ctx = ws->conn->data;
            ngx_del_event(ws->conn->read, NGX_READ_EVENT, 0);
            ngx_free_connection(ws->conn);
            ws->conn->fd = (ngx_socket_t) -1;
            ws->conn     = NULL;
            ngx_free(recv_ctx);
        }
    }
}


void
ngx_js_sw_exit_master(ngx_js_conf_t *jcf)
{
    ngx_js_sw_state_t  *sw, *next;
    ngx_uint_t          i;

    for (sw = jcf->sw_list; sw != NULL; sw = next) {
        next = sw->next;

        /* Signal all channels to terminate */
        for (i = 0; i < sw->nchannels; i++) {
            xpipe_send(&sw->channels[i].inbox, NGX_JS_SW_MSG_TERM,
                       NULL, 0, NULL, 0);
        }

        pthread_join(sw->tid, NULL);

        for (i = 0; i < sw->nchannels; i++) {
            xpipe_destroy(&sw->channels[i].inbox);
            xpipe_destroy(&sw->channels[i].outbox);
        }

        ngx_free(sw->channels);
        ngx_free(sw->worker_slots);
        ngx_free(sw->url);
        ngx_free(sw->script);
        ngx_free(sw);
    }

    jcf->sw_list = NULL;
}
