
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP + Stream COM layer — Stage 6 / Stage D: SSL server configuration.
 *
 * Exposes server.ssl as a NginxSSL object (or null for non-SSL servers):
 *
 * NginxSSL (HTTP):
 *   protocols            string[]   active TLS versions                 r/w*
 *   ciphers              string     OpenSSL cipher string                r/w*
 *   certificate          string[]   certificate file paths               r/w*
 *   certificateKey       string[]   certificate key file paths           r/w*
 *   sessionTimeout       number     session timeout (seconds)            r/w
 *   sessionTickets       bool       TLS session tickets enabled          r/w
 *   preferServerCiphers  bool       ssl_prefer_server_ciphers            r/w
 *   verify               string     client verify mode                   r/o
 *   verifyDepth          number     client certificate chain depth       r/w
 *   clientCertificate    string     client CA cert path (or "")          r/o
 *   trustedCertificate   string     trusted CA cert path (or "")         r/o
 *   ecdhCurve            string     ECDH curve name                      r/o
 *   dhparam              string     DH params file path (or "")          r/o
 *   setProtocols(arr)    method     update enabled TLS version set
 *   setCiphers(str)      method     update cipher list on live SSL_CTX
 *   setCertificate(c,k)  method     hot-swap certificate + private key
 *   (* r/w via dedicated setter method above)
 *
 * NginxStreamSSL (Stream):  same API + handshakeTimeout (r/w)
 *
 * The HTTP section is wrapped in #if (NGX_HTTP_SSL); stream in
 * #if (NGX_STREAM_SSL), so the file compiles safely when either
 * module is absent.
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
 * Setters for simple scalar properties that also need an SSL_CTX call.
 * Magic matches ngx_js_ssl_get:
 *   0 — sessionTimeout      SSL_CTX_set_timeout()
 *   1 — sessionTickets       SSL_OP_NO_TICKET
 *   2 — preferServerCiphers  SSL_OP_CIPHER_SERVER_PREFERENCE
 *   3 — verifyDepth          SSL_CTX_set_verify_depth()
 */
static JSValue
ngx_js_ssl_set(JSContext *ctx, JSValueConst this_val, JSValue val, int magic)
{
    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;
    int64_t                   n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    sscf = op->sscf;

    switch (magic) {
    case 0: /* sessionTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        sscf->session_timeout = (time_t) n;
        if (sscf->ssl.ctx != NULL) {
            SSL_CTX_set_timeout(sscf->ssl.ctx, (long) n);
        }
        return JS_UNDEFINED;

    case 1: /* sessionTickets */
        n = JS_ToBool(ctx, val);
        if (n < 0) { return JS_EXCEPTION; }
        sscf->session_tickets = (ngx_flag_t) n;
        if (sscf->ssl.ctx != NULL) {
            if (n) {
                SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TICKET);
            } else {
                SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TICKET);
            }
        }
        return JS_UNDEFINED;

    case 2: /* preferServerCiphers */
        n = JS_ToBool(ctx, val);
        if (n < 0) { return JS_EXCEPTION; }
        sscf->prefer_server_ciphers = (ngx_flag_t) n;
        if (sscf->ssl.ctx != NULL) {
            if (n) {
                SSL_CTX_set_options(sscf->ssl.ctx,
                                    SSL_OP_CIPHER_SERVER_PREFERENCE);
            } else {
                SSL_CTX_clear_options(sscf->ssl.ctx,
                                      SSL_OP_CIPHER_SERVER_PREFERENCE);
            }
        }
        return JS_UNDEFINED;

    case 3: /* verifyDepth */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        sscf->verify_depth = (ngx_uint_t) n;
        if (sscf->ssl.ctx != NULL) {
            SSL_CTX_set_verify_depth(sscf->ssl.ctx, (int) n);
        }
        return JS_UNDEFINED;
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
 * setCertificate(certPath, keyPath) — swap the TLS certificate and private
 * key on the live SSL_CTX.
 *
 * Calls SSL_CTX_use_certificate_chain_file() and SSL_CTX_use_PrivateKey_file()
 * (PEM format), then SSL_CTX_check_private_key() to verify they match.
 * Also replaces sscf->certificates and sscf->certificate_keys with single-
 * element arrays so the COM certificate/certificateKey getters reflect the
 * new paths.
 *
 * Takes effect for all new TLS handshakes; existing sessions are unaffected.
 * Throws an Error on any failure (file not found, bad format, key mismatch).
 */
static JSValue
ngx_js_ssl_set_certificate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;
    const char               *cert_s, *key_s;
    size_t                    cert_len, key_len;
    u_char                   *cp, *kp;
    ngx_array_t              *certs, *keys;
    ngx_str_t                *sp;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) { return JS_EXCEPTION; }

    sscf = op->sscf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "setCertificate: certPath and keyPath required");
    }

    cert_s = JS_ToCStringLen(ctx, &cert_len, argv[0]);
    if (!cert_s) { return JS_EXCEPTION; }

    key_s = JS_ToCStringLen(ctx, &key_len, argv[1]);
    if (!key_s) {
        JS_FreeCString(ctx, cert_s);
        return JS_EXCEPTION;
    }

    if (sscf->ssl.ctx == NULL) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: SSL context not initialised");
    }

    /* Load certificate chain (replaces primary cert + chain) */
    if (SSL_CTX_use_certificate_chain_file(sscf->ssl.ctx, cert_s) != 1) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: failed to load certificate");
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(sscf->ssl.ctx, key_s,
                                    SSL_FILETYPE_PEM) != 1)
    {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: failed to load private key");
    }

    /* Verify key matches certificate */
    if (SSL_CTX_check_private_key(sscf->ssl.ctx) != 1) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: private key does not match"
                                     " certificate");
    }

    /* Update COM-visible path arrays (replace with single-element arrays) */
    cp = ngx_pnalloc(ngx_js_conf_cycle()->pool, cert_len + 1);
    kp = ngx_pnalloc(ngx_js_conf_cycle()->pool, key_len + 1);

    if (cp == NULL || kp == NULL) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(cp, cert_s, cert_len);  cp[cert_len] = '\0';
    ngx_memcpy(kp, key_s,  key_len);   kp[key_len]  = '\0';

    JS_FreeCString(ctx, cert_s);
    JS_FreeCString(ctx, key_s);

    certs = ngx_array_create(ngx_js_conf_cycle()->pool, 1, sizeof(ngx_str_t));
    keys  = ngx_array_create(ngx_js_conf_cycle()->pool, 1, sizeof(ngx_str_t));

    if (certs == NULL || keys == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    sp = ngx_array_push(certs);
    if (sp == NULL) { return JS_ThrowOutOfMemory(ctx); }
    sp->data = cp;
    sp->len  = cert_len;

    sp = ngx_array_push(keys);
    if (sp == NULL) { return JS_ThrowOutOfMemory(ctx); }
    sp->data = kp;
    sp->len  = key_len;

    sscf->certificates    = certs;
    sscf->certificate_keys = keys;

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
    p = ngx_pnalloc(ngx_js_conf_cycle()->pool, len);
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


/* ------------------------------------------------------------------ */
/* onClientHello — fire JS at TLS ClientHello time (iRules             */
/* CLIENTSSL_CLIENTHELLO). Exposes the JA3 inputs (version, ciphers,   */
/* extensions, groups, EC point formats) plus SNI and ALPN, and the    */
/* per-connection ctx (flow-local). A hook returning boolean false     */
/* aborts the handshake.                                               */
/* ------------------------------------------------------------------ */

#if (defined SSL_client_hello_cb_fn || OPENSSL_VERSION_NUMBER >= 0x10101000L)
#define NGX_JS_HAVE_CLIENT_HELLO_CB  1
#endif

#if (NGX_JS_HAVE_CLIENT_HELLO_CB)

/* Per-SSL_CTX hook record. The callback array itself is NOT stored here —
 * it lives in the global __ngx_ch_hooks__ array (a GC root, like the accept /
 * L4-filter registries) so it survives garbage collection; this record only
 * remembers the slot index into that global array. (Storing the JSValue array
 * only in C memory let the GC collect it — the cause of the earlier crash.) */
typedef struct {
    uint32_t  slot;
} ngx_js_ch_hooks_t;

static int  ngx_js_ch_ctx_idx = -1;


/* Global registry array of per-CTX callback arrays (rooted on globalThis). */
static JSValue
ngx_js_ch_registry(JSContext *ctx)
{
    JSValue  global, reg;

    global = JS_GetGlobalObject(ctx);
    reg    = JS_GetPropertyStr(ctx, global, "__ngx_ch_hooks__");

    if (JS_IsUndefined(reg)) {
        JS_FreeValue(ctx, reg);
        reg = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, global, "__ngx_ch_hooks__",
                          JS_DupValue(ctx, reg));
    }

    JS_FreeValue(ctx, global);
    return reg;   /* caller frees this ref */
}


static void
ngx_js_ch_ext_ints(JSContext *ctx, JSValue ch, const char *name, SSL *s,
    int ext_type, int stride, int skip_list_len)
{
    const unsigned char  *p, *q, *end;
    size_t                len;
    JSValue               arr;
    uint32_t              j;

    if (SSL_client_hello_get0_ext(s, ext_type, &p, &len) != 1) {
        return;
    }

    q   = p + (skip_list_len ? 2 : 0);
    end = p + len;
    arr = JS_NewArray(ctx);
    j   = 0;

    while (q + stride <= end) {
        JS_SetPropertyUint32(ctx, arr, j++,
            JS_NewInt32(ctx, stride == 2 ? ((q[0] << 8) | q[1]) : q[0]));
        q += stride;
    }

    JS_SetPropertyStr(ctx, ch, name, arr);
}


static JSValue
ngx_js_build_client_hello(JSContext *ctx, SSL *s)
{
    JSValue               ch, arr;
    const unsigned char  *cs, *p, *q, *end;
    size_t                n, k, len;
    int                  *exts;
    size_t                nexts;
    uint32_t              j;

    ch = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, ch, "version",
                      JS_NewInt32(ctx, SSL_client_hello_get0_legacy_version(s)));

    /* cipher suites (2-byte ids) */
    n   = SSL_client_hello_get0_ciphers(s, &cs);
    arr = JS_NewArray(ctx);
    j   = 0;
    for (k = 0; k + 1 < n; k += 2) {
        JS_SetPropertyUint32(ctx, arr, j++,
                             JS_NewInt32(ctx, (cs[k] << 8) | cs[k + 1]));
    }
    JS_SetPropertyStr(ctx, ch, "cipherSuites", arr);

    /* extensions present (already a flat int list) */
    if (SSL_client_hello_get1_extensions_present(s, &exts, &nexts) == 1) {
        arr = JS_NewArray(ctx);
        for (k = 0; k < nexts; k++) {
            JS_SetPropertyUint32(ctx, arr, (uint32_t) k,
                                 JS_NewInt32(ctx, exts[k]));
        }
        JS_SetPropertyStr(ctx, ch, "extensions", arr);
        OPENSSL_free(exts);
    }

    /* SNI (server_name list): 2-byte list len, then type(1)+len(2)+name */
    if (SSL_client_hello_get0_ext(s, TLSEXT_TYPE_server_name, &p, &len) == 1
        && len > 4)
    {
        q = p + 2;
        if (q[0] == 0 /* host_name */) {
            unsigned nlen = (q[1] << 8) | q[2];
            if ((size_t) (nlen + 5) <= len) {
                JS_SetPropertyStr(ctx, ch, "sni",
                    JS_NewStringLen(ctx, (const char *) (q + 3), nlen));
            }
        }
    }

    /* ALPN: 2-byte list len, then [len(1)+proto]* */
    if (SSL_client_hello_get0_ext(s,
            TLSEXT_TYPE_application_layer_protocol_negotiation, &p, &len) == 1
        && len > 2)
    {
        q   = p + 2;
        end = p + len;
        arr = JS_NewArray(ctx);
        j   = 0;
        while (q < end) {
            unsigned pl = q[0];
            if (q + 1 + pl > end) {
                break;
            }
            JS_SetPropertyUint32(ctx, arr, j++,
                JS_NewStringLen(ctx, (const char *) (q + 1), pl));
            q += 1 + pl;
        }
        JS_SetPropertyStr(ctx, ch, "alpn", arr);
    }

    /* supported_groups / curves: 2-byte list len, then 2-byte ids */
    ngx_js_ch_ext_ints(ctx, ch, "supportedGroups", s,
                       TLSEXT_TYPE_supported_groups, 2, 1);

    /* ec_point_formats: 1-byte list len, then 1-byte formats */
    if (SSL_client_hello_get0_ext(s, TLSEXT_TYPE_ec_point_formats, &p, &len) == 1
        && len >= 1)
    {
        q   = p + 1;
        end = p + len;
        arr = JS_NewArray(ctx);
        j   = 0;
        while (q < end) {
            JS_SetPropertyUint32(ctx, arr, j++, JS_NewInt32(ctx, q[0]));
            q++;
        }
        JS_SetPropertyStr(ctx, ch, "ecPointFormats", arr);
    }

    return ch;
}


static int
ngx_js_ch_cb(SSL *s, int *al, void *arg)
{
    ngx_js_ch_hooks_t  *hooks = arg;
    ngx_connection_t   *c;
    ngx_js_conf_t      *jcf;
    JSContext          *ctx;
    JSRuntime          *rt;
    JSValue             ch, args[2], fn, ret, lenv, reg, fns;
    int64_t             len, i;
    int                 rejected;

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return SSL_CLIENT_HELLO_SUCCESS;
    }

    ctx = jcf->ctx;
    rt  = JS_GetRuntime(ctx);
    c   = ngx_ssl_get_connection(s);

    reg = ngx_js_ch_registry(ctx);
    fns = JS_GetPropertyUint32(ctx, reg, hooks->slot);
    JS_FreeValue(ctx, reg);

    ch       = ngx_js_build_client_hello(ctx, s);
    args[0]  = ch;
    args[1]  = (c != NULL) ? ngx_js_connection_ctx_obj(ctx, c) : JS_UNDEFINED;
    rejected = 0;

    len  = 0;
    lenv = JS_GetPropertyStr(ctx, fns, "length");
    JS_ToInt64(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);

    for (i = 0; i < len; i++) {
        fn  = JS_GetPropertyUint32(ctx, fns, (uint32_t) i);
        ret = JS_Call(ctx, fn, JS_UNDEFINED, 2, (JSValueConst *) args);
        if (JS_IsException(ret)) {
            ngx_js_log_exception(ctx, c ? c->log : ngx_cycle->log);
        } else if (JS_IsBool(ret) && !JS_ToBool(ctx, ret)) {
            rejected = 1;   /* explicit `return false` aborts the handshake */
        }
        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, fn);
        while (JS_ExecutePendingJob(rt, NULL) > 0) { /* drain */ }
    }

    JS_FreeValue(ctx, fns);
    JS_FreeValue(ctx, ch);
    JS_FreeValue(ctx, args[1]);

    if (rejected) {
        *al = SSL_AD_HANDSHAKE_FAILURE;
        return SSL_CLIENT_HELLO_ERROR;
    }

    return SSL_CLIENT_HELLO_SUCCESS;
}


/* Frees the hooks record when its SSL_CTX is destroyed (e.g. on reload). */
static void
ngx_js_ch_free(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx,
    long argl, void *argp)
{
    if (ptr != NULL) {
        OPENSSL_free(ptr);
    }
}


static JSValue
ngx_js_ssl_on_client_hello(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_ssl_opaque_t      *op;
    ngx_http_ssl_srv_conf_t  *sscf;
    ngx_js_ch_hooks_t        *hooks;
    JSValue                   reg, fns, lenv;
    int64_t                   len;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "onClientHello(fn): expected a function");
    }

    sscf = op->sscf;
    if (sscf == NULL || sscf->ssl.ctx == NULL) {
        return JS_ThrowInternalError(ctx,
                                     "onClientHello: SSL context not initialised");
    }

    if (ngx_js_ch_ctx_idx < 0) {
        /* free func ties the hooks record to the SSL_CTX lifetime */
        ngx_js_ch_ctx_idx = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL,
                                                     ngx_js_ch_free);
    }

    reg = ngx_js_ch_registry(ctx);

    hooks = SSL_CTX_get_ex_data(sscf->ssl.ctx, ngx_js_ch_ctx_idx);
    if (hooks == NULL) {
        /* NOT an nginx pool: at js_source-eval time ngx_cycle is the transient
         * init cycle, so its pool would be freed out from under us. OPENSSL_zalloc
         * lives with the SSL_CTX and is released by ngx_js_ch_free. */
        hooks = OPENSSL_zalloc(sizeof(ngx_js_ch_hooks_t));
        if (hooks == NULL) {
            JS_FreeValue(ctx, reg);
            return JS_ThrowOutOfMemory(ctx);
        }

        /* allocate a slot in the global registry for this CTX's fn-array */
        len = 0;
        lenv = JS_GetPropertyStr(ctx, reg, "length");
        JS_ToInt64(ctx, &len, lenv);
        JS_FreeValue(ctx, lenv);

        hooks->slot = (uint32_t) len;
        JS_SetPropertyUint32(ctx, reg, hooks->slot, JS_NewArray(ctx));

        SSL_CTX_set_ex_data(sscf->ssl.ctx, ngx_js_ch_ctx_idx, hooks);
        SSL_CTX_set_client_hello_cb(sscf->ssl.ctx, ngx_js_ch_cb, hooks);
    }

    /* append fn to this CTX's rooted fn-array */
    fns  = JS_GetPropertyUint32(ctx, reg, hooks->slot);
    len  = 0;
    lenv = JS_GetPropertyStr(ctx, fns, "length");
    JS_ToInt64(ctx, &len, lenv);
    JS_FreeValue(ctx, lenv);
    JS_SetPropertyUint32(ctx, fns, (uint32_t) len, JS_DupValue(ctx, argv[0]));
    JS_FreeValue(ctx, fns);
    JS_FreeValue(ctx, reg);

    return JS_UNDEFINED;
}

#else  /* !NGX_JS_HAVE_CLIENT_HELLO_CB */

static JSValue
ngx_js_ssl_on_client_hello(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    return JS_ThrowInternalError(ctx,
        "onClientHello requires OpenSSL 1.1.1+ (SSL_CTX_set_client_hello_cb)");
}

#endif


static const JSCFunctionListEntry ngx_js_ssl_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sessionTimeout",      ngx_js_ssl_get, ngx_js_ssl_set, 0),
    JS_CGETSET_MAGIC_DEF("sessionTickets",       ngx_js_ssl_get, ngx_js_ssl_set, 1),
    JS_CGETSET_MAGIC_DEF("preferServerCiphers",  ngx_js_ssl_get, ngx_js_ssl_set, 2),
    JS_CGETSET_MAGIC_DEF("verifyDepth",          ngx_js_ssl_get, ngx_js_ssl_set, 3),
    JS_CGETSET_MAGIC_DEF("ciphers",              ngx_js_ssl_get, NULL,           4),
    JS_CGETSET_MAGIC_DEF("clientCertificate",    ngx_js_ssl_get, NULL,           5),
    JS_CGETSET_MAGIC_DEF("trustedCertificate",   ngx_js_ssl_get, NULL,           6),
    JS_CGETSET_MAGIC_DEF("ecdhCurve",            ngx_js_ssl_get, NULL,           7),
    JS_CGETSET_MAGIC_DEF("dhparam",              ngx_js_ssl_get, NULL,           8),
    JS_CGETSET_DEF       ("protocols",           ngx_js_ssl_get_protocols,      NULL),
    JS_CGETSET_DEF       ("verify",              ngx_js_ssl_get_verify,         NULL),
    JS_CGETSET_DEF       ("certificate",         ngx_js_ssl_get_certificate,    NULL),
    JS_CGETSET_DEF       ("certificateKey",      ngx_js_ssl_get_certificate_key,NULL),
    JS_CFUNC_DEF         ("setCiphers",      1,   ngx_js_ssl_set_ciphers),
    JS_CFUNC_DEF         ("setProtocols",   1,   ngx_js_ssl_set_protocols),
    JS_CFUNC_DEF         ("setCertificate", 2,   ngx_js_ssl_set_certificate),
    JS_CFUNC_DEF         ("onClientHello",  1,   ngx_js_ssl_on_client_hello),
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


/* ================================================================== */
/* NginxStreamSSL — stream server TLS configuration (Stage D)         */
/* ================================================================== */

#if (NGX_STREAM_SSL)

#include <ngx_stream_ssl_module.h>

typedef struct {
    ngx_stream_ssl_srv_conf_t  *sscf;
} ngx_js_stream_ssl_opaque_t;


static void
ngx_js_stream_ssl_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_stream_ssl_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_stream_ssl_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_stream_ssl_class = {
    "NginxStreamSSL",
    .finalizer = ngx_js_stream_ssl_finalizer
};


/*
 * Magic values for ngx_js_stream_ssl_get / ngx_js_stream_ssl_set:
 *   0 — sessionTimeout      writable (SSL_CTX_set_timeout)
 *   1 — sessionTickets       writable (SSL_OP_NO_TICKET)
 *   2 — preferServerCiphers  writable (SSL_OP_CIPHER_SERVER_PREFERENCE)
 *   3 — verifyDepth          writable (SSL_CTX_set_verify_depth)
 *   4 — ciphers              r/o (use setCiphers())
 *   5 — clientCertificate    r/o string
 *   6 — trustedCertificate   r/o string
 *   7 — ecdhCurve            r/o string
 *   8 — dhparam              r/o string
 *   9 — handshakeTimeout     writable (stream-only, ms)
 */
static JSValue
ngx_js_stream_ssl_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_stream_ssl_opaque_t  *op;
    ngx_stream_ssl_srv_conf_t   *sscf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    sscf = op->sscf;

    switch (magic) {
    case 0: return JS_NewInt64(ctx, (int64_t) sscf->session_timeout);
    case 1: return JS_NewBool(ctx,  (int) sscf->session_tickets);
    case 2: return JS_NewBool(ctx,  (int) sscf->prefer_server_ciphers);
    case 3: return JS_NewInt64(ctx, (int64_t) sscf->verify_depth);
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
    case 9: return JS_NewInt64(ctx, (int64_t) sscf->handshake_timeout);
    }

    return JS_UNDEFINED;
}


static JSValue
ngx_js_stream_ssl_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_stream_ssl_opaque_t  *op;
    ngx_stream_ssl_srv_conf_t   *sscf;
    int64_t                      n;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    sscf = op->sscf;

    switch (magic) {
    case 0: /* sessionTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        sscf->session_timeout = (time_t) n;
        if (sscf->ssl.ctx != NULL) {
            SSL_CTX_set_timeout(sscf->ssl.ctx, (long) n);
        }
        return JS_UNDEFINED;

    case 1: /* sessionTickets */
        n = JS_ToBool(ctx, val);
        if (n < 0) { return JS_EXCEPTION; }
        sscf->session_tickets = (ngx_flag_t) n;
        if (sscf->ssl.ctx != NULL) {
            if (n) {
                SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TICKET);
            } else {
                SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TICKET);
            }
        }
        return JS_UNDEFINED;

    case 2: /* preferServerCiphers */
        n = JS_ToBool(ctx, val);
        if (n < 0) { return JS_EXCEPTION; }
        sscf->prefer_server_ciphers = (ngx_flag_t) n;
        if (sscf->ssl.ctx != NULL) {
            if (n) {
                SSL_CTX_set_options(sscf->ssl.ctx,
                                    SSL_OP_CIPHER_SERVER_PREFERENCE);
            } else {
                SSL_CTX_clear_options(sscf->ssl.ctx,
                                      SSL_OP_CIPHER_SERVER_PREFERENCE);
            }
        }
        return JS_UNDEFINED;

    case 3: /* verifyDepth */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        sscf->verify_depth = (ngx_uint_t) n;
        if (sscf->ssl.ctx != NULL) {
            SSL_CTX_set_verify_depth(sscf->ssl.ctx, (int) n);
        }
        return JS_UNDEFINED;

    case 9: /* handshakeTimeout */
        if (JS_ToInt64(ctx, &n, val) < 0) { return JS_EXCEPTION; }
        sscf->handshake_timeout = (ngx_msec_t) n;
        return JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/* ssl.protocols */
static JSValue
ngx_js_stream_ssl_get_protocols(JSContext *ctx, JSValueConst this_val)
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

    ngx_js_stream_ssl_opaque_t  *op;
    JSValue                      arr;
    ngx_uint_t                   mask, i;
    uint32_t                     idx;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
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


/* ssl.verify */
static JSValue
ngx_js_stream_ssl_get_verify(JSContext *ctx, JSValueConst this_val)
{
    static const char  *modes[] = {
        "off", "on", "optional", "optional_no_ca"
    };

    ngx_js_stream_ssl_opaque_t  *op;
    ngx_uint_t                   v;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    v = op->sscf->verify;
    if (v > 3) { v = 0; }

    return JS_NewString(ctx, modes[v]);
}


/* ssl.certificate */
static JSValue
ngx_js_stream_ssl_get_certificate(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_stream_ssl_opaque_t  *op;
    JSValue                      arr;
    ngx_str_t                   *cert;
    ngx_uint_t                   i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
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


/* ssl.certificateKey */
static JSValue
ngx_js_stream_ssl_get_certificate_key(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_stream_ssl_opaque_t  *op;
    JSValue                      arr;
    ngx_str_t                   *key;
    ngx_uint_t                   i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
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


/* setProtocols(arr) — update enabled TLS versions on the live SSL_CTX */
static JSValue
ngx_js_stream_ssl_set_protocols(JSContext *ctx, JSValueConst this_val,
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

    ngx_js_stream_ssl_opaque_t  *op;
    ngx_stream_ssl_srv_conf_t   *sscf;
    JSValue                      arr, elem;
    uint32_t                     len, i;
    ngx_uint_t                   mask, j;
    const char                  *s;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
    if (!op) { return JS_EXCEPTION; }

    sscf = op->sscf;

    if (argc < 1 || !JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "setProtocols: array argument required");
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

    SSL_CTX_clear_options(sscf->ssl.ctx,
                          SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);
    if (!(mask & NGX_SSL_SSLv2)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_SSLv2); }
    if (!(mask & NGX_SSL_SSLv3)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_SSLv3); }
    if (!(mask & NGX_SSL_TLSv1)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1); }
#ifdef SSL_OP_NO_TLSv1_1
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_1);
    if (!(mask & NGX_SSL_TLSv1_1)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_1); }
#endif
#ifdef SSL_OP_NO_TLSv1_2
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_2);
    if (!(mask & NGX_SSL_TLSv1_2)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_2); }
#endif
#ifdef SSL_OP_NO_TLSv1_3
    SSL_CTX_clear_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_3);
    if (!(mask & NGX_SSL_TLSv1_3)) { SSL_CTX_set_options(sscf->ssl.ctx, SSL_OP_NO_TLSv1_3); }
#endif

    sscf->protocols = mask;
    return JS_UNDEFINED;
}


/* setCiphers(str) — update cipher list on the live SSL_CTX */
static JSValue
ngx_js_stream_ssl_set_ciphers(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_stream_ssl_opaque_t  *op;
    ngx_stream_ssl_srv_conf_t   *sscf;
    const char                  *s;
    size_t                       len;
    u_char                      *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
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

    if (SSL_CTX_set_cipher_list(sscf->ssl.ctx, s) == 0) {
        JS_FreeCString(ctx, s);
        return JS_ThrowInternalError(ctx, "setCiphers: no valid ciphers in list");
    }

    p = ngx_pnalloc(ngx_js_conf_cycle()->pool, len);
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


/* setCertificate(certPath, keyPath) — hot-swap TLS certificate */
static JSValue
ngx_js_stream_ssl_set_certificate(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_stream_ssl_opaque_t  *op;
    ngx_stream_ssl_srv_conf_t   *sscf;
    const char                  *cert_s, *key_s;
    size_t                       cert_len, key_len;
    u_char                      *cp, *kp;
    ngx_array_t                 *certs, *keys;
    ngx_str_t                   *sp;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_stream_ssl_class_id);
    if (!op) { return JS_EXCEPTION; }

    sscf = op->sscf;

    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
                                 "setCertificate: certPath and keyPath required");
    }

    cert_s = JS_ToCStringLen(ctx, &cert_len, argv[0]);
    if (!cert_s) { return JS_EXCEPTION; }

    key_s = JS_ToCStringLen(ctx, &key_len, argv[1]);
    if (!key_s) {
        JS_FreeCString(ctx, cert_s);
        return JS_EXCEPTION;
    }

    if (sscf->ssl.ctx == NULL) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: SSL context not initialised");
    }

    if (SSL_CTX_use_certificate_chain_file(sscf->ssl.ctx, cert_s) != 1) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: failed to load certificate");
    }

    if (SSL_CTX_use_PrivateKey_file(sscf->ssl.ctx, key_s,
                                    SSL_FILETYPE_PEM) != 1)
    {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: failed to load private key");
    }

    if (SSL_CTX_check_private_key(sscf->ssl.ctx) != 1) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowInternalError(ctx,
                                     "setCertificate: private key does not match"
                                     " certificate");
    }

    cp = ngx_pnalloc(ngx_js_conf_cycle()->pool, cert_len + 1);
    kp = ngx_pnalloc(ngx_js_conf_cycle()->pool, key_len + 1);

    if (cp == NULL || kp == NULL) {
        JS_FreeCString(ctx, cert_s);
        JS_FreeCString(ctx, key_s);
        return JS_ThrowOutOfMemory(ctx);
    }

    ngx_memcpy(cp, cert_s, cert_len);  cp[cert_len] = '\0';
    ngx_memcpy(kp, key_s,  key_len);   kp[key_len]  = '\0';

    JS_FreeCString(ctx, cert_s);
    JS_FreeCString(ctx, key_s);

    certs = ngx_array_create(ngx_js_conf_cycle()->pool, 1, sizeof(ngx_str_t));
    keys  = ngx_array_create(ngx_js_conf_cycle()->pool, 1, sizeof(ngx_str_t));

    if (certs == NULL || keys == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    sp = ngx_array_push(certs);
    if (sp == NULL) { return JS_ThrowOutOfMemory(ctx); }
    sp->data = cp;  sp->len = cert_len;

    sp = ngx_array_push(keys);
    if (sp == NULL) { return JS_ThrowOutOfMemory(ctx); }
    sp->data = kp;  sp->len = key_len;

    sscf->certificates    = certs;
    sscf->certificate_keys = keys;

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_stream_ssl_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sessionTimeout",
                         ngx_js_stream_ssl_get, ngx_js_stream_ssl_set, 0),
    JS_CGETSET_MAGIC_DEF("sessionTickets",
                         ngx_js_stream_ssl_get, ngx_js_stream_ssl_set, 1),
    JS_CGETSET_MAGIC_DEF("preferServerCiphers",
                         ngx_js_stream_ssl_get, ngx_js_stream_ssl_set, 2),
    JS_CGETSET_MAGIC_DEF("verifyDepth",
                         ngx_js_stream_ssl_get, ngx_js_stream_ssl_set, 3),
    JS_CGETSET_MAGIC_DEF("ciphers",
                         ngx_js_stream_ssl_get, NULL,                   4),
    JS_CGETSET_MAGIC_DEF("clientCertificate",
                         ngx_js_stream_ssl_get, NULL,                   5),
    JS_CGETSET_MAGIC_DEF("trustedCertificate",
                         ngx_js_stream_ssl_get, NULL,                   6),
    JS_CGETSET_MAGIC_DEF("ecdhCurve",
                         ngx_js_stream_ssl_get, NULL,                   7),
    JS_CGETSET_MAGIC_DEF("dhparam",
                         ngx_js_stream_ssl_get, NULL,                   8),
    JS_CGETSET_MAGIC_DEF("handshakeTimeout",
                         ngx_js_stream_ssl_get, ngx_js_stream_ssl_set,  9),
    JS_CGETSET_DEF       ("protocols",
                          ngx_js_stream_ssl_get_protocols, NULL),
    JS_CGETSET_DEF       ("verify",
                          ngx_js_stream_ssl_get_verify, NULL),
    JS_CGETSET_DEF       ("certificate",
                          ngx_js_stream_ssl_get_certificate, NULL),
    JS_CGETSET_DEF       ("certificateKey",
                          ngx_js_stream_ssl_get_certificate_key, NULL),
    JS_CFUNC_DEF         ("setCiphers",     1, ngx_js_stream_ssl_set_ciphers),
    JS_CFUNC_DEF         ("setProtocols",   1, ngx_js_stream_ssl_set_protocols),
    JS_CFUNC_DEF         ("setCertificate", 2, ngx_js_stream_ssl_set_certificate),
};


JSValue
ngx_js_wrap_stream_ssl(JSContext *ctx, ngx_stream_ssl_srv_conf_t *sscf)
{
    JSValue                      obj;
    ngx_js_stream_ssl_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_stream_ssl_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->sscf = sscf;

    obj = JS_NewObjectClass(ctx, ngx_js_stream_ssl_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}

#endif /* NGX_STREAM_SSL */


ngx_int_t
ngx_js_stream_ssl_register_class(JSRuntime *rt)
{
#if (NGX_STREAM_SSL)
    return JS_NewClass(rt, ngx_js_stream_ssl_class_id,
                       &ngx_js_stream_ssl_class) < 0
           ? NGX_ERROR : NGX_OK;
#else
    return NGX_OK;
#endif
}


ngx_int_t
ngx_js_stream_ssl_install_proto(JSContext *ctx)
{
#if (NGX_STREAM_SSL)
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_stream_ssl_proto_funcs,
                               countof(ngx_js_stream_ssl_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_stream_ssl_class_id, proto);
#endif
    return NGX_OK;
}
