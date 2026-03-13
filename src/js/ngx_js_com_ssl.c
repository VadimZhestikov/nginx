
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 6: SSL server configuration.
 *
 * Exposes server.ssl as a NginxSSL object (or null for non-SSL servers):
 *
 *   protocols            string[]   active TLS versions
 *   ciphers              string     OpenSSL cipher string
 *   certificate          string[]   certificate file paths
 *   certificateKey       string[]   certificate key file paths
 *   sessionTimeout       number     session timeout (seconds)
 *   sessionTickets       bool       TLS session tickets enabled
 *   preferServerCiphers  bool       ssl_prefer_server_ciphers
 *   verify               string     client verify: "off"|"on"|"optional"|"optional_no_ca"
 *   verifyDepth          number     client certificate chain depth
 *   clientCertificate    string     client CA cert path (or "")
 *   trustedCertificate   string     trusted CA cert path (or "")
 *   ecdhCurve            string     ECDH curve name
 *   dhparam              string     DH params file path (or "")
 *
 * The entire file is wrapped in #if (NGX_HTTP_SSL) so that it compiles
 * safely when nginx is built without --with-http_ssl_module.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


#if (NGX_HTTP_SSL)

#include <ngx_http_ssl_module.h>


typedef struct {
    ngx_http_ssl_srv_conf_t  *sscf;
} ngx_js_ssl_opaque_t;


static void
ngx_js_ssl_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_ssl_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_ssl_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_ssl_class = {
    "NginxSSL",
    .finalizer = ngx_js_ssl_finalizer
};


/*
 * Magic values for ngx_js_ssl_get:
 *   0 — sessionTimeout
 *   1 — sessionTickets
 *   2 — preferServerCiphers
 *   3 — verifyDepth
 *   4 — ciphers
 *   5 — clientCertificate
 *   6 — trustedCertificate
 *   7 — ecdhCurve
 *   8 — dhparam
 */
static JSValue
ngx_js_ssl_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    sscf = op->sscf;

    switch (magic) {
    case 0: return JS_NewInt64(ctx,  (int64_t) sscf->session_timeout);
    case 1: return JS_NewBool(ctx,   (int) sscf->session_tickets);
    case 2: return JS_NewBool(ctx,   (int) sscf->prefer_server_ciphers);
    case 3: return JS_NewInt64(ctx,  (int64_t) sscf->verify_depth);
    case 4: return JS_NewStringLen(ctx, (const char *) sscf->ciphers.data,
                                   sscf->ciphers.len);
    case 5: return JS_NewStringLen(ctx,
                                   (const char *) sscf->client_certificate.data,
                                   sscf->client_certificate.len);
    case 6: return JS_NewStringLen(ctx,
                                   (const char *) sscf->trusted_certificate.data,
                                   sscf->trusted_certificate.len);
    case 7: return JS_NewStringLen(ctx, (const char *) sscf->ecdh_curve.data,
                                   sscf->ecdh_curve.len);
    case 8: return JS_NewStringLen(ctx, (const char *) sscf->dhparam.data,
                                   sscf->dhparam.len);
    }

    return JS_UNDEFINED;
}


/*
 * ssl.protocols — array of TLS version name strings.
 * Built from the protocols bitmask.
 */
static JSValue
ngx_js_ssl_get_protocols(JSContext *ctx, JSValueConst this_val)
{
    static const struct {
        ngx_uint_t   flag;
        const char  *name;
    } protos[] = {
        { NGX_SSL_SSLv2,   "SSLv2"   },
        { NGX_SSL_SSLv3,   "SSLv3"   },
        { NGX_SSL_TLSv1,   "TLSv1"   },
        { NGX_SSL_TLSv1_1, "TLSv1.1" },
        { NGX_SSL_TLSv1_2, "TLSv1.2" },
        { NGX_SSL_TLSv1_3, "TLSv1.3" },
        { 0, NULL }
    };

    ngx_js_ssl_opaque_t  *op;
    JSValue               arr;
    ngx_uint_t            mask, i;
    uint32_t              idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr  = JS_NewArray(ctx);
    mask = op->sscf->protocols;
    idx  = 0;

    for (i = 0; protos[i].name != NULL; i++) {
        if (mask & protos[i].flag) {
            JS_SetPropertyUint32(ctx, arr, idx++,
                                 JS_NewString(ctx, protos[i].name));
        }
    }

    return arr;
}


/*
 * ssl.verify — client verification mode as a string.
 *   0 = "off", 1 = "on", 2 = "optional", 3 = "optional_no_ca"
 */
static JSValue
ngx_js_ssl_get_verify(JSContext *ctx, JSValueConst this_val)
{
    static const char *modes[] = { "off", "on", "optional", "optional_no_ca" };

    ngx_js_ssl_opaque_t  *op;
    ngx_uint_t            v;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    v = op->sscf->verify;
    if (v > 3) { v = 0; }

    return JS_NewString(ctx, modes[v]);
}


/*
 * ssl.certificate — array of certificate file paths (strings).
 * Maps to the ssl_certificate directive (can be multi-valued).
 */
static JSValue
ngx_js_ssl_get_certificate(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_ssl_opaque_t  *op;
    JSValue               arr;
    ngx_str_t            *cert;
    ngx_uint_t            i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->sscf->certificates == NULL) {
        return arr;
    }

    cert = op->sscf->certificates->elts;

    for (i = 0; i < op->sscf->certificates->nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             JS_NewStringLen(ctx,
                                             (const char *) cert[i].data,
                                             cert[i].len));
    }

    return arr;
}


/*
 * ssl.certificateKey — array of certificate key file paths.
 */
static JSValue
ngx_js_ssl_get_certificate_key(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_ssl_opaque_t  *op;
    JSValue               arr;
    ngx_str_t            *key;
    ngx_uint_t            i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->sscf->certificate_keys == NULL) {
        return arr;
    }

    key = op->sscf->certificate_keys->elts;

    for (i = 0; i < op->sscf->certificate_keys->nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             JS_NewStringLen(ctx,
                                             (const char *) key[i].data,
                                             key[i].len));
    }

    return arr;
}


/*
 * setProtocols(arr) — update the enabled TLS protocol versions on the live
 * SSL_CTX.
 *
 * arr must be an array of strings, each one of: "SSLv2", "SSLv3", "TLSv1",
 * "TLSv1.1", "TLSv1.2", "TLSv1.3".  Unknown strings are silently ignored.
 * Throws TypeError if the argument is not an array.
 *
 * Mirrors the logic in ngx_ssl_create(): clears all SSL_OP_NO_* version bits,
 * then sets SSL_OP_NO_<proto> for each version absent from the new list.
 * Also updates sscf->protocols so the COM protocols getter stays consistent.
 */
static JSValue
ngx_js_ssl_set_protocols(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    static const struct {
        const char  *name;
        ngx_uint_t   flag;
    } protos[] = {
        { "SSLv2",   NGX_SSL_SSLv2   },
        { "SSLv3",   NGX_SSL_SSLv3   },
        { "TLSv1",   NGX_SSL_TLSv1   },
        { "TLSv1.1", NGX_SSL_TLSv1_1 },
        { "TLSv1.2", NGX_SSL_TLSv1_2 },
        { "TLSv1.3", NGX_SSL_TLSv1_3 },
        { NULL, 0 }
    };

    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;
    JSValue                   arr, elem;
    uint32_t                  len, i;
    ngx_uint_t                mask, j;
    const char               *s;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) { return JS_EXCEPTION; }

    sscf = op->sscf;

    if (argc < 1 || !JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx,
                                 "setProtocols: array argument required");
    }

    if (sscf->ssl.ctx == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "setProtocols: SSL context not initialised");
    }

    arr = argv[0];

    {
        JSValue lv = JS_GetPropertyStr(ctx, arr, "length");
        if (JS_ToUint32(ctx, &len, lv) < 0) {
            JS_FreeValue(ctx, lv);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, lv);
    }

    /* Build the protocol bitmask from the JS array */
    mask = 0;

    for (i = 0; i < len; i++) {
        elem = JS_GetPropertyUint32(ctx, arr, i);
        if (JS_IsException(elem)) { return JS_EXCEPTION; }

        s = JS_ToCString(ctx, elem);
        JS_FreeValue(ctx, elem);
        if (!s) { return JS_EXCEPTION; }

        for (j = 0; protos[j].name != NULL; j++) {
            if (ngx_strcmp(s, protos[j].name) == 0) {
                mask |= protos[j].flag;
                break;
            }
        }

        JS_FreeCString(ctx, s);
    }

    /*
     * Apply to the live SSL_CTX.  Mirror ngx_ssl_create() exactly:
     * clear all NO_* version bits then set NO_<proto> for each absent version.
     */
    SSL_CTX_clear_options(sscf->ssl.ctx,
                          SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);

    if (!(mask & NGX_SSL_SSLv2)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_SSLv2);
    }
    if (!(mask & NGX_SSL_SSLv3)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_SSLv3);
    }
    if (!(mask & NGX_SSL_TLSv1)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1);
    }

#ifdef SSL_OP_NO_TLSv1_1
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_1);
    if (!(mask & NGX_SSL_TLSv1_1)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_1);
    }
#endif

#ifdef SSL_OP_NO_TLSv1_2
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_2);
    if (!(mask & NGX_SSL_TLSv1_2)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_2);
    }
#endif

#ifdef SSL_OP_NO_TLSv1_3
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_3);
    if (!(mask & NGX_SSL_TLSv1_3)) {
        SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_3);
    }
#endif

    sscf->protocols = mask;

    return JS_UNDEFINED;
}


/*
 * setCiphers(str) — update the OpenSSL cipher list on the live SSL_CTX.
 *
 * Calls SSL_CTX_set_cipher_list() on sscf->ssl.ctx; takes effect immediately
 * for all new TLS handshakes on this server (existing sessions are unaffected).
 * Also updates sscf->ciphers so the COM getter reflects the new value.
 *
 * Throws an Error if str contains no valid ciphers (OpenSSL returns 0).
 */
static JSValue
ngx_js_ssl_set_ciphers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;
    const char               *s;
    size_t                    len;
    u_char                   *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) { return JS_EXCEPTION; }

    sscf = op->sscf;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "setCiphers: string argument required");
    }

    s = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!s) { return JS_EXCEPTION; }

    if (sscf->ssl.ctx == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowInternalError(ctx,
                                     "setCiphers: SSL context not initialised");
    }

    /* Apply to the live SSL_CTX — new handshakes will use the updated list */
    if (SSL_CTX_set_cipher_list(sscf->ssl.ctx, s) == 0) {
        JS_FreeCString(ctx, s);
        return JS_ThrowInternalError(ctx,
                                     "setCiphers: no valid ciphers in list");
    }

    /* Update the COM-visible conf string for the ciphers getter */
    p = ngx_pnalloc(ngx_cycle->pool, len);
    if (p == NULL) {
        JS_FreeCString(ctx, s);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(p, s, len);
    JS_FreeCString(ctx, s);

    sscf->ciphers.data = p;
    sscf->ciphers.len  = len;

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_ssl_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sessionTimeout",      ngx_js_ssl_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("sessionTickets",       ngx_js_ssl_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("preferServerCiphers",  ngx_js_ssl_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("verifyDepth",          ngx_js_ssl_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("ciphers",              ngx_js_ssl_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("clientCertificate",    ngx_js_ssl_get, NULL, 5),
    JS_CGETSET_MAGIC_DEF("trustedCertificate",   ngx_js_ssl_get, NULL, 6),
    JS_CGETSET_MAGIC_DEF("ecdhCurve",            ngx_js_ssl_get, NULL, 7),
    JS_CGETSET_MAGIC_DEF("dhparam",              ngx_js_ssl_get, NULL, 8),
    JS_CGETSET_DEF       ("protocols",           ngx_js_ssl_get_protocols,      NULL),
    JS_CGETSET_DEF       ("verify",              ngx_js_ssl_get_verify,         NULL),
    JS_CGETSET_DEF       ("certificate",         ngx_js_ssl_get_certificate,    NULL),
    JS_CGETSET_DEF       ("certificateKey",      ngx_js_ssl_get_certificate_key,NULL),
    JS_CFUNC_DEF         ("setCiphers",     1,   ngx_js_ssl_set_ciphers),
    JS_CFUNC_DEF         ("setProtocols",  1,   ngx_js_ssl_set_protocols),
};


ngx_int_t
ngx_js_ssl_install_proto(JSContext *ctx)
{
#if (NGX_HTTP_SSL)
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_ssl_proto_funcs,
                               countof(ngx_js_ssl_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_ssl_class_id, proto);
#endif
    return NGX_OK;
}


JSValue
ngx_js_wrap_ssl(JSContext *ctx, ngx_http_ssl_srv_conf_t *sscf)
{
    JSValue              obj;
    ngx_js_ssl_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_ssl_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->sscf = sscf;

    obj = JS_NewObjectClass(ctx, ngx_js_ssl_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}

#endif /* NGX_HTTP_SSL */


ngx_int_t
ngx_js_ssl_register_class(JSRuntime *rt)
{
#if (NGX_HTTP_SSL)
    return JS_NewClass(rt, ngx_js_ssl_class_id, &ngx_js_ssl_class) < 0
           ? NGX_ERROR : NGX_OK;
#else
    return NGX_OK;
#endif
}
