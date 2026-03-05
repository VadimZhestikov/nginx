
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
 *   0 — method      (r/o string)
 *   1 — uri         (r/o string, decoded, no query string)
 *   2 — args        (r/o string, query string)
 *   3 — remoteAddr  (r/o string)
 *   4 — headers     (r/o object, lowercase keys)
 */
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
    }

    return JS_UNDEFINED;
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


static const JSCFunctionListEntry ngx_js_request_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("method",     ngx_js_request_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("uri",        ngx_js_request_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("args",       ngx_js_request_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("remoteAddr", ngx_js_request_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("headers",    ngx_js_request_get, NULL, 4),
    JS_CFUNC_DEF("respond", 3, ngx_js_request_respond),
};


ngx_int_t
ngx_js_request_register_class(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_request_class_id, &ngx_js_request_class) < 0) {
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
    ngx_str_t   *value, path;
    u_char      *src;
    size_t       src_len;
    JSRuntime   *rt;
    JSContext   *ctx;
    JSValue      global, config_obj, nginx_obj, http_obj, result;
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

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewContext() failed");
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    /*
     * Store cf so that config.write() can call ngx_conf_parse().
     * This is the same pattern used by js_preprocess.
     */
    JS_SetContextOpaque(ctx, cf);

    global = JS_GetGlobalObject(ctx);

    /* Install global `config` with write() */
    config_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, config_obj, "write",
                      JS_NewCFunction(ctx, ngx_js_config_write, "write", 1));
    JS_SetPropertyStr(ctx, global, "config", config_obj);

    /* Install nginx.http.servers[] (read-only parse-time view) */
    nginx_obj = JS_NewObject(ctx);
    http_obj  = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, http_obj, "servers",
                      ngx_js_init_http_servers(ctx, cf));

    JS_SetPropertyStr(ctx, nginx_obj, "http", http_obj);
    JS_SetPropertyStr(ctx, global, "nginx", nginx_obj);

    JS_FreeValue(ctx, global);

    result = JS_Eval(ctx, (const char *) src, src_len,
                     (const char *) path.data, JS_EVAL_TYPE_GLOBAL);

    rv = NGX_CONF_OK;

    if (JS_IsException(result)) {
        ngx_js_log_exception(ctx, cf->log);
        rv = NGX_CONF_ERROR;
    }

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

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
