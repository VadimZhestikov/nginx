
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 11a: access module location configuration.
 *
 * Exposes location.access as a NginxAccess object:
 *
 *   rules      object[]  IPv4 allow/deny rules: [{deny, cidr}]  r/w
 *   rules6     object[]  IPv6 allow/deny rules: [{deny, cidr}]  r/w
 *                        (empty when built without IPv6 support)
 *   rulesUnix  object[]  Unix-socket allow/deny: [{deny}]       r/w
 *                        (empty when built without Unix-domain support)
 *
 * cidr format: "all" for 0.0.0.0/0 (or ::/0), "1.2.3.4" for /32,
 * "1.2.3.0/24" for subnets.
 *
 * Setters replace the rules array in-place; old arrays stay in the pool
 * (they are never freed, pool memory only grows).
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <cutils.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "../http/modules/ngx_http_access_module.h"


typedef struct {
    ngx_http_access_loc_conf_t  *alcf;
} ngx_js_access_opaque_t;


static void
ngx_js_access_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_access_opaque_t *op;

    op = JS_GetOpaque(val, ngx_js_access_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_access_class = {
    "NginxAccess",
    .finalizer = ngx_js_access_finalizer
};


/*
 * Count set bits in a 32-bit value (prefix length from IPv4 netmask).
 * Works regardless of byte order since popcount is bit-pattern only.
 */
static ngx_uint_t
ngx_js_popcount32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0f0f0f0fu;
    return (ngx_uint_t) ((v * 0x01010101u) >> 24);
}


/*
 * Build a JS object {deny: bool, cidr: string} for one IPv4 rule.
 */
static JSValue
ngx_js_access_ipv4_entry(JSContext *ctx, ngx_http_access_rule_t *rule)
{
    JSValue   obj;
    u_char    buf[NGX_INET_ADDRSTRLEN + sizeof("/32")];
    size_t    slen;
    uint32_t  mask;

    obj  = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "deny", JS_NewBool(ctx, (int) rule->deny));

    mask = rule->mask;

    if (mask == 0 && rule->addr == 0) {
        /* special "all" rule */
        JS_SetPropertyStr(ctx, obj, "cidr", JS_NewString(ctx, "all"));
        return obj;
    }

    slen = ngx_inet_ntop(AF_INET, &rule->addr, buf, NGX_INET_ADDRSTRLEN);

    if (mask != 0xffffffffu) {
        /* subnet: append /prefix */
        slen += ngx_snprintf(buf + slen, sizeof("/32"), "/%ui",
                             ngx_js_popcount32(mask)) - buf - slen;
    }

    JS_SetPropertyStr(ctx, obj, "cidr",
                      JS_NewStringLen(ctx, (const char *) buf, slen));

    return obj;
}


/*
 * access.rules — array of IPv4 allow/deny entries.
 */
static JSValue
ngx_js_access_get_rules(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_access_opaque_t  *op;
    JSValue                  arr;
    ngx_http_access_rule_t  *rule;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

    if (op->alcf->rules == NULL) {
        return arr;
    }

    rule = op->alcf->rules->elts;

    for (i = 0; i < op->alcf->rules->nelts; i++) {
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i,
                             ngx_js_access_ipv4_entry(ctx, &rule[i]));
    }

    return arr;
}


/*
 * access.rules6 — array of IPv6 allow/deny entries (or empty array).
 */
static JSValue
ngx_js_access_get_rules6(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_access_opaque_t  *op;
    JSValue                  arr;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

#if (NGX_HAVE_INET6)
    {
        ngx_http_access_rule6_t  *rule6;
        ngx_uint_t                i;

        if (op->alcf->rules6 == NULL) {
            return arr;
        }

        rule6 = op->alcf->rules6->elts;

        for (i = 0; i < op->alcf->rules6->nelts; i++) {
            JSValue   entry;
            u_char    buf[NGX_INET6_ADDRSTRLEN + sizeof("/128")];
            size_t    slen;
            uint32_t *w;
            ngx_uint_t  prefix, j;

            entry = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, entry, "deny",
                              JS_NewBool(ctx, (int) rule6[i].deny));

            /* Check for ::/0 ("all") */
            w = (uint32_t *) rule6[i].mask.s6_addr;
            if (w[0] == 0 && w[1] == 0 && w[2] == 0 && w[3] == 0
                && *(uint32_t *) rule6[i].addr.s6_addr == 0
                && ((uint32_t *) rule6[i].addr.s6_addr)[1] == 0
                && ((uint32_t *) rule6[i].addr.s6_addr)[2] == 0
                && ((uint32_t *) rule6[i].addr.s6_addr)[3] == 0)
            {
                JS_SetPropertyStr(ctx, entry, "cidr",
                                  JS_NewString(ctx, "all"));
                JS_SetPropertyUint32(ctx, arr, (uint32_t) i, entry);
                continue;
            }

            slen = ngx_inet6_ntop(rule6[i].addr.s6_addr, buf,
                                  NGX_INET6_ADDRSTRLEN);

            /* compute prefix length */
            prefix = 0;
            for (j = 0; j < 4; j++) {
                prefix += ngx_js_popcount32(w[j]);
            }

            if (prefix < 128) {
                slen += ngx_snprintf(buf + slen, sizeof("/128"), "/%ui",
                                     prefix) - buf - slen;
            }

            JS_SetPropertyStr(ctx, entry, "cidr",
                              JS_NewStringLen(ctx, (const char *) buf, slen));
            JS_SetPropertyUint32(ctx, arr, (uint32_t) i, entry);
        }
    }
#endif

    return arr;
}


/*
 * access.rulesUnix — array of Unix-socket allow/deny entries (or empty).
 */
static JSValue
ngx_js_access_get_rules_unix(JSContext *ctx, JSValueConst this_val)
{
    ngx_js_access_opaque_t  *op;
    JSValue                  arr;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    arr = JS_NewArray(ctx);

#if (NGX_HAVE_UNIX_DOMAIN)
    {
        ngx_http_access_rule_un_t  *rule_un;
        JSValue                     entry;
        ngx_uint_t                  i;

        if (op->alcf->rules_un == NULL) {
            return arr;
        }

        rule_un = op->alcf->rules_un->elts;

        for (i = 0; i < op->alcf->rules_un->nelts; i++) {
            entry = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, entry, "deny",
                              JS_NewBool(ctx, (int) rule_un[i].deny));
            JS_SetPropertyUint32(ctx, arr, (uint32_t) i, entry);
        }
    }
#endif

    return arr;
}


/* ---- setters ---- */

/*
 * Parse "all", "a.b.c.d", or "a.b.c.d/prefix" into (addr, mask).
 * Returns NGX_OK on success, NGX_ERROR on invalid input.
 */
static ngx_int_t
ngx_js_parse_ipv4_cidr(const char *s, size_t len,
    in_addr_t *addr_out, in_addr_t *mask_out)
{
    u_char      buf[NGX_INET_ADDRSTRLEN + 1];
    u_char     *slash;
    size_t      ip_len;
    ngx_int_t   prefix;
    in_addr_t   a;

    if (len == 3 && ngx_strncmp(s, "all", 3) == 0) {
        *addr_out = 0;
        *mask_out = 0;
        return NGX_OK;
    }

    slash = ngx_strlchr((u_char *) s, (u_char *) s + len, '/');
    ip_len = slash ? (size_t) (slash - (u_char *) s) : len;

    if (ip_len >= sizeof(buf)) {
        return NGX_ERROR;
    }

    ngx_memcpy(buf, s, ip_len);
    buf[ip_len] = '\0';

    a = ngx_inet_addr(buf, ip_len);
    if (a == INADDR_NONE) {
        return NGX_ERROR;
    }

    *addr_out = a;

    if (slash) {
        prefix = ngx_atoi(slash + 1, len - ip_len - 1);
        if (prefix == NGX_ERROR || prefix < 0 || prefix > 32) {
            return NGX_ERROR;
        }
        *mask_out = prefix ? htonl(~((1u << (32 - (ngx_uint_t) prefix)) - 1))
                           : 0;
    } else {
        *mask_out = 0xffffffff;
    }

    return NGX_OK;
}


/*
 * access.rules = [{deny: bool, cidr: string}, ...]
 */
static JSValue
ngx_js_access_set_rules(JSContext *ctx, JSValueConst this_val, JSValue val)
{
    ngx_js_access_opaque_t  *op;
    ngx_array_t             *arr;
    ngx_http_access_rule_t  *rule;
    JSValue                  entry, deny_v, cidr_v, len_v;
    const char              *cidr;
    size_t                   cidr_len;
    int64_t                  len, i;
    int                      b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (!JS_IsArray(ctx, val)) {
        return JS_ThrowTypeError(ctx, "rules must be an array");
    }

    len_v = JS_GetPropertyStr(ctx, val, "length");
    if (JS_ToInt64(ctx, &len, len_v) < 0) {
        JS_FreeValue(ctx, len_v);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_v);

    if (len == 0) {
        op->alcf->rules = NULL;
        return JS_UNDEFINED;
    }

    arr = ngx_array_create(ngx_cycle->pool, (ngx_uint_t) len,
                           sizeof(ngx_http_access_rule_t));
    if (!arr) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < len; i++) {
        entry  = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
        deny_v = JS_GetPropertyStr(ctx, entry, "deny");
        cidr_v = JS_GetPropertyStr(ctx, entry, "cidr");
        JS_FreeValue(ctx, entry);

        b = JS_ToBool(ctx, deny_v);
        JS_FreeValue(ctx, deny_v);
        if (b < 0) {
            JS_FreeValue(ctx, cidr_v);
            return JS_EXCEPTION;
        }

        cidr = JS_ToCStringLen(ctx, &cidr_len, cidr_v);
        JS_FreeValue(ctx, cidr_v);
        if (!cidr) {
            return JS_EXCEPTION;
        }

        rule = ngx_array_push(arr);
        if (!rule) {
            JS_FreeCString(ctx, cidr);
            return JS_EXCEPTION;
        }

        rule->deny = (ngx_uint_t) b;

        if (ngx_js_parse_ipv4_cidr(cidr, cidr_len,
                                    &rule->addr, &rule->mask)
            != NGX_OK)
        {
            JS_FreeCString(ctx, cidr);
            return JS_ThrowTypeError(ctx,
                                     "invalid IPv4 CIDR: \"%s\"", cidr);
        }

        JS_FreeCString(ctx, cidr);
    }

    op->alcf->rules = arr;
    return JS_UNDEFINED;
}


#if (NGX_HAVE_INET6)

/*
 * Parse "all", "::1", or "2001:db8::/32" into (addr, mask).
 */
static ngx_int_t
ngx_js_parse_ipv6_cidr(const char *s, size_t len,
    struct in6_addr *addr_out, struct in6_addr *mask_out)
{
    char        buf[NGX_INET6_ADDRSTRLEN + 1];
    u_char     *slash;
    size_t      ip_len;
    ngx_int_t   prefix;
    ngx_uint_t  j, bits;
    uint32_t   *mw;

    if (len == 3 && ngx_strncmp(s, "all", 3) == 0) {
        ngx_memzero(addr_out, sizeof(*addr_out));
        ngx_memzero(mask_out, sizeof(*mask_out));
        return NGX_OK;
    }

    slash = ngx_strlchr((u_char *) s, (u_char *) s + len, '/');
    ip_len = slash ? (size_t) (slash - (u_char *) s) : len;

    if (ip_len >= sizeof(buf)) {
        return NGX_ERROR;
    }

    ngx_memcpy(buf, s, ip_len);
    buf[ip_len] = '\0';

    if (ngx_inet6_addr((u_char *) buf, ip_len, addr_out->s6_addr) != NGX_OK) {
        return NGX_ERROR;
    }

    mw = (uint32_t *) mask_out->s6_addr;

    if (slash) {
        prefix = ngx_atoi(slash + 1, len - ip_len - 1);
        if (prefix == NGX_ERROR || prefix < 0 || prefix > 128) {
            return NGX_ERROR;
        }

        ngx_memzero(mask_out, sizeof(*mask_out));

        bits = (ngx_uint_t) prefix;

        for (j = 0; j < 4; j++) {
            if (bits >= 32) {
                mw[j] = 0xffffffff;
                bits -= 32;
            } else if (bits > 0) {
                mw[j] = htonl(~((1u << (32 - bits)) - 1));
                bits   = 0;
            }
        }

    } else {
        mw[0] = mw[1] = mw[2] = mw[3] = 0xffffffff;
    }

    return NGX_OK;
}


/*
 * access.rules6 = [{deny: bool, cidr: string}, ...]
 */
static JSValue
ngx_js_access_set_rules6(JSContext *ctx, JSValueConst this_val, JSValue val)
{
    ngx_js_access_opaque_t   *op;
    ngx_array_t              *arr;
    ngx_http_access_rule6_t  *rule;
    JSValue                   entry, deny_v, cidr_v, len_v;
    const char               *cidr;
    size_t                    cidr_len;
    int64_t                   len, i;
    int                       b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (!JS_IsArray(ctx, val)) {
        return JS_ThrowTypeError(ctx, "rules6 must be an array");
    }

    len_v = JS_GetPropertyStr(ctx, val, "length");
    if (JS_ToInt64(ctx, &len, len_v) < 0) {
        JS_FreeValue(ctx, len_v);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_v);

    if (len == 0) {
        op->alcf->rules6 = NULL;
        return JS_UNDEFINED;
    }

    arr = ngx_array_create(ngx_cycle->pool, (ngx_uint_t) len,
                           sizeof(ngx_http_access_rule6_t));
    if (!arr) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < len; i++) {
        entry  = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
        deny_v = JS_GetPropertyStr(ctx, entry, "deny");
        cidr_v = JS_GetPropertyStr(ctx, entry, "cidr");
        JS_FreeValue(ctx, entry);

        b = JS_ToBool(ctx, deny_v);
        JS_FreeValue(ctx, deny_v);
        if (b < 0) {
            JS_FreeValue(ctx, cidr_v);
            return JS_EXCEPTION;
        }

        cidr = JS_ToCStringLen(ctx, &cidr_len, cidr_v);
        JS_FreeValue(ctx, cidr_v);
        if (!cidr) {
            return JS_EXCEPTION;
        }

        rule = ngx_array_push(arr);
        if (!rule) {
            JS_FreeCString(ctx, cidr);
            return JS_EXCEPTION;
        }

        rule->deny = (ngx_uint_t) b;

        if (ngx_js_parse_ipv6_cidr(cidr, cidr_len,
                                    &rule->addr, &rule->mask)
            != NGX_OK)
        {
            JS_FreeCString(ctx, cidr);
            return JS_ThrowTypeError(ctx,
                                     "invalid IPv6 CIDR: \"%s\"", cidr);
        }

        JS_FreeCString(ctx, cidr);
    }

    op->alcf->rules6 = arr;
    return JS_UNDEFINED;
}

#endif  /* NGX_HAVE_INET6 */


#if (NGX_HAVE_UNIX_DOMAIN)

/*
 * access.rulesUnix = [{deny: bool}, ...]
 */
static JSValue
ngx_js_access_set_rules_unix(JSContext *ctx, JSValueConst this_val, JSValue val)
{
    ngx_js_access_opaque_t     *op;
    ngx_array_t                *arr;
    ngx_http_access_rule_un_t  *rule;
    JSValue                     entry, deny_v, len_v;
    int64_t                     len, i;
    int                         b;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_access_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    if (!JS_IsArray(ctx, val)) {
        return JS_ThrowTypeError(ctx, "rulesUnix must be an array");
    }

    len_v = JS_GetPropertyStr(ctx, val, "length");
    if (JS_ToInt64(ctx, &len, len_v) < 0) {
        JS_FreeValue(ctx, len_v);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_v);

    if (len == 0) {
        op->alcf->rules_un = NULL;
        return JS_UNDEFINED;
    }

    arr = ngx_array_create(ngx_cycle->pool, (ngx_uint_t) len,
                           sizeof(ngx_http_access_rule_un_t));
    if (!arr) {
        return JS_EXCEPTION;
    }

    for (i = 0; i < len; i++) {
        entry  = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
        deny_v = JS_GetPropertyStr(ctx, entry, "deny");
        JS_FreeValue(ctx, entry);

        b = JS_ToBool(ctx, deny_v);
        JS_FreeValue(ctx, deny_v);
        if (b < 0) {
            return JS_EXCEPTION;
        }

        rule = ngx_array_push(arr);
        if (!rule) {
            return JS_EXCEPTION;
        }

        rule->deny = (ngx_uint_t) b;
    }

    op->alcf->rules_un = arr;
    return JS_UNDEFINED;
}

#endif  /* NGX_HAVE_UNIX_DOMAIN */


static const JSCFunctionListEntry ngx_js_access_proto_funcs[] = {
    JS_CGETSET_DEF("rules",      ngx_js_access_get_rules,      ngx_js_access_set_rules),
#if (NGX_HAVE_INET6)
    JS_CGETSET_DEF("rules6",     ngx_js_access_get_rules6,     ngx_js_access_set_rules6),
#else
    JS_CGETSET_DEF("rules6",     ngx_js_access_get_rules6,     NULL),
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    JS_CGETSET_DEF("rulesUnix",  ngx_js_access_get_rules_unix, ngx_js_access_set_rules_unix),
#else
    JS_CGETSET_DEF("rulesUnix",  ngx_js_access_get_rules_unix, NULL),
#endif
};


ngx_int_t
ngx_js_access_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_access_proto_funcs,
                               countof(ngx_js_access_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_access_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_access(JSContext *ctx, ngx_http_access_loc_conf_t *alcf)
{
    JSValue                  obj;
    ngx_js_access_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_access_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->alcf = alcf;

    obj = JS_NewObjectClass(ctx, ngx_js_access_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);

    return obj;
}


ngx_int_t
ngx_js_access_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_access_class_id, &ngx_js_access_class) < 0
           ? NGX_ERROR : NGX_OK;
}
