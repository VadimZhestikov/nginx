
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_worker.c — JS Worker class backed by pthreads + nginx event loop.
 *
 * new Worker("script.js")   — spawn independent JS thread
 * worker.postMessage(data)  — send structured-clone message to thread
 * worker.onmessage = fn     — receive messages from thread
 * worker.terminate()        — stop thread (blocks briefly for join)
 *
 * In the worker script:
 *   onmessage = e => { postMessage(e.data); }
 *
 * Integration: the worker thread communicates back via a pipe whose read-end
 * is registered with nginx's epoll event loop via ngx_get_connection().
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cutils.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_sw.h"
#include "ngx_js_worker.h"


/* ------------------------------------------------------------------ */
/* Message types                                                        */
/* ------------------------------------------------------------------ */

/* One queued message.  buf == NULL is the terminate sentinel. */
typedef struct {
    ngx_queue_t  link;
    uint8_t     *buf;
    size_t       len;
    uint8_t    **sab_tab;      /* SAB data pointers (may be NULL) */
    size_t       sab_tab_len;
} ngx_js_msg_t;


/*
 * Mutex-protected queue + a Unix pipe used as a waker.
 * The sender writes one byte to wfd whenever the queue transitions from
 * empty to non-empty; the receiver drains wfd after dequeuing.
 */
typedef struct {
    pthread_mutex_t  mutex;
    ngx_queue_t      queue;
    int              rfd;
    int              wfd;
} ngx_js_msg_pipe_t;


/* ------------------------------------------------------------------ */
/* State structs                                                        */
/* ------------------------------------------------------------------ */

/* Shared between main thread and worker thread (no nginx pool; heap only). */
typedef struct {
    pthread_t           tid;
    ngx_js_msg_pipe_t   to_worker;
    ngx_js_msg_pipe_t   from_worker;
    char               *script;    /* heap copy of script path */
} ngx_js_worker_state_t;


/* Opaque stored in the JS Worker object (main thread). */
typedef struct {
    ngx_js_worker_state_t  *state;
    JSValue                 on_message;
    ngx_connection_t       *conn;    /* wraps from_worker.rfd in nginx epoll */
    ngx_js_worker_t        *w;       /* nginx worker state (rt, ctx, …) */
} ngx_js_worker_opaque_t;


/*
 * One SharedWorker connection owned by a JS Worker thread.
 * Linked list rooted at ngx_js_wthread_ctx_t.sw_list.
 *
 * js_obj holds a strong JSValue reference to the wrapping JS object so that
 * the GC does not prematurely collect it when the user-script's local 'sw'
 * variable goes out of scope after onmessage() returns.  The fd must remain
 * open until the Worker thread has received the SW reply and forwarded it.
 * Released explicitly in done: before JS_FreeContext.
 */
typedef struct ngx_js_wt_sw_s  ngx_js_wt_sw_t;
struct ngx_js_wt_sw_s {
    int               worker_fd;  /* channel fd to SW thread (or -1 if dead) */
    int               wake_fd;    /* write end of SW wake pipe (or -1) */
    ngx_uint_t        wi;         /* channel index = ngx_worker at connect time */
    JSValue           on_message;
    JSValue           js_obj;     /* strong ref — prevents premature GC */
    ngx_js_wt_sw_t  **list;       /* &tctx->sw_list; for unlink in finalizer */
    ngx_js_wt_sw_t   *next;
};


/* Stored in the worker thread's JS context opaque. */
typedef struct {
    ngx_js_msg_pipe_t  *to_worker;
    ngx_js_msg_pipe_t  *from_worker;
    JSValue             on_message;
    ngx_js_wt_sw_t     *sw_list;   /* SharedWorker connections (may be NULL) */
} ngx_js_wthread_ctx_t;


/* ------------------------------------------------------------------ */
/* SharedWorker class (for use inside JS Worker threads)               */
/* ------------------------------------------------------------------ */

static JSClassID  ngx_js_wt_sw_class_id;

/* Max simultaneous SW connections per Worker thread (poll array) */
#define NGX_JS_WT_SW_MAX  16


static void
ngx_js_wt_sw_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_wt_sw_t  *sw, **p;

    sw = JS_GetOpaque(val, ngx_js_wt_sw_class_id);
    if (sw == NULL) {
        return;
    }

    /* Unlink from the Worker thread's sw_list */
    if (sw->list != NULL) {
        for (p = sw->list; *p != NULL; p = &(*p)->next) {
            if (*p == sw) {
                *p = sw->next;
                break;
            }
        }
    }

    JS_FreeValueRT(rt, sw->on_message);

    if (sw->worker_fd >= 0) {
        close(sw->worker_fd);
    }

    if (sw->wake_fd >= 0) {
        close(sw->wake_fd);
    }

    ngx_free(sw);
}


static JSClassDef ngx_js_wt_sw_class = {
    "SharedWorker",
    .finalizer = ngx_js_wt_sw_finalizer
};


static JSValue
ngx_js_wt_sw_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_wt_sw_t  *sw;
    uint8_t         *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t           qjs_len, sab_tab_len;

    sw = JS_GetOpaque2(ctx, this_val, ngx_js_wt_sw_class_id);
    if (sw == NULL) {
        return JS_EXCEPTION;
    }

    if (sw->worker_fd < 0) {
        return JS_ThrowInternalError(ctx, "postMessage: SW disconnected");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage(data): data required");
    }

    qjs_buf = JS_WriteObject2(ctx, &qjs_len, argv[0],
                              JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                              &qjs_sab, &sab_tab_len);
    if (qjs_buf == NULL) {
        return JS_EXCEPTION;
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
    if (sab_tab_len > 0) {
        sab_tab = ngx_alloc(sab_tab_len * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            js_free(ctx, qjs_sab);
            ngx_free(buf);
            return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
        }
        ngx_memcpy(sab_tab, qjs_sab, sab_tab_len * sizeof(uint8_t *));
    }
    js_free(ctx, qjs_sab);

    ngx_js_sw_wt_send(sw->worker_fd, sw->wake_fd, sw->wi,
                      buf, (uint32_t) qjs_len,
                      sab_tab, (uint32_t) sab_tab_len);

    return JS_UNDEFINED;
}


static JSValue
ngx_js_wt_sw_get_onmessage(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_wt_sw_t  *sw;

    sw = JS_GetOpaque2(ctx, this_val, ngx_js_wt_sw_class_id);
    if (sw == NULL) {
        return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, sw->on_message);
}


static JSValue
ngx_js_wt_sw_set_onmessage(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_wt_sw_t  *sw;

    sw = JS_GetOpaque2(ctx, this_val, ngx_js_wt_sw_class_id);
    if (sw == NULL) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, sw->on_message);
    sw->on_message = JS_DupValue(ctx, val);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_wt_sw_proto_funcs[] = {
    JS_CFUNC_DEF("postMessage", 1, ngx_js_wt_sw_post_message),
    JS_CGETSET_MAGIC_DEF("onmessage",
                         ngx_js_wt_sw_get_onmessage,
                         ngx_js_wt_sw_set_onmessage, 0),
};


/*
 * new SharedWorker("script.js") inside a JS Worker thread.
 *
 * Connects to (or creates) a SharedWorker in the master process by
 * calling ngx_js_sw_acquire_channel(), which talks to the SW manager
 * thread via sw_cmd_fds.  The call blocks only for the manager reply,
 * which is safe because Worker threads run their own blocking poll loop.
 */
static JSValue
ngx_js_wt_sw_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    ngx_js_wthread_ctx_t  *tctx;
    ngx_js_wt_sw_t        *sw;
    JSValue                obj;
    const char            *url_cstr;
    size_t                 url_len;
    int                    worker_fd, wake_fd;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "SharedWorker: no thread context");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "new SharedWorker(url): url required");
    }

    url_cstr = JS_ToCStringLen(ctx, &url_len, argv[0]);
    if (url_cstr == NULL) {
        return JS_EXCEPTION;
    }

    /* Blocking acquire: sends cmd to master manager, waits for reply */
    wake_fd   = -1;
    worker_fd = ngx_js_sw_acquire_channel(url_cstr, url_len, ngx_worker,
                                          &wake_fd);
    JS_FreeCString(ctx, url_cstr);

    if (worker_fd < 0) {
        return JS_ThrowInternalError(ctx,
                                     "SharedWorker: failed to connect");
    }

    sw = ngx_alloc(sizeof(ngx_js_wt_sw_t), ngx_cycle->log);
    if (sw == NULL) {
        close(worker_fd);
        if (wake_fd >= 0) {
            close(wake_fd);
        }
        return JS_ThrowInternalError(ctx, "SharedWorker: alloc failed");
    }

    sw->worker_fd  = worker_fd;
    sw->wake_fd    = wake_fd;
    sw->wi         = ngx_worker;
    sw->on_message = JS_UNDEFINED;
    sw->js_obj     = JS_UNDEFINED;  /* filled in after obj is created */
    sw->list       = &tctx->sw_list;
    sw->next       = tctx->sw_list;
    tctx->sw_list  = sw;

    obj = JS_NewObjectClass(ctx, ngx_js_wt_sw_class_id);
    if (JS_IsException(obj)) {
        tctx->sw_list = sw->next;
        close(worker_fd);
        if (wake_fd >= 0) {
            close(wake_fd);
        }
        ngx_free(sw);
        return obj;
    }

    JS_SetOpaque(obj, sw);

    /*
     * Hold a strong reference so the GC does not collect this JS object when
     * the user-script's local variable goes out of scope.  The fd must stay
     * open until the Worker thread has drained the SW reply.  Released in the
     * done: section below (before JS_FreeContext).
     */
    sw->js_obj = JS_DupValue(ctx, obj);

    return obj;
}


/* ------------------------------------------------------------------ */
/* JS class                                                             */
/* ------------------------------------------------------------------ */

static JSClassID ngx_js_worker_class_id;


static void
ngx_js_worker_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_worker_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_worker_class_id);
    if (op == NULL) {
        return;
    }

    JS_FreeValueRT(rt, op->on_message);

    /*
     * state should have been freed by terminate().  If it is not NULL here,
     * the caller forgot to call terminate() and we have a resource leak
     * (worker thread still running).  Log a warning but don't block the GC.
     */
    if (op->state != NULL) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js: Worker GC'd without terminate() — resource leak");
    }

    ngx_free(op);
}


static JSClassDef ngx_js_worker_class = {
    "Worker",
    .finalizer = ngx_js_worker_finalizer
};


/* ------------------------------------------------------------------ */
/* Pipe helpers                                                         */
/* ------------------------------------------------------------------ */

static ngx_int_t
pipe_init(ngx_js_msg_pipe_t *p)
{
    int  fds[2];

    if (pipe(fds) != 0) {
        return NGX_ERROR;
    }

    /* O_NONBLOCK on both ends so neither sender nor receiver ever blocks */
    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0
        || fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0)
    {
        close(fds[0]);
        close(fds[1]);
        return NGX_ERROR;
    }

    if (pthread_mutex_init(&p->mutex, NULL) != 0) {
        close(fds[0]);
        close(fds[1]);
        return NGX_ERROR;
    }

    p->rfd = fds[0];
    p->wfd = fds[1];
    ngx_queue_init(&p->queue);

    return NGX_OK;
}


static void
pipe_send(ngx_js_msg_pipe_t *p, uint8_t *buf, size_t len,
    uint8_t **sab_tab, size_t sab_tab_len)
{
    ngx_js_msg_t  *msg;
    size_t         i;
    int            was_empty;
    char           byte = 1;

    msg = ngx_alloc(sizeof(ngx_js_msg_t), ngx_cycle->log);
    if (msg == NULL) {
        if (buf) {
            ngx_free(buf);
        }
        for (i = 0; i < sab_tab_len; i++) {
            ngx_js_sab_free(NULL, sab_tab[i]);
        }
        ngx_free(sab_tab);
        return;
    }

    /*
     * Increment each SAB's reference count so the data stays alive
     * while the message is in transit (even if the sending JS context
     * GCs the original SharedArrayBuffer object before delivery).
     */
    for (i = 0; i < sab_tab_len; i++) {
        ngx_js_sab_dup(NULL, sab_tab[i]);
    }

    msg->buf         = buf;
    msg->len         = len;
    msg->sab_tab     = sab_tab;
    msg->sab_tab_len = sab_tab_len;

    pthread_mutex_lock(&p->mutex);
    was_empty = ngx_queue_empty(&p->queue);
    ngx_queue_insert_tail(&p->queue, &msg->link);
    pthread_mutex_unlock(&p->mutex);

    if (was_empty) {
        ssize_t  n = write(p->wfd, &byte, 1);
        (void) n;   /* O_NONBLOCK: failure is benign (reader still polls) */
    }
}


static void
pipe_recv_all(ngx_js_msg_pipe_t *p, ngx_queue_t *out)
{
    char  drain[256];

    pthread_mutex_lock(&p->mutex);

    /* Splice entire queue into *out */
    if (!ngx_queue_empty(&p->queue)) {
        /* Cheap O(1) queue concatenation */
        ngx_queue_t *head = ngx_queue_next(&p->queue);
        ngx_queue_t *tail = ngx_queue_last(&p->queue);
        ngx_queue_t *out_tail = ngx_queue_last(out);

        out_tail->next  = head;
        head->prev      = out_tail;
        tail->next      = out;
        out->prev       = tail;

        ngx_queue_init(&p->queue);
    }

    pthread_mutex_unlock(&p->mutex);

    /* Drain waker bytes (non-blocking) */
    while (read(p->rfd, drain, sizeof(drain)) > 0) { }
}


static void
pipe_destroy(ngx_js_msg_pipe_t *p)
{
    ngx_queue_t   *q;
    ngx_js_msg_t  *msg;
    size_t         i;

    pthread_mutex_lock(&p->mutex);

    while (!ngx_queue_empty(&p->queue)) {
        q   = ngx_queue_head(&p->queue);
        ngx_queue_remove(q);
        msg = ngx_queue_data(q, ngx_js_msg_t, link);
        if (msg->buf) {
            ngx_free(msg->buf);
        }
        for (i = 0; i < msg->sab_tab_len; i++) {
            ngx_js_sab_free(NULL, msg->sab_tab[i]);
        }
        ngx_free(msg->sab_tab);
        ngx_free(msg);
    }

    pthread_mutex_unlock(&p->mutex);

    pthread_mutex_destroy(&p->mutex);
    close(p->rfd);
    close(p->wfd);
}


/* ------------------------------------------------------------------ */
/* Worker thread — runs in a separate pthread                           */
/* ------------------------------------------------------------------ */

/*
 * Worker-side postMessage: serialize val and send to the main thread.
 */
static JSValue
ngx_js_wt_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_wthread_ctx_t  *tctx;
    uint8_t               *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t                 qjs_len, sab_tab_len;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_ThrowInternalError(ctx, "postMessage: no worker context");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage(data): data required");
    }

    qjs_buf = JS_WriteObject2(ctx, &qjs_len, argv[0],
                              JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                              &qjs_sab, &sab_tab_len);
    if (qjs_buf == NULL) {
        return JS_EXCEPTION;
    }

    buf = ngx_alloc(qjs_len, ngx_cycle->log);
    if (buf == NULL) {
        js_free(ctx, qjs_buf);
        js_free(ctx, qjs_sab);
        return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
    }

    ngx_memcpy(buf, qjs_buf, qjs_len);
    js_free(ctx, qjs_buf);

    /* Copy sab_tab from JS heap to nginx heap */
    sab_tab = NULL;
    if (sab_tab_len > 0) {
        sab_tab = ngx_alloc(sab_tab_len * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            js_free(ctx, qjs_sab);
            ngx_free(buf);
            return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
        }
        ngx_memcpy(sab_tab, qjs_sab, sab_tab_len * sizeof(uint8_t *));
    }
    js_free(ctx, qjs_sab);

    pipe_send(tctx->from_worker, buf, qjs_len, sab_tab, sab_tab_len);

    return JS_UNDEFINED;
}


/* Worker-side onmessage getter (magic 0) */
static JSValue
ngx_js_wt_get_onmessage(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_wthread_ctx_t  *tctx;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_UNDEFINED;
    }

    return JS_DupValue(ctx, tctx->on_message);
}


/* Worker-side onmessage setter (magic 0) */
static JSValue
ngx_js_wt_set_onmessage(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_wthread_ctx_t  *tctx;

    tctx = JS_GetContextOpaque(ctx);
    if (tctx == NULL) {
        return JS_UNDEFINED;
    }

    JS_FreeValue(ctx, tctx->on_message);
    tctx->on_message = JS_DupValue(ctx, val);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_wt_global_props[] = {
    JS_CGETSET_MAGIC_DEF("onmessage",
                         ngx_js_wt_get_onmessage,
                         ngx_js_wt_set_onmessage, 0),
};


/*
 * Read a file into a NUL-terminated heap buffer.
 * Uses only heap allocation (thread-safe).
 */
static u_char *
wt_read_file(const char *path, size_t *out_len)
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


/*
 * ngx_js_worker_thread — entry point for the worker pthread.
 *
 * Creates a fresh JSRuntime+JSContext, installs global postMessage /
 * onmessage / SharedWorker, evaluates the script, then enters a blocking
 * message loop that also polls any SharedWorker channel fds.
 */
static void *
ngx_js_worker_thread(void *arg)
{
    ngx_js_worker_state_t  *state = arg;
    ngx_js_wthread_ctx_t   *tctx;
    JSRuntime              *rt;
    JSContext              *ctx, *job_ctx;
    JSValue                 global, result, data, event_obj, call_ret;
    u_char                 *src;
    size_t                  src_len;
    struct pollfd           pfds[NGX_JS_WT_SW_MAX + 1];
    int                     nfds;
    ngx_queue_t             msgs;
    ngx_queue_t            *q;
    ngx_js_msg_t           *msg;
    ngx_js_wt_sw_t         *sw;
    int                     terminate;
    sigset_t                sigmask;

    /* Block all signals: process-directed signals should be handled
     * by the nginx main thread (event loop), not this helper thread. */
    sigfillset(&sigmask);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

    rt = JS_NewRuntime();
    if (rt == NULL) {
        return NULL;
    }

    js_std_init_handlers(rt);

    /*
     * Worker threads run their own poll() loop, so they are allowed to
     * block.  This enables Atomics.wait() for inter-thread synchronization
     * via SharedArrayBuffer.  The main nginx worker runtime must NOT have
     * can_block set (it would stall the nginx event loop).
     */
    JS_SetCanBlock(rt, TRUE);

    /*
     * Register the shared SAB alloc/free/dup.  Worker threads may create
     * their own SharedArrayBuffers (via new SharedArrayBuffer() in JS), so
     * the full alloc function is needed here too.
     */
    JS_SetSharedArrayBufferFunctions(rt, &ngx_js_sab_funcs);

    /* Register the SharedWorker class in this runtime */
    JS_NewClass(rt, ngx_js_wt_sw_class_id, &ngx_js_wt_sw_class);

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NULL;
    }

    tctx = ngx_alloc(sizeof(ngx_js_wthread_ctx_t), ngx_cycle->log);
    if (tctx == NULL) {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return NULL;
    }

    tctx->to_worker   = &state->to_worker;
    tctx->from_worker = &state->from_worker;
    tctx->on_message  = JS_UNDEFINED;
    tctx->sw_list     = NULL;

    JS_SetContextOpaque(ctx, tctx);

    /* Install global postMessage and onmessage on the global object */
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "postMessage",
                      JS_NewCFunction(ctx, ngx_js_wt_post_message,
                                      "postMessage", 1));
    JS_SetPropertyFunctionList(ctx, global,
                               ngx_js_wt_global_props,
                               countof(ngx_js_wt_global_props));

    /* Install SharedWorker constructor for use from Worker threads */
    JS_SetPropertyStr(ctx, global, "SharedWorker",
                      JS_NewCFunction2(ctx, ngx_js_wt_sw_ctor,
                                       "SharedWorker", 1,
                                       JS_CFUNC_constructor, 0));

    JS_FreeValue(ctx, global);

    /* Install shared prototype for the wt_sw class in this context */
    {
        JSValue  proto;

        proto = JS_NewObject(ctx);
        if (!JS_IsException(proto)) {
            JS_SetPropertyFunctionList(ctx, proto,
                                       ngx_js_wt_sw_proto_funcs,
                                       countof(ngx_js_wt_sw_proto_funcs));
            JS_SetClassProto(ctx, ngx_js_wt_sw_class_id, proto);
        }
    }

    /* Read and evaluate the worker script */
    src = wt_read_file(state->script, &src_len);
    if (src == NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                      "js worker: failed to read \"%s\"", state->script);
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
                          "js worker eval exception: %s", cstr);
            JS_FreeCString(ctx, cstr);
        }
        JS_FreeValue(ctx, str);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, result);
        goto done;
    }

    JS_FreeValue(ctx, result);

    /* Message loop — polls to_worker pipe + any SharedWorker channel fds */

    for ( ;; ) {
        /*
         * Build pfds fresh each iteration: [0] = to_worker.rfd,
         * [1..N] = worker_fd of each live SW connection.
         * Capped at NGX_JS_WT_SW_MAX SWs; extra connections are polled
         * in subsequent iterations once earlier ones drain.
         */
        pfds[0].fd      = state->to_worker.rfd;
        pfds[0].events  = POLLIN;
        pfds[0].revents = 0;
        nfds = 1;

        for (sw = tctx->sw_list;
             sw != NULL && nfds <= NGX_JS_WT_SW_MAX;
             sw = sw->next)
        {
            pfds[nfds].fd      = sw->worker_fd;
            pfds[nfds].events  = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        if (poll(pfds, nfds, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        /*
         * Process SharedWorker messages first.
         * We build a snapshot of {fd, on_message} pairs before executing
         * any JS so that mutations to sw_list during callbacks don't affect
         * the iteration.
         */
        {
            struct {
                int      fd;
                JSValue  on_message;
            } snap[NGX_JS_WT_SW_MAX];
            int      snap_len, si;
            uint8_t *sw_buf;
            uint32_t sw_type, sw_len, sw_n_sabs;
            uint8_t **sw_sab_tab;

            snap_len = 0;
            {
                int  pidx = 1;
                for (sw = tctx->sw_list;
                     sw != NULL && pidx < nfds && snap_len < NGX_JS_WT_SW_MAX;
                     sw = sw->next, pidx++)
                {
                    if (!(pfds[pidx].revents & POLLIN)) {
                        continue;
                    }
                    snap[snap_len].fd         = sw->worker_fd;
                    snap[snap_len].on_message = JS_DupValue(ctx, sw->on_message);
                    snap_len++;
                }
            }

            for (si = 0; si < snap_len; si++) {
                if (snap[si].fd < 0) {
                    JS_FreeValue(ctx, snap[si].on_message);
                    continue;
                }

                while (ngx_js_sw_wt_recv(snap[si].fd, &sw_type, &sw_buf,
                                         &sw_len, &sw_sab_tab,
                                         &sw_n_sabs) == 0)
                {
                    uint32_t  k;

                    if (sw_type != NGX_JS_SW_MSG_DATA) {
                        if (sw_buf) { ngx_free(sw_buf); }
                        for (k = 0; k < sw_n_sabs; k++) {
                            ngx_js_sab_free(NULL, sw_sab_tab[k]);
                        }
                        ngx_free(sw_sab_tab);
                        continue;
                    }

                    data = JS_ReadObject(ctx, sw_buf, (size_t) sw_len,
                                         JS_READ_OBJ_SAB
                                         | JS_READ_OBJ_REFERENCE);
                    ngx_free(sw_buf);
                    for (k = 0; k < sw_n_sabs; k++) {
                        ngx_js_sab_free(NULL, sw_sab_tab[k]);
                    }
                    ngx_free(sw_sab_tab);

                    if (JS_IsException(data)) {
                        JS_FreeValue(ctx, JS_GetException(ctx));
                        continue;
                    }

                    event_obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, event_obj, "data", data);

                    if (JS_IsFunction(ctx, snap[si].on_message)) {
                        call_ret = JS_Call(ctx, snap[si].on_message,
                                           JS_UNDEFINED, 1, &event_obj);
                        if (JS_IsException(call_ret)) {
                            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                                          "js worker sw onmessage exception");
                            JS_FreeValue(ctx, JS_GetException(ctx));
                        }
                        JS_FreeValue(ctx, call_ret);
                    }

                    JS_FreeValue(ctx, event_obj);
                    while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }
                }

                JS_FreeValue(ctx, snap[si].on_message);
            }
        }

        /* Process to_worker messages */
        if (!(pfds[0].revents & POLLIN)) {
            continue;
        }

        ngx_queue_init(&msgs);
        pipe_recv_all(&state->to_worker, &msgs);

        terminate = 0;

        while (!ngx_queue_empty(&msgs)) {
            q   = ngx_queue_head(&msgs);
            ngx_queue_remove(q);
            msg = ngx_queue_data(q, ngx_js_msg_t, link);

            /* NULL buf is the terminate sentinel */
            if (msg->buf == NULL) {
                ngx_free(msg);
                terminate = 1;
                break;
            }

            data = JS_ReadObject(ctx, msg->buf, msg->len,
                                 JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);
            ngx_free(msg->buf);
            /* Release the in-transit SAB refs (JS_ReadObject took its own) */
            {
                size_t  si;
                for (si = 0; si < msg->sab_tab_len; si++) {
                    ngx_js_sab_free(NULL, msg->sab_tab[si]);
                }
                ngx_free(msg->sab_tab);
            }
            ngx_free(msg);

            if (JS_IsException(data)) {
                JSValue exc = JS_GetException(ctx);
                JS_FreeValue(ctx, exc);
                continue;
            }

            /* Build MessageEvent-like object: { data: <value> } */
            event_obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, event_obj, "data", data);

            if (JS_IsFunction(ctx, tctx->on_message)) {
                call_ret = JS_Call(ctx, tctx->on_message,
                                   JS_UNDEFINED, 1, &event_obj);
                if (JS_IsException(call_ret)) {
                    JSValue exc = JS_GetException(ctx);
                    JSValue str = JS_ToString(ctx, exc);
                    const char *cstr = JS_ToCString(ctx, str);
                    if (cstr) {
                        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                                      "js worker onmessage exception: %s",
                                      cstr);
                        JS_FreeCString(ctx, cstr);
                    }
                    JS_FreeValue(ctx, str);
                    JS_FreeValue(ctx, exc);
                }
                JS_FreeValue(ctx, call_ret);
            }

            JS_FreeValue(ctx, event_obj);

            while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }
        }

        /* Free any messages left in the queue (after a terminate) */
        while (!ngx_queue_empty(&msgs)) {
            size_t  si;
            q   = ngx_queue_head(&msgs);
            ngx_queue_remove(q);
            msg = ngx_queue_data(q, ngx_js_msg_t, link);
            if (msg->buf) {
                ngx_free(msg->buf);
            }
            for (si = 0; si < msg->sab_tab_len; si++) {
                ngx_js_sab_free(NULL, msg->sab_tab[si]);
            }
            ngx_free(msg->sab_tab);
            ngx_free(msg);
        }

        if (terminate) {
            break;
        }
    }

done:
    JS_FreeValue(ctx, tctx->on_message);
    /*
     * Release SW entries.  Two steps are needed to handle cycles:
     *
     * 1. Pre-free on_message and set to UNDEFINED.  This removes the C
     *    struct's external ref to the callback, which may hold an upvalue
     *    closure over the sw JS object (e.g. sw.onmessage = fn(){ sw... }).
     *    Without this, JS_RunGC cannot see the {sw, callback} pair as
     *    unreachable and the cycle survives into JS_FreeRuntime.
     *
     * 2. Free js_obj.  If there is no cycle (callback does not capture sw),
     *    refcount reaches 0 here and the finalizer runs immediately (unlinks,
     *    closes fds, ngx_free).  If there IS a cycle, refcount stays ≥ 1;
     *    the finalizer runs later via JS_RunGC.  Either way the finalizer
     *    sees on_message == UNDEFINED, so it calls JS_FreeValueRT(UNDEFINED)
     *    which is a no-op — no double-free.
     *
     * We save sw->next before any JS_FreeValue call because the finalizer
     * frees the struct.  ngx_free(tctx) comes last so the finalizer can
     * safely dereference sw->list (= &tctx->sw_list).
     */
    {
        ngx_js_wt_sw_t  *sw_iter, *sw_next_iter;

        for (sw_iter = tctx->sw_list; sw_iter != NULL; sw_iter = sw_next_iter) {
            sw_next_iter = sw_iter->next;
            if (sw_iter->worker_fd >= 0) {
                close(sw_iter->worker_fd);
                sw_iter->worker_fd = -1;
            }
            JS_FreeValue(ctx, sw_iter->on_message);
            sw_iter->on_message = JS_UNDEFINED;
            JS_FreeValue(ctx, sw_iter->js_obj);  /* → finalizer if no cycle */
        }
    }
    /*
     * Collect any {sw, onmessage-callback} reference cycles that survived
     * the loop above.  Without this, JS_FreeRuntime would fire its
     * list_empty(&rt->gc_obj_list) assertion.
     */
    JS_RunGC(rt);
    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);
    ngx_free(tctx);

    return NULL;
}


/* ------------------------------------------------------------------ */
/* Main-thread (nginx event loop) side                                  */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_worker_recv_handler — nginx read event on from_worker.rfd.
 *
 * Called when the worker thread has posted one or more messages.
 * Deserializes each, calls worker.onmessage({data: …}), drains
 * microtasks, and checks for a settled async pending request.
 */
static void
ngx_js_worker_recv_handler(ngx_event_t *ev)
{
    ngx_connection_t        *conn;
    ngx_js_worker_opaque_t  *op;
    ngx_js_worker_t         *w;
    JSContext               *ctx, *job_ctx;
    ngx_queue_t              msgs, *q;
    ngx_js_msg_t            *msg;
    JSValue                  data, event_obj, call_ret;

    conn = ev->data;    /* nginx sets ev->data = connection */
    op   = conn->data;  /* our opaque is in conn->data */
    w    = op->w;
    ctx  = w->ctx;

    ngx_queue_init(&msgs);
    pipe_recv_all(&op->state->from_worker, &msgs);

    while (!ngx_queue_empty(&msgs)) {
        q   = ngx_queue_head(&msgs);
        ngx_queue_remove(q);
        msg = ngx_queue_data(q, ngx_js_msg_t, link);

        if (msg->buf == NULL) {
            /* Terminate sentinel from worker — unusual in this direction */
            ngx_free(msg);
            continue;
        }

        data = JS_ReadObject(ctx, msg->buf, msg->len,
                             JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);
        ngx_free(msg->buf);
        /* Release the in-transit SAB refs (JS_ReadObject took its own) */
        {
            size_t  si;
            for (si = 0; si < msg->sab_tab_len; si++) {
                ngx_js_sab_free(NULL, msg->sab_tab[si]);
            }
            ngx_free(msg->sab_tab);
        }
        ngx_free(msg);

        if (JS_IsException(data)) {
            JSValue exc = JS_GetException(ctx);
            JSValue str = JS_ToString(ctx, exc);
            const char *cstr = JS_ToCString(ctx, str);
            if (cstr) {
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                              "js worker recv deserialize error: %s", cstr);
                JS_FreeCString(ctx, cstr);
            }
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, exc);
            continue;
        }

        event_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, event_obj, "data", data);

        if (JS_IsFunction(ctx, op->on_message)) {
            /* Dup before calling: terminate() may free op->on_message
             * from within the callback (e.g. "w.onmessage = e => {
             * w.terminate(); ... }").  Without the dup the closure's
             * ref_count would drop to 0 while still executing → SIGSEGV.
             * With the dup it drops to 1 (safe) and reaches 0 only after
             * on_msg is freed here in C, outside of any JS frame. */
            JSValue on_msg = JS_DupValue(ctx, op->on_message);
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
    ngx_js_bf_async_check(w);
    ngx_js_sf_async_check(w);
}


/*
 * Worker.prototype.postMessage(data) — main thread sends to worker.
 */
static JSValue
ngx_js_worker_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_worker_opaque_t  *op;
    uint8_t                 *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t                   qjs_len, sab_tab_len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_worker_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (op->state == NULL) {
        return JS_ThrowInternalError(ctx, "postMessage: worker terminated");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage(data): data required");
    }

    qjs_buf = JS_WriteObject2(ctx, &qjs_len, argv[0],
                              JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                              &qjs_sab, &sab_tab_len);
    if (qjs_buf == NULL) {
        return JS_EXCEPTION;
    }

    buf = ngx_alloc(qjs_len, ngx_cycle->log);
    if (buf == NULL) {
        js_free(ctx, qjs_buf);
        js_free(ctx, qjs_sab);
        return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
    }

    ngx_memcpy(buf, qjs_buf, qjs_len);
    js_free(ctx, qjs_buf);

    /* Copy sab_tab from JS heap to nginx heap */
    sab_tab = NULL;
    if (sab_tab_len > 0) {
        sab_tab = ngx_alloc(sab_tab_len * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            js_free(ctx, qjs_sab);
            ngx_free(buf);
            return JS_ThrowInternalError(ctx, "postMessage: alloc failed");
        }
        ngx_memcpy(sab_tab, qjs_sab, sab_tab_len * sizeof(uint8_t *));
    }
    js_free(ctx, qjs_sab);

    pipe_send(&op->state->to_worker, buf, qjs_len, sab_tab, sab_tab_len);

    return JS_UNDEFINED;
}


/*
 * Worker.prototype.terminate() — shut down the worker thread.
 *
 * Sends the terminate sentinel, waits for the thread to exit (pthread_join),
 * then tears down all resources.  May block the event loop briefly while the
 * worker thread finishes (typically microseconds).
 */
static JSValue
ngx_js_worker_terminate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_worker_opaque_t  *op;
    ngx_js_worker_state_t   *state;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_worker_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (op->state == NULL) {
        return JS_UNDEFINED;    /* already terminated */
    }

    state = op->state;

    /* Wake worker thread with terminate sentinel (buf == NULL) */
    pipe_send(&state->to_worker, NULL, 0, NULL, 0);

    /* Wait for the worker thread to finish (frees its JSRuntime etc.) */
    pthread_join(state->tid, NULL);

    /* Remove from nginx event loop and release connection slot.
     * ngx_free_connection() clears cycle->files[fd] but does NOT set
     * fd = -1.  nginx's shutdown code iterates ALL connection slots and
     * alerts on any slot where fd != -1, so we must clear it ourselves
     * before nulling the pointer. */
    ngx_del_event(op->conn->read, NGX_READ_EVENT, 0);
    ngx_free_connection(op->conn);
    op->conn->fd = (ngx_socket_t) -1;
    op->conn = NULL;

    /* Free shared pipes and state */
    pipe_destroy(&state->to_worker);
    pipe_destroy(&state->from_worker);
    ngx_free(state->script);
    ngx_free(state);

    /* Break the cycle: closure may capture the Worker object (w), so free
     * on_message here rather than leaving it for the GC finalizer.  The
     * finalizer will see JS_UNDEFINED and skip the free safely. */
    JS_FreeValue(ctx, op->on_message);
    op->on_message = JS_UNDEFINED;

    op->state = NULL;

    return JS_UNDEFINED;
}


/* Worker object getset for onmessage (main thread side, magic 0) */
static JSValue
ngx_js_worker_get_onmessage(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_worker_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_worker_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, op->on_message);
}


static JSValue
ngx_js_worker_set_onmessage(JSContext *ctx, JSValueConst this_val,
    JSValue val, int magic)
{
    ngx_js_worker_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_worker_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, op->on_message);
    op->on_message = JS_DupValue(ctx, val);

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_worker_proto_funcs[] = {
    JS_CFUNC_DEF("postMessage", 1, ngx_js_worker_post_message),
    JS_CFUNC_DEF("terminate",   0, ngx_js_worker_terminate),
    JS_CGETSET_MAGIC_DEF("onmessage",
                         ngx_js_worker_get_onmessage,
                         ngx_js_worker_set_onmessage, 0),
};


/*
 * new Worker("script.js") constructor.
 *
 * Only available in nginx worker processes (where the JS context opaque
 * has been set to ngx_js_worker_t by init_process).
 */
static JSValue
ngx_js_worker_ctor(JSContext *ctx, JSValueConst new_target,
    int argc, JSValueConst *argv)
{
    ngx_js_worker_t        *w;
    ngx_js_worker_state_t  *state;
    ngx_js_worker_opaque_t *opaque;
    ngx_connection_t       *conn;
    JSValue                 obj;
    const char             *path_cstr;
    size_t                  path_len;

    if (ngx_process != NGX_PROCESS_WORKER
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): requires nginx worker");
    }

    w = JS_GetContextOpaque(ctx);
    if (w == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): no worker context");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "new Worker(path): path required");
    }

    path_cstr = JS_ToCStringLen(ctx, &path_len, argv[0]);
    if (path_cstr == NULL) {
        return JS_EXCEPTION;
    }

    /* ---- Allocate shared state ---- */

    state = ngx_alloc(sizeof(ngx_js_worker_state_t), ngx_cycle->log);
    if (state == NULL) {
        JS_FreeCString(ctx, path_cstr);
        return JS_ThrowInternalError(ctx, "new Worker(): alloc failed");
    }

    ngx_memzero(state, sizeof(ngx_js_worker_state_t));

    state->script = ngx_alloc(path_len + 1, ngx_cycle->log);
    if (state->script == NULL) {
        ngx_free(state);
        JS_FreeCString(ctx, path_cstr);
        return JS_ThrowInternalError(ctx, "new Worker(): alloc failed");
    }

    ngx_memcpy(state->script, path_cstr, path_len + 1);
    JS_FreeCString(ctx, path_cstr);

    if (pipe_init(&state->to_worker) != NGX_OK) {
        ngx_free(state->script);
        ngx_free(state);
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): pipe_init failed");
    }

    if (pipe_init(&state->from_worker) != NGX_OK) {
        pipe_destroy(&state->to_worker);
        ngx_free(state->script);
        ngx_free(state);
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): pipe_init failed");
    }

    /* ---- Allocate JS opaque ---- */

    opaque = ngx_alloc(sizeof(ngx_js_worker_opaque_t), ngx_cycle->log);
    if (opaque == NULL) {
        pipe_destroy(&state->from_worker);
        pipe_destroy(&state->to_worker);
        ngx_free(state->script);
        ngx_free(state);
        return JS_ThrowInternalError(ctx, "new Worker(): alloc failed");
    }

    opaque->on_message = JS_UNDEFINED;
    opaque->state      = state;
    opaque->w          = w;

    /* ---- Register from_worker.rfd with nginx event loop ---- */

    conn = ngx_get_connection(state->from_worker.rfd, ngx_cycle->log);
    if (conn == NULL) {
        ngx_free(opaque);
        pipe_destroy(&state->from_worker);
        pipe_destroy(&state->to_worker);
        ngx_free(state->script);
        ngx_free(state);
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): ngx_get_connection failed");
    }

    conn->data          = opaque;   /* our opaque; nginx keeps ev->data = conn */
    conn->read->handler = ngx_js_worker_recv_handler;
    conn->read->log     = ngx_cycle->log;

    if (ngx_add_event(conn->read, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_free_connection(conn);
        conn->fd = (ngx_socket_t) -1;
        ngx_free(opaque);
        pipe_destroy(&state->from_worker);
        pipe_destroy(&state->to_worker);
        ngx_free(state->script);
        ngx_free(state);
        return JS_ThrowInternalError(ctx,
                                     "new Worker(): ngx_add_event failed");
    }

    opaque->conn = conn;

    /* ---- Spawn the worker thread ---- */

    /* Block all signals before pthread_create so the JS Worker thread
     * inherits a fully-blocked mask.  Process-directed signals (SIGQUIT,
     * SIGCHLD, etc.) must be handled by the nginx event-loop main thread. */
    {
        sigset_t  full, prev;
        int       rc;

        sigfillset(&full);
        pthread_sigmask(SIG_BLOCK, &full, &prev);
        rc = pthread_create(&state->tid, NULL, ngx_js_worker_thread, state);
        pthread_sigmask(SIG_SETMASK, &prev, NULL);

        if (rc != 0) {
            ngx_del_event(conn->read, NGX_READ_EVENT, 0);
            ngx_free_connection(conn);
            conn->fd = (ngx_socket_t) -1;
            ngx_free(opaque);
            pipe_destroy(&state->from_worker);
            pipe_destroy(&state->to_worker);
            ngx_free(state->script);
            ngx_free(state);
            return JS_ThrowInternalError(ctx,
                                         "new Worker(): pthread_create failed");
        }
    }

    /* ---- Build the JS Worker object ---- */

    obj = JS_NewObjectClass(ctx, ngx_js_worker_class_id);
    if (JS_IsException(obj)) {
        /* Thread is running; send terminate to clean it up */
        pipe_send(&state->to_worker, NULL, 0, NULL, 0);
        pthread_join(state->tid, NULL);
        ngx_del_event(conn->read, NGX_READ_EVENT, 0);
        ngx_free_connection(conn);
        conn->fd = (ngx_socket_t) -1;
        ngx_free(opaque);
        pipe_destroy(&state->from_worker);
        pipe_destroy(&state->to_worker);
        ngx_free(state->script);
        ngx_free(state);
        return obj;
    }

    JS_SetOpaque(obj, opaque);

    return obj;
}


/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_worker_install(JSContext *ctx)
{
    static ngx_uint_t  initialized;
    JSValue            global, ctor;

    if (!initialized) {
        JS_NewClassID(&ngx_js_worker_class_id);
        JS_NewClassID(&ngx_js_wt_sw_class_id);
        initialized = 1;
    }

    if (JS_NewClass(JS_GetRuntime(ctx),
                    ngx_js_worker_class_id,
                    &ngx_js_worker_class) < 0)
    {
        return NGX_ERROR;
    }

    if (JS_NewClass(JS_GetRuntime(ctx),
                    ngx_js_wt_sw_class_id,
                    &ngx_js_wt_sw_class) < 0)
    {
        return NGX_ERROR;
    }

    /* Install shared prototype for the Worker class */
    {
        JSValue  proto;

        proto = JS_NewObject(ctx);
        if (JS_IsException(proto)) {
            return NGX_ERROR;
        }

        JS_SetPropertyFunctionList(ctx, proto,
                                   ngx_js_worker_proto_funcs,
                                   countof(ngx_js_worker_proto_funcs));

        /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
        JS_SetClassProto(ctx, ngx_js_worker_class_id, proto);
    }

    /* Install shared prototype for the SharedWorker-in-Worker class */
    {
        JSValue  proto;

        proto = JS_NewObject(ctx);
        if (JS_IsException(proto)) {
            return NGX_ERROR;
        }

        JS_SetPropertyFunctionList(ctx, proto,
                                   ngx_js_wt_sw_proto_funcs,
                                   countof(ngx_js_wt_sw_proto_funcs));

        /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
        JS_SetClassProto(ctx, ngx_js_wt_sw_class_id, proto);
    }

    global = JS_GetGlobalObject(ctx);

    ctor = JS_NewCFunction2(ctx, ngx_js_worker_ctor, "Worker", 1,
                            JS_CFUNC_constructor, 0);

    JS_SetPropertyStr(ctx, global, "Worker", ctor);

    JS_FreeValue(ctx, global);

    return NGX_OK;
}
