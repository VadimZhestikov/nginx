
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 13c: realip location configuration.
 * Guarded by NGX_HTTP_REALIP (requires --with-http_realip_module).
 *
 * Exposes location.realip as a NginxRealIP object:
 *
 *   header     string     effective header used to find the real IP:
 *                           "X-Real-IP"       (default / real_ip_header X-Real-IP)
 *                           "X-Forwarded-For" (real_ip_header X-Forwarded-For)
 *                           "proxy_protocol"  (real_ip_header proxy_protocol)
 *                           <custom>          (real_ip_header <name>)
 *   recursive  boolean    real_ip_recursive on/off
 *   from       string[]   set_real_ip_from CIDRs (e.g. "10.0.0.0/8", "::1")
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (NGX_HTTP_REALIP)

#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_realip_module.h"


typedef struct {
    ngx_http_realip_loc_conf_t  *rlcf;
} ngx_js_realip_opaque_t;


static void
ngx_js_realip_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_realip_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_realip_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_realip_class = {
    "NginxRealIP",
    .finalizer = ngx_js_realip_finalizer
};


/*
 * Helper: count set bits in a 32-bit value (portable popcount).
 */
static ngx_uint_t
ngx_js_realip_popcount32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0f0f0f0fu;
    return (v * 0x01010101u) >> 24;
}


/*
 * Format a single ngx_cidr_t as a CIDR string, e.g. "192.168.1.0/24".
 * Returns a JS string value.
 */
static JSValue
ngx_js_realip_cidr_str(JSContext *ctx, ngx_cidr_t *cidr)
{
    u_char      buf[NGX_INET6_ADDRSTRLEN + sizeof("/128")];
    size_t      n;
    ngx_uint_t  bits;
    u_char     *p;

    if (cidr->family == AF_INET) {
        n = ngx_inet_ntop(AF_INET, &cidr->u.in.addr, buf, NGX_INET_ADDRSTRLEN);
        bits = ngx_js_realip_popcount32(ntohl(cidr->u.in.mask));
        if (bits < 32) {
            p = buf + n;
            p = ngx_sprintf(p, "/%ui", bits);
            n = (size_t) (p - buf);
        }
        return JS_NewStringLen(ctx, (const char *) buf, n);
    }

#if (NGX_HAVE_INET6)
    if (cidr->family == AF_INET6) {
        n = ngx_inet_ntop(AF_INET6, &cidr->u.in6.addr,
                          buf, NGX_INET6_ADDRSTRLEN);
        bits = 0;
        bits += ngx_js_realip_popcount32(
                    ntohl(((uint32_t *) &cidr->u.in6.mask)[0]));
        bits += ngx_js_realip_popcount32(
                    ntohl(((uint32_t *) &cidr->u.in6.mask)[1]));
        bits += ngx_js_realip_popcount32(
                    ntohl(((uint32_t *) &cidr->u.in6.mask)[2]));
        bits += ngx_js_realip_popcount32(
                    ntohl(((uint32_t *) &cidr->u.in6.mask)[3]));
        if (bits < 128) {
            p = buf + n;
            p = ngx_sprintf(p, "/%ui", bits);
            n = (size_t) (p - buf);
        }
        return JS_NewStringLen(ctx, (const char *) buf, n);
    }
#endif

    /* AF_UNIX — should not appear in set_real_ip_from, but handle gracefully */
    return JS_NewString(ctx, "unix");
}


/*
 * from getter: string[] of trusted proxy CIDR ranges.
 */
static JSValue
ngx_js_realip_get_from(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_realip_opaque_t  *op;
    ngx_cidr_t              *cidr;
    JSValue                  arr;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_realip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->rlcf->from == NULL) {
        return arr;
    }

    cidr = op->rlcf->from->elts;

    for (i = 0; i < op->rlcf->from->nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             ngx_js_realip_cidr_str(ctx, &cidr[i]));
    }

    return arr;
}


/*
 * Magic values for ngx_js_realip_get:
 *   0 — header
 *   1 — recursive
 */
static JSValue
ngx_js_realip_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_realip_opaque_t      *op;
    ngx_http_realip_loc_conf_t  *rlcf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_realip_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    rlcf = op->rlcf;

    switch (magic) {
    case 0: /* header */
        switch (rlcf->type) {
        case NGX_HTTP_REALIP_XREALIP:
            return JS_NewString(ctx, "X-Real-IP");
        case NGX_HTTP_REALIP_XFWD:
            return JS_NewString(ctx, "X-Forwarded-For");
        case NGX_HTTP_REALIP_PROXY:
            return JS_NewString(ctx, "proxy_protocol");
        default: /* NGX_HTTP_REALIP_HEADER — custom */
            return JS_NewStringLen(ctx,
                       (const char *) rlcf->header.data, rlcf->header.len);
        }

    case 1: return JS_NewBool(ctx, rlcf->recursive);
    }

    return JS_UNDEFINED;
}


/* recursive setter — magic 1 */
static JSValue
ngx_js_realip_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_realip_opaque_t  *op;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_realip_class_id);
    if (!op) { return JS_EXCEPTION; }

    if (magic == 1) { /* recursive */
        op->rlcf->recursive = JS_ToBool(ctx, val);
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_realip_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("header",    ngx_js_realip_get,      NULL,              0),
    JS_CGETSET_MAGIC_DEF("recursive", ngx_js_realip_get,      ngx_js_realip_set, 1),
    JS_CGETSET_DEF       ("from",     ngx_js_realip_get_from, NULL),
};


ngx_int_t
ngx_js_realip_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_realip_proto_funcs,
                               countof(ngx_js_realip_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_realip_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_realip(JSContext *ctx, ngx_http_realip_loc_conf_t *rlcf)
{
    JSValue                  obj;
    ngx_js_realip_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_realip_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->rlcf = rlcf;

    obj = JS_NewObjectClass(ctx, ngx_js_realip_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_realip_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_realip_class_id, &ngx_js_realip_class) < 0
           ? NGX_ERROR : NGX_OK;
}

#endif /* NGX_HTTP_REALIP */
