
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
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "js: async handler still pending after "
                              "job drain (real async I/O not supported)");
                JS_FreeValue(ctx, result);
                JS_FreeValue(ctx, req_obj);
                return NGX_HTTP_INTERNAL_SERVER_ERROR;

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
    NULL,                       /* module directives */
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
