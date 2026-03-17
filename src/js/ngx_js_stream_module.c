
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage E — Stream JS session handlers.
 *
 * Implements:
 *   ngx_js_stream_module   — NGX_STREAM_MODULE, provides per-server
 *                            handler_idx config and stream content handler.
 *   NginxStreamSession     — JS class wrapping ngx_stream_session_t.
 *
 * JS API (set during init_conf via nginx.stream.servers[i].handler):
 *
 *   nginx.stream.servers[0].handler = function(session) {
 *       nginx.log(6, session.remoteAddress + ":" + session.remotePort);
 *       session.finalize(200);   // close connection
 *   };
 *
 * NginxStreamSession properties (all read-only):
 *   remoteAddress  string   client IP address
 *   remotePort     number   client port
 *   localAddress   string   server-side IP address
 *   localPort      number   server-side port
 *   received       number   bytes received from client
 *   status         number   current session status code
 *   ssl            bool     whether connection is TLS
 *
 * NginxStreamSession methods:
 *   finalize([code])         close the session (default: NGX_STREAM_OK=200)
 *   variable(name)           look up a stream variable by name (or null)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "ngx_js_stream_module.h"


/* ================================================================== */
/* NginxStreamSession class                                            */
/* ================================================================== */

/*
 * The session object holds back-pointers to stack variables in
 * ngx_js_stream_content_handler() so that session.finalize() can
 * communicate the desired status code without needing to call
 * ngx_stream_finalize_session() directly (which would free the session
 * before we can check the result).
 */
typedef struct {
    ngx_stream_session_t  *session;
    ngx_int_t             *pending_code_p;   /* ptr into handler's stack */
    ngx_uint_t            *did_finalize_p;   /* ptr into handler's stack */
} ngx_js_stream_session_opaque_t;


static void
ngx_js_stream_session_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_session_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_session_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_session_class = {
    "NginxStreamSession",
    .finalizer = ngx_js_stream_session_finalizer,
};


/*
 * Getter magic:
 *   0 remoteAddress   1 remotePort   2 localAddress  3 localPort
 *   4 received        5 status       6 ssl
 */
static JSValue
ngx_js_stream_session_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_session_opaque_t  *op;
    ngx_stream_session_t            *s;
    ngx_connection_t                *c;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_session_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    s = op->session;
    if (s == NULL) {
        return JS_ThrowRangeError(ctx, "stream session already finalized");
    }

    c = s->connection;

    switch (magic) {
    case 0:   /* remoteAddress */
        return JS_NewStringLen(ctx, (const char *) c->addr_text.data,
                               c->addr_text.len);
    case 1:   /* remotePort */
        return JS_NewInt32(ctx, (int) ngx_inet_get_port(c->sockaddr));
    case 2:   /* localAddress */
    {
        u_char  buf[NGX_SOCKADDR_STRLEN];
        size_t  len;

        len = ngx_sock_ntop(c->local_sockaddr, c->local_socklen,
                            buf, NGX_SOCKADDR_STRLEN, 0);
        return JS_NewStringLen(ctx, (const char *) buf, len);
    }
    case 3:   /* localPort */
        return JS_NewInt32(ctx, (int) ngx_inet_get_port(c->local_sockaddr));
    case 4:   /* received */
        return JS_NewInt64(ctx, (int64_t) s->received);
    case 5:   /* status */
        return JS_NewInt32(ctx, (int) s->status);
    case 6:   /* ssl */
        return JS_NewBool(ctx, (int) s->ssl);
    }

    return JS_UNDEFINED;
}


/*
 * session.finalize([code])
 *
 * Records the desired exit code in the handler's stack variable and marks
 * the session as finalised.  The actual ngx_stream_finalize_session() call
 * happens in ngx_js_stream_content_handler() after JS_Call() returns so
 * that nginx's memory is not freed while JS is still running.
 *
 * If called more than once the second call is silently ignored.
 */
static JSValue
ngx_js_stream_session_finalize(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_stream_session_opaque_t  *op;
    int64_t                          code;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_session_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (op->did_finalize_p == NULL || *op->did_finalize_p) {
        /* Already finalized or session detached — ignore silently. */
        return JS_UNDEFINED;
    }

    code = NGX_STREAM_OK;
    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &code, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
    }

    *op->pending_code_p = (ngx_int_t) code;
    *op->did_finalize_p = 1;

    /* Detach so a second call can be detected. */
    op->pending_code_p = NULL;
    op->did_finalize_p = NULL;
    op->session        = NULL;

    return JS_UNDEFINED;
}


/*
 * session.variable(name) → string | null
 *
 * Looks up a stream variable by lower-case name.  Returns null when the
 * variable is not defined or its value is not found.
 */
static JSValue
ngx_js_stream_session_variable(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_stream_session_opaque_t  *op;
    ngx_stream_session_t            *s;
    const char                      *cstr;
    size_t                           clen;
    ngx_str_t                        name;
    ngx_stream_variable_value_t     *vv;
    ngx_uint_t                       key;
    u_char                          *lc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_session_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    s = op->session;
    if (s == NULL) {
        return JS_ThrowRangeError(ctx, "stream session already finalized");
    }

    if (argc < 1 || JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "variable(name): name argument required");
    }

    cstr = JS_ToCStringLen(ctx, &clen, argv[0]);
    if (!cstr) {
        return JS_EXCEPTION;
    }

    /* nginx variable lookup requires a lower-case key */
    lc = ngx_palloc(s->connection->pool, clen);
    if (lc == NULL) {
        JS_FreeCString(ctx, cstr);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_strlow(lc, (u_char *) cstr, clen);
    JS_FreeCString(ctx, cstr);

    name.data = lc;
    name.len  = clen;
    key       = ngx_hash_key_lc(name.data, name.len);

    vv = ngx_stream_get_variable(s, &name, key);

    if (vv == NULL || vv->not_found) {
        return JS_NULL;
    }

    return JS_NewStringLen(ctx, (const char *) vv->data, vv->len);
}


static const JSCFunctionListEntry  ngx_js_stream_session_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("remoteAddress",  ngx_js_stream_session_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("remotePort",     ngx_js_stream_session_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("localAddress",   ngx_js_stream_session_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("localPort",      ngx_js_stream_session_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("received",       ngx_js_stream_session_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("status",         ngx_js_stream_session_get, NULL, 5),
    JS_CGETSET_MAGIC_DEF("ssl",            ngx_js_stream_session_get, NULL, 6),
    JS_CFUNC_DEF        ("finalize",   0,  ngx_js_stream_session_finalize),
    JS_CFUNC_DEF        ("variable",   1,  ngx_js_stream_session_variable),
};


ngx_int_t
ngx_js_stream_session_register_class(JSRuntime *rt)
{
    if (JS_NewClass(rt, ngx_js_stream_session_class_id,
                    &ngx_js_stream_session_class) < 0)
    {
        return NGX_ERROR;
    }
    return NGX_OK;
}


ngx_int_t
ngx_js_stream_session_install_proto(JSContext *ctx)
{
    JSValue  proto, global;

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, ngx_js_stream_session_proto_funcs,
                               countof(ngx_js_stream_session_proto_funcs));
    JS_SetClassProto(ctx, ngx_js_stream_session_class_id, proto);

    /* Expose NginxStreamSession constructor name on global for instanceof */
    global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "NginxStreamSession",
                      JS_NewObjectProtoClass(ctx, proto,
                                            ngx_js_stream_session_class_id));
    JS_FreeValue(ctx, global);

    return NGX_OK;
}


JSValue
ngx_js_wrap_stream_session(JSContext *ctx, ngx_stream_session_t *s,
    ngx_int_t *pending_code_p, ngx_uint_t *did_finalize_p)
{
    ngx_js_stream_session_opaque_t  *op;
    JSValue                          obj;

    op = js_mallocz(ctx, sizeof(*op));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->session         = s;
    op->pending_code_p  = pending_code_p;
    op->did_finalize_p  = did_finalize_p;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_session_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ================================================================== */
/* Stream content handler                                              */
/* ================================================================== */

void
ngx_js_stream_content_handler(ngx_stream_session_t *s)
{
    ngx_js_conf_t             *jcf;
    ngx_js_stream_srv_conf_t  *jscf;
    ngx_js_worker_t           *w;
    JSContext                 *ctx;
    JSValue                    global, registry, fn, sess_obj, result;
    ngx_int_t                  pending_code;
    ngx_uint_t                 did_finalize;

    jcf  = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    jscf = ngx_stream_get_module_srv_conf(s, ngx_js_stream_module);

    w = jcf->worker;

    if (w == NULL || w->ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js: worker runtime not available");
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    if (jscf->handler_idx < 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js: no stream session handler configured");
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    ctx = w->ctx;

    global   = JS_GetGlobalObject(ctx);
    registry = JS_GetPropertyStr(ctx, global, "__ngx_handlers__");
    JS_FreeValue(ctx, global);

    fn = JS_GetPropertyUint32(ctx, registry, (uint32_t) jscf->handler_idx);
    JS_FreeValue(ctx, registry);

    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "js: stream handler #%i not found or not callable",
                      jscf->handler_idx);
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    /*
     * Stack variables used by session.finalize() via back-pointers in the
     * opaque.  The session object writes here; we read them after JS_Call().
     */
    pending_code  = NGX_STREAM_OK;
    did_finalize  = 0;

    sess_obj = ngx_js_wrap_stream_session(ctx, s, &pending_code, &did_finalize);
    if (JS_IsException(sess_obj)) {
        JS_FreeValue(ctx, fn);
        ngx_js_log_exception(ctx, s->connection->log);
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    result = JS_Call(ctx, fn, JS_UNDEFINED, 1, &sess_obj);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, sess_obj);   /* opaque freed; stack vars already set */

    if (JS_IsException(result)) {
        ngx_js_log_exception(ctx, s->connection->log);
        JS_FreeValue(ctx, result);
        ngx_stream_finalize_session(s, NGX_STREAM_INTERNAL_SERVER_ERROR);
        return;
    }

    JS_FreeValue(ctx, result);

    /*
     * If the handler never called session.finalize(), close with OK.
     */
    if (!did_finalize) {
        pending_code = NGX_STREAM_OK;
    }

    ngx_stream_finalize_session(s, (ngx_uint_t) pending_code);
}


/* ================================================================== */
/* ngx_js_stream_module — NGX_STREAM_MODULE lifecycle                  */
/* ================================================================== */

static void *
ngx_js_stream_create_srv_conf(ngx_conf_t *cf)
{
    ngx_js_stream_srv_conf_t  *jscf;

    jscf = ngx_pcalloc(cf->pool, sizeof(ngx_js_stream_srv_conf_t));
    if (jscf == NULL) {
        return NULL;
    }

    jscf->handler_idx = -1;

    return jscf;
}


static char *
ngx_js_stream_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_js_stream_srv_conf_t  *prev = parent;
    ngx_js_stream_srv_conf_t  *conf = child;

    if (conf->handler_idx == -1) {
        conf->handler_idx = prev->handler_idx;
    }

    return NGX_CONF_OK;
}


static ngx_stream_module_t  ngx_js_stream_module_ctx = {
    NULL,                                   /* preconfiguration  */
    NULL,                                   /* postconfiguration */

    NULL,                                   /* create_main_conf  */
    NULL,                                   /* init_main_conf    */

    ngx_js_stream_create_srv_conf,          /* create_srv_conf   */
    ngx_js_stream_merge_srv_conf,           /* merge_srv_conf    */
};


ngx_module_t  ngx_js_stream_module = {
    NGX_MODULE_V1,
    &ngx_js_stream_module_ctx,              /* module context    */
    NULL,                                   /* module directives */
    NGX_STREAM_MODULE,                      /* module type       */
    NULL,                                   /* init master       */
    NULL,                                   /* init module       */
    NULL,                                   /* init process      */
    NULL,                                   /* init thread       */
    NULL,                                   /* exit thread       */
    NULL,                                   /* exit process      */
    NULL,                                   /* exit master       */
    NGX_MODULE_V1_PADDING
};
