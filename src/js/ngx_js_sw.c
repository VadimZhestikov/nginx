
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_sw.c — SharedWorker class backed by a master-process pthread.
 *
 * new SharedWorker("script.js") in master (init_conf):
 *   - allocates N bidirectional socketpairs (one per nginx worker)
 *   - spawns a pthread running the script with a fresh JSRuntime
 *
 * new SharedWorker("script.js") in a worker process:
 *   - looks up the existing state by URL
 *   - on first use per worker: activates the worker_fd in nginx epoll
 *     and sends a CONNECT sentinel to the SW thread
 *
 * SW thread receives CONNECT for channel[i] → fires onconnect({ports:[p]})
 * where p.postMessage(data) sends back to the nginx worker.
 *
 * Worker-side sw.postMessage(data) → serialise → sendmsg(worker_fd)
 * SW thread receives data from sw_fd → calls port[i].onmessage({data:…})
 *
 * Worker-side sw.onmessage = fn → stored in worker_slots[i].on_message
 * nginx event on worker_fd → deserialise → call on_message({data:…})
 *
 * CHANNEL PROTOCOL (AF_UNIX SOCK_SEQPACKET, one message per sendmsg)
 * ------------------------------------------------------------------
 * Each message is one atomic sendmsg/recvmsg on the channel socket.
 *
 * Message layout:
 *   [uint32_t type][uint32_t data_len][uint32_t n_sabs][uint32_t n_memfds]
 *   [data_len bytes: serialised JS]
 *   [n_sabs × 8 bytes: pre-fork SAB sender VAs]
 *   [n_memfds × 16 bytes: {uint64_t sender_va, uint32_t size, uint32_t pad}]
 *   SCM_RIGHTS: n_memfds memfd file descriptors
 *
 * type values:
 *   NGX_JS_SW_MSG_DATA    (0) — serialised JS value
 *   NGX_JS_SW_MSG_CONNECT (1) — worker connects for the first time
 *   NGX_JS_SW_MSG_TERM    (2) — shutdown signal
 *
 * SharedArrayBuffers:
 *   Pre-fork (NGX_JS_SAB_SHARED): same VA in all processes; transit dup
 *     keeps the mapping alive; receiver calls sab_free after JS_ReadObject.
 *   Worker-created (NGX_JS_SAB_MEMFD): backed by memfd; fd sent via
 *     SCM_RIGHTS; receiver mmaps to a local VA, patches the serialised
 *     buffer, then calls JS_ReadObject + releases the mmap ref.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <cutils.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_sw.h"


/* ------------------------------------------------------------------ */
/* Channel message constants                                           */
/* ------------------------------------------------------------------ */

/* NGX_JS_SW_MSG_* are defined in ngx_js_sw.h */

/* Maximum memfd SABs and total message body per sendmsg */
#define NGX_JS_SW_MAX_SABS    8u
#define NGX_JS_SW_MAX_MEMFDS  8u
#define NGX_JS_SW_MAX_MSG     (32u * 1024u)


/* ------------------------------------------------------------------ */
/* Channel: bidirectional AF_UNIX SOCK_SEQPACKET socketpair           */
/* ------------------------------------------------------------------ */

typedef struct {
    int  sw_fd;      /* SW thread's end: read=from-worker, write=to-worker */
    int  worker_fd;  /* worker's end: read=from-SW, write=to-SW */
} ngx_js_sw_channel_t;


static ngx_int_t
channel_init(ngx_js_sw_channel_t *ch)
{
    int  fds[2];

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0) {
        return NGX_ERROR;
    }

    if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0
        || fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0)
    {
        close(fds[0]);
        close(fds[1]);
        return NGX_ERROR;
    }

    ch->sw_fd     = fds[0];
    ch->worker_fd = fds[1];

    return NGX_OK;
}


static void
channel_destroy(ngx_js_sw_channel_t *ch)
{
    if (ch->sw_fd >= 0) {
        close(ch->sw_fd);
        ch->sw_fd = -1;
    }
    if (ch->worker_fd >= 0) {
        close(ch->worker_fd);
        ch->worker_fd = -1;
    }
}


/*
 * Memfd SAB info packed into the message body.
 * 16 bytes per entry (8-byte aligned).
 */
typedef struct {
    uint64_t  sender_va;  /* SAB data pointer in sending process */
    uint32_t  size;       /* payload bytes */
    uint32_t  _pad;
} ngx_js_sw_memfd_info_t;


/*
 * channel_send — send one framed message over socket fd.
 *
 * Ownership of buf and sab_tab is transferred; both are ngx_free()'d here.
 * Pre-fork SABs: transit sab_dup here, receiver releases after JS_ReadObject.
 * Memfd SABs: fd sent via SCM_RIGHTS; receiver mmap+releases the mmap ref.
 */
static void
channel_send(int fd, uint32_t type,
             uint8_t *buf, uint32_t data_len,
             uint8_t **sab_tab, uint32_t n_sabs)
{
    uint32_t                hdr[4];
    ngx_js_sab_hdr_t       *sab_hdr;
    uint64_t                shared_vas[NGX_JS_SW_MAX_SABS];
    ngx_js_sw_memfd_info_t  memfd_info[NGX_JS_SW_MAX_MEMFDS];
    int                     memfd_fds[NGX_JS_SW_MAX_MEMFDS];
    uint32_t                n_shared, n_memfds, i;
    int                     sab_fd;
    struct iovec            iov[5];
    int                     niov;
    struct msghdr           msg;
    char                    cmsg_buf[CMSG_SPACE(NGX_JS_SW_MAX_MEMFDS
                                                * sizeof(int))];
    struct cmsghdr         *cmh;

    n_shared = 0;
    n_memfds = 0;

    for (i = 0; i < n_sabs; i++) {
        sab_hdr = (ngx_js_sab_hdr_t *) sab_tab[i] - 1;

        if (sab_hdr->flags & NGX_JS_SAB_MEMFD) {
            if (n_memfds >= NGX_JS_SW_MAX_MEMFDS) {
                continue;
            }
            sab_fd = ngx_js_sab_get_fd(sab_tab[i]);
            if (sab_fd < 0) {
                continue;
            }
            memfd_fds[n_memfds]             = sab_fd;
            memfd_info[n_memfds].sender_va  = (uint64_t)(uintptr_t) sab_tab[i];
            memfd_info[n_memfds].size       = sab_hdr->size;
            memfd_info[n_memfds]._pad       = 0;
            n_memfds++;

        } else if (sab_hdr->flags & NGX_JS_SAB_SHARED) {
            if (n_shared >= NGX_JS_SW_MAX_SABS) {
                continue;
            }
            /* Transit dup: keeps the mapping alive during pipe transit */
            ngx_js_sab_dup(NULL, sab_tab[i]);
            shared_vas[n_shared++] = (uint64_t)(uintptr_t) sab_tab[i];
        }
        /* SABs with flags==0 (no sharing) are silently dropped */
    }

    hdr[0] = type;
    hdr[1] = data_len;
    hdr[2] = n_shared;
    hdr[3] = n_memfds;

    niov = 0;
    iov[niov].iov_base = hdr;
    iov[niov].iov_len  = 16;
    niov++;

    if (buf != NULL && data_len > 0) {
        iov[niov].iov_base = buf;
        iov[niov].iov_len  = data_len;
        niov++;
    }

    if (n_shared > 0) {
        iov[niov].iov_base = shared_vas;
        iov[niov].iov_len  = n_shared * sizeof(uint64_t);
        niov++;
    }

    if (n_memfds > 0) {
        iov[niov].iov_base = memfd_info;
        iov[niov].iov_len  = n_memfds * sizeof(ngx_js_sw_memfd_info_t);
        niov++;
    }

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov    = iov;
    msg.msg_iovlen = niov;

    if (n_memfds > 0) {
        msg.msg_control    = cmsg_buf;
        msg.msg_controllen = CMSG_SPACE(n_memfds * sizeof(int));
        cmh                = CMSG_FIRSTHDR(&msg);
        cmh->cmsg_level    = SOL_SOCKET;
        cmh->cmsg_type     = SCM_RIGHTS;
        cmh->cmsg_len      = CMSG_LEN(n_memfds * sizeof(int));
        ngx_memcpy(CMSG_DATA(cmh), memfd_fds, n_memfds * sizeof(int));
    }

    (void) sendmsg(fd, &msg, 0);

    if (sab_tab) {
        ngx_free(sab_tab);
    }
    if (buf) {
        ngx_free(buf);
    }
}


/*
 * Scan serialized buffer and replace all occurrences of old_va with new_va.
 * QuickJS writes SAB pointers as raw uint64 little-endian in the buffer.
 */
static void
channel_patch_va(uint8_t *buf, uint32_t len,
                 uint64_t old_va, uint64_t new_va)
{
    uint32_t  i;
    uint64_t  v;

    for (i = 0; i + 8 <= len; i++) {
        ngx_memcpy(&v, buf + i, 8);
        if (v == old_va) {
            ngx_memcpy(buf + i, &new_va, 8);
        }
    }
}


/*
 * channel_recv — receive one framed message from socket fd.
 *
 * Returns 0 on success, -1 on EAGAIN/error.
 * On success:
 *   *buf_out     — heap buffer with serialised JS (caller ngx_free's it)
 *   *sab_tab_out — receiver-side SAB data pointers (all types unified)
 *   *n_sabs_out  — total SABs (pre-fork + memfd)
 *
 * Caller must call ngx_js_sab_free on each sab_tab entry after
 * JS_ReadObject to release the transit/mmap ref.
 */
static int
channel_recv(int fd, uint32_t *type_out,
             uint8_t **buf_out, uint32_t *len_out,
             uint8_t ***sab_tab_out, uint32_t *n_sabs_out)
{
    uint8_t                 recv_body[NGX_JS_SW_MAX_MSG];
    char                    cmsg_buf[CMSG_SPACE(NGX_JS_SW_MAX_MEMFDS
                                                * sizeof(int))];
    struct iovec            iov;
    struct msghdr           msg;
    ssize_t                 n;
    uint32_t               *hdr;
    uint32_t                type, data_len, n_shared, n_memfds, n_total;
    uint8_t                *data_ptr;
    uint64_t               *shared_vas;
    ngx_js_sw_memfd_info_t *memfd_info;
    uint8_t               **sab_tab;
    struct cmsghdr         *cmh;
    int                     recv_fds[NGX_JS_SW_MAX_MEMFDS];
    uint32_t                n_recv_fds;
    uint32_t                i;
    ngx_js_sab_hdr_t       *sab_hdr;
    void                   *new_ptr;
    size_t                  total_map;
    int                     rfd;

    iov.iov_base = recv_body;
    iov.iov_len  = sizeof(recv_body);

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    n = recvmsg(fd, &msg, MSG_DONTWAIT);
    if (n <= 0) {
        return -1;   /* EAGAIN or EOF */
    }

    if ((size_t) n < 16) {
        return -1;   /* truncated header */
    }

    if (msg.msg_flags & MSG_TRUNC) {
        return -1;   /* message too large */
    }

    hdr       = (uint32_t *)(void *) recv_body;
    type      = hdr[0];
    data_len  = hdr[1];
    n_shared  = hdr[2];
    n_memfds  = hdr[3];

    /* Sanity-check */
    if (n_shared > NGX_JS_SW_MAX_SABS
        || n_memfds > NGX_JS_SW_MAX_MEMFDS
        || (size_t) n < 16 + data_len
                          + n_shared * sizeof(uint64_t)
                          + n_memfds * sizeof(ngx_js_sw_memfd_info_t))
    {
        return -1;
    }

    data_ptr   = recv_body + 16;
    shared_vas = (uint64_t *)(void *)(data_ptr + data_len);
    memfd_info = (ngx_js_sw_memfd_info_t *)(void *)
                     ((uint8_t *) shared_vas + n_shared * sizeof(uint64_t));

    /* Extract received fds from SCM_RIGHTS */
    n_recv_fds = 0;
    cmh = CMSG_FIRSTHDR(&msg);
    if (cmh != NULL
        && cmh->cmsg_level == SOL_SOCKET
        && cmh->cmsg_type  == SCM_RIGHTS)
    {
        n_recv_fds = (uint32_t)((cmh->cmsg_len - CMSG_LEN(0)) / sizeof(int));
        if (n_recv_fds > NGX_JS_SW_MAX_MEMFDS) {
            n_recv_fds = NGX_JS_SW_MAX_MEMFDS;
        }
        ngx_memcpy(recv_fds, CMSG_DATA(cmh), n_recv_fds * sizeof(int));
    }

    *type_out    = type;
    *buf_out     = NULL;
    *sab_tab_out = NULL;
    *n_sabs_out  = 0;

    /* Copy serialised JS data to a heap buffer */
    if (data_len > 0) {
        *buf_out = ngx_alloc((size_t) data_len, ngx_cycle->log);
        if (*buf_out == NULL) {
            for (i = 0; i < n_recv_fds; i++) { close(recv_fds[i]); }
            return -1;
        }
        ngx_memcpy(*buf_out, data_ptr, data_len);
    }

    n_total = n_shared + n_memfds;

    if (n_total > 0) {
        sab_tab = ngx_alloc(n_total * sizeof(uint8_t *), ngx_cycle->log);
        if (sab_tab == NULL) {
            ngx_free(*buf_out);
            *buf_out = NULL;
            for (i = 0; i < n_recv_fds; i++) { close(recv_fds[i]); }
            return -1;
        }

        /* Pre-fork SABs: same VA is valid in this process */
        for (i = 0; i < n_shared; i++) {
            sab_tab[i] = (uint8_t *)(uintptr_t) shared_vas[i];
        }

        /* Memfd SABs: mmap the received fd to a local VA, patch buffer */
        for (i = 0; i < n_memfds; i++) {
            if (i >= n_recv_fds) {
                /* No fd received for this entry — skip */
                sab_tab[n_shared + i] = NULL;
                continue;
            }

            rfd        = recv_fds[i];
            total_map  = sizeof(ngx_js_sab_hdr_t) + memfd_info[i].size;

            new_ptr = mmap(NULL, total_map, PROT_READ | PROT_WRITE,
                           MAP_SHARED, rfd, 0);
            if (new_ptr == MAP_FAILED) {
                close(rfd);
                sab_tab[n_shared + i] = NULL;
                continue;
            }

            sab_hdr = (ngx_js_sab_hdr_t *) new_ptr;
            /* Data pointer in this process */
            new_ptr = (void *) sab_hdr->buf;

            /* Register in this process's fd table (local_refs = 1) */
            ngx_js_sab_register_memfd(new_ptr, rfd,
                                      (size_t) memfd_info[i].size);

            /* Patch the serialised buffer: replace sender VA with local VA */
            if (*buf_out != NULL) {
                channel_patch_va(*buf_out, data_len,
                                 memfd_info[i].sender_va,
                                 (uint64_t)(uintptr_t) new_ptr);
            }

            sab_tab[n_shared + i] = (uint8_t *) new_ptr;
        }

        *sab_tab_out = sab_tab;
        *n_sabs_out  = n_total;
    }

    *len_out = data_len;
    return 0;
}


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
    ngx_uint_t          local_wi;  /* (ngx_uint_t) -1 = use ngx_worker;
                                    * else use this index into stub state */
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
/* JS class IDs and manager-thread state (file-static)                */
/* ------------------------------------------------------------------ */

static JSClassID  ngx_js_sw_class_id;
static JSClassID  ngx_js_sw_port_class_id;

/*
 * Pre-fork socketpair for worker→master "create SW" commands.
 * sw_cmd_fds[0] — master side (manager reads requests here)
 * sw_cmd_fds[1] — worker side (workers write requests here, COW-shared)
 *
 * Command message format  (SOCK_SEQPACKET, atomic send):
 *   [uint32_t url_len][uint32_t worker_idx][url_len bytes url]
 * plus SCM_RIGHTS carrying one reply socket fd.
 *
 * Reply message format (sent back on the reply socket):
 *   [uint8_t status (0 = ok)]
 * plus SCM_RIGHTS carrying two fds: {inbox.wfd, outbox.rfd}.
 */
#define NGX_JS_SW_CMD_HDR  (2 * sizeof(uint32_t))
#define NGX_JS_SW_URL_MAX  512
#define NGX_JS_SW_CMD_MAX  (NGX_JS_SW_CMD_HDR + NGX_JS_SW_URL_MAX + 1)

static int        sw_cmd_fds[2]  = {-1, -1};
static int        sw_term_fds[2] = {-1, -1};
static pthread_t  sw_mgr_tid;
static int        sw_mgr_started;

/*
 * Set to 1 inside ngx_js_sw_thread so that ngx_js_sab_alloc uses
 * memfd even in the master process (post-fork SW thread cannot share
 * a MAP_SHARED|MAP_ANONYMOUS mapping with workers).
 */
__thread int  ngx_js_sw_thread_active;


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */

static void *ngx_js_sw_thread(void *arg);
static void *ngx_js_sw_manager_thread(void *arg);
static ngx_int_t ngx_js_sw_activate(JSContext *ctx,
    ngx_js_sw_state_t *state, ngx_uint_t wi);
static JSValue ngx_js_sw_request_dynamic(JSContext *ctx,
    ngx_js_conf_t *jcf, const char *url_cstr, size_t url_len);


/*
 * Resolve the worker-slot index for a SharedWorker opaque.
 * Static SWs (created in master) use ngx_worker.
 * Dynamic stub SWs (created per-worker) store index 0.
 */
static ngx_uint_t
sw_wi(ngx_js_sw_opaque_t *op)
{
    if (op->local_wi == (ngx_uint_t) -1) {
        return (ngx_uint_t) ngx_worker;
    }
    return op->local_wi;
}


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
 * SharedArrayBuffers: pre-fork SABs are passed by reference (same VA
 * in all processes).  Worker-created memfd SABs are sent by fd via
 * SCM_RIGHTS; the receiver mmap's to a local VA and patches the buffer.
 */
static JSValue
ngx_js_sw_port_post_message(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_sw_port_opaque_t  *op;
    uint8_t                  *qjs_buf, *buf, **qjs_sab, **sab_tab;
    size_t                    qjs_len, n_sabs;

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

    channel_send(op->state->channels[op->wi].sw_fd,
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
/* SW thread helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Rebuild the global `clients` array in the SW JS context from the
 * currently-connected ports (tctx->ports[i] != JS_UNDEFINED).
 * Called immediately before invoking the global onmessage handler so
 * that `clients` is always fresh when the script runs.
 */
static void
ngx_js_sw_refresh_clients(JSContext *ctx, ngx_js_sw_thread_ctx_t *tctx)
{
    JSValue    global, clients;
    uint32_t   idx, i;

    clients = JS_NewArray(ctx);
    idx     = 0;

    for (i = 0; i < (uint32_t) tctx->state->nchannels; i++) {
        if (!JS_IsUndefined(tctx->ports[i])) {
            JS_SetPropertyUint32(ctx, clients, idx++,
                                 JS_DupValue(ctx, tctx->ports[i]));
        }
    }

    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "clients", clients);  /* consumes clients */
    JS_FreeValue(ctx, global);
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

    ngx_js_sw_thread_active = 1;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        return NULL;
    }

    js_std_init_handlers(rt);

    /*
     * The SW thread runs a blocking poll() loop, so it may use
     * Atomics.wait().  Install the shared SAB allocator so SABs
     * created here use memfd (so the fd can be passed to workers
     * via SCM_RIGHTS).
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
        pfds[i].fd      = state->channels[i].sw_fd;
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

            /* Drain all messages from this channel */
            while (channel_recv(state->channels[wi].sw_fd,
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
                        /* per-port onmessage not set — try global onmessage */
                        goto global_onmessage;
                    }
                } else {
                global_onmessage:
                    {
                        JSValue  global_fn, g;

                        g         = JS_GetGlobalObject(ctx);
                        global_fn = JS_GetPropertyStr(ctx, g, "onmessage");
                        JS_FreeValue(ctx, g);

                        if (JS_IsFunction(ctx, global_fn)) {
                            ngx_js_sw_refresh_clients(ctx, tctx);
                            call_ret = JS_Call(ctx, global_fn,
                                               JS_UNDEFINED, 1, &data);
                            if (JS_IsException(call_ret)) {
                                JSValue exc = JS_GetException(ctx);
                                JSValue str = JS_ToString(ctx, exc);
                                const char *cs = JS_ToCString(ctx, str);
                                if (cs) {
                                    ngx_log_error(NGX_LOG_ERR,
                                        ngx_cycle->log, 0,
                                        "js SharedWorker onmessage: %s", cs);
                                    JS_FreeCString(ctx, cs);
                                }
                                JS_FreeValue(ctx, str);
                                JS_FreeValue(ctx, exc);
                            }
                            JS_FreeValue(ctx, call_ret);
                        }

                        JS_FreeValue(ctx, global_fn);
                        JS_FreeValue(ctx, data);
                    }
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

    conn = ngx_get_connection(state->channels[wi].worker_fd,
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
    channel_send(state->channels[wi].worker_fd, NGX_JS_SW_MSG_CONNECT,
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

    while (channel_recv(state->channels[wi].worker_fd,
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
    ngx_uint_t           wi;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_sw_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage(data): data required");
    }

    wi = sw_wi(op);

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

    channel_send(op->state->channels[wi].worker_fd,
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

    wi = sw_wi(op);
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

    wi = sw_wi(op);
    if (wi >= op->state->nchannels) {
        return JS_ThrowInternalError(ctx,
                                     "onmessage: worker index out of range");
    }

    ws = &op->state->worker_slots[wi];

    JS_FreeValue(ctx, ws->on_message);
    ws->on_message = JS_DupValue(ctx, val);

    /*
     * Activate on first setter call if the event loop is ready.
     * During init_process the connection pool is not yet initialised
     * (ngx_event_process_init runs after ngx_js_init_process); attempting
     * activation there produces a spurious "worker_connections not enough"
     * alert.  The lazy path in ngx_js_sw_post_message handles activation
     * on the first actual postMessage() call instead.
     */
    if (ws->conn == NULL && ngx_cycle->free_connections != NULL) {
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
ngx_js_sw_make_object(JSContext *ctx, ngx_js_sw_state_t *state,
    ngx_uint_t local_wi)
{
    ngx_js_sw_opaque_t  *opaque;
    JSValue              obj;

    opaque = ngx_alloc(sizeof(ngx_js_sw_opaque_t), ngx_cycle->log);
    if (opaque == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "SharedWorker: opaque alloc failed");
    }

    opaque->state    = state;
    opaque->local_wi = local_wi;

    obj = JS_NewObjectClass(ctx, ngx_js_sw_class_id);
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
                return ngx_js_sw_make_object(ctx, sw, (ngx_uint_t) -1);
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
            if (channel_init(&sw->channels[i]) != NGX_OK) {
                while (i-- > 0) {
                    channel_destroy(&sw->channels[i]);
                }
                ngx_free(sw->worker_slots);
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                return JS_ThrowInternalError(ctx,
                    "new SharedWorker: channel_init failed");
            }

            sw->worker_slots[i].conn       = NULL;
            sw->worker_slots[i].on_message = JS_UNDEFINED;
            sw->worker_slots[i].w          = NULL;
        }

        if (pthread_create(&sw->tid, NULL, ngx_js_sw_thread, sw) != 0) {
            for (i = 0; i < nchannels; i++) {
                channel_destroy(&sw->channels[i]);
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

        return ngx_js_sw_make_object(ctx, sw, (ngx_uint_t) -1);
    }

    /* ---- Worker / single-process request phase ---- */

    /* 1. Check static SWs created in master (jcf->sw_list COW copy) */
    for (sw = jcf->sw_list; sw != NULL; sw = sw->next) {
        if (ngx_strcmp(sw->url, url_cstr) == 0) {
            JS_FreeCString(ctx, url_cstr);
            return ngx_js_sw_make_object(ctx, sw, (ngx_uint_t) -1);
        }
    }

    /* 2. Check dynamic SWs already created in this worker */
    {
        ngx_js_worker_t  *w;

        w = (ngx_js_worker_t *) jcf->worker;

        for (sw = w->local_sw_list; sw != NULL; sw = sw->next) {
            if (ngx_strcmp(sw->url, url_cstr) == 0) {
                JS_FreeCString(ctx, url_cstr);
                return ngx_js_sw_make_object(ctx, sw, 0);
            }
        }
    }

    /* 3. Not found — request dynamic creation from the master manager.
     *    url_cstr ownership is passed; it is freed inside the callee. */
    return ngx_js_sw_request_dynamic(ctx, jcf, url_cstr, url_len);
}


/*
 * ngx_js_sw_request_dynamic — worker asks the master manager thread to
 * create a new SharedWorker (or return an existing one for this URL),
 * then builds a per-worker stub state for the channel.
 *
 * Protocol (atomic SEQPACKET):
 *   send: [uint32_t url_len][uint32_t worker_idx][url] + SCM_RIGHTS{reply}
 *   recv: [uint8_t 0=ok]                               + SCM_RIGHTS{inbox_wfd, outbox_rfd}
 *
 * The blocking recvmsg() completes in microseconds (manager just allocates
 * and creates pipes/thread, no I/O).  url_cstr is consumed (freed) here.
 */
static JSValue
ngx_js_sw_request_dynamic(JSContext *ctx, ngx_js_conf_t *jcf,
    const char *url_cstr, size_t url_len)
{
    ngx_js_worker_t           *w;
    ngx_js_sw_state_t         *stub;
    ngx_js_sw_channel_t       *ch;
    ngx_js_sw_worker_slot_t   *ws_slot;
    int                        reply_fds[2];
    int                        recv_fd;
    uint32_t                   url_len32, worker_idx32;
    uint8_t                   *cmdbuf;
    uint8_t                    status;
    struct msghdr              msg;
    struct iovec               iov;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_snd;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_rcv;
    struct cmsghdr            *cmh;
    ssize_t                    n;

    if (sw_cmd_fds[1] < 0) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx,
            "SharedWorker: manager not running");
    }

    /* Create a private reply socket for this request */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, reply_fds) != 0) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx,
            "SharedWorker: socketpair failed");
    }

    /* Command buffer: [url_len32][worker_idx32][url bytes] */
    url_len32    = (uint32_t) url_len;
    worker_idx32 = (uint32_t) ngx_worker;

    cmdbuf = ngx_alloc(NGX_JS_SW_CMD_HDR + url_len, ngx_cycle->log);
    if (cmdbuf == NULL) {
        close(reply_fds[0]);
        close(reply_fds[1]);
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx, "SharedWorker: alloc failed");
    }

    ngx_memcpy(cmdbuf,                    &url_len32,    sizeof(uint32_t));
    ngx_memcpy(cmdbuf + sizeof(uint32_t), &worker_idx32, sizeof(uint32_t));
    ngx_memcpy(cmdbuf + NGX_JS_SW_CMD_HDR, url_cstr, url_len);

    /* Send command + reply_fds[1] via SCM_RIGHTS */
    iov.iov_base = cmdbuf;
    iov.iov_len  = NGX_JS_SW_CMD_HDR + url_len;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_snd.buf;
    msg.msg_controllen = sizeof(cmsg_snd.buf);

    cmh             = CMSG_FIRSTHDR(&msg);
    cmh->cmsg_level = SOL_SOCKET;
    cmh->cmsg_type  = SCM_RIGHTS;
    cmh->cmsg_len   = CMSG_LEN(sizeof(int));
    ngx_memcpy(CMSG_DATA(cmh), &reply_fds[1], sizeof(int));

    n = sendmsg(sw_cmd_fds[1], &msg, 0);
    ngx_free(cmdbuf);
    close(reply_fds[1]);

    if (n < 0) {
        close(reply_fds[0]);
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx,
            "SharedWorker: sendmsg to manager failed");
    }

    /* Blocking receive: status byte + {inbox_wfd, outbox_rfd} */
    iov.iov_base = &status;
    iov.iov_len  = 1;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_rcv.buf;
    msg.msg_controllen = sizeof(cmsg_rcv.buf);

    n = recvmsg(reply_fds[0], &msg, 0);
    close(reply_fds[0]);

    recv_fd = -1;
    if (n >= 1 && status == 0) {
        cmh = CMSG_FIRSTHDR(&msg);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&recv_fd, CMSG_DATA(cmh), sizeof(int));
        }
    }

    if (recv_fd < 0) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx,
            "SharedWorker: manager did not return channel fd");
    }

    /* Build local stub state: nchannels=1, slot 0 = this worker */
    stub    = ngx_alloc(sizeof(ngx_js_sw_state_t), ngx_cycle->log);
    ch      = ngx_alloc(sizeof(ngx_js_sw_channel_t), ngx_cycle->log);
    ws_slot = ngx_alloc(sizeof(ngx_js_sw_worker_slot_t), ngx_cycle->log);

    if (stub == NULL || ch == NULL || ws_slot == NULL) {
        ngx_free(stub);
        ngx_free(ch);
        ngx_free(ws_slot);
        close(recv_fd);
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx, "SharedWorker: alloc failed");
    }

    ngx_memzero(stub, sizeof(ngx_js_sw_state_t));

    stub->url = ngx_alloc(url_len32 + 1, ngx_cycle->log);
    if (stub->url == NULL) {
        ngx_free(ws_slot);
        ngx_free(ch);
        ngx_free(stub);
        close(recv_fd);
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowInternalError(ctx, "SharedWorker: alloc failed");
    }

    ngx_memcpy(stub->url, url_cstr, url_len32 + 1);
    JS_FreeCString(ctx, url_cstr);

    /* recv_fd is the worker_fd end of the channel socketpair */
    ch->worker_fd = recv_fd;
    ch->sw_fd     = -1;   /* SW end lives in master; not used by this worker */

    ws_slot->conn       = NULL;
    ws_slot->on_message = JS_UNDEFINED;
    ws_slot->w          = NULL;

    stub->nchannels   = 1;
    stub->channels    = ch;
    stub->worker_slots = ws_slot;
    stub->tid         = 0;   /* SW thread lives in master; don't join here */

    /* Add to this worker's local list */
    w = (ngx_js_worker_t *) jcf->worker;
    stub->next       = w->local_sw_list;
    w->local_sw_list = stub;

    return ngx_js_sw_make_object(ctx, stub, 0);
}


/* ------------------------------------------------------------------ */
/* Public API for JS Worker threads                                    */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_sw_acquire_channel — connect to (or create) a SharedWorker for
 * the given URL from any thread in a nginx worker process.
 *
 * This is the non-JS, non-event-loop subset of ngx_js_sw_request_dynamic:
 * it communicates with the master manager via sw_cmd_fds (SEQPACKET, safe
 * for concurrent callers) and blocks only on the per-request reply socket.
 * The CONNECT sentinel is sent on the returned fd.
 */
int
ngx_js_sw_acquire_channel(const char *url, size_t url_len,
    ngx_uint_t worker_idx)
{
    int                reply_fds[2];
    int                recv_fd;
    uint32_t           url_len32, worker_idx32;
    uint8_t           *cmdbuf;
    uint8_t            status;
    struct msghdr      msg;
    struct iovec       iov;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_snd;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_rcv;
    struct cmsghdr    *cmh;
    ssize_t            n;

    if (sw_cmd_fds[1] < 0) {
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, reply_fds) != 0) {
        return -1;
    }

    url_len32    = (uint32_t) url_len;
    worker_idx32 = (uint32_t) worker_idx;

    cmdbuf = ngx_alloc(NGX_JS_SW_CMD_HDR + url_len, ngx_cycle->log);
    if (cmdbuf == NULL) {
        close(reply_fds[0]);
        close(reply_fds[1]);
        return -1;
    }

    ngx_memcpy(cmdbuf,                    &url_len32,    sizeof(uint32_t));
    ngx_memcpy(cmdbuf + sizeof(uint32_t), &worker_idx32, sizeof(uint32_t));
    ngx_memcpy(cmdbuf + NGX_JS_SW_CMD_HDR, url, url_len);

    iov.iov_base = cmdbuf;
    iov.iov_len  = NGX_JS_SW_CMD_HDR + url_len;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_snd.buf;
    msg.msg_controllen = sizeof(cmsg_snd.buf);

    cmh             = CMSG_FIRSTHDR(&msg);
    cmh->cmsg_level = SOL_SOCKET;
    cmh->cmsg_type  = SCM_RIGHTS;
    cmh->cmsg_len   = CMSG_LEN(sizeof(int));
    ngx_memcpy(CMSG_DATA(cmh), &reply_fds[1], sizeof(int));

    n = sendmsg(sw_cmd_fds[1], &msg, 0);
    ngx_free(cmdbuf);
    close(reply_fds[1]);

    if (n < 0) {
        close(reply_fds[0]);
        return -1;
    }

    /* Blocking receive: status byte + worker_fd via SCM_RIGHTS */
    iov.iov_base = &status;
    iov.iov_len  = 1;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_rcv.buf;
    msg.msg_controllen = sizeof(cmsg_rcv.buf);

    n = recvmsg(reply_fds[0], &msg, 0);
    close(reply_fds[0]);

    recv_fd = -1;
    if (n >= 1 && status == 0) {
        cmh = CMSG_FIRSTHDR(&msg);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&recv_fd, CMSG_DATA(cmh), sizeof(int));
        }
    }

    if (recv_fd >= 0) {
        channel_send(recv_fd, NGX_JS_SW_MSG_CONNECT, NULL, 0, NULL, 0);
    }

    return recv_fd;
}


void
ngx_js_sw_wt_send(int worker_fd, uint8_t *buf, uint32_t len,
    uint8_t **sab_tab, uint32_t n_sabs)
{
    channel_send(worker_fd, NGX_JS_SW_MSG_DATA, buf, len, sab_tab, n_sabs);
}


int
ngx_js_sw_wt_recv(int worker_fd, uint32_t *type_out,
    uint8_t **buf_out, uint32_t *len_out,
    uint8_t ***sab_tab_out, uint32_t *n_sabs_out)
{
    return channel_recv(worker_fd, type_out, buf_out, len_out,
                        sab_tab_out, n_sabs_out);
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

    /* Install shared prototype for the SharedWorker class */
    {
        JSValue  proto;

        proto = JS_NewObject(ctx);
        if (JS_IsException(proto)) {
            return NGX_ERROR;
        }

        JS_SetPropertyFunctionList(ctx, proto,
                                   ngx_js_sw_proto_funcs,
                                   countof(ngx_js_sw_proto_funcs));

        /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
        JS_SetClassProto(ctx, ngx_js_sw_class_id, proto);
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
    ngx_js_worker_t          *w;
    ngx_js_sw_state_t        *sw, *next;
    ngx_js_sw_worker_slot_t  *ws;
    ngx_js_sw_recv_ctx_t     *recv_ctx;
    ngx_uint_t                wi;

    if (ngx_process != NGX_PROCESS_WORKER
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return;
    }

    wi = (ngx_uint_t) ngx_worker;

    /* Clean up static (init_conf) SharedWorkers */
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

    /* Clean up dynamic (worker-local) SharedWorker stubs */
    w = (ngx_js_worker_t *) jcf->worker;
    if (w == NULL) {
        return;
    }

    for (sw = w->local_sw_list; sw != NULL; sw = next) {
        next = sw->next;
        ws   = &sw->worker_slots[0];

        if (!JS_IsUndefined(ws->on_message)) {
            JS_FreeValue(jcf->ctx, ws->on_message);
        }

        if (ws->conn != NULL) {
            recv_ctx = ws->conn->data;
            ngx_del_event(ws->conn->read, NGX_READ_EVENT, 0);
            ngx_free_connection(ws->conn);
            ws->conn->fd = (ngx_socket_t) -1;
            ngx_free(recv_ctx);
        }

        /* Close the channel fd we received from the manager */
        if (sw->channels[0].worker_fd >= 0) {
            close(sw->channels[0].worker_fd);
            sw->channels[0].worker_fd = -1;
        }

        ngx_free(sw->channels);
        ngx_free(sw->worker_slots);
        ngx_free(sw->url);
        ngx_free(sw);
    }

    w->local_sw_list = NULL;
}


void
ngx_js_sw_exit_master(ngx_js_conf_t *jcf)
{
    ngx_js_sw_state_t  *sw, *next;
    ngx_uint_t          i;
    char                c;

    /* Stop the manager thread first so it can't add to sw_list anymore */
    if (sw_mgr_started) {
        c = 0;
        if (write(sw_term_fds[1], &c, 1) < 0) { /* ignore */ }
        pthread_join(sw_mgr_tid, NULL);
        sw_mgr_started = 0;
        close(sw_cmd_fds[0]);  sw_cmd_fds[0]  = -1;
        close(sw_cmd_fds[1]);  sw_cmd_fds[1]  = -1;
        close(sw_term_fds[0]); sw_term_fds[0] = -1;
        close(sw_term_fds[1]); sw_term_fds[1] = -1;
    }

    for (sw = jcf->sw_list; sw != NULL; sw = next) {
        next = sw->next;

        /* Signal all channels to terminate */
        for (i = 0; i < sw->nchannels; i++) {
            channel_send(sw->channels[i].worker_fd, NGX_JS_SW_MSG_TERM,
                         NULL, 0, NULL, 0);
        }

        if (sw->tid) {
            pthread_join(sw->tid, NULL);
        }

        for (i = 0; i < sw->nchannels; i++) {
            channel_destroy(&sw->channels[i]);
        }

        ngx_free(sw->channels);
        ngx_free(sw->worker_slots);
        ngx_free(sw->url);
        ngx_free(sw->script);
        ngx_free(sw);
    }

    jcf->sw_list = NULL;
}


/* ------------------------------------------------------------------ */
/* SW manager thread — creates SW threads on worker demand             */
/* ------------------------------------------------------------------ */

/*
 * ------------------------------------------------------------------ *
 * Phase F — manager-side createSocket handler                         *
 *                                                                     *
 * Extended command protocol (url_len == 0 sentinel):                  *
 *   Worker → manager: [0:u32][cmd_type:u32][worker_idx:u32]           *
 *                     [addr_len:u32][addr:bytes]                      *
 *   plus SCM_RIGHTS carrying one reply socket fd.                     *
 *                                                                     *
 *   Manager → worker: [status:u8]  (0=ok, 1=err)                     *
 *   plus SCM_RIGHTS carrying the new socket fd (on success).          *
 *                                                                     *
 * cmd_type values:                                                     *
 *   NGX_JS_MGR_CMD_CREATE_SOCKET (1) — bind+listen on the given addr  *
 * ------------------------------------------------------------------ */

#define NGX_JS_MGR_CMD_CREATE_SOCKET  1u

/* Extended command header: [0:u32][cmd_type:u32][worker_idx:u32] */
#define NGX_JS_MGR_EXT_HDR  (3 * sizeof(uint32_t))

/* Maximum address string length for createSocket (e.g. "127.0.0.1:9000") */
#define NGX_JS_MGR_ADDR_MAX  63


/*
 * Manager-side: create a bound+listening TCP socket for the given
 * "host:port" address string.  Returns the fd on success, -1 on failure.
 * Called from inside the manager thread (blocking socket calls are fine).
 */
static int
ngx_js_mgr_do_create_socket(const char *addr_str)
{
    char                host[48];
    const char         *colon;
    size_t              host_len;
    long                port;
    char               *endp;
    struct sockaddr_in  sin;
    int                 fd, opt, saved;

    /* Parse "host:port" */
    colon = strrchr(addr_str, ':');
    if (colon == NULL) {
        return -1;
    }

    host_len = (size_t) (colon - addr_str);
    if (host_len == 0 || host_len >= sizeof(host)) {
        return -1;
    }

    ngx_memcpy(host, addr_str, host_len);
    host[host_len] = '\0';

    port = strtol(colon + 1, &endp, 10);
    if (*endp != '\0' || port < 1 || port > 65535) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    opt = 1;
    (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ngx_memzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((uint16_t) port);

    if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
        saved = errno;
        close(fd);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, saved,
                      "JS manager: createSocket bind(\"%s\") failed",
                      addr_str);
        return -1;
    }

    if (listen(fd, 511) < 0) {
        saved = errno;
        close(fd);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, saved,
                      "JS manager: createSocket listen() failed");
        return -1;
    }

    return fd;
}


/*
 * Worker-side: send a createSocket request to the manager thread and
 * block until the manager replies with the new socket fd.
 * Returns the new fd on success, -1 on failure.
 * Safe to call from a worker main process (same blocking pattern as
 * the dynamic SharedWorker round-trip).
 */
int
ngx_js_socket_mgr_create(const char *addr_str, size_t addr_len)
{
    int      reply_fds[2];
    int      recv_fd;
    uint32_t zero, cmd_type, worker_idx32, addr_len32;
    uint8_t *cmdbuf;
    size_t   cmdbuf_len;
    uint8_t  status;
    struct msghdr  msg;
    struct iovec   iov;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_snd;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_rcv;
    struct cmsghdr *cmh;
    ssize_t         n;

    if (sw_cmd_fds[1] < 0) {
        return -1;   /* manager not running */
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, reply_fds) != 0) {
        return -1;
    }

    /*
     * Command buffer: [0:u32][cmd_type:u32][worker_idx:u32][addr_len:u32]
     *                 [addr:bytes]
     */
    zero         = 0;
    cmd_type     = NGX_JS_MGR_CMD_CREATE_SOCKET;
    worker_idx32 = (uint32_t) ngx_worker;
    addr_len32   = (uint32_t) addr_len;
    cmdbuf_len   = NGX_JS_MGR_EXT_HDR + sizeof(uint32_t) + addr_len;

    cmdbuf = ngx_alloc(cmdbuf_len, ngx_cycle->log);
    if (cmdbuf == NULL) {
        close(reply_fds[0]);
        close(reply_fds[1]);
        return -1;
    }

    ngx_memcpy(cmdbuf,                      &zero,         sizeof(uint32_t));
    ngx_memcpy(cmdbuf +   sizeof(uint32_t), &cmd_type,     sizeof(uint32_t));
    ngx_memcpy(cmdbuf + 2*sizeof(uint32_t), &worker_idx32, sizeof(uint32_t));
    ngx_memcpy(cmdbuf + 3*sizeof(uint32_t), &addr_len32,   sizeof(uint32_t));
    ngx_memcpy(cmdbuf + 4*sizeof(uint32_t), addr_str,      addr_len);

    iov.iov_base = cmdbuf;
    iov.iov_len  = cmdbuf_len;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_snd.buf;
    msg.msg_controllen = sizeof(cmsg_snd.buf);

    cmh             = CMSG_FIRSTHDR(&msg);
    cmh->cmsg_level = SOL_SOCKET;
    cmh->cmsg_type  = SCM_RIGHTS;
    cmh->cmsg_len   = CMSG_LEN(sizeof(int));
    ngx_memcpy(CMSG_DATA(cmh), &reply_fds[1], sizeof(int));

    n = sendmsg(sw_cmd_fds[1], &msg, 0);
    ngx_free(cmdbuf);
    close(reply_fds[1]);

    if (n < 0) {
        close(reply_fds[0]);
        return -1;
    }

    /* Block until manager replies with status + fd */
    iov.iov_base = &status;
    iov.iov_len  = 1;

    ngx_memzero(&msg, sizeof(msg));
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_rcv.buf;
    msg.msg_controllen = sizeof(cmsg_rcv.buf);

    n = recvmsg(reply_fds[0], &msg, 0);
    close(reply_fds[0]);

    recv_fd = -1;
    if (n >= 1 && status == 0) {
        cmh = CMSG_FIRSTHDR(&msg);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&recv_fd, CMSG_DATA(cmh), sizeof(int));
        }
    }

    return recv_fd;   /* -1 means failure */
}


/*
 * ngx_js_sw_manager_thread — runs in the master process; receives
 * "create SharedWorker" requests from worker processes via sw_cmd_fds[0].
 *
 * For each request it either finds an existing sw in jcf->sw_list (by URL)
 * or creates a new one (allocates channels, starts the SW pthread, prepends
 * to jcf->sw_list).  It then sends the requesting worker's
 * {channels[wi].inbox.wfd, channels[wi].outbox.rfd} back on the reply fd
 * received via SCM_RIGHTS.
 */
static void *
ngx_js_sw_manager_thread(void *arg)
{
    ngx_js_conf_t             *jcf = arg;
    ngx_core_conf_t           *ccf;
    ngx_js_sw_state_t         *sw;
    struct pollfd              pfds[2];
    char                       cmdbuf[NGX_JS_SW_CMD_MAX];
    uint32_t                   url_len, worker_idx;
    char                      *url;
    ngx_uint_t                 nchannels, i;
    int                        reply_fd, ok;
    int                        fds[1];
    uint8_t                    status;
    struct msghdr              msg;
    struct iovec               iov;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_rcv;
    union {
        char            buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr  hdr;
    } cmsg_snd;
    struct cmsghdr            *cmh;
    ssize_t                    n;

    pfds[0].fd     = sw_cmd_fds[0];
    pfds[0].events = POLLIN;
    pfds[1].fd     = sw_term_fds[0];
    pfds[1].events = POLLIN;

    for ( ;; ) {
        pfds[0].revents = 0;
        pfds[1].revents = 0;

        if (poll(pfds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (pfds[1].revents & POLLIN) {
            break;   /* terminate signal */
        }

        if (!(pfds[0].revents & POLLIN)) {
            continue;
        }

        /* Receive command + reply_fd via SCM_RIGHTS */
        iov.iov_base = cmdbuf;
        iov.iov_len  = sizeof(cmdbuf) - 1;  /* leave room for NUL */

        ngx_memzero(&msg, sizeof(msg));
        msg.msg_iov        = &iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = cmsg_rcv.buf;
        msg.msg_controllen = sizeof(cmsg_rcv.buf);

        n = recvmsg(sw_cmd_fds[0], &msg, 0);
        if (n < (ssize_t) NGX_JS_SW_CMD_HDR) {
            continue;
        }

        /* Extract reply_fd from ancillary data */
        reply_fd = -1;
        cmh = CMSG_FIRSTHDR(&msg);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&reply_fd, CMSG_DATA(cmh), sizeof(int));
        }

        if (reply_fd < 0) {
            continue;   /* malformed — no reply fd */
        }

        ngx_memcpy(&url_len,    cmdbuf,                    sizeof(uint32_t));
        ngx_memcpy(&worker_idx, cmdbuf + sizeof(uint32_t), sizeof(uint32_t));

        /* url_len == 0: Phase F extended command */
        if (url_len == 0) {
            uint32_t  cmd_type, addr_len;
            char      addr_buf[NGX_JS_MGR_ADDR_MAX + 1];
            int       new_fd;

            /* Minimum message: [0][cmd_type][worker_idx] + [addr_len][addr] */
            if ((ssize_t) n < (ssize_t)(NGX_JS_MGR_EXT_HDR + sizeof(uint32_t)))
            {
                close(reply_fd);
                continue;
            }

            ngx_memcpy(&cmd_type, cmdbuf + sizeof(uint32_t), sizeof(uint32_t));
            /* worker_idx already parsed above */

            if (cmd_type == NGX_JS_MGR_CMD_CREATE_SOCKET) {
                ngx_memcpy(&addr_len,
                           cmdbuf + 3 * sizeof(uint32_t), sizeof(uint32_t));

                if (addr_len == 0 || addr_len > NGX_JS_MGR_ADDR_MAX
                    || (ssize_t) n < (ssize_t)(NGX_JS_MGR_EXT_HDR
                                               + sizeof(uint32_t) + addr_len))
                {
                    status = 1;
                    iov.iov_base = &status;
                    iov.iov_len  = 1;
                    ngx_memzero(&msg, sizeof(msg));
                    msg.msg_iov    = &iov;
                    msg.msg_iovlen = 1;
                    (void) sendmsg(reply_fd, &msg, 0);
                    close(reply_fd);
                    continue;
                }

                ngx_memcpy(addr_buf,
                           cmdbuf + 4 * sizeof(uint32_t), addr_len);
                addr_buf[addr_len] = '\0';

                new_fd = ngx_js_mgr_do_create_socket(addr_buf);

                /* Send reply: status byte + fd on success */
                status = (new_fd < 0) ? 1 : 0;
                iov.iov_base = &status;
                iov.iov_len  = 1;

                ngx_memzero(&msg, sizeof(msg));
                msg.msg_iov    = &iov;
                msg.msg_iovlen = 1;

                if (new_fd >= 0) {
                    msg.msg_control    = cmsg_snd.buf;
                    msg.msg_controllen = sizeof(cmsg_snd.buf);

                    cmh             = CMSG_FIRSTHDR(&msg);
                    cmh->cmsg_level = SOL_SOCKET;
                    cmh->cmsg_type  = SCM_RIGHTS;
                    cmh->cmsg_len   = CMSG_LEN(sizeof(int));
                    ngx_memcpy(CMSG_DATA(cmh), &new_fd, sizeof(int));
                }

                (void) sendmsg(reply_fd, &msg, 0);
                close(reply_fd);

                /* Master closes its copy; worker now owns the fd */
                if (new_fd >= 0) {
                    close(new_fd);
                }
            } else {
                /* Unknown extended command — send error */
                status = 1;
                iov.iov_base = &status;
                iov.iov_len  = 1;
                ngx_memzero(&msg, sizeof(msg));
                msg.msg_iov    = &iov;
                msg.msg_iovlen = 1;
                (void) sendmsg(reply_fd, &msg, 0);
                close(reply_fd);
            }

            continue;
        }

        /* url_len > 0: existing SharedWorker request */
        if (url_len > NGX_JS_SW_URL_MAX
            || (ssize_t)(NGX_JS_SW_CMD_HDR + url_len) > n)
        {
            close(reply_fd);
            continue;
        }

        url              = cmdbuf + NGX_JS_SW_CMD_HDR;
        url[url_len]     = '\0';

        /* Find or create SW state */
        for (sw = jcf->sw_list; sw != NULL; sw = sw->next) {
            if (ngx_strcmp(sw->url, url) == 0) {
                break;
            }
        }

        if (sw == NULL) {
            /* Create new SW state */
            ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                                    ngx_core_module);
            nchannels = (ngx_uint_t) ccf->worker_processes;
            if (nchannels < 1) {
                nchannels = 1;
            }
            if (nchannels > NGX_MAX_PROCESSES) {
                nchannels = NGX_MAX_PROCESSES;
            }

            sw = ngx_alloc(sizeof(ngx_js_sw_state_t), ngx_cycle->log);
            if (sw == NULL) {
                close(reply_fd);
                continue;
            }

            ngx_memzero(sw, sizeof(ngx_js_sw_state_t));

            sw->url = ngx_alloc(url_len + 1, ngx_cycle->log);
            if (sw->url == NULL) {
                ngx_free(sw);
                close(reply_fd);
                continue;
            }
            ngx_memcpy(sw->url, url, url_len + 1);

            sw->script = ngx_alloc(url_len + 1, ngx_cycle->log);
            if (sw->script == NULL) {
                ngx_free(sw->url);
                ngx_free(sw);
                close(reply_fd);
                continue;
            }
            ngx_memcpy(sw->script, url, url_len + 1);

            sw->nchannels = nchannels;
            sw->channels  = ngx_alloc(
                nchannels * sizeof(ngx_js_sw_channel_t), ngx_cycle->log);
            if (sw->channels == NULL) {
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                close(reply_fd);
                continue;
            }

            sw->worker_slots = ngx_alloc(
                nchannels * sizeof(ngx_js_sw_worker_slot_t),
                ngx_cycle->log);
            if (sw->worker_slots == NULL) {
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                close(reply_fd);
                continue;
            }

            ok = 1;
            for (i = 0; i < nchannels; i++) {
                if (channel_init(&sw->channels[i]) != NGX_OK) {
                    ok = 0;
                    break;
                }
                sw->worker_slots[i].conn       = NULL;
                sw->worker_slots[i].on_message = JS_UNDEFINED;
                sw->worker_slots[i].w          = NULL;
            }

            if (!ok) {
                while (i-- > 0) {
                    channel_destroy(&sw->channels[i]);
                }
                ngx_free(sw->worker_slots);
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                close(reply_fd);
                continue;
            }

            if (pthread_create(&sw->tid, NULL, ngx_js_sw_thread, sw) != 0) {
                for (i = 0; i < nchannels; i++) {
                    channel_destroy(&sw->channels[i]);
                }
                ngx_free(sw->worker_slots);
                ngx_free(sw->channels);
                ngx_free(sw->script);
                ngx_free(sw->url);
                ngx_free(sw);
                close(reply_fd);
                continue;
            }

            sw->next     = jcf->sw_list;
            jcf->sw_list = sw;
        }

        if (worker_idx >= sw->nchannels) {
            close(reply_fd);
            continue;
        }

        /* Reply: status=0 + worker_fd via SCM_RIGHTS */
        fds[0] = sw->channels[worker_idx].worker_fd;
        status = 0;

        iov.iov_base = &status;
        iov.iov_len  = 1;

        ngx_memzero(&msg, sizeof(msg));
        msg.msg_iov        = &iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = cmsg_snd.buf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));

        cmh             = CMSG_FIRSTHDR(&msg);
        cmh->cmsg_level = SOL_SOCKET;
        cmh->cmsg_type  = SCM_RIGHTS;
        cmh->cmsg_len   = CMSG_LEN(sizeof(int));
        ngx_memcpy(CMSG_DATA(cmh), fds, sizeof(int));

        (void) sendmsg(reply_fd, &msg, 0);
        close(reply_fd);
    }

    return NULL;
}


ngx_int_t
ngx_js_sw_manager_start(ngx_js_conf_t *jcf, ngx_cycle_t *cycle)
{
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sw_cmd_fds) != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: SharedWorker manager socketpair failed");
        return NGX_ERROR;
    }

    if (pipe(sw_term_fds) != 0) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: SharedWorker manager term pipe failed");
        close(sw_cmd_fds[0]);
        close(sw_cmd_fds[1]);
        sw_cmd_fds[0] = sw_cmd_fds[1] = -1;
        return NGX_ERROR;
    }

    if (pthread_create(&sw_mgr_tid, NULL,
                       ngx_js_sw_manager_thread, jcf) != 0)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: SharedWorker manager pthread_create failed");
        close(sw_cmd_fds[0]);
        close(sw_cmd_fds[1]);
        close(sw_term_fds[0]);
        close(sw_term_fds[1]);
        sw_cmd_fds[0]  = sw_cmd_fds[1]  = -1;
        sw_term_fds[0] = sw_term_fds[1] = -1;
        return NGX_ERROR;
    }

    sw_mgr_started = 1;
    return NGX_OK;
}
