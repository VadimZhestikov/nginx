
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 Phases B-D — nginx.http.attach(sock) + listener methods
 *
 * Phase B — nginx.http.attach(sock):
 *   Wires a NginxSocket into nginx's HTTP connection pipeline by building
 *   the routing structures (ngx_http_port_t / ngx_http_in_addr_t /
 *   ngx_http_addr_conf_t) and preparing the ngx_listening_t template.
 *
 * Phase C — listener.addServer(srv):
 *   Sets the default_server on the listener and activates it by pushing
 *   the ngx_listening_t into cycle->listening.
 *
 * Phase D — listener.addVirtualServer(srv):
 *   Adds an additional server for Host-header routing.  The virtual_names
 *   hash (ngx_http_virtual_names_t) is rebuilt from all vservers[] entries
 *   and stored in addr_conf->virtual_names so workers see the updated table
 *   after fork.
 *
 * JS API:
 *
 *   const listener = nginx.http.attach(sock);
 *   listener.address                    // "host:port" string
 *   listener.addServer(srv)             // activate with default server
 *   listener.addVirtualServer(srv)      // add named virtual host
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "ngx_js_socket.h"
#include "ngx_js_listener.h"


/* F2: ngx_js_wrap_server is defined in ngx_js_com_http.c */
extern JSValue  ngx_js_wrap_server(JSContext *ctx,
    ngx_http_core_srv_conf_t *cscf, ngx_cycle_t *cycle);

/* P17: HTTP module — needed in ngx_js_srv_accept_handler */
extern ngx_module_t  ngx_js_http_module;


JSClassID                     ngx_js_http_listener_class_id;
JSClassID                     ngx_js_connection_class_id;
ngx_js_http_listener_state_t *ngx_js_listener_reg[NGX_JS_LISTENER_REG_MAX];


/* ------------------------------------------------------------------ */
/* NginxHttpListener opaque + finalizer                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_listener_reg[] */
} ngx_js_listener_opaque_t;


static void
ngx_js_listener_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_listener_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_http_listener_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_listener_class = {
    "NginxHttpListener",
    .finalizer = ngx_js_listener_finalizer,
};


/* ------------------------------------------------------------------ */
/* NginxConnection — JS wrapper for a freshly-accepted ngx_connection_t */
/* P4: created inside ngx_js_http_accept_handler, short-lived.         */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_connection_t  *c;
    unsigned           rejected:1;
} ngx_js_conn_opaque_t;


static void
ngx_js_connection_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_conn_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_connection_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_connection_class = {
    "NginxConnection",
    .finalizer = ngx_js_connection_finalizer,
};


/* magic: 0=remoteAddr  1=remotePort */
static JSValue
ngx_js_connection_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_conn_opaque_t  *op;
    ngx_connection_t      *c;
    u_char                 buf[NGX_SOCKADDR_STRLEN];
    size_t                 len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_connection_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    c = op->c;

    switch (magic) {
    case 0: /* remoteAddr — IP only, no port */
        len = ngx_sock_ntop(c->sockaddr, c->socklen, buf, sizeof(buf), 0);
        if (len == 0) {
            return JS_NewString(ctx, "");
        }
        return JS_NewStringLen(ctx, (char *) buf, len);

    case 1: /* remotePort */
        switch (c->sockaddr->sa_family) {
        case AF_INET:
            return JS_NewInt32(ctx, ntohs(
                ((struct sockaddr_in *) c->sockaddr)->sin_port));
#if (NGX_HAVE_INET6)
        case AF_INET6:
            return JS_NewInt32(ctx, ntohs(
                ((struct sockaddr_in6 *) c->sockaddr)->sin6_port));
#endif
        default:
            return JS_NewInt32(ctx, 0);
        }
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_connection_reject(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_conn_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_connection_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rejected = 1;
    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_connection_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("remoteAddr", ngx_js_connection_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("remotePort", ngx_js_connection_get, NULL, 1),
    JS_CFUNC_DEF(        "reject",     0, ngx_js_connection_reject),
};


static JSValue
ngx_js_wrap_connection(JSContext *ctx, ngx_connection_t *c)
{
    ngx_js_conn_opaque_t  *op;
    JSValue                obj;

    op = js_mallocz(ctx, sizeof(ngx_js_conn_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->c = c;

    obj = JS_NewObjectClass(ctx, ngx_js_connection_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return obj;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* NginxHttpListener property getters                                  */
/* magic: 0=address  1=socket  2=serverNames                          */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_js_socket_state_t         *sock;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    switch (magic) {
    case 0: /* address — same as sock.address */
        if (st->socket_handle >= NGX_JS_SOCKET_REG_MAX
            || ngx_js_socket_reg[st->socket_handle] == NULL)
        {
            return JS_NewString(ctx, "");
        }
        sock = ngx_js_socket_reg[st->socket_handle];
        return JS_NewString(ctx, sock->addr);

    case 1: /* socket — NginxSocket back-reference */
        return ngx_js_socket_wrap(ctx, st->socket_handle);

    case 2: /* serverNames[] — array of server name strings */
    {
        JSValue                    arr;
        ngx_uint_t                 idx, s, n;
        ngx_http_core_srv_conf_t  *cscf;
        ngx_http_server_name_t    *sn;

        arr = JS_NewArray(ctx);
        if (JS_IsException(arr)) {
            return arr;
        }

        idx = 0;

        /* default server */
        if (st->default_server != NULL) {
            cscf = st->default_server;
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                    JS_NewStringLen(ctx, (char *) sn[n].name.data,
                                   sn[n].name.len));
            }
        }

        /* virtual servers */
        for (s = 0; s < st->nvservers; s++) {
            cscf = st->vservers[s];
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                JS_SetPropertyUint32(ctx, arr, idx++,
                    JS_NewStringLen(ctx, (char *) sn[n].name.data,
                                   sn[n].name.len));
            }
        }

        return arr;
    }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Accept hook registry — P4                                           */
/* Functions stored in global __ngx_accept_hooks__ array (GC root).   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_accept_hooks_get_registry(JSContext *ctx)
{
    JSValue  global, reg;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_accept_hooks__");

    if (JS_IsUndefined(reg)) {
        JS_FreeValue(ctx, reg);
        reg = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_accept_hooks__",
                          JS_DupValue(ctx, reg));
    }

    JS_FreeValue(ctx, global);
    return reg;
}


uint32_t
ngx_js_accept_hook_register_fn(JSContext *ctx, JSValueConst fn)
{
    JSValue   reg, lenval;
    uint32_t  idx;

    reg    = ngx_js_accept_hooks_get_registry(ctx);
    lenval = JS_GetPropertyStr(ctx, reg, "length");
    JS_ToUint32(ctx, &idx, lenval);
    JS_FreeValue(ctx, lenval);
    JS_SetPropertyUint32(ctx, reg, idx, JS_DupValue(ctx, fn));
    JS_FreeValue(ctx, reg);

    return idx;
}


static JSValue
ngx_js_accept_hook_get_fn(JSContext *ctx, uint32_t idx)
{
    JSValue  reg, fn;

    reg = ngx_js_accept_hooks_get_registry(ctx);
    fn  = JS_GetPropertyUint32(ctx, reg, idx);
    JS_FreeValue(ctx, reg);

    return fn;
}


/* ------------------------------------------------------------------ */
/* L4 filter registry — P6                                             */
/* Functions stored in global __ngx_l4_filters__ array (GC root).     */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_l4_filters_get_registry(JSContext *ctx)
{
    JSValue  global, reg;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_l4_filters__");

    if (JS_IsUndefined(reg)) {
        JS_FreeValue(ctx, reg);
        reg = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_l4_filters__",
                          JS_DupValue(ctx, reg));
    }

    JS_FreeValue(ctx, global);
    return reg;
}


uint32_t
ngx_js_l4_filter_register_fn(JSContext *ctx, JSValueConst fn)
{
    JSValue   reg, lenval;
    uint32_t  idx;

    reg    = ngx_js_l4_filters_get_registry(ctx);
    lenval = JS_GetPropertyStr(ctx, reg, "length");
    JS_ToUint32(ctx, &idx, lenval);
    JS_FreeValue(ctx, lenval);
    JS_SetPropertyUint32(ctx, reg, idx, JS_DupValue(ctx, fn));
    JS_FreeValue(ctx, reg);

    return idx;
}


static JSValue
ngx_js_l4_filter_get_fn(JSContext *ctx, uint32_t idx)
{
    JSValue  reg, fn;

    reg = ngx_js_l4_filters_get_registry(ctx);
    fn  = JS_GetPropertyUint32(ctx, reg, idx);
    JS_FreeValue(ctx, reg);

    return fn;
}


/* ------------------------------------------------------------------ */
/* L4 filter state — per-accepted-connection preread context           */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_recv_pt  orig_recv;  /* saved original c->recv     */
    u_char      *filt;       /* filtered bytes to serve     */
    size_t       filt_len;
    size_t       filt_off;   /* bytes already returned      */
} ngx_js_l4_state_t;


/* ------------------------------------------------------------------ */
/* P13 — per-connection send filter state                              */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_send_pt                    orig_send;
    ngx_send_chain_pt              orig_send_chain;
    ngx_connection_t              *c;
    ngx_js_http_listener_state_t  *st;
} ngx_js_l4_send_state_t;

/*
 * Pool cleanup used as a type tag so ngx_js_l4_send / ngx_js_l4_send_chain
 * can recover the send state by scanning c->pool->cleanup.
 * No JS values are stored here, so the handler is a no-op.
 */
static void
ngx_js_l4_send_state_cleanup(void *data)
{
    (void) data;
}

static ngx_js_l4_send_state_t *
ngx_js_l4_send_state_get(ngx_connection_t *c)
{
    ngx_pool_cleanup_t  *cln;

    for (cln = c->pool->cleanup; cln != NULL; cln = cln->next) {
        if (cln->handler == ngx_js_l4_send_state_cleanup) {
            return (ngx_js_l4_send_state_t *) cln->data;
        }
    }
    return NULL;
}


/*
 * Run all registered L4 send filters on outbound bytes.
 * Uses fresh generator instances (per-call model): deliver chunk → EOF → done.
 * Sync-only: if a generator step is pending (await inside filter), log a
 * warning and pass bytes through unchanged.
 *
 * On success, *out / *out_len contain the filtered result (pool-allocated if
 * different from raw, or raw pointer reused if all filters pass through).
 * Returns NGX_OK or NGX_ERROR.
 */
static ngx_int_t
ngx_js_l4_run_send_filters(ngx_connection_t *c,
    ngx_js_http_listener_state_t *st,
    JSContext *ctx, JSRuntime *rt,
    const u_char *raw, size_t raw_len,
    u_char **out, size_t *out_len)
{
    JSValue     global, ctor, make_fn, source, deliver_fn, fn;
    JSValue     ab, ta, gen, next_fn, gen_result, iter_result;
    JSValue     done_v, value_v, args[2];
    JSContext  *job_ctx;
    ngx_uint_t  fi;
    u_char     *cur_buf, *filt_buf, *new_filt;
    size_t      cur_len, filt_len, filt_cap, ylen, extra;
    int         done;
    const char *cs;
    size_t      slen;

    cur_buf = (u_char *) raw;
    cur_len = raw_len;

    global  = JS_GetGlobalObject(ctx);
    ctor    = JS_GetPropertyStr(ctx, global, "Uint8Array");
    make_fn = JS_GetPropertyStr(ctx, global, "__ngx_l4_make_source__");
    JS_FreeValue(ctx, global);

    if (JS_IsUndefined(make_fn) || JS_IsException(make_fn)) {
        JS_FreeValue(ctx, make_fn);
        JS_FreeValue(ctx, ctor);
        *out     = cur_buf;
        *out_len = cur_len;
        return NGX_OK;
    }

    for (fi = 0; fi < st->n_l4_send_filters; fi++) {

        /* Create fresh source for this filter invocation */
        source = JS_Call(ctx, make_fn, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(source)) {
            ngx_js_log_exception(ctx, c->log);
            continue;
        }

        deliver_fn = JS_GetPropertyStr(ctx, source, "_deliver");

        fn  = ngx_js_l4_filter_get_fn(ctx, st->l4_send_filters[fi]);
        gen = JS_Call(ctx, fn, JS_UNDEFINED, 1, &source);
        JS_FreeValue(ctx, fn);

        if (JS_IsException(gen)) {
            ngx_js_log_exception(ctx, c->log);
            JS_FreeValue(ctx, deliver_fn);
            JS_FreeValue(ctx, source);
            continue;
        }

        next_fn    = JS_GetPropertyStr(ctx, gen, "next");
        gen_result = JS_Call(ctx, next_fn, gen, 0, NULL);
        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

        if (JS_IsException(gen_result)) {
            ngx_js_log_exception(ctx, c->log);
            JS_FreeValue(ctx, next_fn);
            JS_FreeValue(ctx, gen);
            JS_FreeValue(ctx, deliver_fn);
            JS_FreeValue(ctx, source);
            continue;
        }

        /* Deliver the outbound chunk */
        ab = JS_NewArrayBufferCopy(ctx, cur_buf, cur_len);
        if (!JS_IsException(ab)) {
            ta = JS_CallConstructor(ctx, ctor, 1, &ab);
            JS_FreeValue(ctx, ab);
        } else {
            ta = JS_EXCEPTION;
        }

        if (!JS_IsException(ta)) {
            args[0] = ta;
            args[1] = JS_FALSE;
            JS_Call(ctx, deliver_fn, source, 2, args);
            JS_FreeValue(ctx, ta);
        }
        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

        /* Collect yielded output synchronously.
         * EOF is delivered lazily inside the loop: after the generator yields
         * its first value it loops back to source.next(), making gen_result
         * PENDING again.  Only then does _deliver(undefined, true) have a
         * live _res to resolve.  Delivering EOF before the collect loop is a
         * no-op because _res is undefined while the generator is suspended at
         * the yield statement rather than at the for-await call. */
        filt_cap = (cur_len < 4096) ? 4096 : cur_len + 256;
        filt_buf = ngx_palloc(c->pool, filt_cap);
        filt_len = 0;

        if (filt_buf == NULL) {
            JS_FreeValue(ctx, gen_result);
            JS_FreeValue(ctx, next_fn);
            JS_FreeValue(ctx, gen);
            JS_FreeValue(ctx, deliver_fn);
            JS_FreeValue(ctx, source);
            break;
        }

        {
        int  eof_delivered = 0;
        for (;;) {
            while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

            if (JS_PromiseState(ctx, gen_result) == JS_PROMISE_PENDING) {
                if (!eof_delivered) {
                    /*
                     * Generator has looped back to source.next() (or is still
                     * waiting for the first chunk because deliver failed).
                     * Signal EOF so the for-await terminates.
                     */
                    args[0] = JS_UNDEFINED;
                    args[1] = JS_TRUE;
                    JS_Call(ctx, deliver_fn, source, 2, args);
                    eof_delivered = 1;
                    while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}
                    continue;
                }
                ngx_log_error(NGX_LOG_WARN, c->log, 0,
                              "js l4 send filter: await not supported;"
                              " passing bytes through unchanged");
                filt_len = 0;   /* signal passthrough */
                break;
            }

            if (JS_PromiseState(ctx, gen_result) == JS_PROMISE_REJECTED) {
                JSValue  reason, str;
                reason = JS_PromiseResult(ctx, gen_result);
                str    = JS_ToString(ctx, reason);
                cs     = JS_ToCString(ctx, str);
                if (cs) {
                    ngx_log_error(NGX_LOG_ERR, c->log, 0,
                                  "js l4 send filter: rejected: %s", cs);
                    JS_FreeCString(ctx, cs);
                }
                JS_FreeValue(ctx, str);
                JS_FreeValue(ctx, reason);
                filt_len = 0;
                break;
            }

            iter_result = JS_PromiseResult(ctx, gen_result);
            JS_FreeValue(ctx, gen_result);
            gen_result = JS_UNDEFINED;

            done_v = JS_GetPropertyStr(ctx, iter_result, "done");
            done   = JS_ToBool(ctx, done_v);
            JS_FreeValue(ctx, done_v);

            if (done) {
                JS_FreeValue(ctx, iter_result);
                break;
            }

            value_v = JS_GetPropertyStr(ctx, iter_result, "value");
            JS_FreeValue(ctx, iter_result);

            if (!JS_IsUndefined(value_v) && !JS_IsNull(value_v)) {
                JSValue   vab;
                size_t    boff, blen, bpe, ab_len;
                uint8_t  *vptr = NULL;

                vab = JS_GetTypedArrayBuffer(ctx, value_v,
                                             &boff, &blen, &bpe);
                if (!JS_IsException(vab)) {
                    vptr = JS_GetArrayBuffer(ctx, &ab_len, vab);
                    if (vptr) {
                        ylen = blen;
                        if (filt_len + ylen > filt_cap) {
                            extra    = filt_len + ylen - filt_cap + 256;
                            new_filt = ngx_palloc(c->pool, filt_cap + extra);
                            if (new_filt) {
                                ngx_memcpy(new_filt, filt_buf, filt_len);
                                filt_buf  = new_filt;
                                filt_cap += extra;
                            }
                        }
                        if (filt_len + ylen <= filt_cap) {
                            ngx_memcpy(filt_buf + filt_len,
                                       vptr + boff, ylen);
                            filt_len += ylen;
                        }
                    }
                    JS_FreeValue(ctx, vab);
                } else {
                    JS_FreeValue(ctx, JS_GetException(ctx));
                }

                if (vptr == NULL) {
                    cs = JS_ToCStringLen(ctx, &slen, value_v);
                    if (cs && slen > 0) {
                        if (filt_len + slen > filt_cap) {
                            extra    = filt_len + slen - filt_cap + 256;
                            new_filt = ngx_palloc(c->pool, filt_cap + extra);
                            if (new_filt) {
                                ngx_memcpy(new_filt, filt_buf, filt_len);
                                filt_buf  = new_filt;
                                filt_cap += extra;
                            }
                        }
                        if (filt_len + slen <= filt_cap) {
                            ngx_memcpy(filt_buf + filt_len, cs, slen);
                            filt_len += slen;
                        }
                        JS_FreeCString(ctx, cs);
                    }
                }
            }

            JS_FreeValue(ctx, value_v);

            gen_result = JS_Call(ctx, next_fn, gen, 0, NULL);
            while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}
        }
        } /* end eof_delivered scope */

        JS_FreeValue(ctx, gen_result);
        JS_FreeValue(ctx, next_fn);
        JS_FreeValue(ctx, gen);
        JS_FreeValue(ctx, deliver_fn);
        JS_FreeValue(ctx, source);

        if (filt_len > 0) {
            cur_buf = filt_buf;
            cur_len = filt_len;
        }
        /* else: passthrough / error — keep cur_buf/cur_len unchanged */
    }

    JS_FreeValue(ctx, make_fn);
    JS_FreeValue(ctx, ctor);

    *out     = cur_buf;
    *out_len = cur_len;
    return NGX_OK;
}


/*
 * Replacement c->send: runs outbound bytes through L4 send filters
 * then writes to the real socket via orig_send.
 */
static ssize_t
ngx_js_l4_send(ngx_connection_t *c, u_char *buf, size_t size)
{
    ngx_js_l4_send_state_t  *ss;
    ngx_js_conf_t           *jcf;
    JSContext               *ctx;
    JSRuntime               *rt;
    u_char                  *out;
    size_t                   out_len;

    ss = ngx_js_l4_send_state_get(c);
    if (ss == NULL) {
        /* Should not happen; fall through to real send */
        return c->send(c, buf, size);
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return ss->orig_send(c, buf, size);
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);

    if (ngx_js_l4_run_send_filters(c, ss->st, ctx, rt,
                                    buf, size, &out, &out_len) != NGX_OK)
    {
        return ss->orig_send(c, buf, size);
    }

    return ss->orig_send(c, out, out_len);
}


/*
 * Replacement c->send_chain: flattens the in-memory chain, runs it through
 * L4 send filters, then writes the result via orig_send.
 * File buffers are not filtered — fall back to orig_send_chain unchanged.
 * Returns NULL (all consumed) on success; remaining chain on partial write.
 */
static ngx_chain_t *
ngx_js_l4_send_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{
    ngx_js_l4_send_state_t  *ss;
    ngx_js_conf_t           *jcf;
    JSContext               *ctx;
    JSRuntime               *rt;
    ngx_chain_t             *cl;
    ngx_buf_t               *b;
    u_char                  *flat, *p, *out;
    size_t                   total, blen, out_len;
    off_t                    acc;
    ssize_t                  n;

    ss = ngx_js_l4_send_state_get(c);
    if (ss == NULL) {
        /* No state — should not happen; call orig directly */
        return in;
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return ss->orig_send_chain(c, in, limit);
    }

    /* Check for file buffers — don't filter those */
    for (cl = in; cl != NULL; cl = cl->next) {
        if (cl->buf->in_file) {
            return ss->orig_send_chain(c, in, limit);
        }
    }

    /* Measure total bytes (respect limit) */
    total = 0;
    acc   = 0;
    for (cl = in; cl != NULL; cl = cl->next) {
        blen   = (size_t) (cl->buf->last - cl->buf->pos);
        total += blen;
        acc   += (off_t) blen;
        if (limit > 0 && acc >= limit) {
            break;
        }
    }

    if (total == 0) {
        return ss->orig_send_chain(c, in, limit);
    }

    /* Flatten to contiguous buffer */
    flat = ngx_palloc(c->pool, total);
    if (flat == NULL) {
        return ss->orig_send_chain(c, in, limit);
    }

    p   = flat;
    acc = 0;
    for (cl = in; cl != NULL; cl = cl->next) {
        b    = cl->buf;
        blen = (size_t) (b->last - b->pos);
        ngx_memcpy(p, b->pos, blen);
        p   += blen;
        acc += (off_t) blen;
        if (limit > 0 && acc >= limit) {
            break;
        }
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);

    if (ngx_js_l4_run_send_filters(c, ss->st, ctx, rt,
                                    flat, total, &out, &out_len) != NGX_OK)
    {
        out     = flat;
        out_len = total;
    }

    /* Write filtered output; mark original chain buffers as consumed */
    p = out;
    while (out_len > 0) {
        n = ss->orig_send(c, p, out_len);
        if (n <= 0) {
            break;
        }
        p       += (size_t) n;
        out_len -= (size_t) n;
    }

    /* Advance input chain: mark consumed buffers */
    acc = 0;
    for (cl = in; cl != NULL; cl = cl->next) {
        b    = cl->buf;
        blen = (size_t) (b->last - b->pos);
        b->pos = b->last;  /* consumed */
        acc   += (off_t) blen;
        if (limit > 0 && acc >= limit) {
            break;
        }
    }

    /* Return remaining (unsent) chain — NULL if everything consumed */
    for (cl = in; cl != NULL; cl = cl->next) {
        if (cl->buf->pos < cl->buf->last) {
            return cl;
        }
    }
    return NULL;
}


/*
 * Custom recv: serves pre-filtered bytes first, then restores the
 * original recv for subsequent socket reads.  Stored pointer lives
 * in c->buffer->tag so it survives ngx_http_init_connection.
 */
static ssize_t
ngx_js_l4_recv(ngx_connection_t *c, u_char *buf, size_t size)
{
    ngx_js_l4_state_t  *lctx;
    size_t              avail, n;

    lctx  = (ngx_js_l4_state_t *) c->buffer->tag;
    avail = lctx->filt_len - lctx->filt_off;

    if (avail == 0) {
        c->recv = lctx->orig_recv;
        return lctx->orig_recv(c, buf, size);
    }

    n = (size < avail) ? size : avail;
    ngx_memcpy(buf, lctx->filt + lctx->filt_off, n);
    lctx->filt_off += n;

    if (lctx->filt_off >= lctx->filt_len) {
        /* All pre-filtered bytes served; next call goes to real socket */
        c->recv = lctx->orig_recv;
    }

    return (ssize_t) n;
}


/* ------------------------------------------------------------------ */
/* P12 — per-connection async generator state                          */
/* ------------------------------------------------------------------ */

/*
 * P12 — per-connection async generator state.
 * Allocated in c->pool; lives until ngx_http_init_connection or connection
 * close.  Stored in c->data during the L4 filter phase.
 */
struct ngx_js_l4_pending_s {
    ngx_connection_t              *c;
    ngx_js_http_listener_state_t  *st;
    /* Active async generator */
    JSValue                        gen;
    JSValue                        next_fn;
    JSValue                        gen_result;  /* pending gen.next() Promise */
    /* Async iterable source */
    JSValue                        source_obj;
    JSValue                        deliver_fn;  /* source._deliver(val,done) */
    /* Current filter index */
    ngx_uint_t                     fi;
    /* Accumulated output */
    u_char                        *out_buf;
    size_t                         out_len;
    size_t                         out_cap;
    /* Intrusive list for worker async-pending tracking */
    struct ngx_js_l4_pending_s    *next;
};


static void
ngx_js_l4_pending_free_jsvals(JSContext *ctx, ngx_js_l4_pending_t *p)
{
    JS_FreeValue(ctx, p->gen);
    JS_FreeValue(ctx, p->next_fn);
    JS_FreeValue(ctx, p->gen_result);
    JS_FreeValue(ctx, p->source_obj);
    JS_FreeValue(ctx, p->deliver_fn);
    p->gen = p->next_fn = p->gen_result = p->source_obj = p->deliver_fn
           = JS_UNDEFINED;
}


/* Append bytes to pending->out_buf (pool-allocated, doubles on overflow). */
static ngx_int_t
ngx_js_l4_out_append_bytes(ngx_connection_t *c, ngx_js_l4_pending_t *p,
    const u_char *data, size_t len)
{
    u_char  *new_buf;
    size_t   new_cap;

    if (len == 0) {
        return NGX_OK;
    }

    if (p->out_len + len > p->out_cap) {
        new_cap = (p->out_cap == 0) ? 4096 : p->out_cap * 2;
        if (new_cap < p->out_len + len) {
            new_cap = p->out_len + len + 256;
        }
        new_buf = ngx_palloc(c->pool, new_cap);
        if (new_buf == NULL) {
            return NGX_ERROR;
        }
        if (p->out_len > 0) {
            ngx_memcpy(new_buf, p->out_buf, p->out_len);
        }
        p->out_buf = new_buf;
        p->out_cap = new_cap;
    }

    ngx_memcpy(p->out_buf + p->out_len, data, len);
    p->out_len += len;

    return NGX_OK;
}


/* Append a JS yield value (TypedArray or string) to out_buf. */
static ngx_int_t
ngx_js_l4_out_append_jsval(JSContext *ctx, ngx_js_l4_pending_t *p,
    JSValue value_v)
{
    JSValue        vab;
    size_t         byte_offset, byte_length, bpe, ab_len;
    const uint8_t *vptr;
    const char    *cs;
    size_t         slen;
    ngx_int_t      rc;

    if (JS_IsUndefined(value_v) || JS_IsNull(value_v)) {
        return NGX_OK;
    }

    /* Try TypedArray first */
    vab = JS_GetTypedArrayBuffer(ctx, value_v, &byte_offset, &byte_length, &bpe);
    if (!JS_IsException(vab)) {
        vptr = JS_GetArrayBuffer(ctx, &ab_len, vab);
        JS_FreeValue(ctx, vab);
        if (vptr) {
            return ngx_js_l4_out_append_bytes(p->c, p,
                                              vptr + byte_offset, byte_length);
        }
    } else {
        JS_FreeValue(ctx, JS_GetException(ctx));
    }

    /* String fallback */
    cs = JS_ToCStringLen(ctx, &slen, value_v);
    if (cs) {
        rc = ngx_js_l4_out_append_bytes(p->c, p, (const u_char *) cs, slen);
        JS_FreeCString(ctx, cs);
        return rc;
    }

    return NGX_OK;
}


/* Forward declaration for mutual recursion */
static ngx_int_t ngx_js_l4_chain_next_filter(JSContext *ctx, JSRuntime *rt,
    ngx_js_l4_pending_t *p);


/*
 * Drive the current filter generator, collecting yielded output into
 * pending->out_buf.  Returns:
 *   NGX_OK    — generator done (filter fi complete; caller handles chain
 *               advance)
 *   NGX_AGAIN — waiting for next TCP chunk from source
 *   NGX_DONE  — awaiting an async op; caller adds to w->l4_pending
 *   NGX_ERROR — exception or rejection
 */
static ngx_int_t
ngx_js_l4_collect(JSContext *ctx, JSRuntime *rt, ngx_js_l4_pending_t *p)
{
    JSContext   *job_ctx;
    JSValue      iter_result, done_v, value_v, waiting_v, reason_v, str_v;
    const char  *cs;
    int          done;
    ngx_int_t    rc;

    for (;;) {

        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

        switch (JS_PromiseState(ctx, p->gen_result)) {

        case JS_PROMISE_PENDING:
            waiting_v = JS_GetPropertyStr(ctx, p->source_obj, "_isWaiting");
            if (JS_ToBool(ctx, waiting_v)) {
                JS_FreeValue(ctx, waiting_v);
                return NGX_AGAIN;  /* needs more TCP data */
            }
            JS_FreeValue(ctx, waiting_v);
            return NGX_DONE;  /* async op in progress */

        case JS_PROMISE_REJECTED:
            reason_v = JS_PromiseResult(ctx, p->gen_result);
            str_v    = JS_ToString(ctx, reason_v);
            cs       = JS_ToCString(ctx, str_v);
            if (cs) {
                ngx_log_error(NGX_LOG_ERR, p->c->log, 0,
                              "js l4 filter: generator rejected: %s", cs);
                JS_FreeCString(ctx, cs);
            }
            JS_FreeValue(ctx, str_v);
            JS_FreeValue(ctx, reason_v);
            return NGX_ERROR;

        case JS_PROMISE_FULFILLED:
        default:
            break;
        }

        iter_result = JS_PromiseResult(ctx, p->gen_result);
        JS_FreeValue(ctx, p->gen_result);
        p->gen_result = JS_UNDEFINED;

        done_v = JS_GetPropertyStr(ctx, iter_result, "done");
        done   = JS_ToBool(ctx, done_v);
        JS_FreeValue(ctx, done_v);

        if (done) {
            JS_FreeValue(ctx, iter_result);
            return NGX_OK;
        }

        value_v = JS_GetPropertyStr(ctx, iter_result, "value");
        JS_FreeValue(ctx, iter_result);

        rc = ngx_js_l4_out_append_jsval(ctx, p, value_v);
        JS_FreeValue(ctx, value_v);

        if (rc != NGX_OK) {
            return NGX_ERROR;
        }

        /* Advance generator */
        p->gen_result = JS_Call(ctx, p->next_fn, p->gen, 0, NULL);
        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

        if (JS_IsException(p->gen_result)) {
            ngx_js_log_exception(ctx, p->c->log);
            p->gen_result = JS_UNDEFINED;
            return NGX_ERROR;
        }
    }
}


/*
 * Start filter fi: create source, call fn(source), call gen.next() once to
 * prime the generator until it suspends at "for await (const chunk of source)".
 * On success, pending->gen_result is a pending Promise
 * (source._isWaiting==true).
 */
static ngx_int_t
ngx_js_l4_start_filter(JSContext *ctx, JSRuntime *rt,
    ngx_js_l4_pending_t *p)
{
    JSValue     global, make_src_fn, source, deliver_fn, fn;
    JSValue     gen, next_fn, gen_result;
    JSContext  *job_ctx;

    global      = JS_GetGlobalObject(ctx);
    make_src_fn = JS_GetPropertyStr(ctx, global, "__ngx_l4_make_source__");
    JS_FreeValue(ctx, global);

    if (JS_IsUndefined(make_src_fn) || JS_IsException(make_src_fn)) {
        JS_FreeValue(ctx, make_src_fn);
        ngx_log_error(NGX_LOG_ERR, p->c->log, 0,
                      "js l4: __ngx_l4_make_source__ not installed");
        return NGX_ERROR;
    }

    source = JS_Call(ctx, make_src_fn, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, make_src_fn);

    if (JS_IsException(source)) {
        ngx_js_log_exception(ctx, p->c->log);
        return NGX_ERROR;
    }

    deliver_fn = JS_GetPropertyStr(ctx, source, "_deliver");

    fn  = ngx_js_l4_filter_get_fn(ctx, p->st->l4_filters[p->fi]);
    gen = JS_Call(ctx, fn, JS_UNDEFINED, 1, &source);
    JS_FreeValue(ctx, fn);

    if (JS_IsException(gen)) {
        ngx_js_log_exception(ctx, p->c->log);
        JS_FreeValue(ctx, deliver_fn);
        JS_FreeValue(ctx, source);
        return NGX_ERROR;
    }

    next_fn    = JS_GetPropertyStr(ctx, gen, "next");
    gen_result = JS_Call(ctx, next_fn, gen, 0, NULL);
    while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

    if (JS_IsException(gen_result)) {
        ngx_js_log_exception(ctx, p->c->log);
        JS_FreeValue(ctx, next_fn);
        JS_FreeValue(ctx, gen);
        JS_FreeValue(ctx, deliver_fn);
        JS_FreeValue(ctx, source);
        return NGX_ERROR;
    }

    /* Free previous filter's JS values if any */
    JS_FreeValue(ctx, p->gen);
    JS_FreeValue(ctx, p->next_fn);
    JS_FreeValue(ctx, p->gen_result);
    JS_FreeValue(ctx, p->source_obj);
    JS_FreeValue(ctx, p->deliver_fn);

    p->gen        = gen;
    p->next_fn    = next_fn;
    p->gen_result = gen_result;
    p->source_obj = source;
    p->deliver_fn = deliver_fn;

    return NGX_OK;
}


/*
 * Deliver raw bytes to the current filter's source, then collect yields.
 * Returns NGX_OK (filter done), NGX_AGAIN (needs more data),
 * NGX_DONE (async parked), NGX_ERROR.
 */
static ngx_int_t
ngx_js_l4_deliver_bytes(JSContext *ctx, JSRuntime *rt,
    ngx_js_l4_pending_t *p, const u_char *raw, size_t len)
{
    JSValue     global, ctor, ab, ta, args[2];
    JSContext  *job_ctx;
    ngx_int_t   rc;

    /* Build Uint8Array from raw bytes */
    global = JS_GetGlobalObject(ctx);
    ctor   = JS_GetPropertyStr(ctx, global, "Uint8Array");
    JS_FreeValue(ctx, global);

    ab = JS_NewArrayBufferCopy(ctx, raw, len);
    if (JS_IsException(ab)) {
        JS_FreeValue(ctx, ctor);
        return NGX_ERROR;
    }

    ta = JS_CallConstructor(ctx, ctor, 1, &ab);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, ab);

    if (JS_IsException(ta)) {
        return NGX_ERROR;
    }

    args[0] = ta;
    args[1] = JS_FALSE;
    JS_Call(ctx, p->deliver_fn, p->source_obj, 2, args);
    JS_FreeValue(ctx, ta);

    while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

    rc = ngx_js_l4_collect(ctx, rt, p);

    if (rc != NGX_OK) {
        return rc;
    }

    /* Filter fi done — advance to next filter */
    p->fi++;
    if (p->fi < p->st->n_l4_filters) {
        return ngx_js_l4_chain_next_filter(ctx, rt, p);
    }

    return NGX_OK;
}


/*
 * Start filter p->fi (already incremented) and deliver the accumulated
 * output of the previous filter as a single chunk, then signal EOF.
 * Chained filters run synchronously (no async suspension expected).
 */
static ngx_int_t
ngx_js_l4_chain_next_filter(JSContext *ctx, JSRuntime *rt,
    ngx_js_l4_pending_t *p)
{
    u_char    *prev_buf;
    size_t     prev_len;
    JSValue    args[2];
    JSContext *job_ctx;
    ngx_int_t  rc;

    /* Save previous filter's output — it's our input */
    prev_buf = p->out_buf;
    prev_len = p->out_len;
    p->out_buf = NULL;
    p->out_len = 0;
    p->out_cap = 0;

    if (ngx_js_l4_start_filter(ctx, rt, p) != NGX_OK) {
        return NGX_ERROR;
    }

    if (prev_len > 0) {
        rc = ngx_js_l4_deliver_bytes(ctx, rt, p, prev_buf, prev_len);
    } else {
        /* Deliver EOF to an empty stream */
        args[0] = JS_UNDEFINED;
        args[1] = JS_TRUE;
        JS_Call(ctx, p->deliver_fn, p->source_obj, 2, args);
        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}
        rc = ngx_js_l4_collect(ctx, rt, p);
        if (rc == NGX_OK) {
            p->fi++;
            if (p->fi < p->st->n_l4_filters) {
                return ngx_js_l4_chain_next_filter(ctx, rt, p);
            }
        }
    }

    return rc;
}


/*
 * All filters done: install the recv shim so HTTP sees filtered bytes,
 * then prepare the connection for ngx_http_init_connection.
 * Returns NGX_OK on success or NGX_ERROR on alloc failure (connection
 * already closed in that case).
 */
static ngx_int_t
ngx_js_l4_finish(ngx_connection_t *c, ngx_js_l4_pending_t *p)
{
    ngx_js_l4_state_t  *lctx;
    ngx_buf_t          *b;

    lctx = ngx_palloc(c->pool, sizeof(ngx_js_l4_state_t));
    if (lctx == NULL) {
        ngx_close_connection(c);
        return NGX_ERROR;
    }

    lctx->orig_recv = c->recv;
    lctx->filt      = p->out_buf;
    lctx->filt_len  = p->out_len;
    lctx->filt_off  = 0;

    b = ngx_create_temp_buf(c->pool, p->out_len + 8192);
    if (b == NULL) {
        ngx_close_connection(c);
        return NGX_ERROR;
    }

    b->tag    = (ngx_buf_tag_t) lctx;
    c->buffer = b;
    c->recv   = ngx_js_l4_recv;
    c->read->ready = 1;
    c->data   = NULL;

    return NGX_OK;
}


/*
 * Read event handler used when the L4 filter needs raw bytes before
 * ngx_http_init_connection.  Fires on each readable event after accept.
 */
static void
ngx_js_l4_read_handler(ngx_event_t *rev)
{
    ngx_connection_t              *c;
    ngx_js_http_listener_state_t  *st;
    ngx_js_conf_t                 *jcf;
    ngx_js_worker_t               *w;
    ngx_js_l4_pending_t           *p;
    JSContext                     *ctx;
    JSRuntime                     *rt;
    u_char                         raw[8192];
    ssize_t                        n;
    ngx_int_t                      rc;

    c  = rev->data;

    /* Recover listener state (same trick as the accept handler) */
    st = (ngx_js_http_listener_state_t *)
             ((u_char *) c->listening->servers
              - offsetof(ngx_js_http_listener_state_t, port));

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        ngx_http_init_connection(c);
        return;
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);
    w   = JS_GetContextOpaque(ctx);

    n = c->recv(c, raw, sizeof(raw));

    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_close_connection(c);
        }
        return;
    }

    if (n <= 0) {
        p = (ngx_js_l4_pending_t *) c->data;
        if (p != NULL) {
            ngx_js_l4_pending_free_jsvals(ctx, p);
        }
        ngx_close_connection(c);
        return;
    }

    p = (ngx_js_l4_pending_t *) c->data;

    if (p == NULL || JS_IsUndefined(p->gen)) {
        /* First recv: allocate pending (P6) or start pre-allocated (P17) */
        if (p == NULL) {
            p = ngx_pcalloc(c->pool, sizeof(ngx_js_l4_pending_t));
            if (p == NULL) {
                ngx_close_connection(c);
                return;
            }
            p->c          = c;
            p->st         = st;
            p->fi         = 0;
            p->gen        = JS_UNDEFINED;
            p->next_fn    = JS_UNDEFINED;
            p->gen_result = JS_UNDEFINED;
            p->source_obj = JS_UNDEFINED;
            p->deliver_fn = JS_UNDEFINED;
            c->data       = p;
        }

        if (ngx_js_l4_start_filter(ctx, rt, p) != NGX_OK) {
            ngx_js_l4_pending_free_jsvals(ctx, p);
            ngx_close_connection(c);
            return;
        }
    }

    rc = ngx_js_l4_deliver_bytes(ctx, rt, p, raw, (size_t) n);

    switch (rc) {

    case NGX_OK:
        ngx_js_l4_pending_free_jsvals(ctx, p);
        if (ngx_js_l4_finish(c, p) != NGX_OK) {
            return;  /* connection already closed */
        }
        ngx_http_init_connection(c);
        break;

    case NGX_AGAIN:
        /* Generator waiting for more TCP data — re-arm read event */
        if (ngx_handle_read_event(rev, 0) != NGX_OK) {
            ngx_js_l4_pending_free_jsvals(ctx, p);
            ngx_close_connection(c);
        }
        break;

    case NGX_DONE:
        /* Generator awaiting async op — disable read, add to worker list */
        if (w == NULL) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "js l4: NGX_DONE in master process context");
            ngx_js_l4_pending_free_jsvals(ctx, p);
            ngx_close_connection(c);
            return;
        }
        if (ngx_del_event(c->read, NGX_READ_EVENT, 0) != NGX_OK) {
            ngx_js_l4_pending_free_jsvals(ctx, p);
            ngx_close_connection(c);
            return;
        }
        p->next       = w->l4_pending;
        w->l4_pending = p;
        break;

    default:
        ngx_js_l4_pending_free_jsvals(ctx, p);
        ngx_close_connection(c);
        break;
    }
}


void
ngx_js_l4_async_check(ngx_js_worker_t *w)
{
    ngx_js_l4_pending_t  *p, **pp;
    JSContext            *ctx;
    JSRuntime            *rt;
    JSContext            *job_ctx;
    ngx_int_t             rc;

    ctx = w->ctx;
    rt  = JS_GetRuntime(ctx);
    pp  = &w->l4_pending;

    while (*pp != NULL) {
        p = *pp;

        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) {}

        switch (JS_PromiseState(ctx, p->gen_result)) {

        case JS_PROMISE_PENDING:
            pp = &p->next;
            continue;

        case JS_PROMISE_FULFILLED:
        case JS_PROMISE_REJECTED:
        default:
            *pp = p->next;
            rc = ngx_js_l4_collect(ctx, rt, p);

            if (rc == NGX_OK) {
                /* Generator done — advance filter chain */
                p->fi++;
                if (p->fi < p->st->n_l4_filters) {
                    rc = ngx_js_l4_chain_next_filter(ctx, rt, p);
                }
            }

            if (rc == NGX_OK) {
                ngx_js_l4_pending_free_jsvals(ctx, p);
                if (ngx_js_l4_finish(p->c, p) != NGX_OK) {
                    /* connection already closed */
                } else {
                    ngx_http_init_connection(p->c);
                }
            } else if (rc == NGX_AGAIN) {
                /* Waiting for TCP data — re-enable read event */
                if (ngx_add_event(p->c->read, NGX_READ_EVENT, 0) != NGX_OK) {
                    ngx_js_l4_pending_free_jsvals(ctx, p);
                    ngx_close_connection(p->c);
                }
            } else if (rc == NGX_DONE) {
                /* Still async-parked — re-add to list */
                p->next       = w->l4_pending;
                w->l4_pending = p;
            } else {
                ngx_js_l4_pending_free_jsvals(ctx, p);
                ngx_close_connection(p->c);
            }
            break;
        }
    }
}


ngx_int_t
ngx_js_l4_install_source_factory(JSContext *ctx)
{
    static const char  script[] =
        "(function(){"
        "  globalThis.__ngx_l4_make_source__ = function(){"
        "    var _res, _waiting = false;"
        "    return {"
        "      [Symbol.asyncIterator](){ return this; },"
        "      next(){"
        "        _waiting = true;"
        "        return new Promise(function(r){ _res = r; });"
        "      },"
        "      _deliver: function(val, done){"
        "        if (_res) {"
        "          _waiting = false;"
        "          var r = _res; _res = undefined;"
        "          r({value: val, done: !!done});"
        "        }"
        "      },"
        "      get _isWaiting(){ return _waiting; }"
        "    };"
        "  };"
        "})();";
    JSValue  ret;

    ret = JS_Eval(ctx, script, sizeof(script) - 1,
                  "<l4-source-factory>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, ret);
        return NGX_ERROR;
    }
    JS_FreeValue(ctx, ret);
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* C accept handler — replaces ngx_http_init_connection for JS        */
/* listeners that have at least one accept hook registered.            */
/* ------------------------------------------------------------------ */

/*
 * Core accept-handler logic shared between JS-created listeners (P4/P6)
 * and standard nginx listen sockets (P17).
 *
 * Runs accept hooks from st->accept_handlers, then installs L4 send/recv
 * filters from st->l4_send_filters / st->l4_filters, then calls
 * ngx_http_init_connection(c) to hand off to the HTTP pipeline.
 */
static void
ngx_js_run_accept_handler(ngx_connection_t *c,
    ngx_js_http_listener_state_t *st)
{
    ngx_js_conf_t                 *jcf;
    ngx_js_conn_opaque_t          *op;
    JSContext                     *ctx;
    JSRuntime                     *rt;
    JSValue                        conn_obj, fn, ret;
    uint32_t                      *indices;
    ngx_uint_t                     i;

    /*
     * Get the JS context.  jcf->ctx is COW-shared; in worker processes
     * its opaque points to the current ngx_js_worker_t.
     */
    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        ngx_http_init_connection(c);
        return;
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);

    /* Wrap the raw connection */
    conn_obj = ngx_js_wrap_connection(ctx, c);
    if (JS_IsException(conn_obj)) {
        ngx_log_error(NGX_LOG_ERR, c->log, 0,
                      "js accept hook: failed to allocate NginxConnection");
        ngx_http_init_connection(c);
        return;
    }

    op      = JS_GetOpaque(conn_obj, ngx_js_connection_class_id);
    indices = st->accept_handlers;

    for (i = 0; i < st->n_accept_handlers; i++) {
        fn  = ngx_js_accept_hook_get_fn(ctx, indices[i]);
        ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &conn_obj);
        JS_FreeValue(ctx, fn);

        if (JS_IsException(ret)) {
            ngx_js_log_exception(ctx, c->log);
            JS_FreeValue(ctx, ret);
            op->rejected = 1;
            break;
        }

        JS_FreeValue(ctx, ret);

        /* Drain microtasks */
        while (JS_ExecutePendingJob(rt, NULL) > 0) { /* empty */ }

        if (op->rejected) {
            break;
        }
    }

    if (op->rejected) {
        JS_FreeValue(ctx, conn_obj);
        ngx_close_connection(c);
        return;
    }

    JS_FreeValue(ctx, conn_obj);

    /* P13: install send filter hooks before HTTP init */
    if (st->n_l4_send_filters > 0) {
        ngx_pool_cleanup_t      *cln;
        ngx_js_l4_send_state_t  *ss;

        cln = ngx_pool_cleanup_add(c->pool, sizeof(ngx_js_l4_send_state_t));
        if (cln == NULL) {
            ngx_close_connection(c);
            return;
        }

        ss               = cln->data;
        ss->orig_send       = c->send;
        ss->orig_send_chain = c->send_chain;
        ss->c               = c;
        ss->st              = st;
        cln->handler        = ngx_js_l4_send_state_cleanup;

        c->send       = ngx_js_l4_send;
        c->send_chain = ngx_js_l4_send_chain;
    }

    /* P6/P12/P17: L4 inbound filter — intercept raw bytes before HTTP init */
    if (st->n_l4_filters > 0) {
        ngx_js_l4_pending_t  *p;

        /*
         * Pre-allocate the pending struct so that ngx_js_l4_read_handler
         * can find the correct st pointer even for P17 standard sockets
         * (where the offsetof trick on c->listening->servers does not work).
         * The gen field is left JS_UNDEFINED so that ngx_js_l4_read_handler
         * knows to call ngx_js_l4_start_filter on the first data event.
         */
        p = ngx_pcalloc(c->pool, sizeof(ngx_js_l4_pending_t));
        if (p == NULL) {
            ngx_close_connection(c);
            return;
        }
        p->c          = c;
        p->st         = st;
        p->fi         = 0;
        p->gen        = JS_UNDEFINED;
        p->next_fn    = JS_UNDEFINED;
        p->gen_result = JS_UNDEFINED;
        p->source_obj = JS_UNDEFINED;
        p->deliver_fn = JS_UNDEFINED;
        c->data       = p;

        c->read->handler = ngx_js_l4_read_handler;
        if (c->read->ready) {
            ngx_js_l4_read_handler(c->read);
        } else {
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                ngx_close_connection(c);
            }
        }
        return;
    }

    ngx_http_init_connection(c);
}


/*
 * P4/P6/P13: accept handler for JS-created listeners.
 * Recovers the listener state via the offsetof trick on ls->servers.
 */
static void
ngx_js_http_accept_handler(ngx_connection_t *c)
{
    ngx_js_http_listener_state_t  *st;

    /*
     * Recover the listener state from ls->servers, which points to &st->port.
     * Use offsetof to convert the port pointer back to the containing struct.
     */
    st = (ngx_js_http_listener_state_t *)
             ((u_char *) c->listening->servers
              - offsetof(ngx_js_http_listener_state_t, port));

    ngx_js_run_accept_handler(c, st);
}


/* ------------------------------------------------------------------ */
/* P17: accept handler for standard nginx listen sockets               */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_srv_accept_handler — installed on standard HTTP listen sockets
 * (ls->handler = ngx_http_init_connection by default) when the socket's
 * default server has JS accept hooks or L4 filters registered via
 * server.on('accept', fn) / server.addL4Filter(fn).
 *
 * Retrieves the per-server ngx_js_http_listener_state_t (pre-built in
 * ngx_js_srv_install_accept_hooks) from the server's ngx_js_http_srv_conf_t,
 * then delegates to ngx_js_run_accept_handler.
 */
static void
ngx_js_srv_accept_handler(ngx_connection_t *c)
{
    ngx_http_port_t               *port;
    ngx_http_in_addr_t            *addr;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_js_http_srv_conf_t        *jscf;
    ngx_js_http_listener_state_t  *st;
#if (NGX_HAVE_INET6)
    ngx_http_in6_addr_t           *addr6;
    struct sockaddr_in6           *sin6;
#endif
    struct sockaddr_in            *sin;
    ngx_uint_t                     i;

    port = c->listening->servers;

    if (port->naddrs > 1) {
        /*
         * Multiple addresses on this port — determine the actual local
         * address the connection arrived on (same logic as
         * ngx_http_init_connection).
         */
        if (ngx_connection_local_sockaddr(c, NULL, 0) != NGX_OK) {
            ngx_http_init_connection(c);
            return;
        }

        switch (c->local_sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            sin6  = (struct sockaddr_in6 *) c->local_sockaddr;
            addr6 = port->addrs;
            for (i = 0; i < port->naddrs - 1; i++) {
                if (ngx_memcmp(&addr6[i].addr6, &sin6->sin6_addr, 16) == 0) {
                    break;
                }
            }
            cscf = addr6[i].conf.default_server;
            break;
#endif

        default: /* AF_INET */
            sin  = (struct sockaddr_in *) c->local_sockaddr;
            addr = port->addrs;
            for (i = 0; i < port->naddrs - 1; i++) {
                if (addr[i].addr == sin->sin_addr.s_addr) {
                    break;
                }
            }
            cscf = addr[i].conf.default_server;
            break;
        }

    } else {

#if (NGX_HAVE_INET6)
        if (c->local_sockaddr->sa_family == AF_INET6) {
            addr6 = port->addrs;
            cscf  = addr6[0].conf.default_server;
        } else
#endif
        {
            addr = port->addrs;
            cscf = addr[0].conf.default_server;
        }
    }

    jscf = cscf->ctx->srv_conf[ngx_js_http_module.ctx_index];
    st   = (ngx_js_http_listener_state_t *) jscf->srv_listener_state;

    if (st == NULL) {
        ngx_http_init_connection(c);
        return;
    }

    ngx_js_run_accept_handler(c, st);
}


/* ------------------------------------------------------------------ */
/* P17: install accept hooks on standard listen sockets               */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_srv_install_accept_hooks — called at the end of ngx_js_init_conf
 * after all JS scripts have been evaluated (so server.on() / addL4Filter()
 * calls are complete).
 *
 * For every standard HTTP listening socket (ls->handler ==
 * ngx_http_init_connection) whose default server has JS accept/L4 hooks
 * registered, we:
 *   1. Build a ngx_js_http_listener_state_t in cycle->pool that mirrors
 *      the hook arrays from the server's ngx_js_http_srv_conf_t.
 *   2. Store a pointer to it in jscf->srv_listener_state.
 *   3. Override ls->handler = ngx_js_srv_accept_handler.
 *
 * If multiple sockets belong to the same server, they share one state
 * (idempotent: srv_listener_state is only created once per jscf).
 */
void
ngx_js_srv_install_accept_hooks(ngx_cycle_t *cycle)
{
    ngx_listening_t               *ls;
    ngx_http_port_t               *port;
    ngx_http_addr_conf_t          *aconf;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_js_http_srv_conf_t        *jscf;
    ngx_js_http_listener_state_t  *st;
    ngx_uint_t                     i, j;
    ngx_int_t                      found;

    if (cycle->listening.nelts == 0) {
        return;
    }

    ls = cycle->listening.elts;

    for (i = 0; i < cycle->listening.nelts; i++) {

        if (ls[i].handler != ngx_http_init_connection) {
            continue;  /* not a standard HTTP socket */
        }

        port = ls[i].servers;
        if (port == NULL || port->naddrs == 0) {
            continue;
        }

        /*
         * Scan all addresses on this port.  Override the handler if
         * ANY addr's default server has JS hooks.
         */
        found = 0;

        for (j = 0; j < port->naddrs; j++) {

#if (NGX_HAVE_INET6)
            if (ls[i].sockaddr->sa_family == AF_INET6) {
                ngx_http_in6_addr_t *a6 = port->addrs;
                aconf = &a6[j].conf;
            } else
#endif
            {
                ngx_http_in_addr_t *a = port->addrs;
                aconf = &a[j].conf;
            }

            cscf = aconf->default_server;
            if (cscf == NULL) {
                continue;
            }

            jscf = cscf->ctx->srv_conf[ngx_js_http_module.ctx_index];
            if (jscf == NULL) {
                continue;
            }

            if (jscf->n_accept_handlers == 0
                && jscf->n_l4_filters == 0
                && jscf->n_l4_send_filters == 0)
            {
                continue;
            }

            /* Build a listener state for this server if not already done */
            if (jscf->srv_listener_state == NULL) {
                st = ngx_pcalloc(cycle->pool,
                                 sizeof(ngx_js_http_listener_state_t));
                if (st == NULL) {
                    return;
                }

                ngx_memcpy(st->accept_handlers, jscf->accept_handlers,
                           jscf->n_accept_handlers * sizeof(uint32_t));
                st->n_accept_handlers = jscf->n_accept_handlers;

                ngx_memcpy(st->l4_filters, jscf->l4_filters,
                           jscf->n_l4_filters * sizeof(uint32_t));
                st->n_l4_filters = jscf->n_l4_filters;

                ngx_memcpy(st->l4_send_filters, jscf->l4_send_filters,
                           jscf->n_l4_send_filters * sizeof(uint32_t));
                st->n_l4_send_filters = jscf->n_l4_send_filters;

                jscf->srv_listener_state = st;
            }

            found = 1;
            break;
        }

        if (found) {
            ls[i].handler = ngx_js_srv_accept_handler;
        }
    }
}


/* ------------------------------------------------------------------ */
/* ngx_js_listener_build_vnames — Phase D helper                       */
/*                                                                     */
/* Builds ngx_http_virtual_names_t from st->vservers[] and installs it */
/* in st->addr.conf.virtual_names.  Hash tables are allocated in       */
/* cycle->pool; a temporary pool is used for ngx_hash_keys_arrays_t.  */
/* ------------------------------------------------------------------ */

static int
ngx_js_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first  = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


static ngx_int_t
ngx_js_listener_build_vnames(ngx_js_http_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_uint_t                  s, n;
    ngx_http_core_srv_conf_t   *cscf;
    ngx_http_server_name_t     *sn;
    ngx_http_virtual_names_t   *vn;
    ngx_http_conf_ctx_t        *http_ctx;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_pool_t                 *temp_pool;
    ngx_int_t                   rc;

    if (st->nvservers == 0) {
        st->addr.conf.virtual_names = NULL;
        return NGX_OK;
    }

    http_ctx = (ngx_http_conf_ctx_t *)
                   cycle->conf_ctx[ngx_http_module.index];
    cmcf     = http_ctx->main_conf[ngx_http_core_module.ctx_index];

    temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cycle->log);
    if (temp_pool == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));
    ha.temp_pool = temp_pool;
    ha.pool      = cycle->pool;

    if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
        ngx_destroy_pool(temp_pool);
        return NGX_ERROR;
    }

    for (s = 0; s < st->nvservers; s++) {
        cscf = st->vservers[s];
        sn   = cscf->server_names.elts;

        for (n = 0; n < cscf->server_names.nelts; n++) {
#if (NGX_PCRE)
            if (sn[n].regex) {
                continue;
            }
#endif
            rc = ngx_hash_add_key(&ha, &sn[n].name, sn[n].server,
                                  NGX_HASH_WILDCARD_KEY);
            if (rc == NGX_ERROR) {
                ngx_destroy_pool(temp_pool);
                return NGX_ERROR;
            }

            if (rc == NGX_BUSY) {
                ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                              "JS listener: duplicate server name \"%V\","
                              " ignored", &sn[n].name);
            }
        }
    }

    vn = ngx_pcalloc(cycle->pool, sizeof(ngx_http_virtual_names_t));
    if (vn == NULL) {
        ngx_destroy_pool(temp_pool);
        return NGX_ERROR;
    }

    ngx_memzero(&hash, sizeof(ngx_hash_init_t));
    hash.key         = ngx_hash_key_lc;
    hash.max_size    = cmcf->server_names_hash_max_size;
    hash.bucket_size = cmcf->server_names_hash_bucket_size;
    hash.name        = "js_listener_server_names_hash";
    hash.pool        = cycle->pool;

    if (ha.keys.nelts) {
        hash.hash      = &vn->names.hash;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }
    }

    if (ha.dns_wc_head.nelts) {
        ngx_qsort(ha.dns_wc_head.elts, ha.dns_wc_head.nelts,
                  sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

        hash.hash      = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts,
                                   ha.dns_wc_head.nelts) != NGX_OK)
        {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }

        vn->names.wc_head = (ngx_hash_wildcard_t *) hash.hash;
    }

    if (ha.dns_wc_tail.nelts) {
        ngx_qsort(ha.dns_wc_tail.elts, ha.dns_wc_tail.nelts,
                  sizeof(ngx_hash_key_t), ngx_js_cmp_dns_wildcards);

        hash.hash      = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                   ha.dns_wc_tail.nelts) != NGX_OK)
        {
            ngx_destroy_pool(temp_pool);
            return NGX_ERROR;
        }

        vn->names.wc_tail = (ngx_hash_wildcard_t *) hash.hash;
    }

#if (NGX_PCRE)
    {
        ngx_uint_t  nregex = 0;

        for (s = 0; s < st->nvservers; s++) {
            cscf = st->vservers[s];
            sn   = cscf->server_names.elts;
            for (n = 0; n < cscf->server_names.nelts; n++) {
                if (sn[n].regex) {
                    nregex++;
                }
            }
        }

        if (nregex) {
            vn->nregex = nregex;
            vn->regex  = ngx_palloc(cycle->pool,
                                    nregex * sizeof(ngx_http_server_name_t));
            if (vn->regex == NULL) {
                ngx_destroy_pool(temp_pool);
                return NGX_ERROR;
            }

            nregex = 0;
            for (s = 0; s < st->nvservers; s++) {
                cscf = st->vservers[s];
                sn   = cscf->server_names.elts;
                for (n = 0; n < cscf->server_names.nelts; n++) {
                    if (sn[n].regex) {
                        vn->regex[nregex++] = sn[n];
                    }
                }
            }
        }
    }
#endif

    ngx_destroy_pool(temp_pool);

    st->addr.conf.virtual_names = vn;
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* listener.addVirtualServer(srv) — Phase D implementation             */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_virtual_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_cycle_t                   *cycle;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: post-fork not yet supported");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (!st->activated) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: call addServer() first to activate"
            " the listener");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "listener.addVirtualServer: NginxServer argument required");
    }

    cscf = ngx_js_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "listener.addVirtualServer: argument must be a NginxServer"
            " object");
    }

    if (st->nvservers >= NGX_JS_LISTENER_VSERVERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: virtual server limit reached"
            " (max %d)", NGX_JS_LISTENER_VSERVERS_MAX);
    }

    st->vservers[st->nvservers++] = cscf;

    if (ngx_js_listener_build_vnames(st, cycle) != NGX_OK) {
        st->nvservers--;
        st->vservers[st->nvservers] = NULL;
        return JS_ThrowInternalError(ctx,
            "listener.addVirtualServer: failed to build virtual names hash");
    }

    return JS_DupValue(ctx, argv[0]);
}


/* ------------------------------------------------------------------ */
/* listener.addServer(srv) — Phase C implementation                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_cycle_t                   *cycle;

    /* Phase C: pre-fork only */
    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: post-fork not yet supported");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "listener.addServer: NginxServer argument required");
    }

    cscf = ngx_js_server_get_cscf(argv[0], &cycle);
    if (cscf == NULL) {
        return JS_ThrowTypeError(ctx,
            "listener.addServer: argument must be a NginxServer object");
    }

    if (cycle == NULL) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: server has no associated cycle");
    }

    if (st->activated) {
        return JS_ThrowInternalError(ctx,
            "listener.addServer: listener already activated");
    }

    /* Wire the cscf into both the state and the routing structures */
    st->default_server           = cscf;
    st->addr.conf.default_server = cscf;

    if (ngx_js_listener_activate(st, cycle) != NGX_OK) {
        st->default_server           = NULL;
        st->addr.conf.default_server = NULL;
        return JS_ThrowInternalError(ctx,
            "listener.addServer: activation failed");
    }

    return JS_DupValue(ctx, argv[0]);
}


/* ------------------------------------------------------------------ */
/* listener.serverByName(name) — F2 cross-reference                   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_server_by_name(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    ngx_http_core_srv_conf_t      *cscf;
    ngx_http_server_name_t        *sn;
    ngx_cycle_t                   *cycle;
    const char                    *query;
    char                          *lc;
    size_t                         qlen, i;
    ngx_uint_t                     s, n;
    int                            found;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "listener.serverByName: string argument required");
    }

    query = JS_ToCString(ctx, argv[0]);
    if (!query) {
        return JS_EXCEPTION;
    }

    qlen = strlen(query);
    lc   = js_malloc(ctx, qlen + 1);
    if (!lc) {
        JS_FreeCString(ctx, query);
        return JS_EXCEPTION;
    }

    for (i = 0; i < qlen; i++) {
        lc[i] = (char) ngx_tolower((u_char) query[i]);
    }
    lc[qlen] = '\0';
    JS_FreeCString(ctx, query);

    /* We need cycle for ngx_js_wrap_server — use ngx_cycle global */
    cycle = (ngx_cycle_t *) ngx_cycle;

    cscf  = NULL;
    found = 0;

    /* Search default server */
    if (st->default_server != NULL) {
        sn = st->default_server->server_names.elts;
        for (n = 0; n < st->default_server->server_names.nelts; n++) {
            if (sn[n].name.len == qlen
                && ngx_strncasecmp(sn[n].name.data,
                                   (u_char *) lc, qlen) == 0)
            {
                cscf  = st->default_server;
                found = 1;
                break;
            }
        }
    }

    /* Search virtual servers */
    if (!found) {
        for (s = 0; s < st->nvservers && !found; s++) {
            sn = st->vservers[s]->server_names.elts;
            for (n = 0; n < st->vservers[s]->server_names.nelts; n++) {
                if (sn[n].name.len == qlen
                    && ngx_strncasecmp(sn[n].name.data,
                                       (u_char *) lc, qlen) == 0)
                {
                    cscf  = st->vservers[s];
                    found = 1;
                    break;
                }
            }
        }
    }

    js_free(ctx, lc);

    if (!found) {
        return JS_NULL;
    }

    return ngx_js_wrap_server(ctx, cscf, cycle);
}


/* ------------------------------------------------------------------ */
/* listener.on(event, fn) — P4: register accept hook                  */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_on(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    const char                    *event;
    uint32_t                       idx;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.on: cannot register hooks after fork");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "listener.on: expected (event, fn)");
    }

    event = JS_ToCString(ctx, argv[0]);
    if (!event) {
        return JS_EXCEPTION;
    }

    if (ngx_strcmp(event, "accept") != 0) {
        JS_FreeCString(ctx, event);
        return JS_ThrowTypeError(ctx,
            "listener.on: unknown event (expected 'accept')");
    }

    JS_FreeCString(ctx, event);

    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx,
            "listener.on: second argument must be a function");
    }

    if (st->n_accept_handlers >= NGX_JS_ACCEPT_HANDLERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.on: accept handler limit reached (max %d)",
            NGX_JS_ACCEPT_HANDLERS_MAX);
    }

    idx = ngx_js_accept_hook_register_fn(ctx, argv[1]);
    st->accept_handlers[st->n_accept_handlers++] = idx;

    /*
     * If this is the first handler and the listener is already activated,
     * switch ls->handler from ngx_http_init_connection to our wrapper.
     * If not yet activated, ngx_js_listener_activate will pick this up.
     */
    if (st->n_accept_handlers == 1 && st->ls != NULL
        && st->n_l4_filters == 0)
    {
        st->ls->handler = ngx_js_http_accept_handler;
    }

    return JS_DupValue(ctx, this_val);   /* allow chaining */
}


/* ------------------------------------------------------------------ */
/* listener.addL4Filter(fn) — P6: register a raw TCP data filter       */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_l4_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    uint32_t                       idx;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addL4Filter: cannot register filters after fork");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
            "listener.addL4Filter: function argument required");
    }

    if (st->n_l4_filters >= NGX_JS_L4_FILTERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.addL4Filter: filter limit reached (max %d)",
            NGX_JS_L4_FILTERS_MAX);
    }

    idx = ngx_js_l4_filter_register_fn(ctx, argv[0]);
    st->l4_filters[st->n_l4_filters++] = idx;

    /* If listener is already activated, switch handler to use our wrapper */
    if (st->n_l4_filters == 1 && st->ls != NULL) {
        st->ls->handler = ngx_js_http_accept_handler;
    }

    return JS_DupValue(ctx, this_val);   /* allow chaining */
}


/* ------------------------------------------------------------------ */
/* listener.addL4SendFilter(fn) — P13: register outbound byte filter   */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_listener_add_l4_send_filter(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_listener_opaque_t      *op;
    ngx_js_http_listener_state_t  *st;
    uint32_t                       idx;

    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "listener.addL4SendFilter: cannot register filters after fork");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_http_listener_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->handle >= NGX_JS_LISTENER_REG_MAX
        || ngx_js_listener_reg[op->handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "NginxHttpListener: invalid handle");
    }

    st = ngx_js_listener_reg[op->handle];

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
            "listener.addL4SendFilter: function argument required");
    }

    if (st->n_l4_send_filters >= NGX_JS_L4_FILTERS_MAX) {
        return JS_ThrowInternalError(ctx,
            "listener.addL4SendFilter: filter limit reached (max %d)",
            NGX_JS_L4_FILTERS_MAX);
    }

    /* Reuse the same __ngx_l4_filters__ registry as recv filters */
    idx = ngx_js_l4_filter_register_fn(ctx, argv[0]);
    st->l4_send_filters[st->n_l4_send_filters++] = idx;

    /* Ensure the accept handler is installed */
    if (st->ls != NULL) {
        st->ls->handler = ngx_js_http_accept_handler;
    }

    return JS_DupValue(ctx, this_val);
}


static const JSCFunctionListEntry  ngx_js_listener_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",          ngx_js_listener_get,              NULL, 0),
    JS_CGETSET_MAGIC_DEF("socket",           ngx_js_listener_get,              NULL, 1),
    JS_CGETSET_MAGIC_DEF("serverNames",      ngx_js_listener_get,              NULL, 2),
    JS_CFUNC_DEF(        "serverByName",     1, ngx_js_listener_server_by_name),
    JS_CFUNC_DEF(        "addServer",        1, ngx_js_listener_add_server),
    JS_CFUNC_DEF(        "addVirtualServer", 1, ngx_js_listener_add_virtual_server),
    JS_CFUNC_DEF(        "on",               2, ngx_js_listener_on),
    JS_CFUNC_DEF(        "addL4Filter",      1, ngx_js_listener_add_l4_filter),
    JS_CFUNC_DEF(        "addL4SendFilter",  1, ngx_js_listener_add_l4_send_filter),
};


ngx_int_t
ngx_js_listener_register_class(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_http_listener_class_id,
                    &ngx_js_listener_class) < 0)
    {
        return NGX_ERROR;
    }

    if (JS_NewClass(rt, ngx_js_connection_class_id,
                    &ngx_js_connection_class) < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_js_listener_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_listener_proto_funcs,
                               countof(ngx_js_listener_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_http_listener_class_id, proto);

    /* NginxConnection proto */
    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_connection_proto_funcs,
                               countof(ngx_js_connection_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_connection_class_id, proto);

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Wrap a listener registry slot in a JS NginxHttpListener object      */
/* ------------------------------------------------------------------ */

JSValue
ngx_js_wrap_listener(JSContext *ctx, uint32_t handle)
{
    JSValue                    obj;
    ngx_js_listener_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_listener_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;

    obj = JS_NewObjectClass(ctx, ngx_js_http_listener_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* ngx_js_listener_activate — push ls into cycle->listening            */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_listener_activate(ngx_js_http_listener_state_t *st,
    ngx_cycle_t *cycle)
{
    ngx_listening_t           *ls;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;
    ngx_js_socket_state_t     *sock;

    if (st->activated) {
        return NGX_OK;
    }

    if (st->default_server == NULL) {
        return NGX_ERROR;
    }

    sock = ngx_js_socket_reg[st->socket_handle];
    if (sock == NULL) {
        return NGX_ERROR;
    }

    ls = ngx_array_push(&cycle->listening);
    if (ls == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(ls, sizeof(ngx_listening_t));

    ls->fd      = (ngx_socket_t) sock->fd;
    ls->type    = SOCK_STREAM;
    ls->backlog = NGX_LISTEN_BACKLOG;
    ls->rcvbuf  = -1;
    ls->sndbuf  = -1;

    ls->handler = (st->n_accept_handlers > 0 || st->n_l4_filters > 0
                   || st->n_l4_send_filters > 0)
                  ? ngx_js_http_accept_handler
                  : ngx_http_init_connection;
    st->ls = ls;
    ls->servers = &st->port;

    ls->sockaddr = (struct sockaddr *) &st->sin;
    ls->socklen  = sizeof(struct sockaddr_in);

    ls->addr_text_max_len = NGX_INET_ADDRSTRLEN;
    ls->addr_text.data    = st->addr_text_buf;
    ls->addr_text.len     = st->addr_text_len;

    ls->addr_ntop = 1;
    ls->open      = 1;
    ls->bound     = 1;

    /* cscf-dependent fields */
    cscf = st->default_server;
    ls->pool_size = cscf->connection_pool_size;

    clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];
    ls->logp = clcf->error_log;
    ls->log.data    = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;

#if !(NGX_WIN32)
    ngx_rbtree_init(&ls->rbtree, &ls->sentinel, ngx_udp_rbtree_insert_value);
#endif

    sock->in_listening = 1;
    st->activated      = 1;
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* nginx.http.attach(sock) — Phase B implementation                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_http_attach(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_http_listener_state_t  *st;
    ngx_js_socket_state_t         *sock;
    uint32_t                       socket_handle, handle;
    int                            i;

    /* Phase B: only valid before fork */
    if (ngx_process == NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "http.attach: post-fork worker attach not yet supported");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
            "http.attach: expected NginxSocket argument");
    }

    socket_handle = ngx_js_socket_get_handle(argv[0]);
    if (socket_handle >= NGX_JS_SOCKET_REG_MAX) {
        return JS_ThrowTypeError(ctx,
            "http.attach: argument must be a NginxSocket");
    }

    if (ngx_js_socket_reg[socket_handle] == NULL) {
        return JS_ThrowInternalError(ctx,
            "http.attach: invalid socket handle");
    }

    sock = ngx_js_socket_reg[socket_handle];

    /* Find a free listener registry slot */
    handle = (uint32_t) NGX_JS_LISTENER_REG_MAX;
    for (i = 0; i < NGX_JS_LISTENER_REG_MAX; i++) {
        if (ngx_js_listener_reg[i] == NULL) {
            handle = (uint32_t) i;
            break;
        }
    }

    if (handle == (uint32_t) NGX_JS_LISTENER_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "http.attach: listener registry full (max %d)",
            NGX_JS_LISTENER_REG_MAX);
    }

    /* Allocate listener state on the heap (COW-shared after fork) */
    st = ngx_alloc(sizeof(ngx_js_http_listener_state_t), ngx_cycle->log);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "http.attach: ngx_alloc failed");
    }

    ngx_memzero(st, sizeof(ngx_js_http_listener_state_t));
    st->socket_handle = socket_handle;

    /* Fill sockaddr from the socket state */
    st->sin.sin_family = AF_INET;
    st->sin.sin_port   = htons(sock->port);
    /* Reconstruct in_addr from sock->addr string (up to ':') */
    {
        char  host[48];
        char *colon = strrchr(sock->addr, ':');
        if (colon) {
            size_t n = (size_t)(colon - sock->addr);
            if (n >= sizeof(host)) { n = sizeof(host) - 1; }
            ngx_memcpy(host, sock->addr, n);
            host[n] = '\0';
            (void) inet_pton(AF_INET, host, &st->sin.sin_addr);
        }
    }

    /* Build addr_text ("host:port") */
    {
        u_char *p = ngx_snprintf(st->addr_text_buf,
                                 sizeof(st->addr_text_buf) - 1,
                                 "%s%Z", sock->addr);
        st->addr_text_len = (size_t)(p - st->addr_text_buf) - 1; /* skip NUL */
    }

    /* Set up routing structures */
    st->addr.addr         = st->sin.sin_addr.s_addr;
    st->addr.conf.default_server = NULL;     /* set by addServer() */
    st->addr.conf.virtual_names  = NULL;
    st->addr.conf.ssl            = 0;
    st->addr.conf.http2          = 0;
    st->addr.conf.quic           = 0;
    st->addr.conf.proxy_protocol = 0;

    st->port.addrs  = &st->addr;
    st->port.naddrs = 1;

    ngx_js_listener_reg[handle] = st;

    return ngx_js_wrap_listener(ctx, handle);
}


ngx_int_t
ngx_js_listener_install(JSContext *ctx, JSValue http_obj)
{
    JS_SetPropertyStr(ctx, http_obj, "attach",
                      JS_NewCFunction(ctx, ngx_js_http_attach, "attach", 1));
    return NGX_OK;
}
