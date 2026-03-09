
/*
 * Copyright (C) nginx JS contributors
 *
 * HTTP COM layer — Stage 11a: access module location configuration.
 *
 * Exposes location.access as a NginxAccess object:
 *
 *   rules      object[]  IPv4 allow/deny rules: [{deny, cidr}]
 *   rules6     object[]  IPv6 allow/deny rules: [{deny, cidr}]
 *                        (empty when built without IPv6 support)
 *   rulesUnix  object[]  Unix-socket allow/deny: [{deny}]
 *                        (empty when built without Unix-domain support)
 *
 * cidr format: "all" for 0.0.0.0/0 (or ::/0), "1.2.3.4" for /32,
 * "1.2.3.0/24" for subnets.
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


static const JSCFunctionListEntry ngx_js_access_proto_funcs[] = {
    JS_CGETSET_DEF("rules",      ngx_js_access_get_rules,      NULL),
    JS_CGETSET_DEF("rules6",     ngx_js_access_get_rules6,     NULL),
    JS_CGETSET_DEF("rulesUnix",  ngx_js_access_get_rules_unix, NULL),
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
