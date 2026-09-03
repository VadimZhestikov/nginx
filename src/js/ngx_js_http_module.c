
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_http_module — NGX_HTTP_MODULE companion to ngx_js_module.
 *
 * Provides:
 *   - ngx_js_loc_conf_t (per-location JS handler function reference)
 *   - ngx_js_content_handler — content-phase handler called by NGINX
 *   - NginxRequest COM class — wraps ngx_http_request_t for JS
 *   - req.respond(status, headers, body) — sends response and finalizes
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_event_connect.h>
#include <cutils.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_compartment.h"
#include "ngx_js_com.h"
#include "ngx_js_sw.h"
#include "ngx_js_listener.h"


/* ------------------------------------------------------------------ */
/* Filter chain                                                         */
/* ------------------------------------------------------------------ */

static ngx_http_output_header_filter_pt  ngx_js_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_js_next_body_filter;

/* Forward declaration — defined after NginxRequest class setup */
static void  ngx_js_response_hooks_run(ngx_js_worker_t *w,
    ngx_http_request_t *r, ngx_js_loc_conf_t *jlcf);


static ngx_int_t
ngx_js_header_filter(ngx_http_request_t *r)
{
    ngx_js_conf_t      *jcf;
    ngx_js_loc_conf_t  *jlcf;
    ngx_js_worker_t    *w;
    int                 was_set;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_js_http_module);

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    w   = jcf->worker;

    /*
     * If body filters exist for this location, suppress Content-Length so
     * nginx uses chunked encoding — the body length may change after JS
     * transformation.  Do this unconditionally, even if header filters are
     * absent, so the body filter can always write a new length-unknown body.
     */
    if (r == r->main
        && jlcf->body_filters != NULL
        && jlcf->body_filters->nelts > 0
        && w != NULL && w->ctx != NULL)
    {
        r->headers_out.content_length_n = -1;
        ngx_http_clear_content_length(r);
    }

    if ((jlcf->header_filters == NULL || jlcf->header_filters->nelts == 0)
        && (jlcf->response_hooks == NULL || jlcf->response_hooks->nelts == 0))
    {
        return ngx_js_next_header_filter(r);
    }

    if (w == NULL || w->ctx == NULL) {
        return ngx_js_next_header_filter(r);
    }

    was_set = (w->current_request == r);
    if (!was_set) {
        w->current_request = r;
    }

    if (jlcf->header_filters != NULL && jlcf->header_filters->nelts > 0) {
        ngx_js_header_filters_run(w->ctx, w->rt, r, jlcf);
    }

    /* Response hooks (P3) — modify status/headers before send */
    if (r == r->main
        && jlcf->response_hooks != NULL
        && jlcf->response_hooks->nelts > 0)
    {
        ngx_js_response_hooks_run(w, r, jlcf);
    }

    if (!was_set) {
        w->current_request = NULL;
    }

    return ngx_js_next_header_filter(r);
}


/*
 * Emit rctx->wb_body as a single downstream buffer with last_buf = 1.
 * Shared by ngx_js_body_filter_run_from and (later) async resume.
 */
static ngx_int_t
ngx_js_body_emit_wb(ngx_http_request_t *r, ngx_js_req_ctx_t *rctx)
{
    ngx_buf_t    *b;
    ngx_chain_t  *out;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }

    b->last_buf = 1;

    if (rctx->wb_body.len > 0) {
        b->pos    = rctx->wb_body.data;
        b->last   = rctx->wb_body.data + rctx->wb_body.len;
        b->memory = 1;
    } else {
        /* Zero-length body: emit a sync/flush marker so the write filter
         * finalises the chunked response without triggering the "zero size
         * buf in writer" alert that a memory buf with pos==last would cause. */
        b->sync = 1;
    }

    out = ngx_alloc_chain_link(r->pool);
    if (out == NULL) {
        return NGX_ERROR;
    }
    out->buf  = b;
    out->next = NULL;

    return ngx_js_next_body_filter(r, out);
}


/*
 * Run the whole-body filter chain starting from start_idx.
 * rctx->wb_body is the body on entry; updated in place by each filter.
 * On completion (all filters done) emits via ngx_js_body_emit_wb.
 * Returns NGX_OK or NGX_ERROR.
 */
ngx_int_t
ngx_js_body_filter_run_from(ngx_js_worker_t *w, ngx_http_request_t *r,
    ngx_js_req_ctx_t *rctx, ngx_js_loc_conf_t *jlcf, ngx_uint_t start_idx)
{
    ngx_str_t  out_body;
    ngx_int_t  rc;
    int        was_set;

    was_set = (w->current_request == r);
    if (!was_set) {
        w->current_request = r;
    }

    out_body = rctx->wb_body;

    if (jlcf->body_filters != NULL && jlcf->body_filters->nelts > 0) {
        rc = ngx_js_body_filters_run(w->ctx, w->rt, r, jlcf,
                                     &rctx->wb_body, &out_body, start_idx);
        rctx->wb_body = out_body;

        if (!was_set) {
            w->current_request = NULL;
        }

        if (rc == NGX_AGAIN) {
            /* async suspension: keep request alive until promise settles */
            r->main->count++;
            return NGX_AGAIN;
        }

        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }
    } else {
        if (!was_set) {
            w->current_request = NULL;
        }
    }

    /* P14: upstream response filters — run after body_filters, proxied only */
    if (r->upstream != NULL
        && jlcf->upstream_filters != NULL
        && jlcf->upstream_filters->nelts > 0)
    {
        ngx_js_upstream_filters_run(w->ctx, w->rt, r,
                                    jlcf->upstream_filters,
                                    &rctx->wb_body);
    }

    return ngx_js_body_emit_wb(r, rctx);
}


/*
 * Emit rctx->stream_out downstream, setting last_buf when is_last.
 */
static ngx_int_t
ngx_js_streaming_emit(ngx_http_request_t *r, ngx_js_req_ctx_t *rctx,
    ngx_uint_t is_last)
{
    ngx_chain_t  *cl, *lc;
    ngx_buf_t    *lb;

    cl = rctx->stream_out;

    if (cl != NULL) {
        if (is_last) {
            lc = cl;
            while (lc->next != NULL) {
                lc = lc->next;
            }
            lc->buf->last_buf      = 1;
            lc->buf->last_in_chain = 1;
        }
        return ngx_js_next_body_filter(r, cl);
    }

    if (is_last) {
        lb = ngx_calloc_buf(r->pool);
        if (lb == NULL) {
            return NGX_ERROR;
        }
        lb->last_buf = 1;
        lb->sync     = 1;

        lc = ngx_alloc_chain_link(r->pool);
        if (lc == NULL) {
            return NGX_ERROR;
        }
        lc->buf  = lb;
        lc->next = NULL;

        return ngx_js_next_body_filter(r, lc);
    }

    return NGX_OK;
}


/*
 * Run streaming filter chain from start_idx.
 * On NGX_AGAIN suspends via r->main->count++.
 * On NGX_OK emits stream_out downstream.
 */
ngx_int_t
ngx_js_streaming_run_from(ngx_js_worker_t *w, ngx_http_request_t *r,
    ngx_js_req_ctx_t *rctx, ngx_js_loc_conf_t *jlcf,
    u_char *cur_data, size_t cur_len, ngx_uint_t is_last,
    ngx_uint_t start_idx)
{
    ngx_int_t  rc;
    int        was_set;

    was_set = (w->current_request == r);
    if (!was_set) {
        w->current_request = r;
    }

    rc = ngx_js_streaming_filters_run(w->ctx, w->rt, r, jlcf,
                                      cur_data, cur_len, is_last, start_idx);

    if (!was_set) {
        w->current_request = NULL;
    }

    if (rc == NGX_AGAIN) {
        r->main->count++;
        return NGX_AGAIN;
    }

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    return ngx_js_streaming_emit(r, rctx, is_last);
}


/* ------------------------------------------------------------------ */
/* P14: Upstream request-body filter access handler                    */
/* ------------------------------------------------------------------ */

/*
 * Called from ngx_js_upstream_req_access_handler after the client request
 * body is fully read.  Transforms it through upstream_req_filters, then
 * resumes phase processing.
 */
static void
ngx_js_upstream_req_body_handler(ngx_http_request_t *r)
{
    ngx_js_conf_t      *jcf;
    ngx_js_worker_t    *w;
    ngx_js_loc_conf_t  *jlcf;
    ngx_js_req_ctx_t   *rctx;
    ngx_str_t           body;
    ngx_chain_t        *cl;
    ngx_buf_t          *b;
    size_t              total;
    u_char             *p;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_js_http_module);
    jcf  = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    w    = jcf->worker;

    if (w == NULL || w->ctx == NULL
        || r->request_body == NULL
        || jlcf->upstream_req_filters == NULL
        || jlcf->upstream_req_filters->nelts == 0)
    {
        ngx_http_core_run_phases(r);
        return;
    }

    /* Ensure the per-request context exists */
    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL) {
        rctx = ngx_pcalloc(r->pool, sizeof(ngx_js_req_ctx_t));
        if (rctx == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        rctx->write_mode     = NGX_JS_WRITE_GLOBAL;
        rctx->read_mode      = NGX_JS_WRITE_GLOBAL;
        rctx->body_bufs_last = &rctx->body_bufs;
        rctx->ctx_obj        = JS_UNDEFINED;
        ngx_http_set_ctx(r, rctx, ngx_js_http_module);
    }

    if (rctx->upstream_req_filtered) {
        /* Already filtered — should not happen, but guard anyway */
        ngx_http_core_run_phases(r);
        return;
    }

    /* Flatten in-memory request body bufs into a single ngx_str_t */
    total = 0;
    for (cl = r->request_body->bufs; cl; cl = cl->next) {
        b = cl->buf;
        if (ngx_buf_in_memory(b)) {
            total += (size_t)(b->last - b->pos);
        }
    }

    if (total > 0) {
        p = ngx_pnalloc(r->pool, total);
        if (p == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        body.data = p;
        body.len  = total;
        for (cl = r->request_body->bufs; cl; cl = cl->next) {
            b = cl->buf;
            if (ngx_buf_in_memory(b)) {
                p = ngx_copy(p, b->pos, (size_t)(b->last - b->pos));
            }
        }
    } else {
        body.data = (u_char *) "";
        body.len  = 0;
    }

    ngx_js_upstream_req_filters_run(w->ctx, w->rt, r,
                                    jlcf->upstream_req_filters, &body);

    /* Replace request body bufs with filtered content */
    if (body.len > 0) {
        ngx_buf_t    *nb;
        ngx_chain_t  *nc;

        nb = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
        nc = ngx_alloc_chain_link(r->pool);

        if (nb && nc) {
            nb->pos    = body.data;
            nb->last   = body.data + body.len;
            nb->memory = 1;
            nb->last_buf = 1;
            nc->buf  = nb;
            nc->next = NULL;
            r->request_body->bufs = nc;

            /*
             * Update Content-Length so the upstream module sends the correct
             * value in the forwarded request.
             */
            r->headers_in.content_length_n = (off_t) body.len;
        }
    } else {
        r->request_body->bufs = NULL;
        r->headers_in.content_length_n = 0;
    }

    rctx->upstream_req_filtered = 1;

    /*
     * Balance the r->main->count++ done by ngx_http_read_client_request_body.
     * Without this the request count sits at 2 when the content handler runs,
     * so the upstream finalisation only brings it to 1 and the connection
     * never closes (60 s timeout, "open socket left in connection" alert).
     */
    r->main->count--;

    r->write_event_handler = ngx_http_core_run_phases;
    ngx_http_core_run_phases(r);
}


/*
 * Access-phase handler: reads the client request body (async if needed),
 * then runs upstream_req_filters on the in-memory body before the content
 * handler (proxy_pass or similar) picks it up.
 * Returns NGX_DECLINED when no upstream_req_filters are set (fast path).
 */
static ngx_int_t
ngx_js_upstream_req_access_handler(ngx_http_request_t *r)
{
    ngx_js_loc_conf_t  *jlcf;
    ngx_js_req_ctx_t   *rctx;
    ngx_int_t           rc;

    if (r != r->main) {
        return NGX_DECLINED;
    }

    jlcf = ngx_http_get_module_loc_conf(r, ngx_js_http_module);

    if (jlcf->upstream_req_filters == NULL
        || jlcf->upstream_req_filters->nelts == 0)
    {
        return NGX_DECLINED;
    }

    /* Avoid re-entry: if we already filtered the body, continue */
    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx != NULL && rctx->upstream_req_filtered) {
        return NGX_DECLINED;
    }

    /*
     * Request body not yet read.  Trigger async read; the callback
     * (ngx_js_upstream_req_body_handler) will resume phase processing
     * after transforming the body.
     */
    rc = ngx_http_read_client_request_body(r,
                                           ngx_js_upstream_req_body_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;
}


static ngx_int_t
ngx_js_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_js_conf_t      *jcf;
    ngx_js_loc_conf_t  *jlcf;
    ngx_js_worker_t    *w;
    ngx_js_req_ctx_t   *rctx;
    ngx_chain_t        *cl, *link;
    ngx_buf_t          *b;
    u_char             *p;
    size_t              total;
    int                 last;

    /* Only intercept the main request */
    if (r != r->main) {
        return ngx_js_next_body_filter(r, in);
    }

    jlcf = ngx_http_get_module_loc_conf(r, ngx_js_http_module);

    {
        ngx_uint_t  has_bf, has_uf;
        has_bf = (jlcf->body_filters != NULL && jlcf->body_filters->nelts > 0);
        has_uf = (r->upstream != NULL
                  && jlcf->upstream_filters != NULL
                  && jlcf->upstream_filters->nelts > 0);

        if (!has_bf && !has_uf) {
            return ngx_js_next_body_filter(r, in);
        }
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    w   = jcf->worker;

    if (w == NULL || w->ctx == NULL) {
        return ngx_js_next_body_filter(r, in);
    }

    /* Get or create the per-request context */
    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL) {
        rctx = ngx_pcalloc(r->pool, sizeof(ngx_js_req_ctx_t));
        if (rctx == NULL) {
            return NGX_ERROR;
        }
        rctx->write_mode     = NGX_JS_WRITE_GLOBAL;
        rctx->read_mode      = NGX_JS_WRITE_GLOBAL;
        rctx->body_bufs_last = &rctx->body_bufs;
        rctx->ctx_obj        = JS_UNDEFINED;
        ngx_http_set_ctx(r, rctx, ngx_js_http_module);
    }

    /*
     * Mode B (whole-body): accumulate all chunks, then run the filter chain
     * once on the complete body.  This path is taken whenever the filter list
     * contains at least one wholeBody* filter, or when upstream_filters are
     * present (they are always whole-body GENERATOR mode).
     *
     * Mode A (streaming): each nginx body filter call = one chunk delivered
     * to all streamingSync filters.  Filters emit output via req.sendBuffer().
     */
    {
        ngx_uint_t  has_uf;
        has_uf = (r->upstream != NULL
                  && jlcf->upstream_filters != NULL
                  && jlcf->upstream_filters->nelts > 0);

    if (!jlcf->body_filter_has_wb && !has_uf) {
        /* ── Mode A: streaming ─────────────────────────────────────── */

        ngx_chain_t  *cl;
        ngx_buf_t    *b;
        u_char       *p, *chunk_data;
        size_t        total;
        int           last;

        /* Flatten the chain into a single chunk string. */
        total = 0;
        last  = 0;
        for (cl = in; cl; cl = cl->next) {
            b = cl->buf;
            if (ngx_buf_in_memory(b)) {
                total += (size_t)(b->last - b->pos);
            }
            if (b->last_buf) {
                last = 1;
            }
        }

        if (total > 0) {
            p = ngx_pnalloc(r->pool, total);
            if (p == NULL) {
                return NGX_ERROR;
            }
            chunk_data = p;
            for (cl = in; cl; cl = cl->next) {
                b = cl->buf;
                if (ngx_buf_in_memory(b)) {
                    p = ngx_copy(p, b->pos, (size_t)(b->last - b->pos));
                }
            }
        } else {
            chunk_data = (u_char *) "";
        }

        /* Reset per-chunk output accumulator. */
        rctx->stream_out      = NULL;
        rctx->stream_out_last = &rctx->stream_out;

        {
            ngx_int_t  rc = ngx_js_streaming_run_from(w, r, rctx, jlcf,
                                                       chunk_data, total,
                                                       (ngx_uint_t) last, 0);
            if (rc == NGX_AGAIN) {
                return NGX_DONE;
            }
            return rc;
        }
    }

    /* ── Mode B: accumulate ─────────────────────────────────────────── */

    if (rctx->body_bufs_last == NULL) {
        rctx->body_bufs_last = &rctx->body_bufs;
    }

    /* Scan for last_buf flag */
    last = 0;
    for (cl = in; cl; cl = cl->next) {
        if (cl->buf->last_buf) {
            last = 1;
        }
    }

    /* Accumulate chain links into body_bufs (data stays in existing bufs) */
    for (cl = in; cl; cl = cl->next) {
        link = ngx_alloc_chain_link(r->pool);
        if (link == NULL) {
            return NGX_ERROR;
        }
        link->buf  = cl->buf;
        link->next = NULL;
        *rctx->body_bufs_last = link;
        rctx->body_bufs_last  = &link->next;
    }

    if (!last) {
        return NGX_OK;  /* more chunks incoming — keep accumulating */
    }

    /* Flatten all accumulated in-memory buffers into a single ngx_str_t */
    total = 0;
    for (cl = rctx->body_bufs; cl; cl = cl->next) {
        b = cl->buf;
        if (ngx_buf_in_memory(b)) {
            total += (size_t)(b->last - b->pos);
        }
    }

    if (total > 0) {
        p = ngx_pnalloc(r->pool, total);
        if (p == NULL) {
            return NGX_ERROR;
        }
        rctx->wb_body.data = p;
        rctx->wb_body.len  = total;
        for (cl = rctx->body_bufs; cl; cl = cl->next) {
            b = cl->buf;
            if (ngx_buf_in_memory(b)) {
                p = ngx_copy(p, b->pos, (size_t)(b->last - b->pos));
            }
        }
    } else {
        rctx->wb_body.data = (u_char *) "";
        rctx->wb_body.len  = 0;
    }

    /* Reset accumulation state */
    rctx->body_bufs      = NULL;
    rctx->body_bufs_last = NULL;

    {
        ngx_int_t  rc = ngx_js_body_filter_run_from(w, r, rctx, jlcf, 0);

        if (rc == NGX_AGAIN) {
            /* async suspended: count already incremented in run_from */
            return NGX_DONE;
        }

        return rc;
    }
    } /* end has_uf scope */
}


static void *
ngx_js_http_create_main_conf(ngx_conf_t *cf)
{
    ngx_js_http_main_conf_t  *jmcf;

    jmcf = ngx_pcalloc(cf->pool, sizeof(ngx_js_http_main_conf_t));
    if (jmcf == NULL) {
        return NULL;
    }
    /* jmcf->hooks = NULL by pcalloc */
    return jmcf;
}


static void *
ngx_js_http_create_srv_conf(ngx_conf_t *cf)
{
    ngx_js_http_srv_conf_t  *jscf;

    jscf = ngx_pcalloc(cf->pool, sizeof(ngx_js_http_srv_conf_t));
    if (jscf == NULL) {
        return NULL;
    }
    return jscf;
}


static char *
ngx_js_http_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    /* Server hooks are per-server; no inheritance from main. */
    return NGX_CONF_OK;
}


/* forward declarations — defined after the hook helpers section below */
static ngx_int_t  ngx_js_http_access_handler(ngx_http_request_t *r);
static void       ngx_js_p2_hook_resume(ngx_js_worker_t *w,
                                        ngx_js_async_ctx_t *actx);


static ngx_int_t
ngx_js_shared_zone_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_slab_pool_t      *sp;
    ngx_js_shared_hdr_t  *hdr;

    sp = (ngx_slab_pool_t *) shm_zone->shm.addr;

    /* data != NULL means hot-reload: keep existing contents */
    if (data) {
        shm_zone->data = data;
        return NGX_OK;
    }

    hdr = (ngx_js_shared_hdr_t *) shm_zone->shm.addr;
    ngx_memzero(hdr, NGX_JS_SHARED_SIZE);
    hdr->capacity = NGX_JS_SHARED_CAPACITY;

    /*
     * nginx core calls ngx_unlock_mutexes(pid) from the SIGCHLD handler for
     * every shared memory zone, treating the start of each zone as an
     * ngx_slab_pool_t and calling ngx_shmtx_force_unlock(&sp->mutex, pid).
     * If sp->mutex.lock is NULL (uninitialized), that CAS crashes the master.
     *
     * We do not use the slab allocator, but we must give the embedded mutex a
     * valid lock pointer.  We point it at sp->lock.lock (offset 0, which is
     * hdr->lock), and set spin = -1 so no semaphore is initialised.  If a
     * worker exits while holding our spinlock, force_unlock will correctly
     * clear it.
     */
    sp->mutex.lock = &sp->lock.lock;
    sp->mutex.spin = (ngx_uint_t) -1;

    shm_zone->data = hdr;

    return NGX_OK;
}


static ngx_int_t
ngx_js_http_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_handler_pt        *h;
    ngx_js_conf_t              *jcf;
    ngx_str_t                   zone_name;

    /*
     * Register access-phase handlers.  nginx builds the phase engine by
     * iterating cmcf->phases[i].handlers in REVERSE (last-pushed = first-run).
     * Push upstream_req FIRST so it ends up running SECOND (after the global
     * HTTP hook has populated req.ctx).
     */
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_js_upstream_req_access_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_js_http_access_handler;

    /* Install response filter hooks (existing) */
    ngx_js_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_js_header_filter;

    ngx_js_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_js_body_filter;

    /* P11: allocate nginx.shared zone only when JS is active */
    jcf = (ngx_js_conf_t *) ngx_get_conf(cf->cycle->conf_ctx, ngx_js_module);

    if (jcf->sources.nelts > 0) {
        ngx_str_set(&zone_name, "ngx_js_shared");

        jcf->shared_zone = ngx_shared_memory_add(cf, &zone_name,
                                                  NGX_JS_SHARED_SIZE,
                                                  &ngx_js_http_module);
        if (jcf->shared_zone == NULL) {
            return NGX_ERROR;
        }

        jcf->shared_zone->init = ngx_js_shared_zone_init;
        jcf->shared_zone->data = NULL;
    }

    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* NginxRequest class                                                   */
/* ------------------------------------------------------------------ */

/* Forward declaration needed by ngx_js_request_opaque_t */
typedef struct ngx_js_subreq_list_s  ngx_js_subreq_list_t;

typedef struct {
    ngx_http_request_t    *r;
    ngx_js_subreq_list_t  *subreq_list;  /* current in-flight subrequest group */
    ngx_int_t              respond_rc;   /* rc from ngx_http_output_filter */
    unsigned               responded:1;    /* set when req.respond()/finish() called */
    unsigned               headers_sent:1; /* set after writeHead()/first write() */
    unsigned               hijacked:1;     /* set by req.hijack() */
    unsigned               passed:1;       /* set by req.pass() — internal redirect */
} ngx_js_request_opaque_t;


/* Pool cleanup that releases the req.ctx JS object when the request ends. */
typedef struct {
    JSContext         *js_ctx;
    ngx_js_req_ctx_t  *rctx;
} ngx_js_ctx_obj_cleanup_t;

static void
ngx_js_ctx_obj_cleanup(void *data)
{
    ngx_js_ctx_obj_cleanup_t  *cl = data;
    if (!JS_IsUndefined(cl->rctx->ctx_obj)) {
        JS_FreeValue(cl->js_ctx, cl->rctx->ctx_obj);
        cl->rctx->ctx_obj = JS_UNDEFINED;
    }
}


static void
ngx_js_request_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_request_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_request_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_request_class = {
    "NginxRequest",
    .finalizer = ngx_js_request_finalizer
};


/*
 * Magic values for ngx_js_request_get:
 *   0 — method         (r/o string)
 *   1 — uri            (r/o string, decoded, no query string)
 *   2 — args           (r/o string, query string)
 *   3 — remoteAddr     (r/o string)
 *   4 — headers        (r/o object, lowercase keys)
 *   5 — host           (r/o string, parsed Host without port)
 *   6 — httpVersion    (r/o string, e.g. "1.1", "2.0")
 *   7 — isInternal     (r/o boolean)
 *   8 — keepalive      (r/o boolean)
 *   9 — contentLength  (r/o number, -1 when absent)
 *  10 — contentType    (r/o string, "" when absent)
 *  11 — startTime      (r/o number, ms since nginx epoch)
 *  12 — remotePort     (r/o number)
 *  13 — scheme         (r/o string, "http" or "https")
 *  14 — connection     (r/o object {id, requests, fd})
 *  15 — location      (r/o NginxLocation for the matched location)
 *  16 — queryParams  (r/o object, %XX-decoded key/value pairs from r->args)
 *  17 — cookies      (r/o object, name/value pairs from Cookie header)
 *  18 — upstream     (r/o object or null, last upstream attempt metadata)
 *  19 — variables    (r/w NginxRequestVariables exotic object)
 *  20 — body         (r/o string or null — present only if already read)
 *  21 — serverAddr    (r/o string, local IP address)
 *  22 — serverPort    (r/o number, local port)
 *  23 — requestLength (r/o number, total bytes received for this request)
 *  24 — statusCode    (r/w number, response status; 0 when unset)
 *  25 — responded     (r/o boolean, true after req.respond() was called)
 *  26 — ctx           (r/o object, persistent per-request plain object — P10)
 *  27 — bodyPreread   (r/o string, bytes already in read buffer past headers)
 */

/* Forward declaration — defined after ngx_js_request_set_variable */
static JSValue ngx_js_collect_body(JSContext *ctx, ngx_http_request_t *r);

/*
 * Build a plain JS object from one ngx_http_upstream_state_t entry:
 *   { status, responseTime, connectTime, bytesReceived, addr }
 *
 * Returns JS_NULL when state is NULL.
 * Called by the r.upstream getter (magic 18) and ngx_js_subreq_done.
 */
static JSValue
ngx_js_upstream_state_obj(JSContext *ctx, ngx_http_upstream_state_t *st)
{
    JSValue  obj;

    if (st == NULL) {
        return JS_NULL;
    }

    obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "status",
                      JS_NewInt32(ctx, (int32_t) st->status));
    JS_SetPropertyStr(ctx, obj, "responseTime",
                      JS_NewInt64(ctx, (int64_t) st->response_time));
    JS_SetPropertyStr(ctx, obj, "connectTime",
                      JS_NewInt64(ctx, (int64_t) st->connect_time));
    JS_SetPropertyStr(ctx, obj, "bytesReceived",
                      JS_NewInt64(ctx, (int64_t) st->bytes_received));

    if (st->peer && st->peer->len > 0) {
        JS_SetPropertyStr(ctx, obj, "addr",
                          JS_NewStringLen(ctx,
                                          (const char *) st->peer->data,
                                          st->peer->len));
    } else {
        JS_SetPropertyStr(ctx, obj, "addr", JS_NewString(ctx, ""));
    }

    return obj;
}


static JSValue
ngx_js_request_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    JSValue                   obj;
    ngx_list_part_t          *part;
    ngx_table_elt_t          *h;
    ngx_uint_t                i;
    u_char                    key_buf[256];
    size_t                    klen;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    switch (magic) {

    case 0: /* method */
        return JS_NewStringLen(ctx, (const char *) r->method_name.data,
                               r->method_name.len);

    case 1: /* uri */
        return JS_NewStringLen(ctx, (const char *) r->uri.data,
                               r->uri.len);

    case 2: /* args */
        return JS_NewStringLen(ctx, (const char *) r->args.data,
                               r->args.len);

    case 3: /* remoteAddr */
        return JS_NewStringLen(ctx,
                               (const char *) r->connection->addr_text.data,
                               r->connection->addr_text.len);

    case 4: /* headers — all incoming request headers as a plain object */
        obj  = JS_NewObject(ctx);
        part = &r->headers_in.headers.part;
        h    = part->elts;

        for (i = 0; /* break below */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                h    = part->elts;
                i    = 0;
            }

            /* NUL-terminate the lowercase key for JS_SetPropertyStr */
            klen = h[i].key.len < sizeof(key_buf) - 1
                   ? h[i].key.len : sizeof(key_buf) - 1;
            ngx_memcpy(key_buf, h[i].lowcase_key, klen);
            key_buf[klen] = '\0';

            JS_SetPropertyStr(ctx, obj, (const char *) key_buf,
                              JS_NewStringLen(ctx,
                                             (const char *) h[i].value.data,
                                             h[i].value.len));
        }

        return obj;

    case 5: /* host — parsed Host header value without port */
        return JS_NewStringLen(ctx, (const char *) r->headers_in.server.data,
                               r->headers_in.server.len);

    case 6: /* httpVersion */
        switch (r->http_version) {
        case NGX_HTTP_VERSION_9:  return JS_NewString(ctx, "0.9");
        case NGX_HTTP_VERSION_10: return JS_NewString(ctx, "1.0");
        case NGX_HTTP_VERSION_11: return JS_NewString(ctx, "1.1");
        case NGX_HTTP_VERSION_20: return JS_NewString(ctx, "2.0");
        case NGX_HTTP_VERSION_30: return JS_NewString(ctx, "3.0");
        default:                  return JS_NewString(ctx, "1.1");
        }

    case 7: /* isInternal */
        return JS_NewBool(ctx, r->internal);

    case 8: /* keepalive */
        return JS_NewBool(ctx, r->keepalive);

    case 9: /* contentLength */
        return JS_NewInt64(ctx, (int64_t) r->headers_in.content_length_n);

    case 10: /* contentType */
        if (r->headers_in.content_type) {
            return JS_NewStringLen(ctx,
                               (const char *) r->headers_in.content_type->value.data,
                               r->headers_in.content_type->value.len);
        }
        return JS_NewString(ctx, "");

    case 11: /* startTime — ms since nginx start */
        return JS_NewInt64(ctx, (int64_t) r->start_msec);

    case 12: /* remotePort */
        return JS_NewInt32(ctx,
                           (int32_t) ngx_inet_get_port(r->connection->sockaddr));

    case 13: /* scheme */
        if (r->http_connection->ssl) {
            return JS_NewString(ctx, "https");
        }
        return JS_NewString(ctx, "http");

    case 14: /* connection — {id, requests, fd} */
    {
        JSValue  conn;

        conn = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, conn, "id",
                          JS_NewInt64(ctx,
                              (int64_t) r->connection->number));
        JS_SetPropertyStr(ctx, conn, "requests",
                          JS_NewInt64(ctx,
                              (int64_t) r->connection->requests));
        JS_SetPropertyStr(ctx, conn, "fd",
                          JS_NewInt32(ctx, (int32_t) r->connection->fd));
        return conn;
    }

    case 15: /* location — NginxLocation for the matched location */
    {
        ngx_http_core_loc_conf_t  *clcf;

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
        return ngx_js_wrap_location(ctx, clcf);
    }

    case 16: /* queryParams — %XX-decoded query-string key/value pairs */
    {
        JSValue  qobj;
        u_char  *p, *end, *ks, *ke, *vs, *ve, *src;
        u_char  *key_buf, *val_buf, *kd, *vd;

        qobj = JS_NewObject(ctx);

        if (r->args.len == 0) {
            return qobj;
        }

        /* Upper bound for decoded output is the encoded length */
        key_buf = ngx_pnalloc(r->pool, r->args.len + 1);
        val_buf = ngx_pnalloc(r->pool, r->args.len + 1);
        if (!key_buf || !val_buf) {
            JS_FreeValue(ctx, qobj);
            return JS_ThrowOutOfMemory(ctx);
        }

        p   = r->args.data;
        end = r->args.data + r->args.len;

        while (p < end) {

            /* Locate key span */
            ks = p;
            while (p < end && *p != '=' && *p != '&') { p++; }
            ke = p;

            /* Locate value span */
            vs = ve = p;
            if (p < end && *p == '=') {
                p++;
                vs = p;
                while (p < end && *p != '&') { p++; }
                ve = p;
            }

            if (p < end) { p++; }   /* skip '&' */
            if (ke == ks) { continue; }  /* empty key — skip */

            /* %XX-decode key */
            kd  = key_buf;
            src = ks;
            ngx_unescape_uri(&kd, &src, (size_t)(ke - ks), NGX_UNESCAPE_URI);
            *kd = '\0';

            /* %XX-decode value */
            vd  = val_buf;
            src = vs;
            ngx_unescape_uri(&vd, &src, (size_t)(ve - vs), NGX_UNESCAPE_URI);

            JS_SetPropertyStr(ctx, qobj, (const char *) key_buf,
                              JS_NewStringLen(ctx,
                                             (const char *) val_buf,
                                             (size_t)(vd - val_buf)));
        }

        return qobj;
    }

    case 17: /* cookies — name/value pairs from Cookie header(s) */
    {
        JSValue          cobj;
        ngx_list_part_t *part;
        ngx_table_elt_t *h;
        ngx_uint_t       i;
        u_char          *p, *end, *ns, *ne, *vs, *ve;
        u_char           name_buf[256];
        size_t           nlen;

        static const u_char cookie_lc[] = "cookie";

        cobj = JS_NewObject(ctx);

        part = &r->headers_in.headers.part;
        h    = part->elts;

        for (i = 0; /* break below */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) { break; }
                part = part->next;
                h    = part->elts;
                i    = 0;
            }

            /* Skip headers that are not "cookie" */
            if (h[i].key.len != sizeof(cookie_lc) - 1
                || ngx_memcmp(h[i].lowcase_key, cookie_lc,
                              sizeof(cookie_lc) - 1) != 0)
            {
                continue;
            }

            /* Parse "name=value; name2=value2; ..." */
            p   = h[i].value.data;
            end = h[i].value.data + h[i].value.len;

            while (p < end) {

                /* Skip leading whitespace / semicolons */
                while (p < end && (*p == ' ' || *p == ';')) { p++; }
                if (p >= end) { break; }

                /* Cookie name: up to '=' or ';' */
                ns = p;
                while (p < end && *p != '=' && *p != ';') { p++; }
                ne = p;

                /* Trim trailing spaces from name */
                while (ne > ns && *(ne - 1) == ' ') { ne--; }

                /* Cookie value: after '=' up to ';' */
                vs = ve = p;
                if (p < end && *p == '=') {
                    p++;
                    vs = p;
                    while (p < end && *p != ';') { p++; }
                    ve = p;
                    /* Trim surrounding spaces from value */
                    while (vs < ve && *vs == ' ')        { vs++; }
                    while (ve > vs && *(ve - 1) == ' ')  { ve--; }
                }

                if (ne == ns) { continue; }   /* empty name — skip */

                /* NUL-terminate name for JS_SetPropertyStr */
                nlen = (size_t)(ne - ns);
                if (nlen >= sizeof(name_buf)) {
                    nlen = sizeof(name_buf) - 1;
                }
                ngx_memcpy(name_buf, ns, nlen);
                name_buf[nlen] = '\0';

                JS_SetPropertyStr(ctx, cobj, (const char *) name_buf,
                                  JS_NewStringLen(ctx,
                                                  (const char *) vs,
                                                  (size_t)(ve - vs)));
            }
        }

        return cobj;
    }

    case 18: /* upstream — last upstream attempt metadata or null */
    {
        ngx_http_upstream_state_t  *st;
        ngx_uint_t                  last;

        if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
            return JS_NULL;
        }

        last = r->upstream_states->nelts - 1;
        st   = (ngx_http_upstream_state_t *) r->upstream_states->elts + last;

        return ngx_js_upstream_state_obj(ctx, st);
    }

    case 19: /* variables — live r/w access to all nginx variables */
    {
        JSValue  vobj;

        vobj = JS_NewObjectClass(ctx, ngx_js_req_vars_class_id);
        if (JS_IsException(vobj)) {
            return JS_EXCEPTION;
        }

        JS_SetOpaque(vobj, r);
        return vobj;
    }

    case 20: /* body — request body string if already buffered, else null */
        return ngx_js_collect_body(ctx, r);

    case 21: /* serverAddr — local IP address as string */
    {
        u_char     addr[NGX_SOCKADDR_STRLEN];
        ngx_str_t  s;

        s.len  = NGX_SOCKADDR_STRLEN;
        s.data = addr;

        if (ngx_connection_local_sockaddr(r->connection, &s, 0) != NGX_OK) {
            return JS_NewString(ctx, "");
        }

        return JS_NewStringLen(ctx, (const char *) s.data, s.len);
    }

    case 22: /* serverPort — local port number */
        if (ngx_connection_local_sockaddr(r->connection, NULL, 0) != NGX_OK) {
            return JS_NewInt32(ctx, 0);
        }
        return JS_NewInt32(ctx,
                           (int32_t) ngx_inet_get_port(r->connection->local_sockaddr));

    case 23: /* requestLength — total bytes received for this request */
        return JS_NewInt64(ctx, (int64_t) r->request_length);

    case 24: /* statusCode — staged response status (0 if unset) */
        return JS_NewInt32(ctx, (int32_t) r->headers_out.status);

    case 25: /* responded — true after req.respond() was called */
        op = JS_GetOpaque(this_val, ngx_js_request_class_id);
        return JS_NewBool(ctx, op != NULL && op->responded);

    case 26: /* ctx — persistent per-request plain object (P10) */
    {
        ngx_js_req_ctx_t          *rctx;
        ngx_pool_cleanup_t        *cln;
        ngx_js_ctx_obj_cleanup_t  *cl;

        rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
        if (rctx == NULL) {
            return JS_ThrowInternalError(ctx, "req.ctx: no request context");
        }

        if (JS_IsUndefined(rctx->ctx_obj)) {
            rctx->ctx_obj = JS_NewObject(ctx);
            if (JS_IsException(rctx->ctx_obj)) {
                return JS_EXCEPTION;
            }

            cln = ngx_pool_cleanup_add(r->pool,
                                       sizeof(ngx_js_ctx_obj_cleanup_t));
            if (cln == NULL) {
                JS_FreeValue(ctx, rctx->ctx_obj);
                rctx->ctx_obj = JS_UNDEFINED;
                return JS_ThrowInternalError(ctx, "req.ctx: pool cleanup alloc failed");
            }

            cl          = cln->data;
            cl->js_ctx  = ctx;
            cl->rctx    = rctx;
            cln->handler = ngx_js_ctx_obj_cleanup;
        }

        return JS_DupValue(ctx, rctx->ctx_obj);
    }

    case 27: /* bodyPreread — body bytes already in connection read buffer.
              * After request headers are parsed, any data that arrived in
              * the same recv() beyond \r\n\r\n sits in r->header_in->pos
              * through r->header_in->last.  ngx_http_read_client_request_body
              * consumes and zeroes this region, so after readBody() or
              * bodyChunks() have been called this returns "".             */
        return JS_NewStringLen(ctx,
                               (const char *) r->header_in->pos,
                               (size_t)(r->header_in->last - r->header_in->pos));

    case 28: /* connCtx — persistent PER-CONNECTION object (survives keepalive
              * requests); the same object as conn.ctx in the accept hook.    */
        return ngx_js_connection_ctx_obj(ctx, r->connection);

    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_request_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_request_opaque_t  *op;
    int32_t                   n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    switch (magic) {

    case 24: /* statusCode */
        if (op->headers_sent || op->responded) {
            return JS_ThrowTypeError(ctx,
                                     "r.statusCode: headers already sent");
        }
        if (JS_ToInt32(ctx, &n, val) < 0) {
            return JS_EXCEPTION;
        }
        op->r->headers_out.status = (ngx_uint_t) n;
        return JS_UNDEFINED;

    }

    return JS_UNDEFINED;
}


/*
 * req.variable(name) → string | null
 *
 * Reads the nginx variable named `name` (without leading $) in the context
 * of this request.  Returns null when the variable is not found or has no
 * value.
 */
static JSValue
ngx_js_request_variable(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t    *op;
    ngx_http_request_t         *r;
    ngx_http_variable_value_t  *vv;
    ngx_str_t                   name;
    const char                 *name_cstr;
    size_t                      name_len;
    ngx_uint_t                  key;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.variable: expected name argument");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    name_cstr = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    name.data = (u_char *) name_cstr;
    name.len  = name_len;

    key = ngx_hash_key(name.data, name.len);
    vv  = ngx_http_get_variable(r, &name, key);

    JS_FreeCString(ctx, name_cstr);

    if (vv == NULL || vv->not_found) {
        return JS_NULL;
    }

    return JS_NewStringLen(ctx, (const char *) vv->data, vv->len);
}


/*
 * req.setVariable(name, value) → undefined
 *
 * Sets the nginx variable named `name` (without leading $) to `value`.
 * The variable must already be known to nginx (e.g. declared via `set`).
 * Throws TypeError if the variable is unknown or not settable.
 * The value is copied into the request pool.
 */
static JSValue
ngx_js_request_set_variable(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t    *op;
    ngx_http_request_t         *r;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t   vv;
    ngx_str_t                   name;
    const char                 *name_cstr, *val_cstr;
    size_t                      name_len, val_len;
    ngx_uint_t                  key;
    u_char                     *p;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "r.setVariable: expected (name, value)");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    name_cstr = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    val_cstr = JS_ToCStringLen(ctx, &val_len, argv[1]);
    if (!val_cstr) {
        JS_FreeCString(ctx, name_cstr);
        return JS_EXCEPTION;
    }

    name.data = (u_char *) name_cstr;
    name.len  = name_len;
    key = ngx_hash_key(name.data, name.len);

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);

    if (v == NULL) {
        JS_FreeCString(ctx, val_cstr);
        JS_FreeCString(ctx, name_cstr);
        return JS_ThrowTypeError(ctx,
                                 "r.setVariable: unknown variable \"%.*s\"",
                                 (int) name_len, name_cstr);
    }

    /* copy value into request pool so it outlives the JS string */
    p = ngx_palloc(r->pool, val_len + 1);
    if (p == NULL) {
        JS_FreeCString(ctx, val_cstr);
        JS_FreeCString(ctx, name_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, val_cstr, val_len);
    p[val_len] = '\0';

    JS_FreeCString(ctx, val_cstr);
    JS_FreeCString(ctx, name_cstr);

    if (v->set_handler) {
        ngx_memzero(&vv, sizeof(ngx_http_variable_value_t));
        vv.valid = 1;
        vv.data  = p;
        vv.len   = (ngx_uint_t) val_len;
        v->set_handler(r, &vv, v->data);
        return JS_UNDEFINED;
    }

    if (v->flags & NGX_HTTP_VAR_INDEXED) {
        r->variables[v->index].len          = (ngx_uint_t) val_len;
        r->variables[v->index].valid        = 1;
        r->variables[v->index].no_cacheable = 0;
        r->variables[v->index].not_found    = 0;
        r->variables[v->index].data         = p;
        return JS_UNDEFINED;
    }

    return JS_ThrowTypeError(ctx,
                             "r.setVariable: variable \"%.*s\" is not settable",
                             (int) name.len, name.data);
}


/* ------------------------------------------------------------------ */
/* r.body and r.readBody() — request body access                        */
/* ------------------------------------------------------------------ */

/*
 * Collect all in-memory chain bufs from r->request_body->bufs into a
 * single JS string.  File-buffered data is skipped (counted as zero).
 * Called both from the r.body getter and the body_done callback.
 * Returns JS_NULL when there is no body.
 */
static JSValue
ngx_js_collect_body(JSContext *ctx, ngx_http_request_t *r)
{
    ngx_http_request_body_t  *rb;
    ngx_chain_t              *cl;
    ngx_buf_t                *b;
    size_t                    total;
    u_char                   *buf, *p;
    JSValue                   str;

    rb = r->request_body;
    if (rb == NULL || rb->bufs == NULL) {
        return JS_NULL;
    }

    total = 0;
    for (cl = rb->bufs; cl; cl = cl->next) {
        b = cl->buf;
        if (!b->in_file) {
            total += (size_t) (b->last - b->pos);
        }
    }

    if (total == 0) {
        return JS_NewStringLen(ctx, "", 0);
    }

    buf = js_malloc(ctx, total);
    if (!buf) {
        return JS_ThrowOutOfMemory(ctx);
    }

    p = buf;
    for (cl = rb->bufs; cl; cl = cl->next) {
        b = cl->buf;
        if (!b->in_file) {
            p = ngx_cpymem(p, b->pos, (size_t) (b->last - b->pos));
        }
    }

    str = JS_NewStringLen(ctx, (const char *) buf, total);
    js_free(ctx, buf);
    return str;
}


/*
 * Context stored via ngx_http_set_ctx for the async body-reading path.
 * Temporarily replaces ngx_js_req_ctx_t in the module-ctx slot; restored
 * in ngx_js_body_done.
 */
typedef struct {
    JSContext          *ctx;
    JSRuntime          *rt;
    ngx_js_worker_t    *w;
    JSValue             resolve;
    JSValue             reject;
    ngx_js_req_ctx_t   *rctx;    /* saved req-ctx; restored in body_done */
} ngx_js_body_ctx_t;


/*
 * Callback fired by nginx when the request body has been fully read.
 * Resolves the inner readBody() Promise, drains the QuickJS microtask
 * queue (which resumes the outer async handler), then calls
 * ngx_js_async_check to finalize the request if the outer Promise is
 * settled.
 */
static void
ngx_js_body_done(ngx_http_request_t *r)
{
    ngx_js_body_ctx_t  *bctx;
    JSContext          *job_ctx;
    JSValue             body;

    bctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    ngx_http_set_ctx(r, bctx->rctx, ngx_js_http_module);  /* restore req ctx */

    bctx->w->current_request = r;

    body = ngx_js_collect_body(bctx->ctx, r);

    if (JS_IsException(body)) {
        JSValue  err = JS_GetException(bctx->ctx);
        JS_Call(bctx->ctx, bctx->reject, JS_UNDEFINED, 1, &err);
        JS_FreeValue(bctx->ctx, err);
    } else {
        JS_Call(bctx->ctx, bctx->resolve, JS_UNDEFINED, 1, &body);
        JS_FreeValue(bctx->ctx, body);
    }

    JS_FreeValue(bctx->ctx, bctx->resolve);
    JS_FreeValue(bctx->ctx, bctx->reject);

    while (JS_ExecutePendingJob(bctx->rt, &job_ctx) > 0) { }

    /*
     * ngx_js_async_check consumes the count added by the content handler's
     * async-pending path.  The additional ngx_http_finalize_request below
     * consumes the count added by ngx_http_read_client_request_body itself.
     * Together they leave r->main->count at 1 (sync body path) or 0 (async
     * body path), which triggers the normal keepalive / close logic.
     */
    ngx_js_async_check(bctx->w);
    ngx_js_bf_async_check(bctx->w);
    ngx_js_sf_async_check(bctx->w);
    ngx_js_l4_async_check(bctx->w);
    ngx_http_finalize_request(r, NGX_DONE);

    bctx->w->current_request = NULL;
}


/* ------------------------------------------------------------------ */
/* req.bodyChunks() — async iterator over request body buffers         */
/* ------------------------------------------------------------------ */

/*
 * Per-request iterator state.  Allocated from the request pool so it
 * outlives any JS GC cycle during the request.  The JS wrapper object
 * holds a pointer to this struct as its opaque.
 *
 * Lifecycle:
 *   1. bodyChunks() allocates and returns the wrapper.
 *   2. First .next() call triggers ngx_http_read_client_request_body and
 *      returns a pending Promise; body_done callback resolves it with the
 *      first chunk and marks body_ready.
 *   3. Subsequent .next() calls iterate current_cl synchronously, each
 *      returning an immediately-resolved Promise.
 *   4. When current_cl is NULL .next() returns {done:true}.
 */
typedef struct {
    ngx_http_request_t   *r;
    ngx_js_req_ctx_t     *rctx;           /* saved module-ctx; restored in done */
    JSContext            *ctx;
    JSRuntime            *rt;
    ngx_js_worker_t      *w;
    JSValue               pending_resolve; /* awaiting ngx_http_read... callback */
    JSValue               pending_reject;
    ngx_chain_t          *current_cl;     /* iteration cursor in body buf chain */
    unsigned              body_ready:1;   /* body fully buffered by nginx */
} ngx_js_body_chunks_iter_t;


static void
ngx_js_body_chunks_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_body_chunks_iter_t  *iter;

    iter = JS_GetOpaque(val, ngx_js_body_chunks_class_id);
    if (iter == NULL) { return; }

    /* Free any JSValues the iter holds (struct itself is pool-allocated) */
    if (!JS_IsUndefined(iter->pending_resolve)) {
        JS_FreeValueRT(rt, iter->pending_resolve);
        iter->pending_resolve = JS_UNDEFINED;
    }
    if (!JS_IsUndefined(iter->pending_reject)) {
        JS_FreeValueRT(rt, iter->pending_reject);
        iter->pending_reject = JS_UNDEFINED;
    }
}


static JSClassDef  ngx_js_body_chunks_class = {
    "BodyChunksIterator",
    .finalizer = ngx_js_body_chunks_finalizer,
};


/* [Symbol.asyncIterator]() { return this; } */
static JSValue
ngx_js_body_chunks_self(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return JS_DupValue(ctx, this_val);
}


/*
 * Build a {value, done} iterator result object and wrap it in an
 * already-resolved Promise.  Transfers ownership of `value`.
 */
static JSValue
ngx_js_body_chunks_make_result(JSContext *ctx, JSValue value, int done)
{
    JSValue  iter_result, promise, args[2];

    iter_result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, iter_result, "value", value);
    JS_SetPropertyStr(ctx, iter_result, "done",  JS_NewBool(ctx, done));

    promise = JS_NewPromiseCapability(ctx, args);
    JS_Call(ctx, args[0], JS_UNDEFINED, 1, &iter_result);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, iter_result);

    return promise;
}


/*
 * Advance cl past any zero-length or file-buffered buffers.
 * nginx may produce empty chain links (e.g. for Content-Length: 0 or
 * when partial preread leaves a zero-span buf at the head of the chain).
 * Only buffers that carry in-memory bytes are delivered as JS chunks.
 */
static ngx_chain_t *
ngx_js_skip_empty_cl(ngx_chain_t *cl)
{
    while (cl != NULL) {
        ngx_buf_t *b = cl->buf;
        if (!b->in_file && b->last > b->pos) { break; }
        cl = cl->next;
    }
    return cl;
}


/*
 * Callback fired by nginx when request body has been fully buffered.
 * Resolves the Promise that the first .next() call returned with the
 * first chunk, then drains microtasks so the for-await loop continues
 * processing remaining chunks synchronously.
 */
static void
ngx_js_body_chunks_body_done(ngx_http_request_t *r)
{
    ngx_js_body_chunks_iter_t  *iter;
    JSContext                  *ctx;
    JSContext                  *job_ctx;
    JSValue                     iter_result, value;
    ngx_chain_t                *cl;
    ngx_buf_t                  *b;

    iter = ngx_http_get_module_ctx(r, ngx_js_http_module);
    ngx_http_set_ctx(r, iter->rctx, ngx_js_http_module);   /* restore */

    iter->body_ready = 1;
    iter->w->current_request = r;
    ctx = iter->ctx;

    /* Skip empty/in-file buffers at head of chain */
    cl = r->request_body ? r->request_body->bufs : NULL;
    cl = ngx_js_skip_empty_cl(cl);
    iter->current_cl = cl;

    /* Deliver first non-empty chunk (or done if body is empty) */
    if (cl != NULL) {
        b = cl->buf;
        value = JS_NewStringLen(ctx, (const char *) b->pos,
                                (size_t)(b->last - b->pos));

        /* Advance past the chunk we are about to deliver; skip empties */
        iter->current_cl = ngx_js_skip_empty_cl(cl->next);

        iter_result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, iter_result, "value", value);
        JS_SetPropertyStr(ctx, iter_result, "done",  JS_FALSE);
    } else {
        iter_result = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, iter_result, "value", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, iter_result, "done",  JS_TRUE);
    }

    JS_Call(ctx, iter->pending_resolve, JS_UNDEFINED, 1, &iter_result);
    JS_FreeValue(ctx, iter_result);
    JS_FreeValue(ctx, iter->pending_resolve);
    JS_FreeValue(ctx, iter->pending_reject);
    iter->pending_resolve = JS_UNDEFINED;
    iter->pending_reject  = JS_UNDEFINED;

    /*
     * Drain microtasks: the for-await loop processes each already-resolved
     * .next() Promise as a microtask, so the entire iteration (including
     * any synchronous code after each `yield`) runs before returning.
     */
    while (JS_ExecutePendingJob(iter->rt, &job_ctx) > 0) { }

    ngx_js_async_check(iter->w);
    ngx_js_bf_async_check(iter->w);
    ngx_js_sf_async_check(iter->w);
    ngx_js_l4_async_check(iter->w);
    ngx_http_finalize_request(r, NGX_DONE);

    iter->w->current_request = NULL;
}


/*
 * bodyChunksIterator.next() → Promise<{value: string, done: boolean}>
 *
 * First call: triggers ngx_http_read_client_request_body and returns a
 * pending Promise.  The body_done callback resolves it with the first
 * nginx buffer as a string.
 *
 * Subsequent calls: body is already in r->request_body->bufs; returns
 * an immediately-resolved Promise with the next buffer (or done).
 */
static JSValue
ngx_js_body_chunks_next(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_body_chunks_iter_t  *iter;
    ngx_http_request_t         *r;
    JSValue                     promise, args[2];
    ngx_chain_t                *cl;
    ngx_buf_t                  *b;
    ngx_int_t                   rc;
    JSValue                     value;

    iter = JS_GetOpaque2(ctx, this_val, ngx_js_body_chunks_class_id);
    if (!iter) { return JS_EXCEPTION; }

    r = iter->r;

    /* ---- body not yet buffered: trigger async read ---- */
    if (!iter->body_ready) {
        if (!JS_IsUndefined(iter->pending_resolve)) {
            return JS_ThrowTypeError(ctx,
                "bodyChunks: concurrent .next() calls are not allowed");
        }

        promise = JS_NewPromiseCapability(ctx, args);
        if (JS_IsException(promise)) { return JS_EXCEPTION; }

        iter->pending_resolve = args[0];
        iter->pending_reject  = args[1];
        iter->rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
        ngx_http_set_ctx(r, iter, ngx_js_http_module);

        rc = ngx_http_read_client_request_body(r,
                                               ngx_js_body_chunks_body_done);
        if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
            ngx_http_set_ctx(r, iter->rctx, ngx_js_http_module);
            JS_FreeValue(ctx, iter->pending_resolve);
            JS_FreeValue(ctx, iter->pending_reject);
            iter->pending_resolve = JS_UNDEFINED;
            iter->pending_reject  = JS_UNDEFINED;
            JS_FreeValue(ctx, promise);
            return JS_ThrowTypeError(ctx,
                "bodyChunks: failed to initiate request body read");
        }

        /* NGX_OK  → body_done already called, promise resolved.
         * NGX_AGAIN → reading; promise will be resolved by body_done. */
        return promise;
    }

    /* ---- body buffered: yield next non-empty chunk or signal done ---- */
    cl = iter->current_cl;

    if (cl == NULL) {
        return ngx_js_body_chunks_make_result(ctx, JS_UNDEFINED, 1);
    }

    b     = cl->buf;
    value = JS_NewStringLen(ctx, (const char *) b->pos,
                            (size_t)(b->last - b->pos));

    /* Advance cursor, skip any empty follow-on buffers */
    iter->current_cl = ngx_js_skip_empty_cl(cl->next);

    return ngx_js_body_chunks_make_result(ctx, value, 0);
}


/*
 * req.bodyChunks() → BodyChunksIterator
 *
 * Returns an async iterator that yields each nginx request-body buffer
 * as a string.  Typically one buffer per TCP segment received.  The
 * iterator satisfies the async-iterable protocol so it can be used
 * directly with `for await (const chunk of req.bodyChunks())`.
 *
 * Memory model:
 *   nginx buffers the full body before the first chunk is delivered to JS.
 *   Breaking early avoids creating JS strings for the remaining buffers
 *   but does not reduce nginx's C-level memory usage.  Pair with
 *   req.bodyPreread to handle the common case where routing fields fit
 *   in the initial TCP segment with zero body-read cost.
 *
 * Mutual exclusion:
 *   Do not call both readBody() and bodyChunks() on the same request.
 *   If the body is already buffered (readBody() was called first),
 *   bodyChunks() iterates the existing buffers without re-reading.
 */
static JSValue
ngx_js_request_body_chunks(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t    *op;
    ngx_http_request_t         *r;
    ngx_js_body_chunks_iter_t  *iter;
    JSValue                     obj;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) { return JS_EXCEPTION; }

    r = op->r;

    iter = ngx_pcalloc(r->pool, sizeof(ngx_js_body_chunks_iter_t));
    if (!iter) { return JS_ThrowOutOfMemory(ctx); }

    iter->r               = r;
    iter->ctx             = ctx;
    iter->rt              = JS_GetRuntime(ctx);
    iter->w               = JS_GetContextOpaque(ctx);
    iter->pending_resolve = JS_UNDEFINED;
    iter->pending_reject  = JS_UNDEFINED;

    /* Body already buffered by a prior readBody() call — iterate at once. */
    if (r->request_body != NULL) {
        iter->body_ready = 1;
        iter->current_cl = ngx_js_skip_empty_cl(r->request_body->bufs);
    }

    obj = JS_NewObjectClass(ctx, ngx_js_body_chunks_class_id);
    if (JS_IsException(obj)) { return JS_EXCEPTION; }

    JS_SetOpaque(obj, iter);
    return obj;
}


/*
 * r.readBody() → Promise<string>
 *
 * Triggers nginx body reading.  If the body is already buffered the
 * Promise resolves in the same event-loop turn.  Otherwise nginx reads
 * it asynchronously and ngx_js_body_done resolves the Promise later.
 */
static JSValue
ngx_js_request_read_body(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    ngx_js_body_ctx_t        *bctx;
    ngx_js_worker_t          *w;
    JSValue                   promise, args[2];
    ngx_int_t                 rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;
    w = JS_GetContextOpaque(ctx);

    /* Create resolve/reject pair */
    promise = JS_NewPromiseCapability(ctx, args);
    if (JS_IsException(promise)) {
        return JS_EXCEPTION;
    }

    /* Body already available — resolve immediately */
    if (r->request_body != NULL) {
        JSValue  body = ngx_js_collect_body(ctx, r);
        JS_Call(ctx, args[0], JS_UNDEFINED, 1, &body);
        JS_FreeValue(ctx, body);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        return promise;
    }

    bctx = ngx_palloc(r->pool, sizeof(ngx_js_body_ctx_t));
    if (!bctx) {
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowOutOfMemory(ctx);
    }

    bctx->ctx     = ctx;
    bctx->rt      = JS_GetRuntime(ctx);
    bctx->w       = w;
    bctx->resolve = args[0];
    bctx->reject  = args[1];
    bctx->rctx    = ngx_http_get_module_ctx(r, ngx_js_http_module);

    ngx_http_set_ctx(r, bctx, ngx_js_http_module);

    rc = ngx_http_read_client_request_body(r, ngx_js_body_done);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        /* bctx is still in pool — body_done was not called, free manually */
        ngx_http_set_ctx(r, bctx->rctx, ngx_js_http_module);  /* restore */
        JS_FreeValue(ctx, bctx->resolve);
        JS_FreeValue(ctx, bctx->reject);
        JS_FreeValue(ctx, promise);
        return JS_ThrowTypeError(ctx, "r.readBody: failed to initiate read");
    }

    /*
     * NGX_OK: body was available, ngx_js_body_done already called and
     * already cleared the module ctx.
     * NGX_AGAIN: async read started; ngx_js_body_done will fire later.
     * Either way the Promise will be resolved by the callback.
     */
    return promise;
}


/* ------------------------------------------------------------------ */
/* NginxRequestVariables — exotic class for r.variables                 */
/* ------------------------------------------------------------------ */

/*
 * r.variables is a live read/write object backed by cmcf->variables_hash
 * and r->variables[].  Property get/set/has/enumerate are handled by
 * exotic methods so any nginx variable name works as a JS property.
 *
 *   r.variables.uri           → string value or null if not_found
 *   r.variables.my_var = "x"  → sets an indexed (CHANGEABLE) variable
 *   "uri" in r.variables      → true
 *   Object.keys(r.variables)  → array of all variable names
 */

static void
ngx_js_req_vars_finalizer(JSRuntime *rt, JSValue val)
{
    /* opaque is ngx_http_request_t * — not heap-allocated by us */
    (void) rt; (void) val;
}


/*
 * Helper: convert JSAtom → ngx_str_t + ngx_hash_key.
 * Caller must JS_FreeCString(ctx, name->data) when done.
 * Returns NULL on error (exception already set).
 */
static const char *
ngx_js_atom_to_ngx_str(JSContext *ctx, JSAtom prop,
    ngx_str_t *name, ngx_uint_t *key)
{
    JSValue     name_js;
    const char *cstr;
    size_t      len;

    name_js = JS_AtomToString(ctx, prop);
    if (JS_IsException(name_js)) {
        return NULL;
    }

    cstr = JS_ToCStringLen(ctx, &len, name_js);
    JS_FreeValue(ctx, name_js);
    if (!cstr) {
        return NULL;
    }

    name->data = (u_char *) cstr;
    name->len  = len;
    *key = ngx_hash_key(name->data, name->len);

    return cstr;
}


static JSValue
ngx_js_req_vars_get_property(JSContext *ctx, JSValueConst obj, JSAtom prop,
    JSValueConst receiver)
{
    ngx_http_request_t         *r;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t  *vv;
    const char                 *cstr;
    ngx_str_t                   name;
    ngx_uint_t                  key;

    r = JS_GetOpaque(obj, ngx_js_req_vars_class_id);
    if (r == NULL) {
        return JS_EXCEPTION;
    }

    cstr = ngx_js_atom_to_ngx_str(ctx, prop, &name, &key);
    if (!cstr) {
        return JS_EXCEPTION;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);
    JS_FreeCString(ctx, cstr);

    if (v == NULL) {
        return JS_UNDEFINED;
    }

    if (v->flags & NGX_HTTP_VAR_INDEXED) {
        vv = ngx_http_get_indexed_variable(r, v->index);
        if (vv == NULL || vv->not_found) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx, (const char *) vv->data, vv->len);
    }

    /* Non-indexed: use get_handler directly */
    if (v->get_handler == NULL) {
        return JS_NULL;
    }

    {
        ngx_http_variable_value_t  tmp;
        ngx_memzero(&tmp, sizeof(tmp));
        if (v->get_handler(r, &tmp, v->data) != NGX_OK || tmp.not_found) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx, (const char *) tmp.data, tmp.len);
    }
}


static int
ngx_js_req_vars_set_property(JSContext *ctx, JSValueConst obj, JSAtom prop,
    JSValueConst val, JSValueConst receiver, int flags)
{
    ngx_http_request_t         *r;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t   vv;
    const char                 *name_cstr, *val_cstr;
    ngx_str_t                   name;
    ngx_uint_t                  key;
    size_t                      val_len;
    u_char                     *p;

    r = JS_GetOpaque(obj, ngx_js_req_vars_class_id);
    if (r == NULL) {
        return -1;
    }

    name_cstr = ngx_js_atom_to_ngx_str(ctx, prop, &name, &key);
    if (!name_cstr) {
        return -1;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);
    JS_FreeCString(ctx, name_cstr);

    if (v == NULL) {
        JS_ThrowTypeError(ctx, "r.variables: unknown variable");
        return -1;
    }

    val_cstr = JS_ToCStringLen(ctx, &val_len, val);
    if (!val_cstr) {
        return -1;
    }

    p = ngx_palloc(r->pool, val_len + 1);
    if (p == NULL) {
        JS_FreeCString(ctx, val_cstr);
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }

    ngx_memcpy(p, val_cstr, val_len);
    p[val_len] = '\0';
    JS_FreeCString(ctx, val_cstr);

    if (v->set_handler) {
        ngx_memzero(&vv, sizeof(ngx_http_variable_value_t));
        vv.valid = 1;
        vv.data  = p;
        vv.len   = (ngx_uint_t) val_len;
        v->set_handler(r, &vv, v->data);
        return 1;
    }

    if (v->flags & NGX_HTTP_VAR_INDEXED) {
        r->variables[v->index].len          = (ngx_uint_t) val_len;
        r->variables[v->index].valid        = 1;
        r->variables[v->index].no_cacheable = 0;
        r->variables[v->index].not_found    = 0;
        r->variables[v->index].data         = p;
        return 1;
    }

    JS_ThrowTypeError(ctx, "r.variables: variable is not settable");
    return -1;
}


static int
ngx_js_req_vars_has_property(JSContext *ctx, JSValueConst obj, JSAtom prop)
{
    ngx_http_request_t         *r;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_variable_t        *v;
    const char                 *cstr;
    ngx_str_t                   name;
    ngx_uint_t                  key;

    r = JS_GetOpaque(obj, ngx_js_req_vars_class_id);
    if (r == NULL) {
        return -1;
    }

    cstr = ngx_js_atom_to_ngx_str(ctx, prop, &name, &key);
    if (!cstr) {
        return -1;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    v = ngx_hash_find(&cmcf->variables_hash, key, name.data, name.len);
    JS_FreeCString(ctx, cstr);

    return (v != NULL) ? 1 : 0;
}


static int
ngx_js_req_vars_get_own_property_names(JSContext *ctx, JSPropertyEnum **ptab,
    uint32_t *plen, JSValueConst obj)
{
    ngx_http_request_t         *r;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_hash_elt_t             *elt;
    ngx_http_variable_t        *v;
    JSPropertyEnum             *tab;
    ngx_uint_t                  bi, count, i;

    r = JS_GetOpaque(obj, ngx_js_req_vars_class_id);
    if (r == NULL) {
        return -1;
    }

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);

    /* Count all entries */
    count = 0;
    for (bi = 0; bi < cmcf->variables_hash.size; bi++) {
        elt = cmcf->variables_hash.buckets[bi];
        if (elt == NULL) { continue; }
        while (elt->value != NULL) {
            count++;
            elt = (ngx_hash_elt_t *)
                ngx_align_ptr(&elt->name[0] + elt->len, sizeof(void *));
        }
    }

    tab = js_malloc(ctx, sizeof(JSPropertyEnum) * (count ? count : 1));
    if (tab == NULL) {
        return -1;
    }

    i = 0;
    for (bi = 0; bi < cmcf->variables_hash.size; bi++) {
        elt = cmcf->variables_hash.buckets[bi];
        if (elt == NULL) { continue; }
        while (elt->value != NULL) {
            v = elt->value;
            tab[i].atom = JS_NewAtomLen(ctx,
                                        (const char *) v->name.data,
                                        v->name.len);
            tab[i].is_enumerable = 1;
            i++;
            elt = (ngx_hash_elt_t *)
                ngx_align_ptr(&elt->name[0] + elt->len, sizeof(void *));
        }
    }

    *ptab = tab;
    *plen = (uint32_t) i;
    return 0;
}


static int
ngx_js_req_vars_get_own_property(JSContext *ctx, JSPropertyDescriptor *desc,
    JSValueConst obj, JSAtom prop)
{
    JSValue  val;

    val = ngx_js_req_vars_get_property(ctx, obj, prop, JS_UNDEFINED);
    if (JS_IsUndefined(val)) {
        return FALSE;
    }

    if (JS_IsException(val)) {
        return -1;
    }

    if (desc) {
        desc->flags  = JS_PROP_ENUMERABLE | JS_PROP_WRITABLE;
        desc->value  = val;
        desc->getter = JS_UNDEFINED;
        desc->setter = JS_UNDEFINED;
    } else {
        JS_FreeValue(ctx, val);
    }

    return TRUE;
}


static JSClassExoticMethods ngx_js_req_vars_exotic = {
    .get_own_property       = ngx_js_req_vars_get_own_property,
    .get_own_property_names = ngx_js_req_vars_get_own_property_names,
    .has_property           = ngx_js_req_vars_has_property,
    .get_property           = ngx_js_req_vars_get_property,
    .set_property           = ngx_js_req_vars_set_property,
};


static JSClassDef ngx_js_req_vars_class = {
    "NginxRequestVariables",
    .finalizer = ngx_js_req_vars_finalizer,
    .exotic    = &ngx_js_req_vars_exotic,
};


/*
 * Per-subrequest context — one allocation per r.subrequest() call.
 * Allocated from the parent pool; holds the resolve/reject JSValues.
 */
typedef struct {
    ngx_js_subreq_list_t  *list;    /* back-pointer to parent tracking struct */
    JSContext             *ctx;
    JSRuntime             *rt;
    ngx_js_worker_t       *w;
    JSValue                resolve;
    JSValue                reject;
} ngx_js_subreq_ctx_t;

/*
 * Parent-level subrequest tracking — one per request, stored in the
 * ngx_js_http_module ctx slot once the last in-flight subrequest completes.
 * Counts in-flight subrequests.
 *
 * Why count: with Promise.all([A, B, C]) all three subrequests are in flight
 * simultaneously.  Each individual completion decrements the counter.  Only
 * when the counter reaches zero do we install the resume write_event_handler
 * and wake the parent, guaranteeing that:
 *   (a) every Promise is resolved before any microtask drain runs, and
 *   (b) the parent's write_event_handler fires exactly once, after the last
 *       subrequest's C finalization stack has fully unwound.
 */
struct ngx_js_subreq_list_s {
    ngx_uint_t        pending;   /* subrequests still in flight         */
    ngx_js_worker_t  *w;         /* worker (lives for process lifetime) */
};


/*
 * write_event_handler installed on the parent request by ngx_js_subreq_done
 * when the last in-flight subrequest completes.
 * nginx calls this (via ngx_http_run_posted_requests) after the subrequest
 * finalization machinery has fully unwound — safe to drain JS microtasks here.
 */
static void
ngx_js_subreq_resume(ngx_http_request_t *r)
{
    ngx_js_subreq_list_t  *list;
    JSContext             *job_ctx;

    list = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (list == NULL) {
        return;
    }

    /* Clear slot so a future subrequest group on this request starts fresh */
    ngx_http_set_ctx(r, NULL, ngx_js_http_module);

    /* Restore a safe default write handler before running user JS */
    r->write_event_handler = ngx_http_request_empty_handler;

    /* Run Promise continuations (r.respond() fires here for the parent) */
    while (JS_ExecutePendingJob(list->w->rt, &job_ctx) > 0) { }

    /* Finalize parent request once the top-level handler Promise settles */
    ngx_js_async_check(list->w);
    ngx_js_bf_async_check(list->w);
    ngx_js_sf_async_check(list->w);
    ngx_js_l4_async_check(list->w);
}


/*
 * Post-subrequest callback: resolves this subrequest's JS Promise with
 * {status, headers, body, upstream}.
 *
 * When the last in-flight subrequest for this parent completes (pending
 * reaches zero), installs ngx_js_subreq_resume as write_event_handler and
 * stores the list in the module ctx slot.  nginx then posts the parent and
 * ngx_http_run_posted_requests calls write_event_handler — safely outside
 * every subrequest finalization stack.
 *
 * Earlier completions (pending > 0) simply return: nginx posts the parent,
 * the empty write_event_handler fires and does nothing, and the loop
 * continues with the next pending subrequest.
 */
static ngx_int_t
ngx_js_subreq_done(ngx_http_request_t *sr, void *data, ngx_int_t rc)
{
    ngx_js_subreq_ctx_t  *sctx = data;
    JSContext            *ctx  = sctx->ctx;
    JSValue               result, arg, headers_obj;
    ngx_list_part_t      *part;
    ngx_table_elt_t      *h;
    ngx_uint_t            i;
    u_char               *body_data;
    size_t                body_len;
    u_char                lc_buf[256];
    size_t                klen;

    /* Collect buffered body from sr->out (NGX_HTTP_SUBREQUEST_IN_MEMORY) */
    if (sr->out && sr->out->buf
        && sr->out->buf->last > sr->out->buf->pos)
    {
        body_data = sr->out->buf->pos;
        body_len  = (size_t)(sr->out->buf->last - sr->out->buf->pos);
    } else {
        body_data = (u_char *) "";
        body_len  = 0;
    }

    /* Build {status, headers, body, upstream} result object */
    arg = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, arg, "status",
                      JS_NewInt32(ctx, (int32_t) sr->headers_out.status));

    /* Response headers — lowercase key, first value wins on duplicates */
    headers_obj = JS_NewObject(ctx);

    part = &sr->headers_out.headers.part;
    h    = part->elts;
    for (i = 0; ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h    = part->elts;
            i    = 0;
        }

        if (!h[i].hash) {
            continue;
        }

        klen = h[i].key.len;
        if (klen == 0 || klen >= sizeof(lc_buf)) {
            continue;
        }

        ngx_strlow(lc_buf, h[i].key.data, klen);
        lc_buf[klen] = '\0';

        JS_SetPropertyStr(ctx, headers_obj,
                          (const char *) lc_buf,
                          JS_NewStringLen(ctx,
                                         (const char *) h[i].value.data,
                                         h[i].value.len));
    }

    /* Expose Content-Type from the dedicated headers_out field */
    if (sr->headers_out.content_type.len) {
        JS_SetPropertyStr(ctx, headers_obj, "content-type",
                          JS_NewStringLen(ctx,
                              (const char *) sr->headers_out.content_type.data,
                              sr->headers_out.content_type.len));
    }

    JS_SetPropertyStr(ctx, arg, "headers", headers_obj);

    JS_SetPropertyStr(ctx, arg, "body",
                      JS_NewStringLen(ctx,
                                      (const char *) body_data, body_len));

    /* upstream metadata — non-null only when sr was proxied */
    {
        ngx_http_upstream_state_t  *st = NULL;

        if (sr->upstream_states && sr->upstream_states->nelts > 0) {
            ngx_uint_t  last = sr->upstream_states->nelts - 1;
            st = (ngx_http_upstream_state_t *) sr->upstream_states->elts
                 + last;
        }

        JS_SetPropertyStr(ctx, arg, "upstream",
                          ngx_js_upstream_state_obj(ctx, st));
    }

    result = JS_Call(ctx, sctx->resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, sctx->resolve);
    JS_FreeValue(ctx, sctx->reject);

    /*
     * Force this subrequest to be "active" (c->data == sr) so that
     * ngx_http_finalize_request takes the count-decrementing active branch.
     *
     * With Promise.all([A, B, C]) all three subrequests are in flight
     * simultaneously.  Only the first becomes c->data during
     * ngx_http_subrequest(); when it finalizes its active branch restores
     * c->data to the parent.  Subsequent subrequests then find c->data !=
     * themselves, hit the non-active branch, and skip count--.  The request
     * count stays permanently elevated ("open socket left in connection").
     *
     * Our post_subrequest callback fires before the active/non-active check
     * inside ngx_http_finalize_request, so setting c->data = sr here steers
     * every subrequest through the active branch.  For sequential subrequests
     * (already c->data == sr) this is a no-op.
     */
    if (sr->connection->data != sr && !sr->background) {
        sr->connection->data = sr;
    }

    if (--sctx->list->pending > 0) {
        return NGX_OK;
    }

    /* Last subrequest done — hand the list to the resume handler via the
     * module ctx slot and install the resume write_event_handler.
     * nginx posts the parent after we return; ngx_http_run_posted_requests
     * calls the handler safely outside every subrequest C stack. */
    ngx_http_set_ctx(sr->main, sctx->list, ngx_js_http_module);
    sr->main->write_event_handler = ngx_js_subreq_resume;

    return NGX_OK;
}


/*
 * req.subrequest(uri[, opts]) → Promise<{status, headers, body, upstream}>
 *
 * Issues an nginx internal subrequest to `uri`.  The response body is
 * buffered in memory (NGX_HTTP_SUBREQUEST_IN_MEMORY).  The returned
 * Promise resolves to {status, headers, body, upstream}.
 *
 * opts (optional object):
 *   method  — HTTP method string ("GET", "POST", etc.; default: inherit)
 *   args    — query string to append (ngx_str_t, no leading '?')
 *   headers — plain object of request headers to add to the subrequest
 *
 * Must be used with `await` inside an async handler.
 */
static JSValue
ngx_js_request_subrequest(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t     *op;
    ngx_http_request_t          *r, *sr;
    ngx_js_worker_t             *w;
    ngx_js_subreq_list_t        *list;
    ngx_js_subreq_ctx_t         *sctx;
    ngx_http_post_subrequest_t  *psr;
    JSValue                      resolving[2], promise;
    JSValue                      opts, method_val, args_val, headers_val;
    JSValue                      hkey, hval;
    JSPropertyEnum              *tab;
    uint32_t                     tab_len, j;
    const char                  *uri_cstr, *m_cstr, *k_cstr, *v_cstr;
    size_t                       uri_len;
    ngx_str_t                    uri;
    ngx_str_t                    args;
    ngx_str_t                   *args_ptr;
    ngx_table_elt_t             *he;
    ngx_int_t                    rc;
    u_char                      *lc;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.subrequest(uri): uri required");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    w = JS_GetContextOpaque(ctx);
    if (!w) {
        return JS_ThrowInternalError(ctx, "r.subrequest: no worker context");
    }

    /* Copy URI into the request pool so it outlives the JS string */
    uri_cstr = JS_ToCStringLen(ctx, &uri_len, argv[0]);
    if (!uri_cstr) {
        return JS_EXCEPTION;
    }

    uri.data = ngx_pnalloc(r->pool, uri_len);
    if (!uri.data) {
        JS_FreeCString(ctx, uri_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(uri.data, uri_cstr, uri_len);
    uri.len = uri_len;
    JS_FreeCString(ctx, uri_cstr);

    /* Parse opts */
    args_ptr    = NULL;
    method_val  = JS_UNDEFINED;
    args_val    = JS_UNDEFINED;
    headers_val = JS_UNDEFINED;

    if (argc >= 2 && JS_IsObject(argv[1])) {
        opts        = argv[1];
        method_val  = JS_GetPropertyStr(ctx, opts, "method");
        args_val    = JS_GetPropertyStr(ctx, opts, "args");
        headers_val = JS_GetPropertyStr(ctx, opts, "headers");
    }

    /* opts.args → ngx_str_t for ngx_http_subrequest */
    if (!JS_IsUndefined(args_val) && !JS_IsNull(args_val)) {
        size_t      alen;
        const char *acstr;

        acstr = JS_ToCStringLen(ctx, &alen, args_val);
        if (acstr) {
            args.data = ngx_pnalloc(r->pool, alen);
            if (args.data) {
                ngx_memcpy(args.data, acstr, alen);
                args.len = alen;
                args_ptr = &args;
            }
            JS_FreeCString(ctx, acstr);
        }
    }

    /* Get or create the in-flight subrequest list from the request opaque.
     * pending == 0 means the previous group finished; start a fresh list. */
    list = op->subreq_list;
    if (list == NULL || list->pending == 0) {
        list = ngx_pcalloc(r->pool, sizeof(ngx_js_subreq_list_t));
        if (list == NULL) {
            JS_FreeValue(ctx, method_val);
            JS_FreeValue(ctx, args_val);
            JS_FreeValue(ctx, headers_val);
            return JS_ThrowOutOfMemory(ctx);
        }
        list->w = w;
        op->subreq_list = list;
    }
    list->pending++;

    /* Create Promise */
    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        list->pending--;
        JS_FreeValue(ctx, method_val);
        JS_FreeValue(ctx, args_val);
        JS_FreeValue(ctx, headers_val);
        return promise;
    }

    /* Subrequest context in parent pool */
    sctx = ngx_palloc(r->pool, sizeof(ngx_js_subreq_ctx_t));
    if (!sctx) {
        list->pending--;
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        JS_FreeValue(ctx, method_val);
        JS_FreeValue(ctx, args_val);
        JS_FreeValue(ctx, headers_val);
        return JS_ThrowOutOfMemory(ctx);
    }

    sctx->list    = list;
    sctx->ctx     = ctx;
    sctx->rt      = w->rt;
    sctx->w       = w;
    sctx->resolve = resolving[0];
    sctx->reject  = resolving[1];

    /* Post-subrequest callback in parent pool */
    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (!psr) {
        list->pending--;
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        JS_FreeValue(ctx, method_val);
        JS_FreeValue(ctx, args_val);
        JS_FreeValue(ctx, headers_val);
        return JS_ThrowOutOfMemory(ctx);
    }

    psr->handler = ngx_js_subreq_done;
    psr->data    = sctx;

    rc = ngx_http_subrequest(r, &uri, args_ptr, &sr, psr,
                             NGX_HTTP_SUBREQUEST_IN_MEMORY);
    if (rc != NGX_OK) {
        list->pending--;
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        JS_FreeValue(ctx, method_val);
        JS_FreeValue(ctx, args_val);
        JS_FreeValue(ctx, headers_val);
        return JS_ThrowInternalError(ctx, "r.subrequest: failed (%ld)",
                                     (long) rc);
    }

    /*
     * ngx_http_subrequest() does sr->headers_in = r->headers_in (shallow
     * copy).  The embedded `part` is copied by value, but `last` still
     * points into the PARENT's struct.  Fix it so that pushes below
     * increment sr's own `part.nelts` and stay private to this subrequest.
     */
    sr->headers_in.headers.last = &sr->headers_in.headers.part;

    /* opts.method — set on the subrequest before it runs */
    if (!JS_IsUndefined(method_val) && !JS_IsNull(method_val)) {
        m_cstr = JS_ToCString(ctx, method_val);
        if (m_cstr) {
            static const struct {
                const char  *name;
                ngx_uint_t   code;
            } methods[] = {
                { "GET",     NGX_HTTP_GET     },
                { "POST",    NGX_HTTP_POST    },
                { "PUT",     NGX_HTTP_PUT     },
                { "DELETE",  NGX_HTTP_DELETE  },
                { "HEAD",    NGX_HTTP_HEAD    },
                { "OPTIONS", NGX_HTTP_OPTIONS },
                { "PATCH",   NGX_HTTP_PATCH   },
                { NULL, 0 }
            };
            ngx_uint_t  mi;

            for (mi = 0; methods[mi].name; mi++) {
                if (ngx_strcasecmp((u_char *) m_cstr,
                                   (u_char *) methods[mi].name) == 0)
                {
                    sr->method = methods[mi].code;
                    /* method_name must live in pool */
                    sr->method_name.len  = ngx_strlen(methods[mi].name);
                    sr->method_name.data = ngx_pnalloc(r->pool,
                                                  sr->method_name.len);
                    if (sr->method_name.data) {
                        ngx_memcpy(sr->method_name.data,
                                   methods[mi].name, sr->method_name.len);
                    }
                    break;
                }
            }

            JS_FreeCString(ctx, m_cstr);
        }
    }

    /* opts.headers — add to subrequest's headers_in */
    if (JS_IsObject(headers_val)
        && JS_GetOwnPropertyNames(ctx, &tab, &tab_len, headers_val,
                                  JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0)
    {
        for (j = 0; j < tab_len; j++) {
            hkey = JS_AtomToString(ctx, tab[j].atom);
            hval = JS_GetProperty(ctx, headers_val, tab[j].atom);

            k_cstr = JS_ToCString(ctx, hkey);
            v_cstr = JS_ToCString(ctx, hval);

            if (k_cstr && v_cstr) {
                he = ngx_list_push(&sr->headers_in.headers);
                if (he) {
                    size_t  klen = ngx_strlen(k_cstr);
                    size_t  vlen = ngx_strlen(v_cstr);

                    he->key.data   = ngx_pnalloc(sr->pool, klen + 1);
                    he->value.data = ngx_pnalloc(sr->pool, vlen + 1);
                    lc             = ngx_pnalloc(sr->pool, klen);

                    if (he->key.data && he->value.data && lc) {
                        ngx_memcpy(he->key.data, k_cstr, klen + 1);
                        ngx_memcpy(he->value.data, v_cstr, vlen + 1);
                        he->key.len   = klen;
                        he->value.len = vlen;
                        ngx_strlow(lc, he->key.data, klen);
                        he->lowcase_key = lc;
                        he->hash = ngx_hash_key(lc, klen);
                    }
                }
            }

            JS_FreeCString(ctx, k_cstr);
            JS_FreeCString(ctx, v_cstr);
            JS_FreeValue(ctx, hkey);
            JS_FreeValue(ctx, hval);
            JS_FreeAtom(ctx, tab[j].atom);
        }

        js_free(ctx, tab);
    }

    JS_FreeValue(ctx, method_val);
    JS_FreeValue(ctx, args_val);
    JS_FreeValue(ctx, headers_val);

    return promise;
}


/*
 * req.respond(status, headers, body)
 *
 *   status  — HTTP status code (number)
 *   headers — plain JS object; "content-type" handled specially
 *   body    — string (body text)
 *
 * Sends the complete response and finalizes the request.
 * The JS handler should return immediately after calling this.
 */

/* A header name/value carrying CR or LF would smuggle extra response headers
 * (CRLF injection). Reject the whole header — the same defense js_tenant_handler
 * applies, here for every req.respond caller (host + confined). */
static ngx_int_t
ngx_js_header_has_crlf(const char *s)
{
    if (s == NULL) {
        return 1;
    }
    for (; *s != '\0'; s++) {
        if (*s == '\r' || *s == '\n') {
            return 1;
        }
    }
    return 0;
}


/* Framing-control headers are computed by nginx from the body/connection state;
 * a handler-set value would DUPLICATE or contradict them (a second
 * Content-Length is a request-smuggling / response-desync vector). Drop them —
 * the same defense js_tenant_handler applies, here for every req.respond
 * caller. */
static ngx_int_t
ngx_js_header_is_framing(const char *name)
{
    return ngx_strcasecmp((u_char *) name, (u_char *) "content-length") == 0
        || ngx_strcasecmp((u_char *) name, (u_char *) "transfer-encoding") == 0
        || ngx_strcasecmp((u_char *) name, (u_char *) "connection") == 0;
}


static JSValue
ngx_js_request_respond(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    int32_t                   status;
    const char               *body_cstr;
    size_t                    body_len;
    ngx_buf_t                *b;
    ngx_chain_t               out;
    JSPropertyEnum           *tab;
    uint32_t                  tab_len, j;
    JSValue                   hkey, hval;
    const char               *key_cstr, *val_cstr;
    ngx_table_elt_t          *he;
    size_t                    klen, vlen;
    ngx_int_t                 rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    /*
     * The nginx request may have been abandoned (op->r set to NULL in the
     * content handler's "async_pending already set" error path).  If the
     * async continuation eventually reaches req.respond() after the nginx
     * request was already finalized, silently discard — do not dereference r.
     */
    if (r == NULL) {
        return JS_UNDEFINED;
    }

    /* status — argv[0] or r.statusCode or 200 */
    if (argc >= 1 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_ToInt32(ctx, &status, argv[0])) {
            return JS_EXCEPTION;
        }
        r->headers_out.status = (ngx_uint_t) status;
    } else if (r->headers_out.status == 0) {
        r->headers_out.status = NGX_HTTP_OK;
    }

    /* ---- Response headers from argv[1] JS object (optional) ---- */

    if (argc >= 2 && JS_IsObject(argv[1])
        && JS_GetOwnPropertyNames(ctx, &tab, &tab_len, argv[1],
                                  JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0)
    {
        for (j = 0; j < tab_len; j++) {
            hkey = JS_AtomToString(ctx, tab[j].atom);
            hval = JS_GetProperty(ctx, argv[1], tab[j].atom);

            key_cstr = JS_ToCString(ctx, hkey);
            val_cstr = JS_ToCString(ctx, hval);

            if (key_cstr && val_cstr
                && (ngx_js_header_has_crlf(key_cstr)
                    || ngx_js_header_has_crlf(val_cstr)
                    || ngx_js_header_is_framing(key_cstr)))
            {
                ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "js: response header dropped (name=\"%s\": "
                              "CR/LF or framing-controlled)", key_cstr);
                /* fall through to the frees below; header not applied */

            } else if (key_cstr && val_cstr) {

                if (ngx_strcasecmp((u_char *) key_cstr,
                                   (u_char *) "content-type") == 0)
                {
                    /* Set Content-Type directly on headers_out */
                    vlen = ngx_strlen(val_cstr);

                    r->headers_out.content_type.data =
                        ngx_pnalloc(r->pool, vlen + 1);

                    if (r->headers_out.content_type.data) {
                        ngx_memcpy(r->headers_out.content_type.data,
                                   val_cstr, vlen + 1);
                        r->headers_out.content_type.len  = vlen;
                        r->headers_out.content_type_len  = vlen;
                    }

                } else {
                    /* Generic header via headers_out.headers list */
                    he = ngx_list_push(&r->headers_out.headers);
                    if (he) {
                        klen = ngx_strlen(key_cstr);
                        vlen = ngx_strlen(val_cstr);

                        he->key.data   = ngx_pnalloc(r->pool, klen + 1);
                        he->value.data = ngx_pnalloc(r->pool, vlen + 1);

                        if (he->key.data && he->value.data) {
                            ngx_memcpy(he->key.data,   key_cstr, klen + 1);
                            ngx_memcpy(he->value.data, val_cstr, vlen + 1);
                            he->key.len   = klen;
                            he->value.len = vlen;
                            he->hash      = 1;
                        }
                    }
                }
            }

            if (key_cstr) { JS_FreeCString(ctx, key_cstr); }
            if (val_cstr) { JS_FreeCString(ctx, val_cstr); }

            JS_FreeValue(ctx, hkey);
            JS_FreeValue(ctx, hval);
            JS_FreeAtom(ctx, tab[j].atom);
        }

        js_free(ctx, tab);
    }

    /* ---- Body (optional; default empty string) ---- */

    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        body_cstr = JS_ToCString(ctx, argv[2]);
        if (!body_cstr) {
            return JS_EXCEPTION;
        }
    } else {
        body_cstr = NULL;
    }

    body_len = body_cstr ? ngx_strlen(body_cstr) : 0;
    r->headers_out.content_length_n = (off_t) body_len;

    /*
     * req.respond() must NOT call ngx_http_finalize_request() itself.
     * The correct nginx pattern is: the content handler returns the rc to
     * ngx_http_core_content_phase, which calls ngx_http_finalize_request()
     * exactly once.  We store the rc in the opaque and ngx_js_content_handler
     * reads it after JS_Call returns.
     */

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        if (body_cstr) { JS_FreeCString(ctx, body_cstr); }
        op->respond_rc = rc;
        op->responded  = 1;
        return JS_UNDEFINED;
    }

    /*
     * Build the body buffer.  For an empty body, use a zero-size buf with
     * last_buf = 1 (the nginx idiom, identical to ngx_http_send_special
     * with NGX_HTTP_LAST).  ngx_http_write_filter does not alert on
     * zero-size bufs that carry last_buf or sync flags.
     */
    if (body_len > 0) {
        b = ngx_create_temp_buf(r->pool, body_len);
        if (b == NULL) {
            if (body_cstr) { JS_FreeCString(ctx, body_cstr); }
            op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            op->responded  = 1;
            return JS_UNDEFINED;
        }

        b->last = ngx_cpymem(b->pos, body_cstr, body_len);

    } else {
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            if (body_cstr) { JS_FreeCString(ctx, body_cstr); }
            op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            op->responded  = 1;
            return JS_UNDEFINED;
        }
    }

    b->last_buf      = 1;
    b->last_in_chain = 1;

    if (body_cstr) { JS_FreeCString(ctx, body_cstr); }

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    op->respond_rc = rc;
    op->responded  = 1;

    return JS_UNDEFINED;
}


/*
 * req.log(level, message)
 *
 *   level   — "debug" | "info" | "warn" | "error"  (default: "error")
 *   message — string to log
 *
 * Logs to the nginx error log using the request's connection log context,
 * so the line includes the client address and request id.
 */
static JSValue
ngx_js_request_log(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *level_cstr, *msg_cstr;
    ngx_uint_t                level;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "r.log(level, message): 2 args required");
    }

    level_cstr = JS_ToCString(ctx, argv[0]);
    if (!level_cstr) {
        return JS_EXCEPTION;
    }

    if (ngx_strcasecmp((u_char *) level_cstr, (u_char *) "debug") == 0) {
        level = NGX_LOG_DEBUG;
    } else if (ngx_strcasecmp((u_char *) level_cstr, (u_char *) "info") == 0) {
        level = NGX_LOG_INFO;
    } else if (ngx_strcasecmp((u_char *) level_cstr, (u_char *) "warn") == 0) {
        level = NGX_LOG_WARN;
    } else {
        level = NGX_LOG_ERR;
    }

    JS_FreeCString(ctx, level_cstr);

    msg_cstr = JS_ToCString(ctx, argv[1]);
    if (!msg_cstr) {
        return JS_EXCEPTION;
    }

    ngx_log_error(level, r->connection->log, 0, "js: %s", msg_cstr);

    JS_FreeCString(ctx, msg_cstr);

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* r.sleep(ms) — async timer scoped to a request handler               */
/* ------------------------------------------------------------------ */

typedef struct {
    JSContext        *ctx;
    JSRuntime        *rt;
    ngx_js_worker_t  *w;
    JSValue           resolve;
    JSValue           reject;
    ngx_event_t       ev;
} ngx_js_sleep_timer_t;


static void
ngx_js_sleep_timer_handler(ngx_event_t *ev)
{
    ngx_js_sleep_timer_t  *t = ev->data;
    JSValue                ret;
    JSContext             *job_ctx;

    ret = JS_Call(t->ctx, t->resolve, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(t->ctx, ret);
    JS_FreeValue(t->ctx, t->resolve);
    JS_FreeValue(t->ctx, t->reject);

    while (JS_ExecutePendingJob(t->rt, &job_ctx) > 0) { }

    ngx_js_async_check(t->w);
    ngx_js_bf_async_check(t->w);
    ngx_js_sf_async_check(t->w);
    ngx_js_l4_async_check(t->w);
}


/*
 * r.sleep(ms) → Promise<void>
 *
 * Suspends the async request handler for `ms` milliseconds, returning
 * control to the nginx event loop.  Equivalent to nginx.setTimeout(ms)
 * but allocates the timer from the request pool so it is automatically
 * cleaned up if the request completes before the timer fires.
 */
static JSValue
ngx_js_request_sleep(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    ngx_js_worker_t          *w;
    ngx_js_sleep_timer_t     *t;
    JSValue                   promise, resolving[2];
    uint32_t                  ms;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || JS_ToUint32(ctx, &ms, argv[0])) {
        return JS_ThrowTypeError(ctx, "r.sleep: expected ms argument");
    }

    r = op->r;
    w = JS_GetContextOpaque(ctx);

    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        return promise;
    }

    t = ngx_palloc(r->pool, sizeof(ngx_js_sleep_timer_t));
    if (!t) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowOutOfMemory(ctx);
    }

    t->ctx     = ctx;
    t->rt      = w->rt;
    t->w       = w;
    t->resolve = resolving[0];
    t->reject  = resolving[1];

    ngx_memzero(&t->ev, sizeof(ngx_event_t));
    t->ev.handler = ngx_js_sleep_timer_handler;
    t->ev.data    = t;
    t->ev.log     = r->connection->log;

    ngx_add_timer(&t->ev, (ngx_msec_t) ms);

    return promise;
}


/* ------------------------------------------------------------------ */
/* r.fetch(url[, opts]) — async outbound HTTP/1.1 fetch                */
/* ------------------------------------------------------------------ */

/*
 * Outbound fetch context — allocated from r->pool.
 * Manages a raw TCP connection to the upstream and collects the
 * full HTTP/1.1 response in memory before resolving the JS Promise.
 */
#define NGX_JS_FETCH_RECV_CAP  (256 * 1024)

typedef struct {
    ngx_peer_connection_t   pc;
    ngx_log_t              *log;
    ngx_pool_t             *pool;

    /* send */
    u_char                 *send_pos;   /* next byte to write */
    u_char                 *send_end;   /* one past last byte */
    ngx_buf_t              *send_buf;   /* request buffer */

    /* recv — grows if needed */
    u_char                 *recv_buf;
    size_t                  recv_cap;
    size_t                  recv_len;   /* bytes filled so far */

    /* response parse state */
    int                     resp_status;
    JSValue                 resp_headers;
    size_t                  body_offset;   /* byte offset in recv_buf */
    ssize_t                 content_length; /* -1 = unknown / chunked */
    unsigned                headers_done:1;
    unsigned                resolved:1;

    /* JS context */
    JSContext              *ctx;
    JSRuntime              *rt;
    ngx_js_worker_t        *w;
    ngx_http_request_t     *r;
    JSValue                 resolve;
    JSValue                 reject;

    /* 0-ms timer used to schedule microtask drain after upstream finishes */
    ngx_event_t             ev;
} ngx_js_fetch_ctx_t;


static void ngx_js_fetch_write_handler(ngx_event_t *wev);
static void ngx_js_fetch_read_handler(ngx_event_t *rev);


static void
ngx_js_fetch_resume_handler(ngx_event_t *ev)
{
    ngx_js_fetch_ctx_t  *fctx = ev->data;
    JSContext           *job_ctx;

    while (JS_ExecutePendingJob(fctx->rt, &job_ctx) > 0) { }

    ngx_js_async_check(fctx->w);
    ngx_js_bf_async_check(fctx->w);
    ngx_js_sf_async_check(fctx->w);
    ngx_js_l4_async_check(fctx->w);
}


/*
 * Resolve or reject the fetch Promise and schedule a 0-ms resume timer
 * so that JS microtasks run (and r.respond() fires) after the current
 * event handler returns.  Only resolves once.
 */
static void
ngx_js_fetch_finish(ngx_js_fetch_ctx_t *fctx, JSValue result, int is_error)
{
    JSValue  ret;

    if (fctx->resolved) {
        return;
    }
    fctx->resolved = 1;

    if (is_error) {
        ret = JS_Call(fctx->ctx, fctx->reject, JS_UNDEFINED, 1, &result);
    } else {
        ret = JS_Call(fctx->ctx, fctx->resolve, JS_UNDEFINED, 1, &result);
    }
    JS_FreeValue(fctx->ctx, ret);
    JS_FreeValue(fctx->ctx, result);
    JS_FreeValue(fctx->ctx, fctx->resolve);
    JS_FreeValue(fctx->ctx, fctx->reject);
    JS_FreeValue(fctx->ctx, fctx->resp_headers);

    /* Close the upstream connection */
    if (fctx->pc.connection) {
        ngx_close_connection(fctx->pc.connection);
        fctx->pc.connection = NULL;
    }

    /* Schedule microtask drain on the next event loop tick (0-ms timer) */
    ngx_memzero(&fctx->ev, sizeof(ngx_event_t));
    fctx->ev.handler = ngx_js_fetch_resume_handler;
    fctx->ev.data    = fctx;
    fctx->ev.log     = fctx->r->connection->log;
    ngx_add_timer(&fctx->ev, 0);
}


/*
 * Write handler — sends the HTTP request.
 * Called when the TCP connection is established (or writable again).
 */
static void
ngx_js_fetch_write_handler(ngx_event_t *wev)
{
    ngx_connection_t    *c  = wev->data;
    ngx_js_fetch_ctx_t  *fctx = c->data;
    ssize_t              n;

    if (wev->timedout) {
        ngx_js_fetch_finish(fctx,
            JS_NewString(fctx->ctx, "r.fetch: connect timed out"), 1);
        return;
    }

    while (fctx->send_pos < fctx->send_end) {
        n = c->send(c, fctx->send_pos,
                    (size_t)(fctx->send_end - fctx->send_pos));
        if (n == NGX_ERROR) {
            ngx_js_fetch_finish(fctx,
                JS_NewString(fctx->ctx, "r.fetch: send error"), 1);
            return;
        }
        if (n == NGX_AGAIN) {
            if (ngx_handle_write_event(wev, 0) != NGX_OK) {
                ngx_js_fetch_finish(fctx,
                    JS_NewString(fctx->ctx, "r.fetch: send event error"), 1);
            }
            return;
        }
        fctx->send_pos += n;
    }

    /* Request fully sent — wait for response */
    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_js_fetch_finish(fctx,
            JS_NewString(fctx->ctx, "r.fetch: read event error"), 1);
    }
}


/*
 * Parse the HTTP/1.1 response in recv_buf[0..recv_len).
 * Sets fctx->resp_status, fctx->resp_headers, fctx->body_offset,
 * fctx->content_length, fctx->headers_done.
 * Returns 1 if headers fully parsed, 0 if more data needed.
 */
static int
ngx_js_fetch_parse_headers(ngx_js_fetch_ctx_t *fctx)
{
    u_char  *p, *end, *line_start;
    u_char  *sol, *eol;    /* start/end of line */
    u_char  *colon;
    size_t   klen, vlen;
    u_char   lc[256];
    int      code;

    p   = fctx->recv_buf;
    end = fctx->recv_buf + fctx->recv_len;

    /* --- status line: "HTTP/1.x NNN ..." --- */
    if (fctx->resp_status == 0) {
        /* need at least "HTTP/1.1 200 " */
        if (end - p < 12) {
            return 0;
        }
        if (ngx_strncmp(p, "HTTP/1.", 7) != 0) {
            fctx->resp_status = -1;  /* marker: invalid */
            return 1;
        }
        code = 0;
        p += 9;  /* skip "HTTP/1.x " */
        while (p < end && *p >= '0' && *p <= '9') {
            code = code * 10 + (*p++ - '0');
        }
        fctx->resp_status = code ? code : 200;
    }

    /* skip rest of status line to first \r\n */
    line_start = fctx->recv_buf;
    sol = (u_char *) ngx_strnstr(line_start, "\r\n",
                                  (size_t)(end - line_start));
    if (sol == NULL) {
        return 0;   /* still reading status line */
    }
    sol += 2;   /* skip past \r\n — now at first header line */

    /* --- parse header lines until \r\n\r\n --- */
    for ( ;; ) {
        eol = (u_char *) ngx_strnstr(sol, "\r\n",
                                      (size_t)(end - sol));
        if (eol == NULL) {
            return 0;   /* need more data */
        }

        if (eol == sol) {
            /* blank line → end of headers */
            fctx->body_offset = (size_t)(eol + 2 - fctx->recv_buf);
            fctx->headers_done = 1;
            return 1;
        }

        /* find colon */
        colon = sol;
        while (colon < eol && *colon != ':') {
            colon++;
        }
        if (colon < eol) {
            klen = (size_t)(colon - sol);
            if (klen > 0 && klen < sizeof(lc)) {
                ngx_strlow(lc, sol, klen);
                lc[klen] = '\0';

                /* skip colon and optional leading whitespace */
                u_char *vs = colon + 1;
                while (vs < eol && *vs == ' ') {
                    vs++;
                }
                vlen = (size_t)(eol - vs);

                JS_SetPropertyStr(fctx->ctx, fctx->resp_headers,
                    (const char *) lc,
                    JS_NewStringLen(fctx->ctx, (const char *) vs, vlen));

                /* pick up Content-Length */
                if (klen == 14
                    && ngx_strncasecmp(sol,
                        (u_char *) "content-length", 14) == 0)
                {
                    fctx->content_length = 0;
                    for (u_char *d = vs; d < eol; d++) {
                        if (*d >= '0' && *d <= '9') {
                            fctx->content_length =
                                fctx->content_length * 10 + (*d - '0');
                        }
                    }
                }
            }
        }

        sol = eol + 2;
    }
}


/*
 * Read handler — receives the HTTP response.
 * Accumulates data in recv_buf, then parses headers and body.
 * Resolves the Promise once the full response has arrived.
 */
static void
ngx_js_fetch_read_handler(ngx_event_t *rev)
{
    ngx_connection_t    *c    = rev->data;
    ngx_js_fetch_ctx_t  *fctx = c->data;
    ssize_t              n;
    size_t               body_len;
    JSValue              result, headers_clone;
    u_char              *new_buf;

    if (rev->timedout) {
        ngx_js_fetch_finish(fctx,
            JS_NewString(fctx->ctx, "r.fetch: read timed out"), 1);
        return;
    }

    for ( ;; ) {
        /* Grow recv buffer if full */
        if (fctx->recv_len == fctx->recv_cap) {
            if (fctx->recv_cap >= 16 * 1024 * 1024) {
                ngx_js_fetch_finish(fctx,
                    JS_NewString(fctx->ctx, "r.fetch: response too large"),
                    1);
                return;
            }
            new_buf = ngx_palloc(fctx->pool, fctx->recv_cap * 2);
            if (new_buf == NULL) {
                ngx_js_fetch_finish(fctx,
                    JS_NewString(fctx->ctx, "r.fetch: out of memory"), 1);
                return;
            }
            ngx_memcpy(new_buf, fctx->recv_buf, fctx->recv_len);
            fctx->recv_buf = new_buf;
            fctx->recv_cap *= 2;
        }

        n = c->recv(c, fctx->recv_buf + fctx->recv_len,
                    fctx->recv_cap - fctx->recv_len);

        if (n == NGX_AGAIN) {
            if (ngx_handle_read_event(rev, 0) != NGX_OK) {
                ngx_js_fetch_finish(fctx,
                    JS_NewString(fctx->ctx, "r.fetch: read event error"), 1);
            }
            return;
        }

        if (n == NGX_ERROR || n == 0) {
            /* Connection closed by upstream — response is what we have */
            if (n == NGX_ERROR) {
                ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0,
                               "r.fetch: upstream connection error/close");
            }
            break;
        }

        fctx->recv_len += (size_t) n;

        /* Try to parse headers if not done yet */
        if (!fctx->headers_done) {
            if (!ngx_js_fetch_parse_headers(fctx)) {
                continue;   /* need more data */
            }
        }

        if (!fctx->headers_done) {
            continue;
        }

        /* Check if we have a complete body */
        body_len = fctx->recv_len - fctx->body_offset;
        if (fctx->content_length >= 0
            && body_len >= (size_t) fctx->content_length)
        {
            goto done;
        }
    }

    /* Connection closed — finalize with whatever we have */
    if (!fctx->headers_done) {
        ngx_js_fetch_parse_headers(fctx);
    }

done:
    body_len = fctx->recv_len > fctx->body_offset
               ? fctx->recv_len - fctx->body_offset : 0;

    if (fctx->content_length >= 0
        && body_len > (size_t) fctx->content_length)
    {
        body_len = (size_t) fctx->content_length;
    }

    /* Build {status, headers, body} result object */
    result = JS_NewObject(fctx->ctx);

    JS_SetPropertyStr(fctx->ctx, result, "status",
                      JS_NewInt32(fctx->ctx, fctx->resp_status));

    /* Transfer ownership of resp_headers into result (avoid double-free) */
    headers_clone = JS_DupValue(fctx->ctx, fctx->resp_headers);
    JS_SetPropertyStr(fctx->ctx, result, "headers", headers_clone);

    JS_SetPropertyStr(fctx->ctx, result, "body",
                      JS_NewStringLen(fctx->ctx,
                          (const char *)(fctx->recv_buf + fctx->body_offset),
                          body_len));

    ngx_js_fetch_finish(fctx, result, 0);
}


/*
 * r.fetch(url[, opts]) → Promise<{status, headers, body}>
 *
 * Makes an outbound HTTP/1.1 GET (or opts.method) request to `url`.
 * Supports http:// scheme with IPv4 host:port.
 *
 * opts (optional):
 *   method  — HTTP method (default "GET")
 *   headers — plain object of extra request headers
 *   body    — string request body
 *
 * Returns a Promise that resolves to {status, headers, body}.
 * Must be used with `await` inside an async handler.
 */
static JSValue
ngx_js_request_fetch(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    ngx_js_worker_t          *w;
    ngx_js_fetch_ctx_t       *fctx;
    JSValue                   promise, resolving[2];
    JSValue                   opts, val;
    JSPropertyEnum           *tab;
    uint32_t                  tab_len, j;
    const char               *url_cstr, *method_cstr, *k_cstr, *v_cstr;
    const char               *body_cstr;
    size_t                    url_len, method_len, body_len;
    /* URL components */
    const u_char             *host_start, *host_end;
    in_port_t                 port;
    const u_char             *path_start;
    u_char                    host_buf[256];
    struct sockaddr_in        sin;
    /* request buffer assembly */
    u_char                   *p;
    size_t                    req_len;
    /* extra headers */
    ngx_str_t                 hdr_str;
    ngx_int_t                 rc;

    (void) hdr_str;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "r.fetch(url): url required");
    }

    r = op->r;
    w = JS_GetContextOpaque(ctx);
    if (!w) {
        return JS_ThrowInternalError(ctx, "r.fetch: no worker context");
    }

    /* ---- Parse URL ---- */
    url_cstr = JS_ToCString(ctx, argv[0]);
    if (!url_cstr) {
        return JS_EXCEPTION;
    }
    url_len = ngx_strlen(url_cstr);

    /* Strip "http://" */
    if (url_len < 7 || ngx_strncasecmp((u_char *) url_cstr,
                                        (u_char *) "http://", 7) != 0)
    {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowTypeError(ctx,
            "r.fetch: only http:// scheme supported");
    }

    host_start = (const u_char *) url_cstr + 7;
    host_end   = host_start;

    /* Find end of host (stop at ':', '/', end-of-string) */
    while (*host_end && *host_end != ':' && *host_end != '/') {
        host_end++;
    }

    /* port */
    port = 80;
    if (*host_end == ':') {
        const u_char *pp = host_end + 1;
        port = 0;
        while (*pp >= '0' && *pp <= '9') {
            port = (in_port_t)(port * 10 + (*pp++ - '0'));
        }
        path_start = pp;
    } else {
        path_start = host_end;
    }

    if (*path_start == '\0') {
        path_start = (u_char *) "/";
    }

    /* Copy host to local buffer (NUL-terminated) */
    size_t host_len = (size_t)(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(host_buf)) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowTypeError(ctx, "r.fetch: invalid host");
    }
    ngx_memcpy(host_buf, host_start, host_len);
    host_buf[host_len] = '\0';

    /* Resolve host — only IPv4 for now */
    ngx_memzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(port);
    if (ngx_inet_addr(host_buf, host_len) == INADDR_NONE) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowTypeError(ctx,
            "r.fetch: only IPv4 addresses supported (no DNS)");
    }
    sin.sin_addr.s_addr = ngx_inet_addr(host_buf, host_len);

    /* ---- Parse opts ---- */
    method_cstr = "GET";
    method_len  = 3;
    body_cstr   = NULL;
    body_len    = 0;

    opts = (argc >= 2) ? argv[1] : JS_UNDEFINED;

    JSValue extra_headers = JS_NewObject(ctx);  /* key→value */

    if (!JS_IsUndefined(opts) && JS_IsObject(opts)) {
        val = JS_GetPropertyStr(ctx, opts, "method");
        if (!JS_IsUndefined(val)) {
            method_cstr = JS_ToCStringLen(ctx, &method_len, val);
            JS_FreeValue(ctx, val);
        }

        val = JS_GetPropertyStr(ctx, opts, "body");
        if (!JS_IsUndefined(val) && !JS_IsNull(val)) {
            body_cstr = JS_ToCStringLen(ctx, &body_len, val);
            JS_FreeValue(ctx, val);
        }

        val = JS_GetPropertyStr(ctx, opts, "headers");
        if (JS_IsObject(val)) {
            JS_FreeValue(ctx, val);  /* iterate via GetOwnPropertyNames */
            /* Copy opts.headers into extra_headers */
            JSValue hdrs = JS_GetPropertyStr(ctx, opts, "headers");
            if (JS_GetOwnPropertyNames(ctx, &tab, &tab_len, hdrs,
                                       JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)
                == 0)
            {
                for (j = 0; j < tab_len; j++) {
                    JSValue kv = JS_AtomToString(ctx, tab[j].atom);
                    JSValue vv = JS_GetProperty(ctx, hdrs, tab[j].atom);
                    k_cstr = JS_ToCString(ctx, kv);
                    v_cstr = JS_ToCString(ctx, vv);
                    if (k_cstr && v_cstr) {
                        JS_SetPropertyStr(ctx, extra_headers,
                                          k_cstr, JS_NewString(ctx, v_cstr));
                    }
                    if (k_cstr) JS_FreeCString(ctx, k_cstr);
                    if (v_cstr) JS_FreeCString(ctx, v_cstr);
                    JS_FreeValue(ctx, kv);
                    JS_FreeValue(ctx, vv);
                    JS_FreeAtom(ctx, tab[j].atom);
                }
                js_free(ctx, tab);
            }
            JS_FreeValue(ctx, hdrs);
        } else {
            JS_FreeValue(ctx, val);
        }
    }

    /* ---- Build HTTP request ---- */
    /* Estimate size: method + path + headers + body */
    /* "METHOD /path HTTP/1.1\r\nHost: host:port\r\n
     *  Connection: close\r\n[Content-Length: NNN\r\n][extra]\r\n[body]" */
    req_len = method_len + 1
            + ngx_strlen(path_start) + 9  /* " HTTP/1.1" */
            + 2                             /* \r\n */
            + 7 + host_len + 6 + 2         /* Host: host:port\r\n (port≤5 chars) */
            + 19                            /* Connection: close\r\n */
            + 32                            /* Content-Length: NNN\r\n */
            + body_len
            + 2;                            /* final \r\n */

    /* extra_headers size: iterate to estimate (rough) */
    if (JS_GetOwnPropertyNames(ctx, &tab, &tab_len, extra_headers,
                               JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY) == 0)
    {
        for (j = 0; j < tab_len; j++) {
            JSValue kv = JS_AtomToString(ctx, tab[j].atom);
            JSValue vv = JS_GetProperty(ctx, extra_headers, tab[j].atom);
            k_cstr = JS_ToCString(ctx, kv);
            v_cstr = JS_ToCString(ctx, vv);
            if (k_cstr && v_cstr) {
                req_len += ngx_strlen(k_cstr) + 2
                         + ngx_strlen(v_cstr) + 2;
            }
            if (k_cstr) JS_FreeCString(ctx, k_cstr);
            if (v_cstr) JS_FreeCString(ctx, v_cstr);
            JS_FreeValue(ctx, kv);
            JS_FreeValue(ctx, vv);
            JS_FreeAtom(ctx, tab[j].atom);
        }
        js_free(ctx, tab);
    }

    u_char *req_buf = ngx_palloc(r->pool, req_len + 1);
    if (!req_buf) {
        JS_FreeCString(ctx, url_cstr);
        if (body_cstr) JS_FreeCString(ctx, body_cstr);
        JS_FreeValue(ctx, extra_headers);
        return JS_ThrowOutOfMemory(ctx);
    }

    p = req_buf;
    p = ngx_cpymem(p, method_cstr, method_len);
    *p++ = ' ';
    p = ngx_cpymem(p, path_start, ngx_strlen(path_start));
    p = ngx_cpymem(p, " HTTP/1.1\r\n", 11);
    p = ngx_cpymem(p, "Host: ", 6);
    p = ngx_cpymem(p, host_buf, host_len);
    if (port != 80) {
        p = ngx_snprintf(p, 7, ":%d", (int) port);
    }
    p = ngx_cpymem(p, "\r\n", 2);
    p = ngx_cpymem(p, "Connection: close\r\n", 19);

    if (body_len > 0) {
        p = ngx_snprintf(p, 32, "Content-Length: %uz\r\n", body_len);
    }

    /* Extra request headers from opts.headers */
    if (JS_GetOwnPropertyNames(ctx, &tab, &tab_len, extra_headers,
                               JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY) == 0)
    {
        for (j = 0; j < tab_len; j++) {
            JSValue kv = JS_AtomToString(ctx, tab[j].atom);
            JSValue vv = JS_GetProperty(ctx, extra_headers, tab[j].atom);
            k_cstr = JS_ToCString(ctx, kv);
            v_cstr = JS_ToCString(ctx, vv);
            if (k_cstr && v_cstr) {
                p = ngx_cpymem(p, k_cstr, ngx_strlen(k_cstr));
                p = ngx_cpymem(p, ": ", 2);
                p = ngx_cpymem(p, v_cstr, ngx_strlen(v_cstr));
                p = ngx_cpymem(p, "\r\n", 2);
            }
            if (k_cstr) JS_FreeCString(ctx, k_cstr);
            if (v_cstr) JS_FreeCString(ctx, v_cstr);
            JS_FreeValue(ctx, kv);
            JS_FreeValue(ctx, vv);
            JS_FreeAtom(ctx, tab[j].atom);
        }
        js_free(ctx, tab);
    }

    p = ngx_cpymem(p, "\r\n", 2);

    if (body_cstr && body_len > 0) {
        p = ngx_cpymem(p, body_cstr, body_len);
    }

    if (body_cstr) JS_FreeCString(ctx, body_cstr);
    JS_FreeCString(ctx, url_cstr);
    JS_FreeValue(ctx, extra_headers);

    /* ---- Allocate fetch context ---- */
    fctx = ngx_pcalloc(r->pool, sizeof(ngx_js_fetch_ctx_t));
    if (!fctx) {
        return JS_ThrowOutOfMemory(ctx);
    }

    fctx->pool            = r->pool;
    fctx->log             = r->connection->log;
    fctx->ctx             = ctx;
    fctx->rt              = w->rt;
    fctx->w               = w;
    fctx->r               = r;
    fctx->content_length  = -1;
    fctx->resp_status     = 0;
    fctx->resp_headers    = JS_NewObject(ctx);

    fctx->send_buf = ngx_palloc(r->pool, sizeof(ngx_buf_t));
    if (!fctx->send_buf) {
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memzero(fctx->send_buf, sizeof(ngx_buf_t));
    fctx->send_buf->start = req_buf;
    fctx->send_buf->pos   = req_buf;
    fctx->send_buf->last  = p;
    fctx->send_buf->end   = req_buf + req_len + 1;

    fctx->send_pos = req_buf;
    fctx->send_end = p;

    fctx->recv_buf = ngx_palloc(r->pool, NGX_JS_FETCH_RECV_CAP);
    if (!fctx->recv_buf) {
        return JS_ThrowOutOfMemory(ctx);
    }
    fctx->recv_cap = NGX_JS_FETCH_RECV_CAP;
    fctx->recv_len = 0;

    /* ---- Create Promise ---- */
    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        return promise;
    }
    fctx->resolve = resolving[0];
    fctx->reject  = resolving[1];

    /* ---- Set up peer connection ---- */
    ngx_memzero(&fctx->pc, sizeof(ngx_peer_connection_t));
    fctx->pc.sockaddr  = (struct sockaddr *) &sin;
    fctx->pc.socklen   = sizeof(sin);
    fctx->pc.get       = ngx_event_get_peer;
    fctx->pc.log       = fctx->log;
    fctx->pc.log_error = NGX_ERROR_ERR;

    /* Name string for logging */
    {
        u_char    *peer_name_buf = ngx_palloc(r->pool, host_len + 8);
        ngx_str_t *peer_name_str = ngx_palloc(r->pool, sizeof(ngx_str_t));
        if (peer_name_buf && peer_name_str) {
            u_char *ep = ngx_cpymem(peer_name_buf, host_buf, host_len);
            ep = ngx_snprintf(ep, 8, ":%d", (int) port);
            peer_name_str->data = peer_name_buf;
            peer_name_str->len  = (size_t)(ep - peer_name_buf);
            fctx->pc.name = peer_name_str;
        } else {
            fctx->pc.name = &r->uri;
        }
    }

    /* Connect */
    rc = ngx_event_connect_peer(&fctx->pc);

    if (rc == NGX_ERROR || rc == NGX_DECLINED || rc == NGX_BUSY) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        JS_FreeValue(ctx, fctx->resp_headers);
        return JS_ThrowInternalError(ctx, "r.fetch: connect failed (%d)",
                                     (int) rc);
    }

    ngx_connection_t *conn = fctx->pc.connection;
    conn->data              = fctx;
    conn->write->handler    = ngx_js_fetch_write_handler;
    conn->read->handler     = ngx_js_fetch_read_handler;
    conn->pool              = r->pool;

    if (rc == NGX_OK) {
        /* Connected immediately — start writing */
        ngx_js_fetch_write_handler(conn->write);
    }
    /* rc == NGX_AGAIN: write_handler fires when connection completes */

    return promise;
}


/*
 * r.sendfile(path[, status])
 *
 *   path   — absolute filesystem path to the file
 *   status — HTTP status code (default: 200)
 *
 * Detects the MIME type from the file extension using the nginx types {}
 * map.  Uses nginx's open-file cache when configured.  Throws TypeError
 * for missing / non-regular files, permission errors, and path issues.
 */
static JSValue
ngx_js_request_sendfile(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t    *op;
    ngx_http_request_t         *r;
    const char                 *path_cstr;
    size_t                      path_len;
    ngx_str_t                   path;
    u_char                     *pdata, *p, *last;
    ngx_open_file_info_t        of;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_buf_t                  *b;
    ngx_chain_t                 out;
    ngx_int_t                   rc;
    ngx_uint_t                  status;
    ngx_log_t                  *log;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.sendfile: expected path argument");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->responded) {
        return JS_ThrowTypeError(ctx, "r.sendfile: already responded");
    }

    r   = op->r;
    log = r->connection->log;

    /* Parse optional status (default 200) */
    status = NGX_HTTP_OK;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        int32_t  s;
        if (JS_ToInt32(ctx, &s, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
        if (s < 100 || s > 999) {
            return JS_ThrowRangeError(ctx, "r.sendfile: invalid status code");
        }
        status = (ngx_uint_t) s;
    }

    path_cstr = JS_ToCStringLen(ctx, &path_len, argv[0]);
    if (!path_cstr) {
        return JS_EXCEPTION;
    }

    /* Copy path into pool so it outlives the JS string */
    pdata = ngx_pnalloc(r->pool, path_len + 1);
    if (!pdata) {
        JS_FreeCString(ctx, path_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(pdata, path_cstr, path_len);
    pdata[path_len] = '\0';
    JS_FreeCString(ctx, path_cstr);

    path.data = pdata;
    path.len  = path_len;

    /* Extract file extension (between last '.' and last '/') for MIME type */
    last = pdata + path_len;
    for (p = last - 1; p >= pdata; p--) {
        if (*p == '.') {
            r->exten.data = p + 1;
            r->exten.len  = (size_t) (last - (p + 1));
            break;
        }
        if (*p == '/') {
            break;
        }
    }

    /* Open file via nginx open-file cache */
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_memzero(&of, sizeof(ngx_open_file_info_t));
    of.read_ahead = clcf->read_ahead;
    of.directio   = clcf->directio;
    of.valid      = clcf->open_file_cache_valid;
    of.min_uses   = clcf->open_file_cache_min_uses;
    of.errors     = clcf->open_file_cache_errors;
    of.events     = clcf->open_file_cache_events;

    if (ngx_open_cached_file(clcf->open_file_cache, &path, &of, r->pool)
        != NGX_OK)
    {
        switch (of.err) {
        case 0:
            return JS_ThrowTypeError(ctx,
                                     "r.sendfile: internal open error: %s",
                                     path.data);
        case NGX_ENOENT:
        case NGX_ENOTDIR:
        case NGX_ENAMETOOLONG:
            return JS_ThrowTypeError(ctx,
                                     "r.sendfile: file not found: %s",
                                     path.data);
        case NGX_EACCES:
            return JS_ThrowTypeError(ctx,
                                     "r.sendfile: permission denied: %s",
                                     path.data);
        default:
            return JS_ThrowTypeError(ctx,
                                     "r.sendfile: open failed: %s",
                                     path.data);
        }
    }

    if (!of.is_file) {
        return JS_ThrowTypeError(ctx,
                                 "r.sendfile: not a regular file: %s",
                                 path.data);
    }

    /* Build response headers */
    r->headers_out.status             = status;
    r->headers_out.content_length_n   = of.size;
    r->headers_out.last_modified_time = of.mtime;

    if (ngx_http_set_content_type(r) != NGX_OK) {
        return JS_ThrowTypeError(ctx,
                                 "r.sendfile: failed to set content-type");
    }

    /* Allocate buf and file struct before sending header */
    b = ngx_calloc_buf(r->pool);
    if (!b) {
        return JS_ThrowOutOfMemory(ctx);
    }

    b->file = ngx_pcalloc(r->pool, sizeof(ngx_file_t));
    if (!b->file) {
        return JS_ThrowOutOfMemory(ctx);
    }

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        op->responded  = 1;
        op->respond_rc = rc;
        return JS_UNDEFINED;
    }

    b->file_pos       = 0;
    b->file_last      = of.size;
    b->in_file        = (b->file_last != 0) ? 1 : 0;
    b->last_buf       = (r == r->main) ? 1 : 0;
    b->last_in_chain  = 1;
    b->sync           = (b->last_buf || b->in_file) ? 0 : 1;

    b->file->fd       = of.fd;
    b->file->name     = path;
    b->file->log      = log;
    b->file->directio = of.is_directio;

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    op->responded  = 1;
    op->respond_rc = rc;

    return JS_UNDEFINED;
}


/*
 * r.redirect(url[, code])
 *
 *   url  — Location header value (absolute URL or absolute path)
 *   code — HTTP status code (default: 302)
 *
 * Sends a redirect response with an empty body.
 */
static JSValue
ngx_js_request_redirect(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *url_cstr;
    size_t                    url_len;
    u_char                   *p;
    ngx_table_elt_t          *loc_hdr;
    ngx_buf_t                *b;
    ngx_chain_t               out;
    ngx_int_t                 rc;
    ngx_uint_t                status;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.redirect: expected url argument");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->responded) {
        return JS_ThrowTypeError(ctx, "r.redirect: already responded");
    }

    r = op->r;

    /* Parse optional status (default 302) */
    status = NGX_HTTP_MOVED_TEMPORARILY;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        int32_t  s;
        if (JS_ToInt32(ctx, &s, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
        if (s < 300 || s > 399) {
            return JS_ThrowRangeError(ctx,
                                      "r.redirect: status must be 3xx");
        }
        status = (ngx_uint_t) s;
    }

    url_cstr = JS_ToCStringLen(ctx, &url_len, argv[0]);
    if (!url_cstr) {
        return JS_EXCEPTION;
    }

    /* Copy URL into pool */
    p = ngx_pnalloc(r->pool, url_len);
    if (!p) {
        JS_FreeCString(ctx, url_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, url_cstr, url_len);
    JS_FreeCString(ctx, url_cstr);

    /* Set Location header */
    ngx_http_clear_location(r);

    loc_hdr = ngx_list_push(&r->headers_out.headers);
    if (!loc_hdr) {
        return JS_ThrowOutOfMemory(ctx);
    }

    loc_hdr->hash = 1;
    loc_hdr->next = NULL;
    ngx_str_set(&loc_hdr->key, "Location");
    loc_hdr->value.data = p;
    loc_hdr->value.len  = url_len;

    r->headers_out.location          = loc_hdr;
    r->headers_out.status            = status;
    r->headers_out.content_length_n  = 0;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        op->responded  = 1;
        op->respond_rc = rc;
        return JS_UNDEFINED;
    }

    /* Empty body with last_buf */
    b = ngx_calloc_buf(r->pool);
    if (!b) {
        op->responded  = 1;
        op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return JS_UNDEFINED;
    }

    b->last_buf      = 1;
    b->last_in_chain = 1;
    b->sync          = 1;

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    op->responded  = 1;
    op->respond_rc = rc;

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* req.pass(location) — internal redirect preserving request body      */
/* ------------------------------------------------------------------ */

/*
 * req.pass(location) triggers an nginx internal redirect to `location`.
 *
 * Unlike req.subrequest(), an internal redirect:
 *   • Preserves r->request_body — proxy_pass will stream it to the upstream
 *     without any JS-side buffering.
 *   • Re-enters the full nginx phase engine (server_rewrite → find_config →
 *     … → content) so the new location's proxy_pass / fastcgi_pass fires.
 *   • Is limited to internal locations (add `internal;` in nginx.conf).
 *
 * The JS handler MUST return immediately after calling req.pass().
 * Any subsequent req.respond() will throw "already responded".
 *
 * Typical usage:
 *   location /route/ { }        ← JS content handler calls req.pass(backend)
 *   location /internal/be/ {
 *       internal;
 *       proxy_pass http://upstream/;
 *   }
 */
static JSValue
ngx_js_request_pass(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *loc_cstr;
    size_t                    loc_len;
    u_char                   *p, *qmark;
    ngx_str_t                 uri, args;
    ngx_int_t                 rc;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.pass: expected location argument");
    }

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->responded) {
        return JS_ThrowTypeError(ctx, "r.pass: already responded");
    }

    r = op->r;

    loc_cstr = JS_ToCStringLen(ctx, &loc_len, argv[0]);
    if (!loc_cstr) {
        return JS_EXCEPTION;
    }

    /* Copy URI into request pool — it must outlive the JS string */
    p = ngx_pnalloc(r->pool, loc_len + 1);
    if (p == NULL) {
        JS_FreeCString(ctx, loc_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, loc_cstr, loc_len);
    p[loc_len] = '\0';
    JS_FreeCString(ctx, loc_cstr);

    uri.data = p;
    uri.len  = loc_len;
    ngx_str_null(&args);

    /* Split query string if '?' is present */
    qmark = ngx_strlchr(uri.data, uri.data + uri.len, '?');
    if (qmark != NULL) {
        args.data = qmark + 1;
        args.len  = (size_t) (uri.data + uri.len - (qmark + 1));
        uri.len   = (size_t) (qmark - uri.data);
    }

    rc = ngx_http_internal_redirect(r, &uri, &args);
    if (rc != NGX_DONE) {
        return JS_ThrowInternalError(ctx, "r.pass: internal redirect failed (%d)",
                                     (int) rc);
    }

    /*
     * Mark as "responded" so req.respond() throws, and so the content
     * handler / async_check know to run phases rather than log an error.
     */
    op->responded  = 1;
    op->respond_rc = NGX_DONE;
    op->passed     = 1;

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* Streaming: r.writeHead() + r.write() + r.finish()                   */
/* ------------------------------------------------------------------ */

/*
 * Helper: parse a JS headers object and push entries into
 * r->headers_out, handling Content-Type specially.
 * Mirrors the logic in ngx_js_request_respond.
 */
static void
ngx_js_apply_headers(JSContext *ctx, ngx_http_request_t *r,
    JSValueConst headers_obj)
{
    JSPropertyEnum  *tab;
    uint32_t         tab_len, j;
    JSValue          hkey, hval;
    const char      *key_cstr, *val_cstr;
    ngx_table_elt_t *he;
    size_t           klen, vlen;

    if (!JS_IsObject(headers_obj)) {
        return;
    }

    if (JS_GetOwnPropertyNames(ctx, &tab, &tab_len, headers_obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
    {
        return;
    }

    for (j = 0; j < tab_len; j++) {
        hkey = JS_AtomToString(ctx, tab[j].atom);
        hval = JS_GetProperty(ctx, headers_obj, tab[j].atom);

        key_cstr = JS_ToCString(ctx, hkey);
        val_cstr = JS_ToCString(ctx, hval);

        if (key_cstr && val_cstr) {
            if (ngx_strcasecmp((u_char *) key_cstr,
                               (u_char *) "content-type") == 0)
            {
                vlen = ngx_strlen(val_cstr);
                r->headers_out.content_type.data =
                    ngx_pnalloc(r->pool, vlen + 1);
                if (r->headers_out.content_type.data) {
                    ngx_memcpy(r->headers_out.content_type.data, val_cstr,
                               vlen + 1);
                    r->headers_out.content_type.len = vlen;
                    r->headers_out.content_type_len = vlen;
                }
            } else {
                he = ngx_list_push(&r->headers_out.headers);
                if (he) {
                    klen = ngx_strlen(key_cstr);
                    vlen = ngx_strlen(val_cstr);
                    he->key.data   = ngx_pnalloc(r->pool, klen + 1);
                    he->value.data = ngx_pnalloc(r->pool, vlen + 1);
                    if (he->key.data && he->value.data) {
                        ngx_memcpy(he->key.data,   key_cstr, klen + 1);
                        ngx_memcpy(he->value.data, val_cstr, vlen + 1);
                        he->key.len   = klen;
                        he->value.len = vlen;
                        he->hash      = 1;
                    }
                }
            }
        }

        if (key_cstr) { JS_FreeCString(ctx, key_cstr); }
        if (val_cstr) { JS_FreeCString(ctx, val_cstr); }
        JS_FreeValue(ctx, hkey);
        JS_FreeValue(ctx, hval);
        JS_FreeAtom(ctx, tab[j].atom);
    }

    js_free(ctx, tab);
}


/*
 * r.writeHead(status[, headers])
 *
 * Sends the response status line and headers.  Content-Length is NOT
 * set (streaming mode); nginx uses chunked transfer encoding for
 * HTTP/1.1 or Connection: close for HTTP/1.0.
 *
 * Must be called before r.write() or r.finish().  Throws if headers
 * were already sent (by writeHead, write, or respond).
 */
static JSValue
ngx_js_request_write_head(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    int32_t                   status;
    ngx_int_t                 rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->headers_sent || op->responded) {
        return JS_ThrowTypeError(ctx, "r.writeHead: headers already sent");
    }

    if (argc < 1 || JS_ToInt32(ctx, &status, argv[0])) {
        return JS_ThrowTypeError(ctx, "r.writeHead: expected status argument");
    }

    r = op->r;
    r->headers_out.status = (ngx_uint_t) status;
    /* leave content_length_n = -1 so nginx uses chunked / close */

    if (argc >= 2) {
        ngx_js_apply_headers(ctx, r, argv[1]);
    }

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR) {
        op->responded  = 1;
        op->respond_rc = rc;
        return JS_UNDEFINED;
    }

    op->headers_sent = 1;
    return JS_UNDEFINED;
}


/*
 * r.setHeader(name, value)
 *
 * Stages a response header before the response is sent.  Must be called
 * before r.respond(), r.writeHead(), or the first r.write().
 *
 * "content-type" is handled specially via headers_out.content_type.
 * All other names are stored in headers_out.headers; an existing entry
 * with the same name (case-insensitive) is updated in place; if none
 * exists a new entry is appended.  Setting value to null or "" removes
 * the header (equivalent to r.removeHeader).
 */
static JSValue
ngx_js_request_set_header(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *name_cstr, *val_cstr;
    size_t                    nlen, vlen;
    ngx_list_part_t          *part;
    ngx_table_elt_t          *h, *free_slot;
    ngx_uint_t                i;
    u_char                   *data;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->r == NULL) {
        return JS_UNDEFINED;  /* request already finalized */
    }

    if (op->headers_sent || op->responded) {
        return JS_ThrowTypeError(ctx, "r.setHeader: headers already sent");
    }

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "r.setHeader(name, value) requires 2 arguments");
    }

    name_cstr = JS_ToCStringLen(ctx, &nlen, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    r = op->r;

    /* server: suppress by pointing r->headers_out.server at a hash=0 sentinel */
    if (ngx_strncasecmp((u_char *) name_cstr, (u_char *) "server",
                        nlen) == 0 && nlen == 6)
    {
        JS_FreeCString(ctx, name_cstr);

        if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
            /* Removing: allocate a disabled sentinel so the header filter
             * sees server != NULL (suppresses default) but hash == 0 (not
             * emitted by the general headers loop).                      */
            ngx_table_elt_t  *sent;
            sent = ngx_pcalloc(r->pool, sizeof(ngx_table_elt_t));
            if (sent) {
                r->headers_out.server = sent;   /* hash stays 0 */
            }
            return JS_UNDEFINED;
        }

        /* Setting a custom value: add to headers list and point server at it */
        val_cstr = JS_ToCStringLen(ctx, &vlen, argv[1]);
        if (!val_cstr) {
            return JS_EXCEPTION;
        }
        {
            ngx_table_elt_t  *sv;
            sv = ngx_list_push(&r->headers_out.headers);
            if (sv) {
                data = ngx_pnalloc(r->pool, vlen + 1);
                if (data) {
                    ngx_memcpy(data, val_cstr, vlen + 1);
                    sv->hash           = 1;
                    ngx_str_set(&sv->key, "Server");
                    sv->value.data     = data;
                    sv->value.len      = vlen;
                    r->headers_out.server = sv;
                }
            }
        }
        JS_FreeCString(ctx, val_cstr);
        return JS_UNDEFINED;
    }

    /* content-type lives in its own dedicated field */
    if (ngx_strncasecmp((u_char *) name_cstr, (u_char *) "content-type",
                        nlen) == 0 && nlen == 12)
    {
        JS_FreeCString(ctx, name_cstr);

        if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
            r->headers_out.content_type.len     = 0;
            r->headers_out.content_type.data    = NULL;
            r->headers_out.content_type_len     = 0;
            return JS_UNDEFINED;
        }

        val_cstr = JS_ToCStringLen(ctx, &vlen, argv[1]);
        if (!val_cstr) {
            return JS_EXCEPTION;
        }

        data = ngx_pnalloc(r->pool, vlen + 1);
        if (data) {
            ngx_memcpy(data, val_cstr, vlen + 1);
            r->headers_out.content_type.data = data;
            r->headers_out.content_type.len  = vlen;
            r->headers_out.content_type_len  = vlen;
        }

        JS_FreeCString(ctx, val_cstr);
        return JS_UNDEFINED;
    }

    /* --- generic header: scan headers_out.headers for existing entry --- */

    free_slot = NULL;

    part = &r->headers_out.headers.part;
    h    = part->elts;

    for (i = 0; /* see break below */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h    = part->elts;
            i    = 0;
        }

        if (h[i].hash == 0) {
            if (free_slot == NULL) {
                free_slot = &h[i]; /* remember first cleared slot */
            }
            continue;
        }

        if (h[i].key.len == nlen
            && ngx_strncasecmp(h[i].key.data, (u_char *) name_cstr, nlen) == 0)
        {
            /* found existing entry — remove or overwrite */
            if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
                h[i].hash = 0; /* mark as removed */
                JS_FreeCString(ctx, name_cstr);
                return JS_UNDEFINED;
            }

            val_cstr = JS_ToCStringLen(ctx, &vlen, argv[1]);
            if (!val_cstr) {
                JS_FreeCString(ctx, name_cstr);
                return JS_EXCEPTION;
            }

            data = ngx_pnalloc(r->pool, vlen + 1);
            if (data) {
                ngx_memcpy(data, val_cstr, vlen + 1);
                h[i].value.data = data;
                h[i].value.len  = vlen;
            }

            JS_FreeCString(ctx, val_cstr);
            JS_FreeCString(ctx, name_cstr);
            return JS_UNDEFINED;
        }
    }

    /* not found: if value is null/undefined, nothing to do */
    if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
        JS_FreeCString(ctx, name_cstr);
        return JS_UNDEFINED;
    }

    val_cstr = JS_ToCStringLen(ctx, &vlen, argv[1]);
    if (!val_cstr) {
        JS_FreeCString(ctx, name_cstr);
        return JS_EXCEPTION;
    }

    /* reuse a cleared slot if available, otherwise push a new one */
    h = free_slot ? free_slot : ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        JS_FreeCString(ctx, name_cstr);
        JS_FreeCString(ctx, val_cstr);
        return JS_UNDEFINED; /* OOM: silently skip */
    }

    h->key.data = ngx_pnalloc(r->pool, nlen + 1);
    h->value.data = ngx_pnalloc(r->pool, vlen + 1);

    if (h->key.data && h->value.data) {
        ngx_memcpy(h->key.data, name_cstr, nlen + 1);
        ngx_memcpy(h->value.data, val_cstr, vlen + 1);
        h->key.len   = nlen;
        h->value.len = vlen;
        h->hash      = 1;
    }

    JS_FreeCString(ctx, name_cstr);
    JS_FreeCString(ctx, val_cstr);
    return JS_UNDEFINED;
}


/*
 * r.getHeader(name) → string | null
 *
 * Returns the value of a staged response header, or null if not set.
 * Must be called before headers are sent.
 */
static JSValue
ngx_js_request_get_header(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *name_cstr;
    size_t                    nlen;
    ngx_list_part_t          *part;
    ngx_table_elt_t          *h;
    ngx_uint_t                i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "r.getHeader(name) requires 1 argument");
    }

    name_cstr = JS_ToCStringLen(ctx, &nlen, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    r = op->r;

    if (ngx_strncasecmp((u_char *) name_cstr, (u_char *) "content-type",
                        nlen) == 0 && nlen == 12)
    {
        JS_FreeCString(ctx, name_cstr);
        if (r->headers_out.content_type.len == 0) {
            return JS_NULL;
        }
        return JS_NewStringLen(ctx,
                               (const char *) r->headers_out.content_type.data,
                               r->headers_out.content_type.len);
    }

    part = &r->headers_out.headers.part;
    h    = part->elts;

    for (i = 0; /* see break */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h    = part->elts;
            i    = 0;
        }

        if (h[i].hash == 0) {
            continue;
        }

        if (h[i].key.len == nlen
            && ngx_strncasecmp(h[i].key.data, (u_char *) name_cstr, nlen) == 0)
        {
            JS_FreeCString(ctx, name_cstr);
            return JS_NewStringLen(ctx,
                                   (const char *) h[i].value.data,
                                   h[i].value.len);
        }
    }

    JS_FreeCString(ctx, name_cstr);
    return JS_NULL;
}


/*
 * r.removeHeader(name)
 *
 * Clears a staged response header.  Equivalent to r.setHeader(name, null).
 */
static JSValue
ngx_js_request_remove_header(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  null_argv[2];

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "r.removeHeader(name) requires 1 argument");
    }

    null_argv[0] = argv[0];
    null_argv[1] = JS_NULL;

    return ngx_js_request_set_header(ctx, this_val, 2, null_argv);
}


/*
 * ngx_js_request_respond_typed — shared helper for r.json/text/html.
 *
 * Sends a complete response with a fixed content-type.  Status comes from
 * argv[1] if provided (and not undefined/null), else from the staged
 * r.statusCode, else 200.  Body is body_str (already a JS string value).
 * content_type must be a NUL-terminated C string literal.
 */
static JSValue
ngx_js_respond_typed(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv,
    JSValue body_str, const char *content_type)
{
    JSValue  new_argv[3], result, ct_val, ct_argv[2];

    /* stage the content-type header */
    ct_argv[0] = JS_NewString(ctx, "content-type");
    ct_val     = JS_NewString(ctx, content_type);
    ct_argv[1] = ct_val;
    ngx_js_request_set_header(ctx, this_val, 2, ct_argv);
    JS_FreeValue(ctx, ct_argv[0]);
    JS_FreeValue(ctx, ct_val);

    /* status: argv[1] or staged or 200 */
    new_argv[0] = (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]))
                  ? JS_DupValue(ctx, argv[1])
                  : JS_UNDEFINED; /* respond() will use staged or default 200 */
    new_argv[1] = JS_UNDEFINED;   /* no extra headers object */
    new_argv[2] = JS_DupValue(ctx, body_str);

    result = ngx_js_request_respond(ctx, this_val, 3, new_argv);

    JS_FreeValue(ctx, new_argv[0]);
    JS_FreeValue(ctx, new_argv[2]);
    return result;
}


/*
 * r.json(data[, status])
 *
 * Sends JSON.stringify(data) with Content-Type: application/json.
 * status defaults to r.statusCode or 200.
 */
static JSValue
ngx_js_request_json_respond(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  global, json_obj, stringify_fn, json_str, result;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "r.json(data[, status]) requires at least 1 argument");
    }

    global      = JS_GetGlobalObject(ctx);
    json_obj    = JS_GetPropertyStr(ctx, global, "JSON");
    stringify_fn = JS_GetPropertyStr(ctx, json_obj, "stringify");
    json_str    = JS_Call(ctx, stringify_fn, json_obj, 1, &argv[0]);
    JS_FreeValue(ctx, stringify_fn);
    JS_FreeValue(ctx, json_obj);
    JS_FreeValue(ctx, global);

    if (JS_IsException(json_str)) {
        return JS_EXCEPTION;
    }

    result = ngx_js_respond_typed(ctx, this_val, argc, argv,
                                   json_str, "application/json");
    JS_FreeValue(ctx, json_str);
    return result;
}


/*
 * r.text(str[, status])
 *
 * Sends String(str) with Content-Type: text/plain.
 */
static JSValue
ngx_js_request_text_respond(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  str, result;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "r.text(str[, status]) requires at least 1 argument");
    }

    str    = JS_ToString(ctx, argv[0]);
    result = ngx_js_respond_typed(ctx, this_val, argc, argv,
                                   str, "text/plain");
    JS_FreeValue(ctx, str);
    return result;
}


/*
 * r.html(str[, status])
 *
 * Sends String(str) with Content-Type: text/html.
 */
static JSValue
ngx_js_request_html_respond(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    JSValue  str, result;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "r.html(str[, status]) requires at least 1 argument");
    }

    str    = JS_ToString(ctx, argv[0]);
    result = ngx_js_respond_typed(ctx, this_val, argc, argv,
                                   str, "text/html");
    JS_FreeValue(ctx, str);
    return result;
}


/*
 * r.write(chunk)
 *
 * Sends one body chunk.  If writeHead() has not been called, sends
 * headers automatically (status 200, no extra headers).
 *
 * Does not set last_buf — the stream remains open until r.finish().
 * After r.respond() or r.finish() have been called, write() throws.
 */
static JSValue
ngx_js_request_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    const char               *chunk_cstr;
    size_t                    chunk_len;
    ngx_buf_t                *b;
    ngx_chain_t               out;
    ngx_int_t                 rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->responded) {
        return JS_ThrowTypeError(ctx, "r.write: response already finished");
    }

    r = op->r;

    /* Auto-send headers with 200 if not yet sent */
    if (!op->headers_sent) {
        r->headers_out.status = NGX_HTTP_OK;
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR) {
            op->responded  = 1;
            op->respond_rc = rc;
            return JS_UNDEFINED;
        }
        op->headers_sent = 1;
    }

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        return JS_UNDEFINED;  /* write(undefined) is a no-op */
    }

    chunk_cstr = JS_ToCStringLen(ctx, &chunk_len, argv[0]);
    if (!chunk_cstr) {
        return JS_EXCEPTION;
    }

    if (chunk_len == 0) {
        JS_FreeCString(ctx, chunk_cstr);
        return JS_UNDEFINED;
    }

    b = ngx_create_temp_buf(r->pool, chunk_len);
    if (!b) {
        JS_FreeCString(ctx, chunk_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    b->last          = ngx_cpymem(b->pos, chunk_cstr, chunk_len);
    b->last_buf      = 0;
    b->last_in_chain = 1;
    JS_FreeCString(ctx, chunk_cstr);

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    if (rc == NGX_ERROR) {
        op->responded  = 1;
        op->respond_rc = rc;
    }

    return JS_UNDEFINED;
}


/*
 * r.finish()
 *
 * Closes the streaming response by sending an empty buffer with
 * last_buf=1.  Marks the request as responded.  Throws if already
 * finished.  If writeHead() and write() were never called, auto-sends
 * a 200 header before finishing.
 */
static JSValue
ngx_js_request_finish(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;
    ngx_buf_t                *b;
    ngx_chain_t               out;
    ngx_int_t                 rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->responded) {
        return JS_ThrowTypeError(ctx, "r.finish: response already finished");
    }

    r = op->r;

    /* Auto-send headers with 200 if not yet sent */
    if (!op->headers_sent) {
        r->headers_out.status = NGX_HTTP_OK;
        rc = ngx_http_send_header(r);
        if (rc == NGX_ERROR) {
            op->responded  = 1;
            op->respond_rc = rc;
            return JS_UNDEFINED;
        }
        op->headers_sent = 1;
    }

    b = ngx_calloc_buf(r->pool);
    if (!b) {
        op->responded  = 1;
        op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return JS_UNDEFINED;
    }

    b->last_buf      = 1;
    b->last_in_chain = 1;
    b->sync          = 1;

    out.buf  = b;
    out.next = NULL;

    rc = ngx_http_output_filter(r, &out);
    op->responded  = 1;
    op->respond_rc = rc;

    return JS_UNDEFINED;
}


/*
 * req.sendBuffer(data)
 *
 * Valid only inside a streamingSync or streamingAsync filter.  Appends
 * data to the per-chunk output accumulator (rctx->stream_out); the caller
 * emits the accumulated chain downstream after all filters have run.
 *
 * If called from a wholeBodySync/Async context: silent no-op + NGX_LOG_WARN.
 * If called outside any filter: treated as wholeBody (no-op + warning).
 */
static JSValue
ngx_js_request_send_buffer(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_js_req_ctx_t         *rctx;
    ngx_http_request_t       *r;
    ngx_buf_t                *b;
    ngx_chain_t              *link;
    const char               *data_cstr;
    size_t                    data_len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;
    if (r == NULL) {
        return JS_UNDEFINED;
    }

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);

    /* wholeBody modes (or outside a filter): silent no-op + warning */
    if (rctx == NULL
        || rctx->active_filter_mode == NGX_JS_FILTER_WB_SYNC
        || rctx->active_filter_mode == NGX_JS_FILTER_WB_ASYNC)
    {
        ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                      "js: req.sendBuffer() called outside a streaming"
                      " filter — ignored");
        return JS_UNDEFINED;
    }

    if (argc < 1 || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0])) {
        return JS_UNDEFINED;
    }

    data_cstr = JS_ToCStringLen(ctx, &data_len, argv[0]);
    if (!data_cstr) {
        return JS_EXCEPTION;
    }

    if (data_len == 0) {
        JS_FreeCString(ctx, data_cstr);
        return JS_UNDEFINED;
    }

    b = ngx_create_temp_buf(r->pool, data_len);
    if (!b) {
        JS_FreeCString(ctx, data_cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    b->last = ngx_cpymem(b->pos, data_cstr, data_len);
    JS_FreeCString(ctx, data_cstr);

    link = ngx_alloc_chain_link(r->pool);
    if (!link) {
        return JS_ThrowOutOfMemory(ctx);
    }

    link->buf  = b;
    link->next = NULL;

    if (rctx->stream_out_last == NULL) {
        rctx->stream_out_last = &rctx->stream_out;
    }

    *rctx->stream_out_last = link;
    rctx->stream_out_last  = &link->next;

    return JS_UNDEFINED;
}


/*
 * req.hijack()
 *
 * Takes ownership of the underlying TCP connection fd.
 * Returns the raw file descriptor (number).
 *
 * After calling hijack(), the JS handler must NOT call req.respond(),
 * req.finish(), or return normally.  The content handler will return
 * NGX_DONE (keeping the request alive) so the connection can be used
 * for arbitrary I/O via nginx.repl.listen().  When the connection is
 * closed, the read handler finalises the request.
 *
 * Only callable from a worker process (content handler context).
 */
static JSValue
ngx_js_request_hijack(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t  *op;
    ngx_http_request_t       *r;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_request_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    r = op->r;
    if (r == NULL) {
        return JS_ThrowTypeError(ctx, "req.hijack: request already finalized");
    }

    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowTypeError(ctx,
                                 "req.hijack: only callable from worker process");
    }

    if (!op->hijacked) {
        op->hijacked = 1;
        r->main->count++;
    }

    return JS_NewInt32(ctx, (int32_t) r->connection->fd);
}


static const JSCFunctionListEntry ngx_js_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("method",        ngx_js_request_get, NULL,  0),
    JS_CGETSET_MAGIC_DEF("uri",           ngx_js_request_get, NULL,  1),
    JS_CGETSET_MAGIC_DEF("args",          ngx_js_request_get, NULL,  2),
    JS_CGETSET_MAGIC_DEF("remoteAddr",    ngx_js_request_get, NULL,  3),
    JS_CGETSET_MAGIC_DEF("headers",       ngx_js_request_get, NULL,  4),
    JS_CGETSET_MAGIC_DEF("host",          ngx_js_request_get, NULL,  5),
    JS_CGETSET_MAGIC_DEF("httpVersion",   ngx_js_request_get, NULL,  6),
    JS_CGETSET_MAGIC_DEF("isInternal",    ngx_js_request_get, NULL,  7),
    JS_CGETSET_MAGIC_DEF("keepalive",     ngx_js_request_get, NULL,  8),
    JS_CGETSET_MAGIC_DEF("contentLength", ngx_js_request_get, NULL,  9),
    JS_CGETSET_MAGIC_DEF("contentType",   ngx_js_request_get, NULL, 10),
    JS_CGETSET_MAGIC_DEF("startTime",     ngx_js_request_get, NULL, 11),
    JS_CGETSET_MAGIC_DEF("remotePort",    ngx_js_request_get, NULL, 12),
    JS_CGETSET_MAGIC_DEF("scheme",        ngx_js_request_get, NULL, 13),
    JS_CGETSET_MAGIC_DEF("connection",    ngx_js_request_get, NULL, 14),
    JS_CGETSET_MAGIC_DEF("location",      ngx_js_request_get, NULL, 15),
    JS_CFUNC_DEF("respond",      0, ngx_js_request_respond),
    JS_CFUNC_DEF("json",         1, ngx_js_request_json_respond),
    JS_CFUNC_DEF("text",         1, ngx_js_request_text_respond),
    JS_CFUNC_DEF("html",         1, ngx_js_request_html_respond),
    JS_CFUNC_DEF("setHeader",    2, ngx_js_request_set_header),
    JS_CFUNC_DEF("getHeader",    1, ngx_js_request_get_header),
    JS_CFUNC_DEF("removeHeader", 1, ngx_js_request_remove_header),
    JS_CFUNC_DEF("variable",     1, ngx_js_request_variable),
    JS_CFUNC_DEF("setVariable", 2, ngx_js_request_set_variable),
    JS_CFUNC_DEF("getVar",      1, ngx_js_request_variable),
    JS_CFUNC_DEF("setVar",      2, ngx_js_request_set_variable),
    JS_CFUNC_DEF("subrequest",  1, ngx_js_request_subrequest),
    JS_CFUNC_DEF("log",         2, ngx_js_request_log),
    JS_CGETSET_MAGIC_DEF("queryParams", ngx_js_request_get, NULL, 16),
    JS_CGETSET_MAGIC_DEF("cookies",     ngx_js_request_get, NULL, 17),
    JS_CGETSET_MAGIC_DEF("upstream",    ngx_js_request_get, NULL, 18),
    JS_CGETSET_MAGIC_DEF("variables",     ngx_js_request_get, NULL, 19),
    JS_CGETSET_MAGIC_DEF("body",          ngx_js_request_get, NULL, 20),
    JS_CGETSET_MAGIC_DEF("serverAddr",    ngx_js_request_get, NULL,               21),
    JS_CGETSET_MAGIC_DEF("serverPort",    ngx_js_request_get, NULL,               22),
    JS_CGETSET_MAGIC_DEF("requestLength", ngx_js_request_get, NULL,               23),
    JS_CGETSET_MAGIC_DEF("statusCode",    ngx_js_request_get, ngx_js_request_set, 24),
    JS_CGETSET_MAGIC_DEF("responded",    ngx_js_request_get, NULL,               25),
    JS_CGETSET_MAGIC_DEF("ctx",          ngx_js_request_get, NULL,               26),
    JS_CGETSET_MAGIC_DEF("bodyPreread", ngx_js_request_get, NULL,               27),
    JS_CGETSET_MAGIC_DEF("connCtx",     ngx_js_request_get, NULL,               28),
    JS_CFUNC_DEF("readBody",            0, ngx_js_request_read_body),
    JS_CFUNC_DEF("bodyChunks",          0, ngx_js_request_body_chunks),
    JS_CFUNC_DEF("sendfile",            1, ngx_js_request_sendfile),
    JS_CFUNC_DEF("redirect",            1, ngx_js_request_redirect),
    JS_CFUNC_DEF("pass",               1, ngx_js_request_pass),
    JS_CFUNC_DEF("writeHead",           1, ngx_js_request_write_head),
    JS_CFUNC_DEF("write",               1, ngx_js_request_write),
    JS_CFUNC_DEF("finish",              0, ngx_js_request_finish),
    JS_CFUNC_DEF("sleep",               1, ngx_js_request_sleep),
    JS_CFUNC_DEF("fetch",               1, ngx_js_request_fetch),
    JS_CFUNC_DEF("sendBuffer",          1, ngx_js_request_send_buffer),
    JS_CFUNC_DEF("hijack",              0, ngx_js_request_hijack),
};


ngx_int_t
ngx_js_request_register_class(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_request_class_id, &ngx_js_request_class) < 0) {
        return NGX_ERROR;
    }

    if (JS_NewClass(rt, ngx_js_req_vars_class_id, &ngx_js_req_vars_class) < 0) {
        return NGX_ERROR;
    }

    if (JS_NewClass(rt, ngx_js_body_chunks_class_id,
                    &ngx_js_body_chunks_class) < 0)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/*
 * Install the NginxRequest prototype on the context (once per context).
 * All request instances created by ngx_js_wrap_request() share this
 * prototype instead of allocating a fresh one per request.
 */
/*
 * Koa-style hook chain runner.  Evaluates to a function:
 *   (hooks: Array<fn(req, next)>, req) => Promise | undefined
 *
 * Rules:
 *  - Each hook receives (req, next).  Calling next() runs remaining hooks
 *    and returns a Promise that resolves when they complete.
 *  - If a hook does NOT call next() and does NOT call req.respond(), the
 *    chain auto-advances (backward-compatible).
 *  - If req.responded is true after a hook, the chain stops.
 *  - Throwing (sync or async) rejects the chain Promise.
 */
static const char  ngx_js_hook_chain_src[] =
    "(function(){'use strict';\n"
    "function run(h,i,r){\n"
    "  if(i>=h.length)return;\n"
    "  var nc=false,nr;\n"
    "  function next(){\n"
    "    if(nc)return nr!==undefined?nr:Promise.resolve();\n"
    "    nc=true;nr=run(h,i+1,r);\n"
    "    return nr!==undefined?nr:Promise.resolve();\n"
    "  }\n"
    "  var res;\n"
    "  try{res=h[i](r,next);}catch(e){return Promise.reject(e);}\n"
    "  function adv(){\n"
    "    if(r.responded)return;\n"
    "    if(!nc)return run(h,i+1,r);\n"
    "    return nr;\n"
    "  }\n"
    "  if(res&&typeof res.then==='function')\n"
    "    return res.then(adv,function(e){return Promise.reject(e);});\n"
    "  return adv();\n"
    "}\n"
    "return function(h,r){return run(h,0,r);};\n"
    "})()";


/*
 * Async-continuation helper for the C sync fast path in ngx_js_run_chain: when an
 * arity<2 hook turns out to be async (returns a thenable), await it and then run
 * the REMAINING hooks through the normal chain runner.
 *   __ngx_hook_after__(p, h, r) === p.then(() => __ngx_hook_chain__(h, r))
 */
static const char  ngx_js_hook_after_src[] =
    "(function(){'use strict';\n"
    "return function(p,h,r){\n"
    "  return p.then(function(){return __ngx_hook_chain__(h,r);});\n"
    "};})()";


ngx_int_t
ngx_js_request_install_proto(JSContext *ctx)
{
    JSValue  proto, runner, global;
    JSValue  chunks_proto, symbol, sym_async, self_fn;
    JSAtom   async_iter_atom;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_request_proto_funcs,
                               countof(ngx_js_request_proto_funcs));

    /* JS_SetClassProto takes ownership of proto — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_request_class_id, proto);

    /*
     * Install BodyChunksIterator prototype.
     *   .next()                    — C function defined above
     *   [Symbol.asyncIterator]()   — returns `this` (self-referential)
     */
    chunks_proto = JS_NewObject(ctx);
    if (JS_IsException(chunks_proto)) { return NGX_ERROR; }

    JS_SetPropertyStr(ctx, chunks_proto, "next",
        JS_NewCFunction(ctx, ngx_js_body_chunks_next, "next", 0));

    /* Resolve Symbol.asyncIterator at runtime via the global Symbol object */
    global     = JS_GetGlobalObject(ctx);
    symbol     = JS_GetPropertyStr(ctx, global, "Symbol");
    sym_async  = JS_GetPropertyStr(ctx, symbol, "asyncIterator");
    async_iter_atom = JS_ValueToAtom(ctx, sym_async);
    JS_FreeValue(ctx, sym_async);
    JS_FreeValue(ctx, symbol);
    JS_FreeValue(ctx, global);

    self_fn = JS_NewCFunction(ctx, ngx_js_body_chunks_self,
                              "[Symbol.asyncIterator]", 0);
    JS_DefinePropertyValue(ctx, chunks_proto, async_iter_atom, self_fn,
                           JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE);
    JS_FreeAtom(ctx, async_iter_atom);

    /* Ownership of chunks_proto transferred to the class */
    JS_SetClassProto(ctx, ngx_js_body_chunks_class_id, chunks_proto);

    /* Evaluate and install the hook chain runner as __ngx_hook_chain__ */
    runner = JS_Eval(ctx, ngx_js_hook_chain_src,
                     sizeof(ngx_js_hook_chain_src) - 1,
                     "<hook_chain>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(runner)) {
        return NGX_ERROR;
    }

    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__ngx_hook_chain__", runner);
    JS_FreeValue(ctx, global);
    /* runner ownership transferred to global object */

    /* Install __ngx_hook_after__ (async continuation for the sync fast path) */
    runner = JS_Eval(ctx, ngx_js_hook_after_src,
                     sizeof(ngx_js_hook_after_src) - 1,
                     "<hook_after>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(runner)) {
        return NGX_ERROR;
    }
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__ngx_hook_after__", runner);
    JS_FreeValue(ctx, global);

    return NGX_OK;
}


JSValue
ngx_js_wrap_request(JSContext *ctx, ngx_http_request_t *r)
{
    JSValue                   obj;
    ngx_js_request_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_request_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->r = r;

    obj = JS_NewObjectClass(ctx, ngx_js_request_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* Async body filter check — resume suspended wholeBodyAsync filters    */
/* ------------------------------------------------------------------ */

/*
 * Drive a suspended async generator forward from its current state.
 * bf_p->gen holds the generator; bf_p->gen_out/gen_out_last hold already-
 * yielded output.  Called when bf_p->promise (a gen.next() result) has
 * settled.
 *
 * Processes the settled iter-result {value,done} and continues iterating
 * synchronously until the generator is done, an error occurs, or the next
 * gen.next() Promise is pending again.
 *
 * Returns:
 *   NGX_OK    — generator is done; rctx->wb_body updated; bf_p->promise freed
 *   NGX_AGAIN — new suspension recorded in w->bf_pending; bf_p->promise freed
 *   NGX_ERROR — error logged; bf_p->promise freed
 *
 * In all cases bf_p->promise is freed before return (caller must not free it).
 * bf_p->gen is freed on NGX_OK and NGX_ERROR.
 * On NGX_AGAIN, bf_p->gen ownership passes to the new bf_pending entry.
 */
static ngx_int_t
ngx_js_gen_drive(ngx_js_worker_t *w, ngx_js_bf_pending_t *bf_p,
    ngx_js_req_ctx_t *rctx)
{
    JSContext    *ctx;
    JSRuntime    *rt;
    JSContext    *job_ctx;
    JSValue       iter_result, done_v, value_v, next_fn, next_result;
    JSValue       reason, str_v;
    ngx_chain_t  *gen_out, **gen_out_last;
    const char   *cs;
    int           done;
    ngx_chain_t  *cl;
    size_t        total;
    u_char       *p, *pp;
    ngx_buf_t    *yb;
    ngx_chain_t  *yl;
    const char   *ystr;
    size_t        ylen;
    u_char       *ydata;
    ngx_js_bf_pending_t  *new_bf_p;

    ctx          = w->ctx;
    rt           = w->rt;
    gen_out      = bf_p->gen_out;
    gen_out_last = bf_p->gen_out_last;

    next_fn = JS_GetPropertyStr(ctx, bf_p->gen, "next");

    for (;;) {
        /* Process the currently settled promise */
        iter_result = JS_PromiseResult(ctx, bf_p->promise);
        JS_FreeValue(ctx, bf_p->promise);
        bf_p->promise = JS_UNDEFINED;

        done_v = JS_GetPropertyStr(ctx, iter_result, "done");
        done   = JS_ToBool(ctx, done_v);
        JS_FreeValue(ctx, done_v);

        if (done) {
            JS_FreeValue(ctx, iter_result);
            JS_FreeValue(ctx, next_fn);
            JS_FreeValue(ctx, bf_p->gen);
            bf_p->gen = JS_UNDEFINED;
            goto collapse;
        }

        /* Append yielded value */
        value_v = JS_GetPropertyStr(ctx, iter_result, "value");
        JS_FreeValue(ctx, iter_result);

        if (!JS_IsUndefined(value_v) && !JS_IsNull(value_v)) {
            ystr = JS_ToCStringLen(ctx, &ylen, value_v);
            if (ystr && ylen > 0) {
                ydata = ngx_pnalloc(bf_p->r->pool, ylen);
                if (ydata) {
                    ngx_memcpy(ydata, ystr, ylen);
                    yb = ngx_calloc_buf(bf_p->r->pool);
                    yl = ngx_alloc_chain_link(bf_p->r->pool);
                    if (yb && yl) {
                        yb->pos    = ydata;
                        yb->last   = ydata + ylen;
                        yb->memory = 1;
                        yl->buf    = yb;
                        yl->next   = NULL;
                        *gen_out_last = yl;
                        gen_out_last  = &yl->next;
                        /*
                         * gen_out_last may have pointed at &bf_p->gen_out
                         * (when gen_out was NULL).  After the first append,
                         * sync the local gen_out with bf_p->gen_out so the
                         * collapse loop below sees the chain.
                         */
                        if (gen_out == NULL) {
                            gen_out = bf_p->gen_out;
                        }
                    }
                }
                JS_FreeCString(ctx, ystr);
            }
        }
        JS_FreeValue(ctx, value_v);

        /* Call gen.next() for the next step */
        next_result = JS_Call(ctx, next_fn, bf_p->gen, 0, NULL);
        while (JS_ExecutePendingJob(rt, &job_ctx) > 0) { }

        if (JS_IsException(next_result)) {
            ngx_js_log_exception(ctx, bf_p->r->connection->log);
            JS_FreeValue(ctx, next_result);
            JS_FreeValue(ctx, next_fn);
            JS_FreeValue(ctx, bf_p->gen);
            bf_p->gen = JS_UNDEFINED;
            return NGX_ERROR;
        }

        switch (JS_PromiseState(ctx, next_result)) {

        case JS_PROMISE_FULFILLED:
            /* Already settled — loop to process it */
            bf_p->promise = next_result;
            continue;

        case JS_PROMISE_REJECTED: {
            reason = JS_PromiseResult(ctx, next_result);
            str_v  = JS_ToString(ctx, reason);
            cs     = JS_ToCString(ctx, str_v);
            if (cs) {
                ngx_log_error(NGX_LOG_ERR, bf_p->r->connection->log, 0,
                              "js: generator body filter rejected: %s", cs);
                JS_FreeCString(ctx, cs);
            }
            JS_FreeValue(ctx, str_v);
            JS_FreeValue(ctx, reason);
            JS_FreeValue(ctx, next_result);
            JS_FreeValue(ctx, next_fn);
            JS_FreeValue(ctx, bf_p->gen);
            bf_p->gen = JS_UNDEFINED;
            return NGX_ERROR;
        }

        case JS_PROMISE_PENDING:
            /* Suspend again */
            new_bf_p = ngx_pcalloc(bf_p->r->pool,
                                   sizeof(ngx_js_bf_pending_t));
            if (new_bf_p == NULL) {
                JS_FreeValue(ctx, next_result);
                JS_FreeValue(ctx, next_fn);
                JS_FreeValue(ctx, bf_p->gen);
                bf_p->gen = JS_UNDEFINED;
                return NGX_ERROR;
            }

            new_bf_p->promise      = JS_DupValue(ctx, next_result);
            new_bf_p->gen          = bf_p->gen;  /* ownership transfer */
            bf_p->gen              = JS_UNDEFINED;
            new_bf_p->gen_out      = gen_out;
            /*
             * gen_out_last must point into pool-allocated memory.
             * If no yields have been collected yet, point at
             * new_bf_p->gen_out itself; otherwise gen_out_last already
             * points to the last link's next field (pool-allocated).
             */
            new_bf_p->gen_out_last = (gen_out == NULL)
                                      ? &new_bf_p->gen_out
                                      : gen_out_last;
            new_bf_p->resume_idx   = bf_p->resume_idx;
            new_bf_p->w            = w;
            new_bf_p->r            = bf_p->r;
            new_bf_p->next         = w->bf_pending;
            w->bf_pending          = new_bf_p;

            JS_FreeValue(ctx, next_result);
            JS_FreeValue(ctx, next_fn);
            return NGX_AGAIN;
        }
    }

collapse:
    /* Collapse gen_out chain into rctx->wb_body */
    total = 0;
    for (cl = gen_out; cl; cl = cl->next) {
        ngx_buf_t *b = cl->buf;
        if (ngx_buf_in_memory(b)) {
            total += (size_t)(b->last - b->pos);
        }
    }

    if (total > 0) {
        p = ngx_pnalloc(bf_p->r->pool, total);
        if (p) {
            pp = p;
            for (cl = gen_out; cl; cl = cl->next) {
                ngx_buf_t *b = cl->buf;
                if (ngx_buf_in_memory(b)) {
                    pp = ngx_copy(pp, b->pos, (size_t)(b->last - b->pos));
                }
            }
            rctx->wb_body.data = p;
            rctx->wb_body.len  = total;
        } else {
            rctx->wb_body.data = (u_char *) "";
            rctx->wb_body.len  = 0;
        }
    } else {
        rctx->wb_body.data = (u_char *) "";
        rctx->wb_body.len  = 0;
    }

    return NGX_OK;
}


void
ngx_js_bf_async_check(ngx_js_worker_t *w)
{
    ngx_js_bf_pending_t  *bf_p, **pp;
    ngx_js_req_ctx_t     *rctx;
    ngx_js_loc_conf_t    *jlcf;
    JSContext            *ctx;
    JSValue               result, reason, str;
    const char           *cs;
    size_t                slen;
    u_char               *p;
    ngx_int_t             rc;

    ctx = w->ctx;
    pp  = &w->bf_pending;

    while (*pp != NULL) {
        bf_p = *pp;

        switch (JS_PromiseState(ctx, bf_p->promise)) {

        case JS_PROMISE_FULFILLED:
            *pp = bf_p->next;

            rctx = ngx_http_get_module_ctx(bf_p->r, ngx_js_http_module);
            jlcf = ngx_http_get_module_loc_conf(bf_p->r, ngx_js_http_module);

            /*
             * Generator mode: drive the generator forward until done or
             * pending again, then run remaining filters from resume_idx+1.
             */
            if (!JS_IsUndefined(bf_p->gen)) {
                rc = ngx_js_gen_drive(w, bf_p, rctx);

                if (rc == NGX_AGAIN) {
                    /*
                     * Re-suspended: new entry pushed to w->bf_pending.
                     * We do NOT release the request hold here — the existing
                     * count++ from the original suspension keeps the request
                     * alive for the new suspension.  No finalize call.
                     */
                    break;
                }

                if (rc == NGX_ERROR) {
                    ngx_http_finalize_request(bf_p->r, NGX_ERROR);
                    break;
                }

                /* Done — resume the remaining filter chain */
                rc = ngx_js_body_filter_run_from(bf_p->w, bf_p->r, rctx,
                                                 jlcf, bf_p->resume_idx + 1);
                (void) rc;
                ngx_http_finalize_request(bf_p->r, NGX_DONE);
                break;
            }

            /* Update wb_body with the resolved value if it is a string. */
            result = JS_PromiseResult(ctx, bf_p->promise);
            if (JS_IsString(result)) {
                cs = JS_ToCStringLen(ctx, &slen, result);
                if (cs) {
                    if (slen > 0) {
                        p = ngx_pnalloc(bf_p->r->pool, slen);
                        if (p) {
                            ngx_memcpy(p, cs, slen);
                            rctx->wb_body.data = p;
                            rctx->wb_body.len  = slen;
                        }
                    } else {
                        rctx->wb_body.data = (u_char *) "";
                        rctx->wb_body.len  = 0;
                    }
                    JS_FreeCString(ctx, cs);
                }
            }
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, bf_p->promise);

            rc = ngx_js_body_filter_run_from(bf_p->w, bf_p->r, rctx,
                                             jlcf, bf_p->resume_idx);

            /*
             * Release this suspension's hold on the request.
             * If run_from returned NGX_AGAIN it already incremented count
             * for the new suspension, so NGX_DONE here is a net no-op on
             * the count (new +1, this -1).  If NGX_OK/NGX_ERROR the body
             * was emitted or an error logged; NGX_DONE releases the last
             * hold and finalises the request.
             */
            (void) rc;
            ngx_http_finalize_request(bf_p->r, NGX_DONE);
            break;

        case JS_PROMISE_REJECTED:
            *pp = bf_p->next;

            if (!JS_IsUndefined(bf_p->gen)) {
                JS_FreeValue(ctx, bf_p->gen);
            }

            reason = JS_PromiseResult(ctx, bf_p->promise);
            str    = JS_ToString(ctx, reason);
            cs     = JS_ToCString(ctx, str);
            if (cs) {
                ngx_log_error(NGX_LOG_ERR, bf_p->r->connection->log, 0,
                              "js: async body filter rejected: %s", cs);
                JS_FreeCString(ctx, cs);
            }
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, reason);
            JS_FreeValue(ctx, bf_p->promise);

            ngx_http_finalize_request(bf_p->r, NGX_ERROR);
            break;

        case JS_PROMISE_PENDING:
            pp = &bf_p->next;
            break;
        }
    }
}


/* ------------------------------------------------------------------ */
/* Async streaming filter check — resume suspended streamingAsync       */
/* ------------------------------------------------------------------ */

void
ngx_js_sf_async_check(ngx_js_worker_t *w)
{
    ngx_js_sf_pending_t  *sf_p, **pp;
    ngx_js_req_ctx_t     *rctx;
    ngx_js_loc_conf_t    *jlcf;
    JSContext            *ctx;
    JSValue               result, reason, str;
    const char           *cs;
    size_t                slen;
    u_char               *cur_data;
    size_t                cur_len;
    ngx_int_t             rc;

    ctx = w->ctx;
    pp  = &w->sf_pending;

    while (*pp != NULL) {
        sf_p = *pp;

        switch (JS_PromiseState(ctx, sf_p->promise)) {

        case JS_PROMISE_FULFILLED:
            *pp = sf_p->next;

            rctx = ngx_http_get_module_ctx(sf_p->r, ngx_js_http_module);
            jlcf = ngx_http_get_module_loc_conf(sf_p->r, ngx_js_http_module);

            /* Get the resolved string; if not a string, pass-through. */
            result   = JS_PromiseResult(ctx, sf_p->promise);
            cur_data = sf_p->cur_data;
            cur_len  = sf_p->cur_len;

            if (JS_IsString(result)) {
                cs = JS_ToCStringLen(ctx, &slen, result);
                if (cs) {
                    if (slen > 0) {
                        u_char *p = ngx_pnalloc(sf_p->r->pool, slen);
                        if (p) {
                            ngx_memcpy(p, cs, slen);
                            cur_data = p;
                            cur_len  = slen;
                        } else {
                            cur_data = (u_char *) "";
                            cur_len  = 0;
                        }
                    } else {
                        cur_data = (u_char *) "";
                        cur_len  = 0;
                    }
                    JS_FreeCString(ctx, cs);
                }
            }
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, sf_p->promise);

            /* Reset stream_out before resuming. */
            if (rctx != NULL) {
                rctx->stream_out      = NULL;
                rctx->stream_out_last = &rctx->stream_out;
            }

            rc = ngx_js_streaming_run_from(sf_p->w, sf_p->r, rctx, jlcf,
                                           cur_data, cur_len, sf_p->is_last,
                                           sf_p->resume_idx);
            (void) rc;
            ngx_http_finalize_request(sf_p->r, NGX_DONE);
            break;

        case JS_PROMISE_REJECTED:
            *pp = sf_p->next;

            reason = JS_PromiseResult(ctx, sf_p->promise);
            str    = JS_ToString(ctx, reason);
            cs     = JS_ToCString(ctx, str);
            if (cs) {
                ngx_log_error(NGX_LOG_ERR, sf_p->r->connection->log, 0,
                              "js: async streaming filter rejected: %s", cs);
                JS_FreeCString(ctx, cs);
            }
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, reason);
            JS_FreeValue(ctx, sf_p->promise);

            ngx_http_finalize_request(sf_p->r, NGX_ERROR);
            break;

        case JS_PROMISE_PENDING:
            pp = &sf_p->next;
            break;
        }
    }
}


/* ------------------------------------------------------------------ */
/* Async request finalizer — called from timer handler in ngx_js_com.c */
/* ------------------------------------------------------------------ */

void
ngx_js_async_check(ngx_js_worker_t *w)
{
    ngx_js_async_ctx_t       *actx, **pp;
    ngx_js_request_opaque_t  *req_op;
    JSContext                *ctx;
    JSValue                   reason, str;
    const char               *cstr;

    ctx = w->ctx;
    pp  = &w->async_pending;

    while (*pp != NULL) {
        actx = *pp;

        switch (JS_PromiseState(ctx, actx->promise)) {

        case JS_PROMISE_FULFILLED:
        {
            ngx_http_request_t  *pend_r;
            ngx_int_t            cont_rc;

            *pp = actx->next;

            /* P2 access-phase async hook */
            if (actx->is_p2_hook) {
                ngx_js_p2_hook_resume(w, actx);
                break;
            }

            req_op = JS_GetOpaque(actx->req_obj, ngx_js_request_class_id);

            if (actx->is_hook) {
                /* P1 chain fulfilled */
                if (req_op != NULL && req_op->responded) {
                    /* A hook called req.respond() — just balance the count */
                    JS_FreeValue(ctx, actx->req_obj);
                    JS_FreeValue(ctx, actx->promise);
                    ngx_http_finalize_request(actx->r, NGX_DONE);
                } else {
                    /* All hooks passed — mark chain done, re-enter for handler */
                    ngx_js_req_ctx_t  *rctx_h;
                    pend_r = actx->r;
                    JS_FreeValue(ctx, actx->req_obj);
                    JS_FreeValue(ctx, actx->promise);
                    rctx_h = ngx_http_get_module_ctx(pend_r, ngx_js_http_module);
                    if (rctx_h != NULL) {
                        rctx_h->p1_chain_done = 1;
                    }
                    w->current_request = pend_r;
                    cont_rc = ngx_js_content_handler(pend_r);
                    w->current_request = NULL;
                    /* Balance the r->main->count++ from the chain suspension. */
                    ngx_http_finalize_request(pend_r, cont_rc);
                }
            } else {
                if (req_op != NULL && req_op->passed) {
                    /*
                     * req.pass() was called from an async handler.
                     * ngx_http_internal_redirect already called count++ and
                     * ngx_http_handler(), so the phase engine has already run
                     * for the new location.  Just call finalize to balance
                     * the count++ that the async suspension did.
                     */
                    ngx_http_request_t  *pass_r = actx->r;
                    JS_FreeValue(ctx, actx->req_obj);
                    JS_FreeValue(ctx, actx->promise);
                    ngx_http_finalize_request(pass_r, NGX_DONE);
                    break;
                }

                if (req_op == NULL || !req_op->responded) {
                    ngx_log_error(NGX_LOG_ERR, actx->r->connection->log, 0,
                                  "js: async handler fulfilled without calling "
                                  "req.respond()");
                }
                JS_FreeValue(ctx, actx->req_obj);
                JS_FreeValue(ctx, actx->promise);
                ngx_http_finalize_request(actx->r, NGX_DONE);
            }

            break;
        }

        case JS_PROMISE_REJECTED:
            *pp = actx->next;

            if (actx->is_p2_hook) {
                /*
                 * P2 access-phase hook rejected — log + send 500.
                 * count is 2 here (access phase engine didn't call finalize
                 * for NGX_DONE).  Balance count 2→1, then send 500.
                 */
                reason = JS_PromiseResult(ctx, actx->promise);
                str    = JS_ToString(ctx, reason);
                cstr   = JS_ToCString(ctx, str);
                if (cstr) {
                    ngx_log_error(NGX_LOG_ERR, actx->r->connection->log, 0,
                                  "js P2 async hook exception: %s", cstr);
                    JS_FreeCString(ctx, cstr);
                }
                JS_FreeValue(ctx, str);
                JS_FreeValue(ctx, reason);
                JS_FreeValue(ctx, actx->req_obj);
                JS_FreeValue(ctx, actx->promise);
                /* Balance count 2→1 (no actual finalization at count=2). */
                ngx_http_finalize_request(actx->r, NGX_DONE);
                /* Send 500 (count=1 → actual finalization). */
                ngx_http_finalize_request(actx->r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                break;
            }

            reason = JS_PromiseResult(ctx, actx->promise);
            str    = JS_ToString(ctx, reason);
            cstr   = JS_ToCString(ctx, str);
            if (cstr) {
                ngx_log_error(NGX_LOG_ERR, actx->r->connection->log, 0,
                              "js async exception: %s", cstr);
                JS_FreeCString(ctx, cstr);
            }
            JS_FreeValue(ctx, str);
            JS_FreeValue(ctx, reason);
            JS_FreeValue(ctx, actx->req_obj);
            JS_FreeValue(ctx, actx->promise);
            actx->r->headers_out.status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            ngx_http_finalize_request(actx->r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            break;

        case JS_PROMISE_PENDING:
            pp = &actx->next;  /* still waiting; advance to next entry */
            break;
        }
    }
}


/* ------------------------------------------------------------------ */
/* Graceful-shutdown drain: cancel all pending async requests with 503 */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_async_drain_503 — send 503 to every suspended async HTTP request
 * and clear w->async_pending.
 *
 * Called from ngx_js_exit_process() before SharedWorker sockets are closed,
 * so that a graceful shutdown (SIGQUIT) does not deadlock when a SW thread
 * dies without replying to a pending postMessage.
 *
 * Count invariants at the time this runs:
 *   - Normal content-handler async (neither is_hook nor is_p2_hook):
 *       count = 1  (suspension did count++; content phase did finalize(NGX_DONE)
 *                   which brought 2→1).  One finalize(503) closes the request.
 *   - P1 hook (is_hook == 1): same as above — count = 1.
 *   - P2 access-phase hook (is_p2_hook == 1):
 *       count = 2  (access phase engine returned NGX_OK for our NGX_DONE without
 *                   calling finalize).  Need finalize(NGX_DONE) to go 2→1, then
 *                   finalize(503) to close.
 */
void
ngx_js_async_drain_503(ngx_js_worker_t *w)
{
    ngx_js_async_ctx_t  *actx, *anext;
    JSContext           *ctx;

    ctx = w->ctx;

    for (actx = w->async_pending; actx != NULL; actx = anext) {
        anext = actx->next;

        ngx_log_error(NGX_LOG_WARN, actx->r->connection->log, 0,
                      "js: drain async request with 503 on worker exit");

        JS_FreeValue(ctx, actx->req_obj);
        JS_FreeValue(ctx, actx->promise);

        if (actx->is_p2_hook) {
            /* count = 2: balance 2→1, then send 503 (1→close) */
            ngx_http_finalize_request(actx->r, NGX_DONE);
        }

        ngx_http_finalize_request(actx->r, NGX_HTTP_SERVICE_UNAVAILABLE);
    }

    w->async_pending = NULL;
}


/* ------------------------------------------------------------------ */
/* P2 async hook resume (called from ngx_js_async_check)              */
/* ------------------------------------------------------------------ */

/*
 * Called when a P2 (access-phase) async hook Promise settles.
 *
 * Count invariant when this is called:
 *   - Access phase engine returned NGX_OK for our NGX_DONE without calling
 *     ngx_http_finalize_request, so r->main->count is still 2 (baseline 1
 *     plus the count++ we did on suspension).
 *
 * Resume logic:
 *   - "responded": hook already sent a response internally; handler called
 *     ngx_http_finalize_request internally which decremented count 2→1.
 * P2 chain done (JS chain runner's master Promise settled).
 *   responded → prevent re-entry, double-finalize(NGX_DONE): 2→1→close
 *   all passed → balance finalize(NGX_DONE): 2→1, then advance phase
 */
static void
ngx_js_p2_hook_resume(ngx_js_worker_t *w, ngx_js_async_ctx_t *actx)
{
    ngx_http_request_t       *r;
    ngx_js_request_opaque_t  *req_op;
    ngx_int_t                 responded;

    r         = actx->r;
    req_op    = JS_GetOpaque(actx->req_obj, ngx_js_request_class_id);
    responded = (req_op != NULL && req_op->responded);

    JS_FreeValue(w->ctx, actx->req_obj);
    JS_FreeValue(w->ctx, actx->promise);

    if (responded) {
        r->write_event_handler = ngx_http_request_empty_handler;
        ngx_http_finalize_request(r, NGX_DONE);  /* 2→1 */
        ngx_http_finalize_request(r, NGX_DONE);  /* 1→close */
        return;
    }

    /* All P2 hooks passed — advance to content phase. */
    ngx_http_finalize_request(r, NGX_DONE);  /* balance: 2→1 */
    r->phase_handler++;
    ngx_http_core_run_phases(r);
}


/* ------------------------------------------------------------------ */
/* Per-request loc_conf snapshot                                        */
/* ------------------------------------------------------------------ */

/*
 * Allocate a private copy of the r->loc_conf pointer array in r->pool
 * so that subsequent setters can replace individual module-conf pointers
 * without affecting other requests.  Idempotent — safe to call many times.
 *
 * Individual setters should call this first, then deep-copy their own
 * module's loc_conf struct and update r->loc_conf[module.ctx_index].
 *
 * Must only be called while w->current_request == r.
 */
ngx_int_t
ngx_js_ensure_snapshot(ngx_http_request_t *r)
{
    ngx_js_req_ctx_t  *rctx;
    void             **new_lc;
    size_t             sz;

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL || rctx->loc_conf_snapshotted) {
        return NGX_OK;
    }

    sz     = ngx_http_max_module * sizeof(void *);
    new_lc = ngx_palloc(r->pool, sz);
    if (new_lc == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(new_lc, r->loc_conf, sz);
    r->loc_conf = new_lc;

    rctx->loc_conf_snapshotted = 1;

    return NGX_OK;
}


/*
 * Deep-copy ngx_http_core_loc_conf_t into r->pool so JS setters can modify
 * per-request fields without affecting concurrent requests.  Calls
 * ngx_js_ensure_snapshot first; both are idempotent.
 */
ngx_int_t
ngx_js_ensure_core_snapshot(ngx_http_request_t *r)
{
    ngx_js_req_ctx_t          *rctx;
    ngx_http_core_loc_conf_t  *orig, *copy;

    if (ngx_js_ensure_snapshot(r) != NGX_OK) {
        return NGX_ERROR;
    }

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL || rctx->core_clcf_snapshotted) {
        return NGX_OK;
    }

    orig = r->loc_conf[ngx_http_core_module.ctx_index];

    copy = ngx_palloc(r->pool, sizeof(ngx_http_core_loc_conf_t));
    if (copy == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(copy, orig, sizeof(ngx_http_core_loc_conf_t));

    r->loc_conf[ngx_http_core_module.ctx_index] = copy;
    rctx->core_clcf_snapshotted = 1;

    return NGX_OK;
}


/*
 * Deep-copy an arbitrary module's loc_conf struct into r->pool and update
 * r->loc_conf[module->ctx_index].  Idempotent — safe to call many times.
 */
ngx_int_t
ngx_js_ensure_module_snapshot(ngx_http_request_t *r,
    ngx_module_t *module, size_t conf_size)
{
    ngx_js_req_ctx_t      *rctx;
    ngx_js_module_snap_t  *snap;
    void                  *orig, *copy;
    ngx_uint_t             idx;

    if (ngx_js_ensure_snapshot(r) != NGX_OK) {
        return NGX_ERROR;
    }

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL) {
        return NGX_OK;  /* no request context — no-op */
    }

    idx  = module->ctx_index;
    orig = r->loc_conf[idx];

    for (snap = rctx->snapped; snap; snap = snap->next) {
        if (snap->ctx_index == idx) {
            return NGX_OK;  /* already snapshotted */
        }
    }

    copy = ngx_palloc(r->pool, conf_size);
    if (copy == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(copy, orig, conf_size);
    r->loc_conf[idx] = copy;

    snap = ngx_palloc(r->pool, sizeof(ngx_js_module_snap_t));
    if (snap == NULL) {
        return NGX_ERROR;
    }
    snap->ctx_index = idx;
    snap->orig      = orig;
    snap->next      = rctx->snapped;
    rctx->snapped   = snap;

    return NGX_OK;
}


/*
 * Returns 1 if op_conf is the current request's own conf for this module.
 * Either r->loc_conf[ctx_index] == op_conf (not yet snapshotted — direct
 * match), or the snap list records that op_conf was the original.
 * Returns 0 for cross-location access.
 */
int
ngx_js_is_own_conf(ngx_http_request_t *r, ngx_js_req_ctx_t *rctx,
    ngx_uint_t ctx_index, void *op_conf)
{
    ngx_js_module_snap_t *snap;

    if (r->loc_conf[ctx_index] == op_conf) {
        return 1;  /* not yet snapshotted — direct match */
    }

    if (rctx == NULL) {
        return 0;
    }

    for (snap = rctx->snapped; snap; snap = snap->next) {
        if (snap->ctx_index == ctx_index && snap->orig == op_conf) {
            return 1;  /* already snapshotted from this conf */
        }
    }

    return 0;
}


/* ------------------------------------------------------------------ */
/* Hook helpers                                                         */
/* ------------------------------------------------------------------ */

/*
 * Retrieve hook function at fn_idx from __ngx_hooks__.
 * Caller must JS_FreeValue the returned value.
 */
static JSValue
ngx_js_hook_get_fn(JSContext *ctx, uint32_t fn_idx)
{
    JSValue  global, reg, fn;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_hooks__");
    JS_FreeValue(ctx, global);
    fn  = JS_GetPropertyUint32(ctx, reg, fn_idx);
    JS_FreeValue(ctx, reg);

    return fn;
}


/*
 * Return codes for ngx_js_run_chain.
 */
#define NGX_JS_CHAIN_DONE       0   /* all hooks passed, none responded       */
#define NGX_JS_CHAIN_RESPONDED  1   /* a hook called req.respond() (sync)     */
#define NGX_JS_CHAIN_SUSPENDED  2   /* chain async-suspended, actx queued     */
#define NGX_JS_CHAIN_ERROR    (-1)  /* exception; caller should return 500    */


/*
 * Run a Koa-style hook chain via the __ngx_hook_chain__ JS runner.
 *
 * hook_idxs / n_hooks: C array of hook function indices.
 * req_obj: the NginxRequest wrapper (caller retains ownership).
 * is_p2: 1 → P2 access-phase chain (is_p2_hook flag), 0 → P1 chain (is_hook).
 *
 * On SUSPENDED the master actx is pushed to w->async_pending and count++ is
 * done here; the caller must return NGX_DONE immediately.
 *
 * On all other returns the caller handles cleanup of req_obj / fn / etc.
 */
/*
 * Build the async continuation for the sync fast path: await `promise`, then run
 * the remaining hooks via the normal chain runner. Returns a Promise (owned) or
 * an exception. `promise` and `req_obj` are borrowed.
 */
static JSValue
ngx_js_hook_chain_after(JSContext *ctx, JSValue promise, uint32_t *hook_idxs,
    ngx_uint_t n_hooks, JSValue req_obj)
{
    JSValue     global, after, arr, args[3], combined;
    ngx_uint_t  i;

    arr = JS_NewArray(ctx);
    for (i = 0; i < n_hooks; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             ngx_js_hook_get_fn(ctx, hook_idxs[i]));
    }

    global = JS_GetGlobalObject(ctx);
    after  = JS_GetPropertyStr(ctx, global, "__ngx_hook_after__");
    JS_FreeValue(ctx, global);

    args[0] = promise;
    args[1] = arr;
    args[2] = req_obj;
    combined = JS_Call(ctx, after, JS_UNDEFINED, 3, args);

    JS_FreeValue(ctx, after);
    JS_FreeValue(ctx, arr);

    return combined;
}


static ngx_int_t
ngx_js_run_chain(ngx_js_worker_t *w, ngx_http_request_t *r,
    JSValue req_obj, uint32_t *hook_idxs, ngx_uint_t n_hooks, int is_p2)
{
    JSContext                *ctx, *job_ctx;
    JSValue                   hooks_arr, runner, global, chain_args[2], chain;
    JSValue                   fn, ret, lenv, thenv;
    JSValue                   reason, str;
    const char               *cstr;
    ngx_js_async_ctx_t       *actx;
    ngx_js_request_opaque_t  *req_op;
    ngx_uint_t                i;
    int                       fast;
    int32_t                   len;

    ctx = w->ctx;

    /*
     * Tier-2 fast path: a hook declared function(req) — arity < 2 — cannot
     * reference `next`, so the Koa middleware chain is unnecessary. Such hooks
     * are called directly from C, skipping the per-request JS hook array, the
     * __ngx_hook_chain__ runner and its per-hook next()/adv() closures + promise.
     * A hook with arity >= 2 (may call next()) routes through the runner
     * unchanged; an arity<2 hook that returns a thenable (async) is awaited and
     * the REMAINING hooks run via the runner (__ngx_hook_after__).
     */
    fast = 1;
    for (i = 0; i < n_hooks; i++) {
        fn   = ngx_js_hook_get_fn(ctx, hook_idxs[i]);
        len  = 0;
        lenv = JS_GetPropertyStr(ctx, fn, "length");
        JS_ToInt32(ctx, &len, lenv);
        JS_FreeValue(ctx, lenv);
        JS_FreeValue(ctx, fn);
        if (len >= 2) { fast = 0; break; }
    }

    chain = JS_UNDEFINED;

    if (fast) {
        for (i = 0; i < n_hooks; i++) {
            fn  = ngx_js_hook_get_fn(ctx, hook_idxs[i]);
            ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &req_obj);
            JS_FreeValue(ctx, fn);

            if (JS_IsException(ret)) {
                ngx_js_log_exception(ctx, r->connection->log);
                JS_FreeValue(ctx, ret);
                return NGX_JS_CHAIN_ERROR;
            }

            /* async hook (arity<2 but returned a thenable)? */
            thenv = JS_IsObject(ret) ? JS_GetPropertyStr(ctx, ret, "then")
                                     : JS_UNDEFINED;
            if (JS_IsFunction(ctx, thenv)) {
                JS_FreeValue(ctx, thenv);
                chain = ngx_js_hook_chain_after(ctx, ret, hook_idxs + i + 1,
                                                n_hooks - i - 1, req_obj);
                JS_FreeValue(ctx, ret);
                break;                       /* -> common promise handling */
            }
            JS_FreeValue(ctx, thenv);

            /* sync hook: drain microtasks it scheduled, honour short-circuit */
            while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { /* drain */ }
            JS_FreeValue(ctx, ret);

            req_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);
            if (req_op != NULL && req_op->responded) {
                return NGX_JS_CHAIN_RESPONDED;
            }
        }

        if (i >= n_hooks) {
            return NGX_JS_CHAIN_DONE;         /* all hooks ran synchronously */
        }
        /* else: chain holds the async continuation promise; fall through */

    } else {
        /* Fallback: Koa-style runner (preserves next() middleware + async) */
        hooks_arr = JS_NewArray(ctx);
        for (i = 0; i < n_hooks; i++) {
            JS_SetPropertyUint32(ctx, hooks_arr, (uint32_t) i,
                                 ngx_js_hook_get_fn(ctx, hook_idxs[i]));
        }
        global = JS_GetGlobalObject(ctx);
        runner = JS_GetPropertyStr(ctx, global, "__ngx_hook_chain__");
        JS_FreeValue(ctx, global);

        chain_args[0] = hooks_arr;
        chain_args[1] = req_obj;
        chain = JS_Call(ctx, runner, JS_UNDEFINED, 2, chain_args);
        JS_FreeValue(ctx, runner);
        JS_FreeValue(ctx, hooks_arr);
    }

    /* Drain microtasks — sync hooks (and fulfilled-immediately async ones) run */
    while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { /* drain */ }

    if (JS_IsException(chain)) {
        ngx_js_log_exception(ctx, r->connection->log);
        JS_FreeValue(ctx, chain);
        return NGX_JS_CHAIN_ERROR;
    }

    /* chain == undefined: all hooks ran synchronously */
    if (!JS_IsObject(chain)) {
        JS_FreeValue(ctx, chain);
        req_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);
        return (req_op && req_op->responded) ? NGX_JS_CHAIN_RESPONDED
                                             : NGX_JS_CHAIN_DONE;
    }

    switch (JS_PromiseState(ctx, chain)) {

    case JS_PROMISE_PENDING:
        actx = ngx_pcalloc(r->pool, sizeof(ngx_js_async_ctx_t));
        if (actx == NULL) {
            JS_FreeValue(ctx, chain);
            return NGX_JS_CHAIN_ERROR;
        }
        actx->r          = r;
        actx->req_obj    = JS_DupValue(ctx, req_obj);
        actx->promise    = chain;   /* ownership transferred */
        actx->is_hook    = (unsigned) !is_p2;
        actx->is_p2_hook = (unsigned)  is_p2;
        actx->next       = w->async_pending;
        w->async_pending = actx;
        r->main->count++;
        return NGX_JS_CHAIN_SUSPENDED;

    case JS_PROMISE_REJECTED:
        reason = JS_PromiseResult(ctx, chain);
        str    = JS_ToString(ctx, reason);
        cstr   = JS_ToCString(ctx, str);
        if (cstr) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "js hook chain rejected: %s", cstr);
            JS_FreeCString(ctx, cstr);
        }
        JS_FreeValue(ctx, str);
        JS_FreeValue(ctx, reason);
        JS_FreeValue(ctx, chain);
        return NGX_JS_CHAIN_ERROR;

    default:  /* JS_PROMISE_FULFILLED */
        JS_FreeValue(ctx, chain);
        req_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);
        return (req_op && req_op->responded) ? NGX_JS_CHAIN_RESPONDED
                                             : NGX_JS_CHAIN_DONE;
    }
}


/*
 * Run all response hooks for this location in the header filter phase.
 * Hooks can read/modify req.status and req.headersOut.
 * Hooks must NOT call req.respond() — if they do, a warning is logged.
 * Runs for main requests only (caller ensures r == r->main).
 */
static void
ngx_js_response_hooks_run(ngx_js_worker_t *w, ngx_http_request_t *r,
    ngx_js_loc_conf_t *jlcf)
{
    JSContext                *ctx, *job_ctx;
    JSValue                   req_obj, fn, result;
    ngx_js_request_opaque_t  *req_op;
    uint32_t                 *hooks;
    ngx_uint_t                i;

    ctx     = w->ctx;
    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        ngx_js_log_exception(ctx, r->connection->log);
        return;
    }

    hooks = jlcf->response_hooks->elts;

    for (i = 0; i < jlcf->response_hooks->nelts; i++) {
        fn     = ngx_js_hook_get_fn(ctx, hooks[i]);
        result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &req_obj);
        JS_FreeValue(ctx, fn);

        while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { /* drain */ }

        if (JS_IsException(result)) {
            ngx_js_log_exception(ctx, r->connection->log);
        } else {
            /* Warn if hook mistakenly called req.respond() */
            req_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);
            if (req_op != NULL && req_op->responded) {
                ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
                              "js: req.respond() called from response hook "
                              "-- ignored; use req.headersOut and req.status "
                              "to modify the response");
                req_op->responded = 0;  /* reset so it doesn't confuse callers */
            }
        }

        JS_FreeValue(ctx, result);
    }

    JS_FreeValue(ctx, req_obj);
}



/*
 * Access phase handler for JS-Pilgrim P2 global and server hooks.
 *
 * Runs nginx.http.addHook() (global) then server.addHook() (per-server)
 * hooks via the Koa-style JS chain runner (P9).  Each hook receives
 * (req, next); calling next() runs the remaining hooks and returns a
 * Promise, enabling pre/post wrapping.  Auto-advance is preserved for
 * hooks that do not call next().
 *
 * The chain is run ONCE per request (one-shot).  On suspension, a master
 * actx is queued; ngx_js_p2_hook_resume handles the settled Promise.
 */
static ngx_int_t
ngx_js_http_access_handler(ngx_http_request_t *r)
{
    ngx_js_http_main_conf_t  *jmcf;
    ngx_js_http_srv_conf_t   *jscf;
    ngx_js_conf_t            *jcf;
    ngx_js_worker_t          *w;
    JSContext                *ctx;
    JSValue                   req_obj;
    ngx_js_request_opaque_t  *req_op;
    ngx_int_t                 chain_rc;
    ngx_int_t                 respond_rc;
    uint32_t                 *all_hooks;
    ngx_uint_t                n_global, n_server, n_all, i;

    /* Skip subrequests */
    if (r != r->main) {
        return NGX_DECLINED;
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    w   = jcf->worker;

    if (w == NULL || w->ctx == NULL) {
        return NGX_DECLINED;
    }

    jmcf = ngx_http_get_module_main_conf(r, ngx_js_http_module);
    jscf = ngx_http_get_module_srv_conf(r, ngx_js_http_module);

    n_global = (jmcf && jmcf->hooks) ? jmcf->hooks->nelts : 0;
    n_server = (jscf && jscf->hooks) ? jscf->hooks->nelts : 0;

    /* Fast path: no hooks registered */
    if (n_global == 0 && n_server == 0) {
        return NGX_DECLINED;
    }

    /* Ensure per-request context exists */
    if (ngx_http_get_module_ctx(r, ngx_js_http_module) == NULL) {
        ngx_js_req_ctx_t  *rctx;
        rctx = ngx_pcalloc(r->pool, sizeof(ngx_js_req_ctx_t));
        if (rctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        rctx->ctx_obj = JS_UNDEFINED;
        ngx_http_set_ctx(r, rctx, ngx_js_http_module);
    }

    ctx     = w->ctx;
    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    w->current_request = r;

    /* Build a single flat array: global hooks first, then server hooks */
    n_all     = n_global + n_server;
    all_hooks = ngx_palloc(r->pool, n_all * sizeof(uint32_t));
    if (all_hooks == NULL) {
        JS_FreeValue(ctx, req_obj);
        w->current_request = NULL;
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    if (n_global) {
        ngx_memcpy(all_hooks, jmcf->hooks->elts, n_global * sizeof(uint32_t));
    }
    if (n_server) {
        ngx_memcpy(all_hooks + n_global, jscf->hooks->elts,
                   n_server * sizeof(uint32_t));
    }
    (void) i;   /* suppress unused-variable warning */

    chain_rc = ngx_js_run_chain(w, r, req_obj, all_hooks, n_all, 1 /* P2 */);

    switch (chain_rc) {

    case NGX_JS_CHAIN_SUSPENDED:
        JS_FreeValue(ctx, req_obj);
        w->current_request     = NULL;
        w->request_deadline_ms = 0;
        return NGX_DONE;

    case NGX_JS_CHAIN_RESPONDED:
        req_op     = JS_GetOpaque(req_obj, ngx_js_request_class_id);
        respond_rc = req_op ? req_op->respond_rc : NGX_HTTP_INTERNAL_SERVER_ERROR;
        JS_FreeValue(ctx, req_obj);
        w->current_request     = NULL;
        w->request_deadline_ms = 0;
        r->write_event_handler = ngx_http_request_empty_handler;
        ngx_http_finalize_request(r, respond_rc);
        return NGX_DONE;

    case NGX_JS_CHAIN_ERROR:
        JS_FreeValue(ctx, req_obj);
        w->current_request     = NULL;
        w->request_deadline_ms = 0;
        return NGX_HTTP_INTERNAL_SERVER_ERROR;

    default:  /* NGX_JS_CHAIN_DONE */
        JS_FreeValue(ctx, req_obj);
        w->current_request     = NULL;
        w->request_deadline_ms = 0;
        return NGX_DECLINED;
    }
}


/* ------------------------------------------------------------------ */
/* Content handler — called by NGINX in each worker process            */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_content_handler(ngx_http_request_t *r)
{
    ngx_js_conf_t            *jcf;
    ngx_js_loc_conf_t        *jlcf;
    ngx_js_worker_t          *w;
    ngx_js_req_ctx_t         *rctx;
    JSContext                *ctx;
    JSValue                   global, registry, fn, req_obj, result;
    ngx_js_request_opaque_t  *req_op;
    ngx_int_t                 final_rc;

    jlcf = ngx_http_get_module_loc_conf(r, ngx_js_http_module);
    jcf  = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_js_module);

    w = jcf->worker;

    if (w == NULL || w->ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js: worker runtime not available");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);

    if (rctx == NULL) {
        /* First entry — one-time per-request setup */
        ngx_js_bcast_ensure_active(w);
        ngx_js_sw_ensure_all_active(w->ctx);

        rctx = ngx_pcalloc(r->pool, sizeof(ngx_js_req_ctx_t));
        if (rctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        rctx->write_mode     = NGX_JS_WRITE_GLOBAL;
        rctx->read_mode      = NGX_JS_WRITE_GLOBAL;
        rctx->body_bufs_last = &rctx->body_bufs;
        rctx->ctx_obj        = JS_UNDEFINED;
        ngx_http_set_ctx(r, rctx, ngx_js_http_module);
    }
    /* else: re-entry after P1 chain async suspend; p1_chain_done will be set */

    ctx      = w->ctx;
    global   = JS_GetGlobalObject(ctx);
    registry = JS_GetPropertyStr(ctx, global, "__ngx_handlers__");
    JS_FreeValue(ctx, global);

    if (jlcf->handler_idx >= 0) {
        fn = JS_GetPropertyUint32(ctx, registry, (uint32_t) jlcf->handler_idx);
    } else {
        fn = JS_UNDEFINED;
    }
    JS_FreeValue(ctx, registry);

    /* fn may not be a function when only hooks are set (handler_idx == -1).
     * We check later after the hook phase, only failing if no hooks responded. */

    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        JS_FreeValue(ctx, fn);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * Read nginx.workerMemoryLimit and nginx.workerRequestTimeout live from
     * JS before every JS_Call so that request handlers can reconfigure them
     * at runtime and have the new value take effect on the next request.
     *
     * workerMemoryLimit: cap on additional heap growth above fork baseline.
     *   0 → remove limit (set to SIZE_MAX).
     * workerRequestTimeout: per-request JS execution deadline in ms.
     *   0 → no deadline (interrupt handler is a no-op when deadline == 0).
     */
    {
        JSValue  global, nginx_obj, val;
        int64_t  n;

        global    = JS_GetGlobalObject(ctx);
        nginx_obj = JS_GetPropertyStr(ctx, global, "nginx");
        JS_FreeValue(ctx, global);

        val = JS_GetPropertyStr(ctx, nginx_obj, "workerMemoryLimit");
        if (!JS_IsException(val)
            && JS_ToInt64(ctx, &n, val) == 0 && n > 0)
        {
            JS_SetMemoryLimit(w->rt, w->baseline_malloc_size + (size_t) n);
        } else {
            JS_SetMemoryLimit(w->rt, (size_t) -1);  /* unlimited */
        }
        JS_FreeValue(ctx, val);

        val = JS_GetPropertyStr(ctx, nginx_obj, "workerRequestTimeout");
        if (!JS_IsException(val)
            && JS_ToInt64(ctx, &n, val) == 0 && n > 0)
        {
            struct timespec  ts;

            clock_gettime(CLOCK_MONOTONIC, &ts);
            w->request_deadline_ms = (uint64_t) ts.tv_sec * 1000
                                     + (uint64_t) ts.tv_nsec / 1000000
                                     + (uint64_t) n;
        }
        JS_FreeValue(ctx, val);

        JS_FreeValue(ctx, nginx_obj);
    }

    w->current_request = r;

    /* ---- Hook phase (P1/P9 chain) ---- */
    if (!rctx->p1_chain_done
        && jlcf->hooks != NULL
        && jlcf->hooks->nelts > 0)
    {
        ngx_int_t                 chain_rc;
        ngx_js_request_opaque_t  *req_op_h;

        chain_rc = ngx_js_run_chain(w, r, req_obj,
                                    jlcf->hooks->elts,
                                    jlcf->hooks->nelts, 0 /* P1 */);
        switch (chain_rc) {

        case NGX_JS_CHAIN_RESPONDED:
            req_op_h = JS_GetOpaque(req_obj, ngx_js_request_class_id);
            final_rc = (req_op_h != NULL)
                       ? req_op_h->respond_rc
                       : NGX_HTTP_INTERNAL_SERVER_ERROR;
            JS_FreeValue(ctx, fn);
            JS_FreeValue(ctx, req_obj);
            w->current_request    = NULL;
            w->request_deadline_ms = 0;
            return final_rc;

        case NGX_JS_CHAIN_SUSPENDED:
            JS_FreeValue(ctx, fn);
            JS_FreeValue(ctx, req_obj);
            w->current_request    = NULL;
            w->request_deadline_ms = 0;
            return NGX_DONE;

        case NGX_JS_CHAIN_ERROR:
            JS_FreeValue(ctx, fn);
            JS_FreeValue(ctx, req_obj);
            w->current_request    = NULL;
            w->request_deadline_ms = 0;
            return NGX_HTTP_INTERNAL_SERVER_ERROR;

        default:  /* NGX_JS_CHAIN_DONE — all hooks passed, fall through */
            break;
        }
    }
    /* ---- End hook phase; fall through to main handler ---- */

    /* If no JS handler is set but a previous content handler was saved
     * (e.g. proxy_pass / fastcgi_pass), delegate to it now.           */
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        JS_FreeValue(ctx, req_obj);
        w->current_request    = NULL;
        w->request_deadline_ms = 0;
        if (jlcf->original_handler != NULL) {
            return jlcf->original_handler(r);
        }
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js: handler #%i not found or not callable",
                      jlcf->handler_idx);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &req_obj);

    JS_FreeValue(ctx, fn);

    /* Synchronous exception (includes interrupt-on-timeout) */
    if (JS_IsException(result)) {
        ngx_js_request_opaque_t  *exc_op;

        w->current_request = NULL;
        w->request_deadline_ms = 0;
        ngx_js_log_exception(ctx, r->connection->log);
        JS_FreeValue(ctx, result);

        /* If the connection was already hijacked before the throw, stay alive
         * (don't send an HTTP error response on the hijacked fd).            */
        exc_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);
        JS_FreeValue(ctx, req_obj);
        if (exc_op && exc_op->hijacked) {
            return NGX_DONE;
        }
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * Async handler: if the function returned a thenable (Promise), drain
     * the QuickJS microtask queue so the async body runs to completion.
     * This handles async handlers that settle synchronously — the common
     * case of `await Promise.resolve(...)` or `await asyncFn()` where no
     * real I/O is involved.
     */
    if (JS_IsObject(result)) {
        JSValue  then;
        int      is_promise;

        then       = JS_GetPropertyStr(ctx, result, "then");
        is_promise = JS_IsFunction(ctx, then);
        JS_FreeValue(ctx, then);

        if (is_promise) {
            JSContext  *job_ctx;

            while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { }

            switch (JS_PromiseState(ctx, result)) {

            case JS_PROMISE_REJECTED:
            {
                JSValue      reason, str;
                const char  *cstr;

                reason = JS_PromiseResult(ctx, result);
                str    = JS_ToString(ctx, reason);
                cstr   = JS_ToCString(ctx, str);
                if (cstr) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "js async exception: %s", cstr);
                    JS_FreeCString(ctx, cstr);
                }
                JS_FreeValue(ctx, str);
                JS_FreeValue(ctx, reason);
                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, req_obj);
                w->current_request = NULL;
                w->request_deadline_ms = 0;
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            case JS_PROMISE_PENDING:
            {
                ngx_js_async_ctx_t  *actx;

                actx = ngx_pcalloc(r->pool, sizeof(ngx_js_async_ctx_t));
                if (actx == NULL) {
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, req_obj);
                    w->current_request = NULL;
                    w->request_deadline_ms = 0;
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                actx->r       = r;
                actx->req_obj = JS_DupValue(ctx, req_obj);
                actx->promise = JS_DupValue(ctx, result);

                /* push to front of async_pending list */
                actx->next        = w->async_pending;
                w->async_pending  = actx;
                r->main->count++;

                JS_FreeValue(ctx, req_obj);
                JS_FreeValue(ctx, result);
                w->current_request = NULL;  /* request suspended; no JS running */
                w->request_deadline_ms = 0;
                return NGX_DONE;
            }

            default:  /* JS_PROMISE_FULFILLED — fall through */
                break;
            }
        }
    }

    /*
     * Read respond_rc from the opaque BEFORE JS_FreeValue triggers the
     * finalizer and frees req_op.
     *
     * We do NOT call ngx_http_finalize_request() here.  The correct nginx
     * pattern is to return the rc to ngx_http_core_content_phase, which
     * calls ngx_http_finalize_request() exactly once.
     */
    w->current_request = NULL;
    w->request_deadline_ms = 0;

    req_op = JS_GetOpaque(req_obj, ngx_js_request_class_id);

    /*
     * Hijacked connection: req.hijack() already incremented r->main->count.
     * Return NGX_DONE so the content phase doesn't finalise the request;
     * ngx_js_repl_read_handler will call ngx_http_finalize_request when the
     * connection is closed.
     */
    if (req_op && req_op->hijacked) {
        JS_FreeValue(ctx, req_obj);
        JS_FreeValue(ctx, result);
        return NGX_DONE;
    }

    /*
     * req.pass() was called (sync handler or async Promise that settled
     * before returning to the event loop).  ngx_http_internal_redirect
     * already called r->main->count++ and ngx_http_handler(r), which runs
     * the full phase engine for the new location.  For sync backends (e.g.
     * return 200) the response is already sent and ngx_http_finalize_request
     * was called once (count decremented back to 1).  For async backends
     * (proxy_pass) the upstream is started and count remains 2.
     * Either way, returning NGX_DONE here lets ngx_http_core_content_phase
     * call ngx_http_finalize_request(NGX_DONE) which decrements count to 0
     * (sync) or 1 (async, upstream finalises on completion).
     */
    if (req_op && req_op->passed) {
        JS_FreeValue(ctx, req_obj);
        JS_FreeValue(ctx, result);
        return NGX_DONE;
    }

    final_rc = (req_op && req_op->responded)
               ? req_op->respond_rc
               : NGX_HTTP_INTERNAL_SERVER_ERROR;

    JS_FreeValue(ctx, req_obj);
    JS_FreeValue(ctx, result);

    return final_rc;
}


/* ------------------------------------------------------------------ */
/* js_init_http — JS config hook that fires inside the http{} block    */
/* ------------------------------------------------------------------ */

/* ---- addServer / addLocation builder ---- */

typedef struct {
    ngx_str_t  path;
    ngx_str_t  root;    /* optional; zero-len if not set */
    ngx_str_t  ret;     /* optional; content for "return" directive */
} ngx_js_pending_loc_t;

typedef struct {
    ngx_conf_t   *cf;
    ngx_array_t   listen;     /* ngx_str_t[] */
    ngx_array_t   names;      /* ngx_str_t[] */
    ngx_array_t   locations;  /* ngx_js_pending_loc_t[] */
} ngx_js_pending_server_t;


static JSClassID     ngx_js_pending_server_class_id;
static ngx_array_t  *ngx_js_current_pending;  /* ngx_js_pending_server_t*[] */
static ngx_conf_t   *ngx_js_current_cf;


static void
ngx_js_pending_server_finalizer(JSRuntime *rt, JSValue val)
{
    /* all data lives in cf->pool — nothing to free here */
    (void) rt;
    (void) val;
}


static JSClassDef ngx_js_pending_server_class = {
    "PendingServer",
    .finalizer = ngx_js_pending_server_finalizer
};


/*
 * srv.addLocation(path[, opts])
 *
 *   path          — string, e.g. "/api/"
 *   opts.root     — optional root directory
 *   opts.return   — optional argument to "return" directive
 *
 * Returns `this` for chaining.
 */
static JSValue
ngx_js_pending_server_add_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_pending_server_t  *ps;
    ngx_js_pending_loc_t     *loc;
    const char               *cstr;
    size_t                    len;
    JSValue                   opt;
    ngx_pool_t               *pool;

    ps = JS_GetOpaque2(ctx, this_val, ngx_js_pending_server_class_id);
    if (!ps) {
        return JS_EXCEPTION;
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "addLocation(path[, opts]) requires at "
                                 "least 1 argument");
    }

    pool = ps->cf->pool;

    loc = ngx_array_push(&ps->locations);
    if (loc == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "addLocation: ngx_array_push failed");
    }

    ngx_memzero(loc, sizeof(ngx_js_pending_loc_t));

    cstr = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!cstr) {
        return JS_EXCEPTION;
    }

    loc->path.data = ngx_pnalloc(pool, len + 1);
    if (loc->path.data == NULL) {
        JS_FreeCString(ctx, cstr);
        return JS_ThrowInternalError(ctx, "addLocation: ngx_pnalloc failed");
    }

    ngx_memcpy(loc->path.data, cstr, len + 1);
    loc->path.len = len;
    JS_FreeCString(ctx, cstr);

    if (argc >= 2 && JS_IsObject(argv[1])) {

        opt = JS_GetPropertyStr(ctx, argv[1], "root");
        if (!JS_IsUndefined(opt)) {
            cstr = JS_ToCStringLen(ctx, &len, opt);
            if (cstr) {
                loc->root.data = ngx_pnalloc(pool, len + 1);
                if (loc->root.data) {
                    ngx_memcpy(loc->root.data, cstr, len + 1);
                    loc->root.len = len;
                }
                JS_FreeCString(ctx, cstr);
            }
        }
        JS_FreeValue(ctx, opt);

        opt = JS_GetPropertyStr(ctx, argv[1], "return");
        if (!JS_IsUndefined(opt)) {
            cstr = JS_ToCStringLen(ctx, &len, opt);
            if (cstr) {
                loc->ret.data = ngx_pnalloc(pool, len + 1);
                if (loc->ret.data) {
                    ngx_memcpy(loc->ret.data, cstr, len + 1);
                    loc->ret.len = len;
                }
                JS_FreeCString(ctx, cstr);
            }
        }
        JS_FreeValue(ctx, opt);
    }

    return JS_DupValue(ctx, this_val);
}


static const JSCFunctionListEntry ngx_js_pending_server_proto_funcs[] = {
    JS_CFUNC_DEF("addLocation", 1, ngx_js_pending_server_add_location),
};


ngx_int_t
ngx_js_pending_server_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_pending_server_proto_funcs,
                               countof(ngx_js_pending_server_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_pending_server_class_id, proto);
    return NGX_OK;
}


/*
 * Copy an ngx_str_t from a JS string into cf->pool.
 * Returns NGX_ERROR on allocation failure, NGX_OK otherwise.
 */
static ngx_int_t
ngx_js_str_from_js(JSContext *ctx, JSValueConst val, ngx_pool_t *pool,
    ngx_str_t *out)
{
    const char  *cstr;
    size_t       len;

    cstr = JS_ToCStringLen(ctx, &len, val);
    if (!cstr) {
        return NGX_ERROR;
    }

    out->data = ngx_pnalloc(pool, len + 1);
    if (out->data == NULL) {
        JS_FreeCString(ctx, cstr);
        return NGX_ERROR;
    }

    ngx_memcpy(out->data, cstr, len + 1);
    out->len = len;

    JS_FreeCString(ctx, cstr);
    return NGX_OK;
}


/*
 * Parse one location object {path, root?, return?} into a pending_loc.
 */
static ngx_int_t
ngx_js_parse_loc_obj(JSContext *ctx, JSValueConst item,
    ngx_pool_t *pool, ngx_js_pending_loc_t *loc)
{
    JSValue  v;

    ngx_memzero(loc, sizeof(ngx_js_pending_loc_t));

    v = JS_GetPropertyStr(ctx, item, "path");
    if (ngx_js_str_from_js(ctx, v, pool, &loc->path) != NGX_OK) {
        JS_FreeValue(ctx, v);
        return NGX_ERROR;
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, item, "root");
    if (!JS_IsUndefined(v)) {
        ngx_js_str_from_js(ctx, v, pool, &loc->root);
    }
    JS_FreeValue(ctx, v);

    v = JS_GetPropertyStr(ctx, item, "return");
    if (!JS_IsUndefined(v)) {
        ngx_js_str_from_js(ctx, v, pool, &loc->ret);
    }
    JS_FreeValue(ctx, v);

    return NGX_OK;
}


/*
 * nginx.http.addServer(opts)
 *
 *   opts.listen[]      — array of listen strings, e.g. ["127.0.0.1:8082"]
 *   opts.serverNames[] — array of server_name strings
 *   opts.locations[]   — optional inline location array
 *
 * Returns a PendingServer JS object that supports .addLocation() chaining.
 * All pending servers are flushed via ngx_conf_parse() after JS_Eval returns.
 */
static JSValue
ngx_js_http_add_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_pending_server_t   *ps;
    ngx_js_pending_server_t  **slot;
    JSValue                    obj, arr_val, item, len_val;
    ngx_str_t                 *ns;
    ngx_js_pending_loc_t      *loc;
    ngx_pool_t                *pool;
    uint32_t                   k, arr_len;

    if (ngx_js_current_pending == NULL || ngx_js_current_cf == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "addServer: not in js_init_http context");
    }

    pool = ngx_js_current_cf->pool;

    ps = ngx_pcalloc(pool, sizeof(ngx_js_pending_server_t));
    if (ps == NULL) {
        return JS_ThrowInternalError(ctx, "addServer: ngx_pcalloc failed");
    }

    ps->cf = ngx_js_current_cf;

    if (ngx_array_init(&ps->listen,    pool, 2, sizeof(ngx_str_t))
        != NGX_OK
        || ngx_array_init(&ps->names,  pool, 2, sizeof(ngx_str_t))
        != NGX_OK
        || ngx_array_init(&ps->locations, pool, 4,
                          sizeof(ngx_js_pending_loc_t))
        != NGX_OK)
    {
        return JS_ThrowInternalError(ctx,
                                     "addServer: ngx_array_init failed");
    }

    if (argc >= 1 && JS_IsObject(argv[0])) {

        /* opts.listen[] */
        arr_val = JS_GetPropertyStr(ctx, argv[0], "listen");
        if (JS_IsArray(ctx, arr_val)) {
            len_val = JS_GetPropertyStr(ctx, arr_val, "length");
            JS_ToUint32(ctx, &arr_len, len_val);
            JS_FreeValue(ctx, len_val);

            for (k = 0; k < arr_len; k++) {
                item = JS_GetPropertyUint32(ctx, arr_val, k);
                ns = ngx_array_push(&ps->listen);
                if (ns && ngx_js_str_from_js(ctx, item, pool, ns) != NGX_OK) {
                    JS_FreeValue(ctx, item);
                    JS_FreeValue(ctx, arr_val);
                    return JS_ThrowInternalError(ctx,
                                                 "addServer: listen alloc");
                }
                JS_FreeValue(ctx, item);
            }
        }
        JS_FreeValue(ctx, arr_val);

        /* opts.serverNames[] */
        arr_val = JS_GetPropertyStr(ctx, argv[0], "serverNames");
        if (JS_IsArray(ctx, arr_val)) {
            len_val = JS_GetPropertyStr(ctx, arr_val, "length");
            JS_ToUint32(ctx, &arr_len, len_val);
            JS_FreeValue(ctx, len_val);

            for (k = 0; k < arr_len; k++) {
                item = JS_GetPropertyUint32(ctx, arr_val, k);
                ns = ngx_array_push(&ps->names);
                if (ns && ngx_js_str_from_js(ctx, item, pool, ns) != NGX_OK) {
                    JS_FreeValue(ctx, item);
                    JS_FreeValue(ctx, arr_val);
                    return JS_ThrowInternalError(ctx,
                                                 "addServer: names alloc");
                }
                JS_FreeValue(ctx, item);
            }
        }
        JS_FreeValue(ctx, arr_val);

        /* opts.locations[] — inline location objects */
        arr_val = JS_GetPropertyStr(ctx, argv[0], "locations");
        if (JS_IsArray(ctx, arr_val)) {
            len_val = JS_GetPropertyStr(ctx, arr_val, "length");
            JS_ToUint32(ctx, &arr_len, len_val);
            JS_FreeValue(ctx, len_val);

            for (k = 0; k < arr_len; k++) {
                item = JS_GetPropertyUint32(ctx, arr_val, k);

                if (JS_IsObject(item)) {
                    loc = ngx_array_push(&ps->locations);
                    if (loc == NULL
                        || ngx_js_parse_loc_obj(ctx, item, pool, loc)
                           != NGX_OK)
                    {
                        JS_FreeValue(ctx, item);
                        JS_FreeValue(ctx, arr_val);
                        return JS_ThrowInternalError(ctx,
                                              "addServer: location alloc");
                    }
                }

                JS_FreeValue(ctx, item);
            }
        }
        JS_FreeValue(ctx, arr_val);
    }

    /* Register with pending list */
    slot = ngx_array_push(ngx_js_current_pending);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "addServer: pending push failed");
    }
    *slot = ps;

    /* Build and return PendingServer JS object */
    obj = JS_NewObjectClass(ctx, ngx_js_pending_server_class_id);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, ps);
    return obj;
}


/*
 * Generate a server{} config text from one pending server and feed it
 * to ngx_conf_parse() via a temporary file (same approach as config.write).
 */
static char *
ngx_js_flush_pending_server(ngx_conf_t *cf, ngx_js_pending_server_t *ps)
{
    ngx_str_t             *listen_arr, *names_arr;
    ngx_js_pending_loc_t  *locs;
    ngx_uint_t             i;
    u_char                 buf[16384];
    u_char                *p, *end;
    char                   tmppath[] = "/tmp/ngx_js_srv_XXXXXX";
    ngx_str_t              tmpstr;
    int                    fd;
    ssize_t                n;
    char                  *rv;

    p   = buf;
    end = buf + sizeof(buf);

    listen_arr = ps->listen.elts;
    names_arr  = ps->names.elts;
    locs       = ps->locations.elts;

    p = ngx_slprintf(p, end, "server {\n");

    for (i = 0; i < ps->listen.nelts; i++) {
        p = ngx_slprintf(p, end, "    listen %V;\n", &listen_arr[i]);
    }

    if (ps->names.nelts > 0) {
        p = ngx_slprintf(p, end, "    server_name");
        for (i = 0; i < ps->names.nelts; i++) {
            p = ngx_slprintf(p, end, " %V", &names_arr[i]);
        }
        p = ngx_slprintf(p, end, ";\n");
    }

    for (i = 0; i < ps->locations.nelts; i++) {
        p = ngx_slprintf(p, end, "    location %V {\n", &locs[i].path);
        if (locs[i].root.data) {
            p = ngx_slprintf(p, end, "        root %V;\n", &locs[i].root);
        }
        if (locs[i].ret.data) {
            p = ngx_slprintf(p, end, "        return %V;\n", &locs[i].ret);
        }
        p = ngx_slprintf(p, end, "    }\n");
    }

    p = ngx_slprintf(p, end, "}\n");

    if (p >= end) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: addServer: config text overflow (>%uz bytes)",
                      sizeof(buf));
        return NGX_CONF_ERROR;
    }

    fd = mkstemp(tmppath);
    if (fd < 0) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                      "js: addServer: mkstemp() failed");
        return NGX_CONF_ERROR;
    }

    n = write(fd, buf, (size_t)(p - buf));
    close(fd);

    if (n < 0 || (size_t) n != (size_t)(p - buf)) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                      "js: addServer: write() failed");
        unlink(tmppath);
        return NGX_CONF_ERROR;
    }

    tmpstr.data = (u_char *) tmppath;
    tmpstr.len  = ngx_strlen(tmppath);

    rv = (char *) ngx_conf_parse(cf, &tmpstr);
    unlink(tmppath);
    return rv;
}


static char *
ngx_js_apply_pending_servers(ngx_conf_t *cf, ngx_array_t *pending)
{
    ngx_js_pending_server_t  **slot;
    ngx_uint_t                 i;
    char                      *rv;

    slot = pending->elts;

    for (i = 0; i < pending->nelts; i++) {
        rv = ngx_js_flush_pending_server(cf, slot[i]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    return NGX_CONF_OK;
}


/* ---- delServer / delLocation mutators ---- */

/*
 * Return cmcf from the current ngx_js_current_cf, or NULL if unavailable.
 */
static ngx_http_core_main_conf_t *
ngx_js_get_cmcf(void)
{
    ngx_http_conf_ctx_t  *http_ctx;

    if (ngx_js_current_cf == NULL) {
        return NULL;
    }

    http_ctx = ngx_js_current_cf->ctx;
    if (http_ctx == NULL) {
        return NULL;
    }

    return http_ctx->main_conf[ngx_http_core_module.ctx_index];
}


/*
 * nginx.http.delServer(name)
 *
 * Removes all virtual servers whose server_names include `name` (case-
 * insensitive, matching the nginx server_name convention).
 *
 * Returns the number of servers removed (0 when none matched).
 *
 * Operates on:
 *   1. cmcf->servers — so merge/init_locations/static_trees skip it
 *   2. addr->servers in every cmcf->ports entry — so the server is not
 *      registered in the virtual-host name hash and requests are never
 *      dispatched to it
 *
 * Addrs with no remaining servers are removed from their port; ports
 * with no remaining addrs are removed from cmcf->ports.  This prevents
 * ngx_http_optimize_servers from setting up a listening socket with a
 * NULL default_server.
 *
 * All operations are safe at parse time before ngx_http_block() runs
 * its merge/optimize passes.
 */
static JSValue
ngx_js_http_del_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp, **addr_srvp;
    ngx_http_conf_port_t       *ports;
    ngx_http_conf_addr_t       *addrs;
    ngx_http_server_name_t     *sn;
    const char                 *name_cstr;
    size_t                      name_len;
    ngx_uint_t                  i, j, p, a, s, removed;
    ngx_flag_t                  match;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx,
                                 "delServer(name) requires 1 argument");
    }

    cmcf = ngx_js_get_cmcf();
    if (cmcf == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "delServer: not in js_init_http context");
    }

    name_cstr = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    cscfp   = cmcf->servers.elts;
    removed = 0;
    i       = 0;

    while (i < cmcf->servers.nelts) {

        match = 0;
        sn    = cscfp[i]->server_names.elts;

        for (j = 0; j < cscfp[i]->server_names.nelts; j++) {
            if (sn[j].name.len == name_len
                && ngx_strncasecmp(sn[j].name.data,
                                   (u_char *) name_cstr, name_len) == 0)
            {
                match = 1;
                break;
            }
        }

        if (!match) {
            i++;
            continue;
        }

        /*
         * Also remove this cscf from every addr->servers list inside
         * cmcf->ports.  Without this, ngx_http_optimize_servers would
         * still register the server in the virtual-host name hash and
         * would bind its listen port with a potentially NULL
         * default_server.
         */
        if (cmcf->ports) {
            ports = cmcf->ports->elts;

            for (p = 0; p < cmcf->ports->nelts; /* manual */) {
                addrs = ports[p].addrs.elts;
                a = 0;

                while (a < ports[p].addrs.nelts) {
                    addr_srvp = addrs[a].servers.elts;
                    s = 0;

                    while (s < addrs[a].servers.nelts) {
                        if (addr_srvp[s] == cscfp[i]) {
                            ngx_memmove(
                                &addr_srvp[s],
                                &addr_srvp[s + 1],
                                (addrs[a].servers.nelts - s - 1)
                                * sizeof(ngx_http_core_srv_conf_t *));
                            addrs[a].servers.nelts--;
                        } else {
                            s++;
                        }
                    }

                    /* Fix default_server if it pointed to the deleted cscf */
                    if (addrs[a].default_server == cscfp[i]) {
                        addrs[a].default_server =
                            addrs[a].servers.nelts > 0
                            ? ((ngx_http_core_srv_conf_t **)
                               addrs[a].servers.elts)[0]
                            : NULL;
                    }

                    /* Remove addr if it has no servers left */
                    if (addrs[a].servers.nelts == 0) {
                        ngx_memmove(
                            &addrs[a], &addrs[a + 1],
                            (ports[p].addrs.nelts - a - 1)
                            * sizeof(ngx_http_conf_addr_t));
                        ports[p].addrs.nelts--;
                    } else {
                        a++;
                    }
                }

                /* Remove port if it has no addrs left */
                if (ports[p].addrs.nelts == 0) {
                    ngx_memmove(
                        &ports[p], &ports[p + 1],
                        (cmcf->ports->nelts - p - 1)
                        * sizeof(ngx_http_conf_port_t));
                    cmcf->ports->nelts--;
                } else {
                    p++;
                }
            }
        }

        /* Remove from cmcf->servers */
        ngx_memmove(&cscfp[i], &cscfp[i + 1],
                    (cmcf->servers.nelts - i - 1)
                    * sizeof(ngx_http_core_srv_conf_t *));
        cmcf->servers.nelts--;
        removed++;
    }

    JS_FreeCString(ctx, name_cstr);
    return JS_NewInt32(ctx, (int32_t) removed);
}


/*
 * nginx.http.delLocation(serverName, path)
 *
 * Removes all location entries whose name matches `path` from the named
 * server.  The server is located by server_name (case-insensitive).
 *
 * Returns the number of locations removed (0 when none matched).
 *
 * Uses ngx_queue_remove() on the raw location queue, which is safe at
 * parse time before nginx builds the static-location radix trees.
 */
static JSValue
ngx_js_http_del_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp;
    ngx_http_core_loc_conf_t   *clcf, *lclcf;
    ngx_http_server_name_t     *sn;
    ngx_http_location_queue_t  *lq;
    ngx_queue_t                *q, *next;
    const char                 *sname_cstr, *path_cstr;
    size_t                      sname_len, path_len;
    ngx_uint_t                  i, j, removed;
    ngx_flag_t                  smatch;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "delLocation(serverName, path) requires "
                                 "2 arguments");
    }

    cmcf = ngx_js_get_cmcf();
    if (cmcf == NULL) {
        return JS_ThrowInternalError(ctx,
                                 "delLocation: not in js_init_http context");
    }

    sname_cstr = JS_ToCStringLen(ctx, &sname_len, argv[0]);
    if (!sname_cstr) {
        return JS_EXCEPTION;
    }

    path_cstr = JS_ToCStringLen(ctx, &path_len, argv[1]);
    if (!path_cstr) {
        JS_FreeCString(ctx, sname_cstr);
        return JS_EXCEPTION;
    }

    cscfp   = cmcf->servers.elts;
    removed = 0;

    for (i = 0; i < cmcf->servers.nelts; i++) {

        smatch = 0;
        sn     = cscfp[i]->server_names.elts;

        for (j = 0; j < cscfp[i]->server_names.nelts; j++) {
            if (sn[j].name.len == sname_len
                && ngx_strncasecmp(sn[j].name.data,
                                   (u_char *) sname_cstr, sname_len) == 0)
            {
                smatch = 1;
                break;
            }
        }

        if (!smatch) {
            continue;
        }

        clcf = cscfp[i]->ctx->loc_conf[ngx_http_core_module.ctx_index];
        if (clcf->locations == NULL) {
            continue;
        }

        q = ngx_queue_head(clcf->locations);

        while (q != ngx_queue_sentinel(clcf->locations)) {
            next  = ngx_queue_next(q);
            lq    = (ngx_http_location_queue_t *) q;
            lclcf = lq->exact ? lq->exact : lq->inclusive;

            if (lclcf->name.len == path_len
                && ngx_strncmp(lclcf->name.data,
                               (u_char *) path_cstr, path_len) == 0)
            {
                ngx_queue_remove(q);
                removed++;
            }

            q = next;
        }
    }

    JS_FreeCString(ctx, sname_cstr);
    JS_FreeCString(ctx, path_cstr);
    return JS_NewInt32(ctx, (int32_t) removed);
}


/* ---- modServer / modLocation mutators ---- */

/*
 * nginx.http.modServer(name, opts)
 *
 * Modifies all virtual servers whose server_names include `name`.
 *
 *   opts.serverNames[]  — replace the server_name list with new strings
 *
 * Returns the number of servers modified (0 when none matched).
 *
 * Only the server_names array is replaced; listen addresses and all
 * other server-level config remain unchanged.  The replacement is done
 * before ngx_http_block() calls ngx_http_server_names(), so the new
 * names are used when nginx builds the virtual-host hash tables.
 */
static JSValue
ngx_js_http_mod_server(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp;
    ngx_http_server_name_t     *sn, *new_sn;
    JSValue                     arr_val, item, len_val;
    const char                 *name_cstr, *new_name_cstr;
    size_t                      name_len, new_name_len;
    ngx_pool_t                 *pool;
    ngx_uint_t                  i, j, modified;
    uint32_t                    k, arr_len;
    ngx_flag_t                  match;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "modServer(name, opts) requires 2 arguments");
    }

    cmcf = ngx_js_get_cmcf();
    if (cmcf == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "modServer: not in js_init_http context");
    }

    name_cstr = JS_ToCStringLen(ctx, &name_len, argv[0]);
    if (!name_cstr) {
        return JS_EXCEPTION;
    }

    pool     = ngx_js_current_cf->pool;
    cscfp    = cmcf->servers.elts;
    modified = 0;

    for (i = 0; i < cmcf->servers.nelts; i++) {

        match = 0;
        sn    = cscfp[i]->server_names.elts;

        for (j = 0; j < cscfp[i]->server_names.nelts; j++) {
            if (sn[j].name.len == name_len
                && ngx_strncasecmp(sn[j].name.data,
                                   (u_char *) name_cstr, name_len) == 0)
            {
                match = 1;
                break;
            }
        }

        if (!match) {
            continue;
        }

        /* opts.serverNames[] — replace the server_name list */
        arr_val = JS_GetPropertyStr(ctx, argv[1], "serverNames");

        if (JS_IsArray(ctx, arr_val)) {
            len_val = JS_GetPropertyStr(ctx, arr_val, "length");
            JS_ToUint32(ctx, &arr_len, len_val);
            JS_FreeValue(ctx, len_val);

            /* Reset the server_names array and fill with new names */
            cscfp[i]->server_names.nelts = 0;

            for (k = 0; k < arr_len; k++) {
                item = JS_GetPropertyUint32(ctx, arr_val, k);

                new_name_cstr = JS_ToCStringLen(ctx, &new_name_len, item);
                JS_FreeValue(ctx, item);

                if (!new_name_cstr) {
                    JS_FreeValue(ctx, arr_val);
                    JS_FreeCString(ctx, name_cstr);
                    return JS_EXCEPTION;
                }

                new_sn = ngx_array_push(&cscfp[i]->server_names);
                if (new_sn == NULL) {
                    JS_FreeCString(ctx, new_name_cstr);
                    JS_FreeValue(ctx, arr_val);
                    JS_FreeCString(ctx, name_cstr);
                    return JS_ThrowInternalError(ctx,
                                           "modServer: ngx_array_push failed");
                }

                ngx_memzero(new_sn, sizeof(ngx_http_server_name_t));
                new_sn->server    = cscfp[i];
                new_sn->name.data = ngx_pnalloc(pool, new_name_len + 1);

                if (new_sn->name.data == NULL) {
                    JS_FreeCString(ctx, new_name_cstr);
                    JS_FreeValue(ctx, arr_val);
                    JS_FreeCString(ctx, name_cstr);
                    return JS_ThrowInternalError(ctx,
                                           "modServer: ngx_pnalloc failed");
                }

                ngx_memcpy(new_sn->name.data, new_name_cstr,
                           new_name_len + 1);
                new_sn->name.len = new_name_len;

                JS_FreeCString(ctx, new_name_cstr);
            }

            modified++;
        }

        JS_FreeValue(ctx, arr_val);
    }

    JS_FreeCString(ctx, name_cstr);
    return JS_NewInt32(ctx, (int32_t) modified);
}


/*
 * nginx.http.modLocation(serverName, path, opts)
 *
 * Modifies all locations matching `path` in the named server.
 *
 *   opts.path   — rename the location (update clcf->name)
 *   opts.root   — change the root directory (update clcf->root)
 *
 * Returns the number of locations modified (0 when none matched).
 *
 * Location trees are not yet built when js_init_http fires, so path
 * renames take effect before ngx_http_init_locations builds the trees.
 * Root updates are safe for simple (non-variable) root values.
 */
static JSValue
ngx_js_http_mod_location(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp;
    ngx_http_core_loc_conf_t   *clcf, *lclcf;
    ngx_http_server_name_t     *sn;
    ngx_http_location_queue_t  *lq;
    ngx_queue_t                *q;
    JSValue                     opt;
    const char                 *sname_cstr, *path_cstr, *cstr;
    size_t                      sname_len, path_len, len;
    ngx_pool_t                 *pool;
    ngx_uint_t                  i, j, modified;
    ngx_flag_t                  smatch;

    if (argc < 3) {
        return JS_ThrowTypeError(ctx,
                                 "modLocation(serverName, path, opts) "
                                 "requires 3 arguments");
    }

    cmcf = ngx_js_get_cmcf();
    if (cmcf == NULL) {
        return JS_ThrowInternalError(ctx,
                                 "modLocation: not in js_init_http context");
    }

    sname_cstr = JS_ToCStringLen(ctx, &sname_len, argv[0]);
    if (!sname_cstr) {
        return JS_EXCEPTION;
    }

    path_cstr = JS_ToCStringLen(ctx, &path_len, argv[1]);
    if (!path_cstr) {
        JS_FreeCString(ctx, sname_cstr);
        return JS_EXCEPTION;
    }

    pool     = ngx_js_current_cf->pool;
    cscfp    = cmcf->servers.elts;
    modified = 0;

    for (i = 0; i < cmcf->servers.nelts; i++) {

        smatch = 0;
        sn     = cscfp[i]->server_names.elts;

        for (j = 0; j < cscfp[i]->server_names.nelts; j++) {
            if (sn[j].name.len == sname_len
                && ngx_strncasecmp(sn[j].name.data,
                                   (u_char *) sname_cstr, sname_len) == 0)
            {
                smatch = 1;
                break;
            }
        }

        if (!smatch) {
            continue;
        }

        clcf = cscfp[i]->ctx->loc_conf[ngx_http_core_module.ctx_index];
        if (clcf->locations == NULL) {
            continue;
        }

        for (q = ngx_queue_head(clcf->locations);
             q != ngx_queue_sentinel(clcf->locations);
             q = ngx_queue_next(q))
        {
            lq    = (ngx_http_location_queue_t *) q;
            lclcf = lq->exact ? lq->exact : lq->inclusive;

            if (lclcf->name.len != path_len
                || ngx_strncmp(lclcf->name.data,
                               (u_char *) path_cstr, path_len) != 0)
            {
                continue;
            }

            /* opts.path — rename this location */
            opt = JS_GetPropertyStr(ctx, argv[2], "path");
            if (!JS_IsUndefined(opt)) {
                cstr = JS_ToCStringLen(ctx, &len, opt);
                if (cstr) {
                    lclcf->name.data = ngx_pnalloc(pool, len + 1);
                    if (lclcf->name.data) {
                        ngx_memcpy(lclcf->name.data, cstr, len + 1);
                        lclcf->name.len = len;
                    }
                    JS_FreeCString(ctx, cstr);
                }
            }
            JS_FreeValue(ctx, opt);

            /* opts.root — update root directory (simple paths only) */
            opt = JS_GetPropertyStr(ctx, argv[2], "root");
            if (!JS_IsUndefined(opt)) {
                cstr = JS_ToCStringLen(ctx, &len, opt);
                if (cstr) {
                    lclcf->root.data = ngx_pnalloc(pool, len + 1);
                    if (lclcf->root.data) {
                        ngx_memcpy(lclcf->root.data, cstr, len + 1);
                        lclcf->root.len     = len;
                        /* clear compiled variable arrays so nginx uses
                         * the plain string path */
                        lclcf->root_lengths = NULL;
                        lclcf->root_values  = NULL;
                    }
                    JS_FreeCString(ctx, cstr);
                }
            }
            JS_FreeValue(ctx, opt);

            modified++;
        }
    }

    JS_FreeCString(ctx, sname_cstr);
    JS_FreeCString(ctx, path_cstr);
    return JS_NewInt32(ctx, (int32_t) modified);
}


/*
 * Get-handler for JS-registered variables.
 *
 * Variables added by nginx.http.addVariable() are "set-only" at
 * config time: they have no built-in source.  Reading such a variable
 * returns the value placed in r->variables[index] by a prior write
 * (e.g. via r.variables['name'] = '...' in a JS handler), or
 * not_found if the variable has never been set in this request.
 */
static ngx_int_t
ngx_js_variable_get_handler(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t  index;

    index = (ngx_uint_t) data;

    if (r->variables[index].not_found || !r->variables[index].valid) {
        v->not_found = 1;
        return NGX_OK;
    }

    *v = r->variables[index];
    return NGX_OK;
}


/*
 * nginx.http.addVariable(name)
 *
 * Registers a new indexed nginx variable named `name` (lowercase,
 * no '$' prefix) in the current config context.  Only available
 * during js_init_http execution (ngx_js_current_cf != NULL).
 *
 * The variable is writable at request time via r.variables[name] and
 * readable by nginx modules that look it up by name or index.
 *
 * Returns the variable's integer index, or throws a TypeError if
 * called outside js_init_http or if registration fails.
 */
static JSValue
ngx_js_http_add_variable(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char           *name_cstr;
    u_char               *lc;
    ngx_str_t             name;
    ngx_http_variable_t  *v;
    ngx_int_t             index;

    if (ngx_js_current_cf == NULL) {
        return JS_ThrowTypeError(ctx,
            "nginx.http.addVariable() is only available in js_init_http");
    }

    name_cstr = JS_ToCString(ctx, argv[0]);
    if (name_cstr == NULL) {
        return JS_EXCEPTION;
    }

    name.len  = ngx_strlen(name_cstr);
    lc = ngx_pnalloc(ngx_js_current_cf->pool, name.len ? name.len : 1);
    if (lc == NULL) {
        JS_FreeCString(ctx, name_cstr);
        return JS_ThrowInternalError(ctx, "out of memory");
    }

    ngx_strlow(lc, (u_char *) name_cstr, name.len);
    name.data = lc;

    JS_FreeCString(ctx, name_cstr);

    v = ngx_http_add_variable(ngx_js_current_cf, &name,
                              NGX_HTTP_VAR_CHANGEABLE | NGX_HTTP_VAR_INDEXED);
    if (v == NULL) {
        return JS_ThrowInternalError(ctx,
            "nginx.http.addVariable: ngx_http_add_variable() failed");
    }

    index = ngx_http_get_variable_index(ngx_js_current_cf, &name);
    if (index == NGX_ERROR) {
        return JS_ThrowInternalError(ctx,
            "nginx.http.addVariable: ngx_http_get_variable_index() failed");
    }

    /* Provide a get_handler so variables_init_vars does not reject us */
    v->get_handler = ngx_js_variable_get_handler;
    v->data        = (uintptr_t) index;

    return JS_NewInt32(ctx, (int32_t) index);
}


/*
 * Build a read-only JS array of plain objects representing the nginx
 * virtual servers that have been parsed so far in the http{} block.
 *
 * Each element has:
 *   .name       — first server_name string (or "" if none)
 *   .names[]    — all server_name strings
 *   .locations[] — locations parsed so far; each has .path and .root
 *
 * Called at parse time: location trees are not yet built, so we walk
 * the raw location queue (clcf->locations) instead of the tree.
 */
static JSValue
ngx_js_init_http_servers(JSContext *ctx, ngx_conf_t *cf)
{
    ngx_http_conf_ctx_t        *http_ctx;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_core_srv_conf_t  **cscfp;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_server_name_t     *sn;
    ngx_queue_t                *q;
    ngx_http_location_queue_t  *lq;
    ngx_http_core_loc_conf_t   *lclcf;
    JSValue                     arr, srv_obj, names_arr, locs_arr, loc_obj;
    ngx_uint_t                  i, j;
    uint32_t                    li;

    arr = JS_NewArray(ctx);

    http_ctx = cf->ctx;
    if (http_ctx == NULL) {
        return arr;
    }

    cmcf = http_ctx->main_conf[ngx_http_core_module.ctx_index];
    if (cmcf == NULL || cmcf->servers.nelts == 0) {
        return arr;
    }

    cscfp = cmcf->servers.elts;

    for (i = 0; i < cmcf->servers.nelts; i++) {

        srv_obj = JS_NewObject(ctx);

        /* .name — first server_name */
        sn = cscfp[i]->server_names.elts;

        if (cscfp[i]->server_names.nelts > 0) {
            JS_SetPropertyStr(ctx, srv_obj, "name",
                JS_NewStringLen(ctx,
                                (const char *) sn[0].name.data,
                                sn[0].name.len));
        } else {
            JS_SetPropertyStr(ctx, srv_obj, "name",
                              JS_NewString(ctx, ""));
        }

        /* .names[] — all server_names */
        names_arr = JS_NewArray(ctx);
        for (j = 0; j < cscfp[i]->server_names.nelts; j++) {
            JS_SetPropertyUint32(ctx, names_arr, (uint32_t) j,
                JS_NewStringLen(ctx,
                                (const char *) sn[j].name.data,
                                sn[j].name.len));
        }
        JS_SetPropertyStr(ctx, srv_obj, "names", names_arr);

        /* .locations[] — walk the raw location queue */
        locs_arr = JS_NewArray(ctx);
        li       = 0;

        clcf = cscfp[i]->ctx->loc_conf[ngx_http_core_module.ctx_index];

        if (clcf->locations != NULL) {
            for (q = ngx_queue_head(clcf->locations);
                 q != ngx_queue_sentinel(clcf->locations);
                 q = ngx_queue_next(q))
            {
                lq    = (ngx_http_location_queue_t *) q;
                lclcf = lq->exact ? lq->exact : lq->inclusive;

                loc_obj = JS_NewObject(ctx);

                JS_SetPropertyStr(ctx, loc_obj, "path",
                    JS_NewStringLen(ctx,
                                   (const char *) lclcf->name.data,
                                   lclcf->name.len));

                if (lclcf->root.data) {
                    JS_SetPropertyStr(ctx, loc_obj, "root",
                        JS_NewStringLen(ctx,
                                       (const char *) lclcf->root.data,
                                       lclcf->root.len));
                }

                JS_SetPropertyUint32(ctx, locs_arr, li++, loc_obj);
            }
        }

        JS_SetPropertyStr(ctx, srv_obj, "locations", locs_arr);

        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, srv_obj);
    }

    return arr;
}


/*
 * Handler for:  js_init_http /path/to/script.js;
 *
 * Fires immediately when the directive is encountered during the
 * http{} block parse.  The script receives:
 *
 *   config.write(text)         — same as js_preprocess; feeds nginx
 *                                config text back into ngx_conf_parse()
 *                                so new server{}/location{} blocks are
 *                                added during parse and receive the full
 *                                merge/init_locations/optimize treatment.
 *
 *   nginx.http.servers[]       — read-only view of the virtual servers
 *                                that have been parsed before this
 *                                directive (useful for conditional adds).
 *
 * The JS runtime is short-lived and independent of the js_source
 * runtime.  nginx.setTimeout, Worker, SharedWorker etc. are NOT
 * available here.
 *
 * Place js_init_http AFTER the server{} blocks you want to read.
 * Servers defined after this directive are not visible in servers[].
 */
static char *
ngx_js_init_http(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t    *value, path;
    u_char       *src;
    size_t        src_len;
    JSRuntime    *rt;
    JSContext    *ctx;
    JSValue       global, config_obj, nginx_obj, http_obj;
    ngx_array_t   pending;
    char         *rv;

    value = cf->args->elts;
    path  = value[1];

    if (ngx_conf_full_name(cf->cycle, &path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    src = ngx_js_read_file(cf->cycle, &path, &src_len);
    if (src == NULL) {
        return NGX_CONF_ERROR;
    }

    /* Lazy class ID allocation — safe: single-threaded config parse */
    if (ngx_js_pending_server_class_id == 0) {
        JS_NewClassID(&ngx_js_pending_server_class_id);
    }

    rt = JS_NewRuntime();
    if (rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    js_std_init_handlers(rt);
    JS_SetSharedArrayBufferFunctions(rt, &ngx_js_sab_funcs);

    if (JS_NewClass(rt, ngx_js_pending_server_class_id,
                    &ngx_js_pending_server_class) < 0)
    {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewClass(PendingServer) failed");
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

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
     * Store cf so that config.write() and addServer() can call
     * ngx_conf_parse().  Same pattern as js_preprocess.
     */
    JS_SetContextOpaque(ctx, cf);

    /* Initialise the pending-server list in cf->pool */
    if (ngx_array_init(&pending, cf->pool, 4,
                       sizeof(ngx_js_pending_server_t *)) != NGX_OK)
    {
        JS_FreeContext(ctx);
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    ngx_js_current_pending = &pending;
    ngx_js_current_cf      = cf;

    if (ngx_js_pending_server_install_proto(ctx) != NGX_OK) {
        JS_FreeContext(ctx);
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    global = JS_GetGlobalObject(ctx);

    /* Install global `config` with write() */
    config_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, config_obj, "write",
                      JS_NewCFunction(ctx, ngx_js_config_write, "write", 1));
    JS_SetPropertyStr(ctx, global, "config", config_obj);

    /* Install nginx.http.servers[] and nginx.http.addServer() */
    nginx_obj = JS_NewObject(ctx);
    http_obj  = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, http_obj, "servers",
                      ngx_js_init_http_servers(ctx, cf));
    JS_SetPropertyStr(ctx, http_obj, "addServer",
                      JS_NewCFunction(ctx, ngx_js_http_add_server,
                                      "addServer", 1));
    JS_SetPropertyStr(ctx, http_obj, "delServer",
                      JS_NewCFunction(ctx, ngx_js_http_del_server,
                                      "delServer", 1));
    JS_SetPropertyStr(ctx, http_obj, "delLocation",
                      JS_NewCFunction(ctx, ngx_js_http_del_location,
                                      "delLocation", 2));
    JS_SetPropertyStr(ctx, http_obj, "modServer",
                      JS_NewCFunction(ctx, ngx_js_http_mod_server,
                                      "modServer", 2));
    JS_SetPropertyStr(ctx, http_obj, "modLocation",
                      JS_NewCFunction(ctx, ngx_js_http_mod_location,
                                      "modLocation", 3));
    JS_SetPropertyStr(ctx, http_obj, "addVariable",
                      JS_NewCFunction(ctx, ngx_js_http_add_variable,
                                      "addVariable", 1));

    JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);
    JS_SetPropertyStr(ctx, global, "nginx", nginx_obj);

    JS_FreeValue(ctx, global);

    rv = ngx_js_eval_module(ctx, rt, src, src_len, path.data, cf->log);

    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);

    /* Flush pending servers added via addServer() */
    if (rv == NGX_CONF_OK) {
        rv = ngx_js_apply_pending_servers(cf, &pending);
    }

    ngx_js_current_pending = NULL;
    ngx_js_current_cf      = NULL;

    return rv;
}


/* ------------------------------------------------------------------ */
/* NGX_HTTP_MODULE lifecycle                                            */
/* ------------------------------------------------------------------ */

static void *
ngx_js_create_loc_conf(ngx_conf_t *cf)
{
    ngx_js_loc_conf_t  *jlcf;

    jlcf = ngx_pcalloc(cf->pool, sizeof(ngx_js_loc_conf_t));
    if (jlcf == NULL) {
        return NULL;
    }

    jlcf->handler_idx        = -1;  /* unset */
    jlcf->header_filters     = NULL;
    jlcf->body_filters       = NULL;
    jlcf->own_header_filters = 1;  /* NULL is "owned" */
    jlcf->own_body_filters   = 1;
    jlcf->body_filter_has_wb = 0;
    jlcf->pool               = cf->pool;
    jlcf->hooks              = NULL;
    jlcf->own_hooks          = 1;  /* NULL is "owned" */
    jlcf->response_hooks          = NULL;
    jlcf->own_response_hooks      = 1;  /* NULL is "owned" */
    jlcf->upstream_filters        = NULL;
    jlcf->own_upstream_filters    = 1;
    jlcf->upstream_req_filters    = NULL;
    jlcf->own_upstream_req_filters = 1;

    return jlcf;
}



static char *
ngx_js_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_js_loc_conf_t  *prev = parent;
    ngx_js_loc_conf_t  *conf = child;

    if (conf->handler_idx == -1) {
        conf->handler_idx = prev->handler_idx;
    }

    /* Inherit parent filter lists (pointer copy).
     * own_* = 0 marks the pointer as borrowed; the JS setter triggers
     * copy-on-first-write before any mutation. */
    if (conf->header_filters == NULL && prev->header_filters != NULL) {
        conf->header_filters     = prev->header_filters;
        conf->own_header_filters = 0;
    }

    if (conf->body_filters == NULL && prev->body_filters != NULL) {
        conf->body_filters       = prev->body_filters;
        conf->own_body_filters   = 0;
        conf->body_filter_has_wb = prev->body_filter_has_wb;
    }

    /* Inherit parent hook list (pointer copy, copy-on-first-write on mutation) */
    if (conf->hooks == NULL && prev->hooks != NULL) {
        conf->hooks     = prev->hooks;
        conf->own_hooks = 0;
    }

    /* Inherit parent response hook list */
    if (conf->response_hooks == NULL && prev->response_hooks != NULL) {
        conf->response_hooks     = prev->response_hooks;
        conf->own_response_hooks = 0;
    }

    /* Inherit parent upstream filter lists */
    if (conf->upstream_filters == NULL && prev->upstream_filters != NULL) {
        conf->upstream_filters     = prev->upstream_filters;
        conf->own_upstream_filters = 0;
    }

    if (conf->upstream_req_filters == NULL
        && prev->upstream_req_filters != NULL)
    {
        conf->upstream_req_filters     = prev->upstream_req_filters;
        conf->own_upstream_req_filters = 0;
    }

    return NGX_CONF_OK;
}




static ngx_command_t  ngx_js_http_commands[] = {

    /*
     * js_init_http /path/to/script.js;
     *
     * Valid inside http{}.  Evaluates the named JS file immediately
     * when this directive is encountered during ngx_conf_parse() of
     * the http{} block.  The script receives:
     *
     *   config.write(text)    — inject nginx config text (server{} etc.)
     *   nginx.http.servers[]  — read-only view of servers parsed so far
     *
     * New servers added via config.write() are handled by all of
     * nginx's normal merge/init_locations/optimize_servers machinery
     * because they are added during the http{} parse phase.
     *
     * Place js_init_http AFTER the server{} blocks you want to read.
     */
    { ngx_string("js_init_http"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_js_init_http,
      0,
      0,
      NULL },

    ngx_null_command
};


static ngx_http_module_t  ngx_js_http_module_ctx = {
    NULL,                           /* preconfiguration  */
    ngx_js_http_postconfiguration,  /* postconfiguration */
    ngx_js_http_create_main_conf,   /* create main configuration */
    NULL,                           /* init main configuration   */
    ngx_js_http_create_srv_conf,    /* create server configuration */
    ngx_js_http_merge_srv_conf,     /* merge server configuration  */
    ngx_js_create_loc_conf,         /* create location configuration */
    ngx_js_merge_loc_conf           /* merge location configuration  */
};


/*
 * Run nginx.broadcast() callbacks here, after ngx_event_process_init()
 * (index 7) has already initialized the timer rbtree and free_connections.
 * ngx_js_module (index 3) runs init_process first and only sets up the
 * worker struct; timers added there would be wiped by ngx_event_timer_init.
 */
static ngx_int_t
ngx_js_http_init_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;
    JSValue           global, nginx_obj, queue, len_val, fn, ret;
    JSContext        *job_ctx;
    uint32_t          len, i;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf->rt == NULL || jcf->worker == NULL) {
        return NGX_OK;
    }

    w = jcf->worker;

    global    = JS_GetGlobalObject(w->ctx);
    nginx_obj = JS_GetPropertyStr(w->ctx, global, "nginx");
    JS_FreeValue(w->ctx, global);

    if (JS_IsException(nginx_obj) || JS_IsUndefined(nginx_obj)) {
        JS_FreeValue(w->ctx, nginx_obj);
        return NGX_OK;
    }

    queue = JS_GetPropertyStr(w->ctx, nginx_obj, "__broadcast_queue");
    JS_FreeValue(w->ctx, nginx_obj);

    if (JS_IsException(queue) || JS_IsUndefined(queue)) {
        JS_FreeValue(w->ctx, queue);
        return NGX_OK;
    }

    len_val = JS_GetPropertyStr(w->ctx, queue, "length");
    JS_ToUint32(w->ctx, &len, len_val);
    JS_FreeValue(w->ctx, len_val);

    for (i = 0; i < len; i++) {
        fn  = JS_GetPropertyUint32(w->ctx, queue, i);
        ret = JS_Call(w->ctx, fn, JS_UNDEFINED, 0, NULL);
        JS_FreeValue(w->ctx, fn);

        if (JS_IsException(ret)) {
            ngx_js_log_exception(w->ctx, cycle->log);
        }
        JS_FreeValue(w->ctx, ret);

        /* drain microtasks */
        while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { }
    }

    JS_FreeValue(w->ctx, queue);

    return NGX_OK;
}


ngx_module_t  ngx_js_http_module = {
    NGX_MODULE_V1,
    &ngx_js_http_module_ctx,    /* module context  */
    ngx_js_http_commands,       /* module directives */
    NGX_HTTP_MODULE,            /* module type     */
    NULL,                       /* init master     */
    NULL,                       /* init module     */
    ngx_js_http_init_process,   /* init process    */
    NULL,                       /* init thread     */
    NULL,                       /* exit thread     */
    NULL,                       /* exit process    */
    NULL,                       /* exit master     */
    NGX_MODULE_V1_PADDING
};
