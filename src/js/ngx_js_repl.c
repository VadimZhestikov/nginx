
/*
 * Copyright (C) nginx JS contributors
 *
 * nginx.repl — interactive JS REPL primitives.
 *
 * Exposes the following functions under nginx.repl:
 *
 *   nginx.repl._writeFd(fd, str)
 *     Synchronously writes str to the given file descriptor.
 *
 *   nginx.repl.eval(line)  → {status, value, message, stack}
 *     Evaluates one JS line in the current context.
 *     status: 'ok'         — expression or statement evaluated OK
 *             'incomplete' — input ends mid-expression (multiline prompt)
 *             'error'      — syntax or runtime error
 *
 *   nginx.repl.attach(fd, consoleMinLevel, nginxMaxLevel)
 *     Overrides console.debug/log/warn/error and nginx.log so that messages
 *     at or above the given levels are also written to fd as
 *     "* LOG console:<level> <msg>\n" / "* LOG nginx:<level> <msg>\n".
 *     Originals are saved for detach().
 *     consoleMinLevel: 1=debug, 2=log, 3=warn, 4=error
 *     nginxMaxLevel:   1–8 (nginx: 1=emerg … 8=debug; ≤level means ≥severity)
 *
 *   nginx.repl.detach()
 *     Restores originals saved by attach().  No-op if not attached.
 *
 *   nginx.repl.listen(fd, onLine)
 *     Installs an nginx event-loop read handler on fd (which must be an
 *     active nginx connection fd from req.hijack()).  Calls onLine(line)
 *     for each \n-terminated line received.  When the connection is closed
 *     by the peer, finalises the hijacked request automatically.
 *
 *   nginx.repl.listenRaw(fd, onData)
 *     Like listen() but delivers each received chunk as a Uint8Array instead
 *     of splitting on newlines.  Suitable for binary protocols (WebSocket).
 *
 *   nginx.repl._writeFdRaw(fd, data)
 *     Like _writeFd() but accepts a Uint8Array or ArrayBuffer and writes the
 *     raw bytes to fd without UTF-8 re-encoding.  Required for WebSocket
 *     frames whose headers contain bytes > 0x7F.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <unistd.h>
#include <errno.h>
#include <quickjs.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "ngx_js_repl.h"


/* ------------------------------------------------------------------ */
/* nginx.repl._writeFd(fd, str)                                         */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_repl_write_fd(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    int32_t     fd;
    const char *cstr;
    size_t      len, pos;
    ssize_t     n;

    if (argc < 2) {
        return JS_UNDEFINED;
    }

    if (JS_ToInt32(ctx, &fd, argv[0])) {
        return JS_EXCEPTION;
    }

    cstr = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!cstr) {
        return JS_EXCEPTION;
    }

    pos = 0;
    while (pos < len) {
        n = write((int) fd, cstr + pos, len - pos);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;  /* ignore other errors — client may have disconnected */
        }
        pos += (size_t) n;
    }

    JS_FreeCString(ctx, cstr);
    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* nginx.repl._writeFdRaw(fd, data)                                     */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_repl_write_fd_raw(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    int32_t   fd;
    JSValue   ab;
    uint8_t  *ptr;
    size_t    byte_off, byte_len, total, pos;
    ssize_t   n;

    if (argc < 2) {
        return JS_UNDEFINED;
    }

    if (JS_ToInt32(ctx, &fd, argv[0])) {
        return JS_EXCEPTION;
    }

    /* Accept Uint8Array (or any typed array) */
    ab = JS_GetTypedArrayBuffer(ctx, argv[1], &byte_off, &byte_len, NULL);
    if (JS_IsException(ab)) {
        /* Not a typed array — try treating as ArrayBuffer directly */
        JS_FreeValue(ctx, JS_GetException(ctx));
        ab       = JS_DupValue(ctx, argv[1]);
        byte_off = 0;
        ptr      = JS_GetArrayBuffer(ctx, &byte_len, ab);
    } else {
        ptr = JS_GetArrayBuffer(ctx, &total, ab);
        if (ptr) {
            ptr += byte_off;
        }
    }

    if (ptr == NULL) {
        JS_FreeValue(ctx, ab);
        return JS_ThrowTypeError(ctx,
            "_writeFdRaw: argument must be a Uint8Array or ArrayBuffer");
    }

    pos = 0;
    while (pos < byte_len) {
        n = write((int) fd, ptr + pos, byte_len - pos);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        pos += (size_t) n;
    }

    JS_FreeValue(ctx, ab);
    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* nginx.repl.eval(line)                                                */
/* ------------------------------------------------------------------ */

/*
 * Check whether `input` is syntactically complete.
 * Returns 1 if the input is a complete (possibly erroneous) statement,
 * 0 if it is an incomplete expression ("unexpected end of input").
 * Side-effect: clears any pending exception.
 */
static int
ngx_js_repl_is_complete(JSContext *ctx, const char *input, size_t len)
{
    JSValue     compiled;
    int         complete;
    JSValue     exc, msg;
    const char *cstr;

    compiled = JS_Eval(ctx, input, len, "<repl>",
                       JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);

    if (!JS_IsException(compiled)) {
        JS_FreeValue(ctx, compiled);
        return 1;
    }

    /* Inspect the exception message */
    exc  = JS_GetException(ctx);
    msg  = JS_GetPropertyStr(ctx, exc, "message");
    cstr = JS_ToCString(ctx, msg);

    complete = 1;  /* assume complete unless "end of input" */
    if (cstr && (strstr(cstr, "unexpected end of input")
                 || strstr(cstr, "unexpected token in expression: ''")
                 || strstr(cstr, "missing formal parameter")))
    {
        complete = 0;
    }

    if (cstr) {
        JS_FreeCString(ctx, cstr);
    }
    JS_FreeValue(ctx, msg);
    JS_FreeValue(ctx, exc);

    return complete;
}


static JSValue
ngx_js_repl_eval(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char  *input;
    size_t       input_len;
    char        *expr_buf;
    JSValue      result, ret_obj;
    int          is_expr;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "nginx.repl.eval(line): line required");
    }

    input = JS_ToCStringLen(ctx, &input_len, argv[0]);
    if (!input) {
        return JS_EXCEPTION;
    }

    /* Check for incomplete input before trying to run anything */
    if (!ngx_js_repl_is_complete(ctx, input, input_len)) {
        JS_FreeCString(ctx, input);
        ret_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ret_obj, "status",
                          JS_NewString(ctx, "incomplete"));
        return ret_obj;
    }

    /* Try expression form: "(input\n)" — captures the result value */
    is_expr = 0;
    expr_buf = js_malloc(ctx, input_len + 4);
    if (expr_buf) {
        expr_buf[0] = '(';
        ngx_memcpy(expr_buf + 1, input, input_len);
        expr_buf[input_len + 1] = '\n';
        expr_buf[input_len + 2] = ')';
        expr_buf[input_len + 3] = '\0';

        result = JS_Eval(ctx, expr_buf, input_len + 3, "<repl>",
                         JS_EVAL_TYPE_GLOBAL
                         | JS_EVAL_FLAG_BACKTRACE_BARRIER);
        js_free(ctx, expr_buf);

        if (!JS_IsException(result)) {
            is_expr = 1;
        } else {
            JS_FreeValue(ctx, result);
            result = JS_UNDEFINED;
        }
    }

    if (!is_expr) {
        /* Fall back to statement evaluation */
        result = JS_Eval(ctx, input, input_len, "<repl>",
                         JS_EVAL_TYPE_GLOBAL
                         | JS_EVAL_FLAG_BACKTRACE_BARRIER);
    }

    JS_FreeCString(ctx, input);

    ret_obj = JS_NewObject(ctx);

    if (JS_IsException(result)) {
        JSValue  exc, exc_msg, exc_stack;
        JSValue  msg_str;

        exc = JS_GetException(ctx);

        exc_msg = JS_GetPropertyStr(ctx, exc, "message");
        msg_str = JS_IsUndefined(exc_msg) ? JS_ToString(ctx, exc)
                                          : JS_ToString(ctx, exc_msg);
        JS_FreeValue(ctx, exc_msg);

        JS_SetPropertyStr(ctx, ret_obj, "status",
                          JS_NewString(ctx, "error"));
        JS_SetPropertyStr(ctx, ret_obj, "message", msg_str);

        exc_stack = JS_GetPropertyStr(ctx, exc, "stack");
        JS_SetPropertyStr(ctx, ret_obj, "stack",
                          JS_IsString(exc_stack) ? exc_stack
                                                 : JS_NewString(ctx, ""));
        if (!JS_IsString(exc_stack)) {
            JS_FreeValue(ctx, exc_stack);
        }

        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, result);
        return ret_obj;
    }

    JS_SetPropertyStr(ctx, ret_obj, "status", JS_NewString(ctx, "ok"));

    if (!JS_IsUndefined(result)) {
        /* Serialize to JSON string for transmission; fall back to toString */
        JSValue  global, json_obj, stringify_fn, json_str;

        global      = JS_GetGlobalObject(ctx);
        json_obj    = JS_GetPropertyStr(ctx, global, "JSON");
        stringify_fn = JS_GetPropertyStr(ctx, json_obj, "stringify");
        json_str    = JS_Call(ctx, stringify_fn, json_obj, 1, &result);
        JS_FreeValue(ctx, stringify_fn);
        JS_FreeValue(ctx, json_obj);
        JS_FreeValue(ctx, global);

        if (JS_IsException(json_str) || JS_IsUndefined(json_str)) {
            /* Non-serialisable value (function, circular, etc.) */
            JS_FreeValue(ctx, json_str);
            JS_SetPropertyStr(ctx, ret_obj, "value",
                              JS_ToString(ctx, result));
        } else {
            JS_SetPropertyStr(ctx, ret_obj, "value", json_str);
        }
    } else {
        JS_SetPropertyStr(ctx, ret_obj, "value", JS_UNDEFINED);
    }

    JS_FreeValue(ctx, result);
    return ret_obj;
}


/* ------------------------------------------------------------------ */
/* nginx.repl.attach(fd, consoleMinLevel, nginxMaxLevel)                */
/* ------------------------------------------------------------------ */

/*
 * Installs JS wrappers around console.* and nginx.log that forward
 * matching log lines to fd as streaming LOG messages.
 * All work is done in JS so we avoid C ↔ JS impedance for variadic args.
 */
static JSValue
ngx_js_repl_attach(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    static const char  script_fmt[] =
        "(function(fd, minLevel, nginxLevel) {"
        "  var _p = nginx.repl.__orig;"
        "  if (_p) {"
        "    console.debug = _p.debug;"
        "    console.log   = _p.log;"
        "    console.warn  = _p.warn;"
        "    console.error = _p.error;"
        "    nginx.log     = _p.nginxLog;"
        "    delete nginx.repl.__orig;"
        "  }"
        "  const _wfd = nginx.repl._writeFd;"
        "  const _nl  = nginx.log;"
        "  const _cd  = console.debug;"
        "  const _cl  = console.log;"
        "  const _cw  = console.warn;"
        "  const _ce  = console.error;"
        "  function _fmt(args) {"
        "    return Array.from(args).map(function(a){"
        "      return (typeof a === 'object' && a !== null)"
        "        ? JSON.stringify(a) : String(a);"
        "    }).join(' ');"
        "  }"
        "  if (minLevel <= 1) {"
        "    console.debug = function() {"
        "      _wfd(fd, '* LOG console:debug ' + _fmt(arguments) + '\\n');"
        "      _cd.apply(console, arguments);"
        "    };"
        "  }"
        "  if (minLevel <= 2) {"
        "    console.log = function() {"
        "      _wfd(fd, '* LOG console:log ' + _fmt(arguments) + '\\n');"
        "      _cl.apply(console, arguments);"
        "    };"
        "  }"
        "  if (minLevel <= 3) {"
        "    console.warn = function() {"
        "      _wfd(fd, '* LOG console:warn ' + _fmt(arguments) + '\\n');"
        "      _cw.apply(console, arguments);"
        "    };"
        "  }"
        "  if (minLevel <= 4) {"
        "    console.error = function() {"
        "      _wfd(fd, '* LOG console:error ' + _fmt(arguments) + '\\n');"
        "      _ce.apply(console, arguments);"
        "    };"
        "  }"
        "  nginx.log = function(level, msg) {"
        "    _nl(level, msg);"
        "    if (level <= nginxLevel) {"
        "      _wfd(fd, '* LOG nginx:' + level + ' ' + msg + '\\n');"
        "    }"
        "  };"
        "  nginx.repl.__orig = { debug:_cd, log:_cl, warn:_cw, error:_ce,"
        "                        nginxLog:_nl, fd:fd };"
        "})(%d, %d, %d);";

    char     buf[sizeof(script_fmt) + 64];
    int32_t  fd, min_level, nginx_level;
    JSValue  ret;
    ssize_t  n;

    if (argc < 3) {
        return JS_ThrowTypeError(ctx,
            "nginx.repl.attach(fd, consoleMinLevel, nginxMaxLevel): 3 args");
    }

    if (JS_ToInt32(ctx, &fd,          argv[0])) { return JS_EXCEPTION; }
    if (JS_ToInt32(ctx, &min_level,   argv[1])) { return JS_EXCEPTION; }
    if (JS_ToInt32(ctx, &nginx_level, argv[2])) { return JS_EXCEPTION; }

    n = ngx_snprintf((u_char *) buf, sizeof(buf) - 1,
                     script_fmt, (int) fd, (int) min_level,
                     (int) nginx_level) - (u_char *) buf;
    buf[n] = '\0';

    ret = JS_Eval(ctx, buf, (size_t) n, "<repl.attach>",
                  JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        return ret;
    }
    JS_FreeValue(ctx, ret);
    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* nginx.repl.detach()                                                  */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_repl_detach(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    static const char  script[] =
        "(function() {"
        "  var orig = nginx.repl.__orig;"
        "  if (!orig) return;"
        "  console.debug = orig.debug;"
        "  console.log   = orig.log;"
        "  console.warn  = orig.warn;"
        "  console.error = orig.error;"
        "  nginx.log     = orig.nginxLog;"
        "  delete nginx.repl.__orig;"
        "})();";

    JSValue  ret;

    ret = JS_Eval(ctx, script, sizeof(script) - 1, "<repl.detach>",
                  JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        return ret;
    }
    JS_FreeValue(ctx, ret);
    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* nginx.repl.listen(fd, onLine)  +  read handler                       */
/* ------------------------------------------------------------------ */

#define NGX_JS_REPL_BUF_SIZE  4096

struct ngx_js_repl_conn_s {
    JSContext           *ctx;
    JSRuntime           *rt;
    ngx_js_worker_t     *w;
    JSValue              on_line;   /* onLine callback (listen) or onData (listenRaw) */
    ngx_http_request_t  *r;
    u_char               buf[NGX_JS_REPL_BUF_SIZE];
    size_t               buf_len;
    unsigned             raw:1;     /* 1 = listenRaw: deliver Uint8Array, no line split */
    ngx_js_repl_conn_t  *next;      /* w->repl_pending linked list */
};


/* ------------------------------------------------------------------ */
/* Helper: create a Uint8Array view over a copy of the given bytes     */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_make_uint8array(JSContext *ctx, const u_char *data, size_t len)
{
    JSValue  ab, global, ctor, arr;

    ab = JS_NewArrayBufferCopy(ctx, data, len);
    if (JS_IsException(ab)) {
        return ab;
    }

    global = JS_GetGlobalObject(ctx);
    ctor   = JS_GetPropertyStr(ctx, global, "Uint8Array");
    JS_FreeValue(ctx, global);

    arr = JS_CallConstructor(ctx, ctor, 1, (JSValueConst *) &ab);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, ab);

    return arr;
}


static void
ngx_js_repl_read_handler(ngx_event_t *ev)
{
    ngx_connection_t    *c;
    ngx_http_request_t  *r;
    ngx_js_req_ctx_t    *rctx;
    ngx_js_repl_conn_t  *rc;
    ssize_t              n;
    u_char              *line_start, *newline;
    size_t               line_len;
    JSValue              line_val, result;
    JSContext           *job_ctx;

    c    = ev->data;
    r    = c->data;

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL || rctx->repl == NULL) {
        return;
    }

    rc = (ngx_js_repl_conn_t *) rctx->repl;

    if (ev->timedout || c->close) {
        goto cleanup;
    }

    n = c->recv(c, rc->buf + rc->buf_len,
                NGX_JS_REPL_BUF_SIZE - rc->buf_len - 1);
    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(ev, 0) != NGX_OK) {
            goto cleanup;
        }
        return;
    }

    if (n <= 0) {
        /* Peer closed or read error */
        goto cleanup;
    }

    rc->buf_len += (size_t) n;

    /* Raw mode: deliver the received bytes directly as a Uint8Array */
    if (rc->raw) {
        JSValue  data_val, result;

        data_val = ngx_js_make_uint8array(rc->ctx, rc->buf, (size_t) n);
        rc->buf_len = 0;  /* buffer consumed immediately */

        if (!JS_IsException(data_val)) {
            result = JS_Call(rc->ctx, rc->on_line, JS_UNDEFINED, 1,
                             (JSValueConst *) &data_val);
            JS_FreeValue(rc->ctx, data_val);

            if (JS_IsException(result)) {
                JSValue  exc = JS_GetException(rc->ctx);
                JSValue  str = JS_ToString(rc->ctx, exc);
                const char *cstr = JS_ToCString(rc->ctx, str);
                if (cstr) {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "js: repl onData exception: %s", cstr);
                    JS_FreeCString(rc->ctx, cstr);
                }
                JS_FreeValue(rc->ctx, str);
                JS_FreeValue(rc->ctx, exc);
            }
            JS_FreeValue(rc->ctx, result);
        } else {
            JS_FreeValue(rc->ctx, data_val);
        }

        while (JS_ExecutePendingJob(rc->rt, &job_ctx) > 0) { }
        ngx_js_async_check(rc->w);
        ngx_js_bf_async_check(rc->w);
        ngx_js_sf_async_check(rc->w);

        if (ngx_handle_read_event(ev, 0) != NGX_OK) {
            goto cleanup;
        }
        return;
    }

    /* Dispatch complete lines (\n terminated) */
    line_start = rc->buf;

    for (;;) {
        newline = (u_char *) ngx_strnstr(line_start, "\n",
                             rc->buf + rc->buf_len - line_start);
        if (newline == NULL) {
            break;
        }

        line_len = (size_t) (newline - line_start);

        /* Strip trailing \r for Windows-style CRLF */
        if (line_len > 0 && line_start[line_len - 1] == '\r') {
            line_len--;
        }

        line_val = JS_NewStringLen(rc->ctx, (const char *) line_start,
                                   line_len);
        result = JS_Call(rc->ctx, rc->on_line, JS_UNDEFINED, 1, &line_val);
        JS_FreeValue(rc->ctx, line_val);

        if (JS_IsException(result)) {
            /* Log exception and continue */
            JSValue  exc = JS_GetException(rc->ctx);
            JSValue  str = JS_ToString(rc->ctx, exc);
            const char *cstr = JS_ToCString(rc->ctx, str);
            if (cstr) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                              "js: repl onLine exception: %s", cstr);
                JS_FreeCString(rc->ctx, cstr);
            }
            JS_FreeValue(rc->ctx, str);
            JS_FreeValue(rc->ctx, exc);
        }

        JS_FreeValue(rc->ctx, result);

        /* Drain QuickJS microtasks after each line */
        while (JS_ExecutePendingJob(rc->rt, &job_ctx) > 0) { }
        ngx_js_async_check(rc->w);
        ngx_js_bf_async_check(rc->w);
        ngx_js_sf_async_check(rc->w);

        line_start = newline + 1;
    }

    /* Compact unprocessed data to front of buffer */
    if (line_start > rc->buf) {
        rc->buf_len = (size_t) (rc->buf + rc->buf_len - line_start);
        if (rc->buf_len > 0) {
            ngx_memmove(rc->buf, line_start, rc->buf_len);
        }
    }

    /* Overflow protection — close if a single line exceeds the buffer */
    if (rc->buf_len >= NGX_JS_REPL_BUF_SIZE - 1) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "js: repl input line too long, closing connection");
        goto cleanup;
    }

    if (ngx_handle_read_event(ev, 0) != NGX_OK) {
        goto cleanup;
    }

    return;

cleanup:
    if (rc->w != NULL) {
        ngx_js_repl_conn_t  **pp;
        for (pp = &rc->w->repl_pending; *pp != NULL; pp = &(*pp)->next) {
            if (*pp == rc) { *pp = rc->next; break; }
        }
    }
    JS_FreeValue(rc->ctx, rc->on_line);
    rc->on_line = JS_UNDEFINED;
    rctx->repl  = NULL;

    /* Release the count incremented by req.hijack() */
    ngx_http_finalize_request(r, NGX_DONE);
}


static JSValue
ngx_js_repl_listen_impl(JSContext *ctx, int argc, JSValueConst *argv, int raw)
{
    int32_t              fd;
    JSValue              on_line;
    ngx_connection_t    *c;
    ngx_http_request_t  *r;
    ngx_js_req_ctx_t    *rctx;
    ngx_js_worker_t     *w;
    ngx_js_repl_conn_t  *rc;
    const char          *name;

    name = raw ? "nginx.repl.listenRaw" : "nginx.repl.listen";

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "%s(fd, cb): 2 args required", name);
    }

    if (JS_ToInt32(ctx, &fd, argv[0])) {
        return JS_EXCEPTION;
    }

    on_line = argv[1];
    if (!JS_IsFunction(ctx, on_line)) {
        return JS_ThrowTypeError(ctx, "%s: callback must be a function", name);
    }

    /* Find the active nginx connection whose fd matches the hijacked fd.
     * ngx_cycle->connections is NOT indexed by fd, so scan linearly.
     * Skip freed connections (read->closed == 1). */
    c = NULL;
    {
        ngx_connection_t  *ca;
        ngx_uint_t         i;

        ca = ngx_cycle->connections;
        for (i = 0; i < ngx_cycle->connection_n; i++) {
            if (ca[i].fd == (ngx_socket_t) fd && !ca[i].read->closed) {
                c = &ca[i];
                break;
            }
        }
    }

    if (c == NULL) {
        return JS_ThrowRangeError(ctx, "%s: no connection for fd", name);
    }

    r = (ngx_http_request_t *) c->data;

    if (r == NULL) {
        return JS_ThrowTypeError(ctx, "%s: no request on fd", name);
    }

    rctx = ngx_http_get_module_ctx(r, ngx_js_http_module);
    if (rctx == NULL) {
        return JS_ThrowTypeError(ctx, "%s: no request context on fd", name);
    }

    w = JS_GetContextOpaque(ctx);

    rc = ngx_pcalloc(r->pool, sizeof(ngx_js_repl_conn_t));
    if (rc == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    rc->ctx     = ctx;
    rc->rt      = w ? w->rt : JS_GetRuntime(ctx);
    rc->w       = w;
    rc->on_line = JS_DupValue(ctx, on_line);
    rc->r       = r;
    rc->buf_len = 0;
    rc->raw     = raw ? 1 : 0;
    rc->next    = NULL;

    if (w != NULL) {
        rc->next        = w->repl_pending;
        w->repl_pending = rc;
    }

    rctx->repl = rc;

    /* Replace the connection's read handler with our REPL handler */
    c->read->handler = ngx_js_repl_read_handler;

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        JS_FreeValue(ctx, rc->on_line);
        rc->on_line = JS_UNDEFINED;
        rctx->repl  = NULL;
        return JS_ThrowInternalError(ctx,
            "%s: failed to register read event", name);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_repl_listen(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return ngx_js_repl_listen_impl(ctx, argc, argv, 0);
}


static JSValue
ngx_js_repl_listen_raw(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return ngx_js_repl_listen_impl(ctx, argc, argv, 1);
}


/* ------------------------------------------------------------------ */
/* ngx_js_repl_drain_exit — free JSValues on worker exit                */
/* ------------------------------------------------------------------ */

/*
 * Called from exit_process before JS_FreeContext.  If SIGTERM arrives
 * while a listenRaw/listen callback is registered, rc->on_line is still
 * a live DupValue that was never freed by the normal cleanup path.
 * Freeing it here prevents the QuickJS "list_empty(&rt->gc_obj_list)"
 * assertion in JS_FreeRuntime.
 */
void
ngx_js_repl_drain_exit(ngx_js_worker_t *w)
{
    ngx_js_repl_conn_t  *rc, *rcnext;
    ngx_js_req_ctx_t    *rctx;

    for (rc = w->repl_pending; rc != NULL; rc = rcnext) {
        rcnext = rc->next;
        ngx_log_error(NGX_LOG_WARN, rc->r->connection->log, 0,
                      "js: drain listenRaw/listen connection on worker exit");
        rctx = ngx_http_get_module_ctx(rc->r, ngx_js_http_module);
        if (rctx != NULL) {
            rctx->repl = NULL;
        }
        JS_FreeValue(rc->ctx, rc->on_line);
        rc->on_line = JS_UNDEFINED;
        ngx_http_finalize_request(rc->r, NGX_DONE);
    }

    w->repl_pending = NULL;
}


/* ------------------------------------------------------------------ */
/* ngx_js_repl_install — entry point called from ngx_js_com_init        */
/* ------------------------------------------------------------------ */

ngx_int_t
ngx_js_repl_install(JSContext *ctx, JSValue nginx_obj)
{
    JSValue  repl_obj;

    repl_obj = JS_NewObject(ctx);
    if (JS_IsException(repl_obj)) {
        return NGX_ERROR;
    }

    JS_SetPropertyStr(ctx, repl_obj, "_writeFd",
                      JS_NewCFunction(ctx, ngx_js_repl_write_fd,
                                      "_writeFd", 2));

    JS_SetPropertyStr(ctx, repl_obj, "_writeFdRaw",
                      JS_NewCFunction(ctx, ngx_js_repl_write_fd_raw,
                                      "_writeFdRaw", 2));

    JS_SetPropertyStr(ctx, repl_obj, "eval",
                      JS_NewCFunction(ctx, ngx_js_repl_eval,
                                      "eval", 1));

    JS_SetPropertyStr(ctx, repl_obj, "attach",
                      JS_NewCFunction(ctx, ngx_js_repl_attach,
                                      "attach", 3));

    JS_SetPropertyStr(ctx, repl_obj, "detach",
                      JS_NewCFunction(ctx, ngx_js_repl_detach,
                                      "detach", 0));

    JS_SetPropertyStr(ctx, repl_obj, "listen",
                      JS_NewCFunction(ctx, ngx_js_repl_listen,
                                      "listen", 2));

    JS_SetPropertyStr(ctx, repl_obj, "listenRaw",
                      JS_NewCFunction(ctx, ngx_js_repl_listen_raw,
                                      "listenRaw", 2));

    JS_SetPropertyStr(ctx, nginx_obj, "repl", repl_obj);

    return NGX_OK;
}
