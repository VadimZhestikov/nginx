
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
#include <cutils.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* ------------------------------------------------------------------ */
/* NginxRequest class                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    ngx_http_request_t  *r;
    ngx_int_t            respond_rc;  /* rc from ngx_http_output_filter */
    unsigned             responded:1; /* set when req.respond() was called */
} ngx_js_request_opaque_t;


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
 */
typedef struct {
    JSContext        *ctx;
    JSRuntime        *rt;
    ngx_js_worker_t  *w;
    JSValue           resolve;
    JSValue           reject;
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
    ngx_http_set_ctx(r, NULL, ngx_js_http_module);

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
    ngx_http_finalize_request(r, NGX_DONE);
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

    ngx_http_set_ctx(r, bctx, ngx_js_http_module);

    rc = ngx_http_read_client_request_body(r, ngx_js_body_done);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        /* bctx is still in pool — body_done was not called, free manually */
        ngx_http_set_ctx(r, NULL, ngx_js_http_module);
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
 * Subrequest context — allocated from the parent request's pool.
 * Holds everything the post-subrequest callback and resume handler need.
 */
typedef struct {
    JSContext        *ctx;
    JSRuntime        *rt;
    ngx_js_worker_t  *w;
    JSValue           resolve;
    JSValue           reject;
} ngx_js_subreq_ctx_t;


/*
 * write_event_handler installed on the parent request by ngx_js_subreq_done.
 * nginx calls this (via ngx_http_run_posted_requests) once the subrequest
 * finalization machinery has fully unwound.  Safe to drain JS microtasks
 * and finalize the parent here.
 */
static void
ngx_js_subreq_resume(ngx_http_request_t *r)
{
    ngx_js_subreq_ctx_t  *sctx;
    JSContext            *job_ctx;

    sctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (sctx == NULL) {
        return;
    }

    /* Restore the slot so a future subrequest on this request can reuse it */
    ngx_http_set_ctx(r, NULL, ngx_js_http_module);

    /* Restore a safe default write handler before we run user JS code */
    r->write_event_handler = ngx_http_request_empty_handler;

    /* Run continuations (r.respond() fires here) */
    while (JS_ExecutePendingJob(sctx->rt, &job_ctx) > 0) { }

    /* Finalize parent request once the top-level handler Promise settles */
    ngx_js_async_check(sctx->w);
}


/*
 * Post-subrequest callback: resolves the JS Promise with {status, body},
 * then defers microtask drain + parent resumption to the next event loop
 * iteration by hooking write_event_handler on the parent.
 *
 * After we return, nginx decrements r->main->count and posts the parent
 * request (ngx_http_post_request at ngx_http_request.c:2768).
 * ngx_http_run_posted_requests then calls write_event_handler — safely
 * outside the subrequest finalization stack.
 */
static ngx_int_t
ngx_js_subreq_done(ngx_http_request_t *sr, void *data, ngx_int_t rc)
{
    ngx_js_subreq_ctx_t  *sctx = data;
    JSContext            *ctx  = sctx->ctx;
    JSValue               result, arg;
    u_char               *body_data;
    size_t                body_len;

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

    /* Build {status, body, upstream} result object */
    arg = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, arg, "status",
                      JS_NewInt32(ctx,
                                  (int32_t) sr->headers_out.status));
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
     * Stash sctx and redirect write_event_handler so ngx_js_subreq_resume
     * is called from ngx_http_run_posted_requests after we return.
     */
    ngx_http_set_ctx(sr->main, sctx, ngx_js_http_module);
    sr->main->write_event_handler = ngx_js_subreq_resume;

    return NGX_OK;
}


/*
 * req.subrequest(uri) → Promise<{status, body}>
 *
 * Issues an nginx internal subrequest to `uri` (no leading query string
 * args; use "$uri?args" style if needed).  The response body is buffered
 * in memory (NGX_HTTP_SUBREQUEST_IN_MEMORY).  The returned Promise
 * resolves to a plain object {status: number, body: string}.
 *
 * Must be used with `await` inside an async handler.
 */
static JSValue
ngx_js_request_subrequest(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_request_opaque_t     *op;
    ngx_http_request_t          *r;
    ngx_js_worker_t             *w;
    ngx_js_subreq_ctx_t         *sctx;
    ngx_http_request_t          *sr;
    ngx_http_post_subrequest_t  *psr;
    JSValue                      resolving[2], promise;
    const char                  *uri_cstr;
    size_t                       uri_len;
    ngx_str_t                    uri;
    ngx_int_t                    rc;

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

    /* Create Promise */
    promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        return promise;
    }

    /* Subrequest context in parent pool */
    sctx = ngx_palloc(r->pool, sizeof(ngx_js_subreq_ctx_t));
    if (!sctx) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowOutOfMemory(ctx);
    }

    sctx->ctx     = ctx;
    sctx->rt      = w->rt;
    sctx->w       = w;
    sctx->resolve = resolving[0];
    sctx->reject  = resolving[1];

    /* Post-subrequest callback in parent pool */
    psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (!psr) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowOutOfMemory(ctx);
    }

    psr->handler = ngx_js_subreq_done;
    psr->data    = sctx;

    rc = ngx_http_subrequest(r, &uri, NULL, &sr, psr,
                             NGX_HTTP_SUBREQUEST_IN_MEMORY);
    if (rc != NGX_OK) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "r.subrequest: failed (%ld)",
                                     (long) rc);
    }

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

    if (argc < 3) {
        return JS_ThrowTypeError(ctx,
                                 "respond(status, headers, body) "
                                 "requires 3 arguments");
    }

    r = op->r;

    if (JS_ToInt32(ctx, &status, argv[0])) {
        return JS_EXCEPTION;
    }

    r->headers_out.status = (ngx_uint_t) status;

    /* ---- Response headers from argv[1] JS object ---- */

    if (JS_IsObject(argv[1])
        && JS_GetOwnPropertyNames(ctx, &tab, &tab_len, argv[1],
                                  JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0)
    {
        for (j = 0; j < tab_len; j++) {
            hkey = JS_AtomToString(ctx, tab[j].atom);
            hval = JS_GetProperty(ctx, argv[1], tab[j].atom);

            key_cstr = JS_ToCString(ctx, hkey);
            val_cstr = JS_ToCString(ctx, hval);

            if (key_cstr && val_cstr) {

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

    /* ---- Body ---- */

    body_cstr = JS_ToCString(ctx, argv[2]);
    if (!body_cstr) {
        return JS_EXCEPTION;
    }

    body_len = ngx_strlen(body_cstr);
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
        JS_FreeCString(ctx, body_cstr);
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
            JS_FreeCString(ctx, body_cstr);
            op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            op->responded  = 1;
            return JS_UNDEFINED;
        }

        b->last = ngx_cpymem(b->pos, body_cstr, body_len);

    } else {
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            JS_FreeCString(ctx, body_cstr);
            op->respond_rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            op->responded  = 1;
            return JS_UNDEFINED;
        }
    }

    b->last_buf      = 1;
    b->last_in_chain = 1;

    JS_FreeCString(ctx, body_cstr);

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
    JS_CFUNC_DEF("respond",     3, ngx_js_request_respond),
    JS_CFUNC_DEF("variable",    1, ngx_js_request_variable),
    JS_CFUNC_DEF("setVariable", 2, ngx_js_request_set_variable),
    JS_CFUNC_DEF("subrequest",  1, ngx_js_request_subrequest),
    JS_CFUNC_DEF("log",         2, ngx_js_request_log),
    JS_CGETSET_MAGIC_DEF("queryParams", ngx_js_request_get, NULL, 16),
    JS_CGETSET_MAGIC_DEF("cookies",     ngx_js_request_get, NULL, 17),
    JS_CGETSET_MAGIC_DEF("upstream",    ngx_js_request_get, NULL, 18),
    JS_CGETSET_MAGIC_DEF("variables",   ngx_js_request_get, NULL, 19),
    JS_CGETSET_MAGIC_DEF("body",        ngx_js_request_get, NULL, 20),
    JS_CFUNC_DEF("readBody",            0, ngx_js_request_read_body),
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

    return NGX_OK;
}


JSValue
ngx_js_wrap_request(JSContext *ctx, ngx_http_request_t *r)
{
    JSValue                   obj, proto;
    ngx_js_request_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_request_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->r = r;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_request_proto_funcs,
                               countof(ngx_js_request_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_request_class_id);
    JS_FreeValue(ctx, proto);

    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


/* ------------------------------------------------------------------ */
/* Async request finalizer — called from timer handler in ngx_js_com.c */
/* ------------------------------------------------------------------ */

void
ngx_js_async_check(ngx_js_worker_t *w)
{
    ngx_js_async_ctx_t       *actx;
    ngx_js_request_opaque_t  *req_op;
    JSContext                *ctx;
    JSValue                   reason, str;
    const char               *cstr;

    actx = w->async_pending;
    if (actx == NULL) {
        return;
    }

    ctx = w->ctx;

    switch (JS_PromiseState(ctx, actx->promise)) {

    case JS_PROMISE_FULFILLED:
        w->async_pending = NULL;
        req_op = JS_GetOpaque(actx->req_obj, ngx_js_request_class_id);
        if (req_op == NULL || !req_op->responded) {
            ngx_log_error(NGX_LOG_ERR, actx->r->connection->log, 0,
                          "js: async handler fulfilled without calling "
                          "req.respond()");
        }
        JS_FreeValue(ctx, actx->req_obj);
        JS_FreeValue(ctx, actx->promise);
        ngx_http_finalize_request(actx->r, NGX_DONE);
        break;

    case JS_PROMISE_REJECTED:
        w->async_pending = NULL;
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
        /* still waiting; another timer will call ngx_js_async_check later */
        break;
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

    ctx      = w->ctx;
    global   = JS_GetGlobalObject(ctx);
    registry = JS_GetPropertyStr(ctx, global, "__ngx_handlers__");
    JS_FreeValue(ctx, global);

    fn = JS_GetPropertyUint32(ctx, registry, (uint32_t) jlcf->handler_idx);
    JS_FreeValue(ctx, registry);

    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js: handler #%i not found or not callable",
                      jlcf->handler_idx);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    req_obj = ngx_js_wrap_request(ctx, r);
    if (JS_IsException(req_obj)) {
        JS_FreeValue(ctx, fn);
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &req_obj);

    JS_FreeValue(ctx, fn);

    /* Synchronous exception */
    if (JS_IsException(result)) {
        ngx_js_log_exception(ctx, r->connection->log);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, req_obj);
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
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            case JS_PROMISE_PENDING:
            {
                ngx_js_async_ctx_t  *actx;

                if (w->async_pending != NULL) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "js: another async request already pending");
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, req_obj);
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                actx = ngx_pcalloc(r->pool, sizeof(ngx_js_async_ctx_t));
                if (actx == NULL) {
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, req_obj);
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                actx->r       = r;
                actx->req_obj = JS_DupValue(ctx, req_obj);
                actx->promise = JS_DupValue(ctx, result);

                w->async_pending = actx;
                r->main->count++;

                JS_FreeValue(ctx, req_obj);
                JS_FreeValue(ctx, result);
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
    req_op   = JS_GetOpaque(req_obj, ngx_js_request_class_id);
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
    JSValue                    obj, proto, arr_val, item, len_val;
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
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_pending_server_proto_funcs,
                               countof(ngx_js_pending_server_proto_funcs));

    obj = JS_NewObjectProtoClass(ctx, proto, ngx_js_pending_server_class_id);
    JS_FreeValue(ctx, proto);

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
 * The JS runtime is short-lived and independent of the js_include
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

    jlcf->handler_idx = -1;  /* unset */

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
    NULL,                       /* preconfiguration  */
    NULL,                       /* postconfiguration */
    NULL,                       /* create main configuration */
    NULL,                       /* init main configuration   */
    NULL,                       /* create server configuration */
    NULL,                       /* merge server configuration  */
    ngx_js_create_loc_conf,     /* create location configuration */
    ngx_js_merge_loc_conf       /* merge location configuration  */
};


ngx_module_t  ngx_js_http_module = {
    NGX_MODULE_V1,
    &ngx_js_http_module_ctx,    /* module context  */
    ngx_js_http_commands,       /* module directives */
    NGX_HTTP_MODULE,            /* module type     */
    NULL,                       /* init master     */
    NULL,                       /* init module     */
    NULL,                       /* init process    */
    NULL,                       /* init thread     */
    NULL,                       /* exit thread     */
    NULL,                       /* exit process    */
    NULL,                       /* exit master     */
    NGX_MODULE_V1_PADDING
};
