
/*
 * Copyright (C) nginx JS contributors
 *
 * COM Stage 13j — location.userid (NginxUserid)
 *
 * Exposes ngx_http_userid_conf_t as a JS object on location.userid:
 *
 *   enable     string  — "off" / "log" / "v1" / "on"
 *   name       string  — userid_name (cookie name)
 *   domain     string  — userid_domain
 *   path       string  — userid_path
 *   p3p        string  — userid_p3p header value
 *   expires    number  — userid_expires in seconds (-1 = max, 0 = session)
 *   mark       string  — userid_mark character (single char or "")
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <quickjs.h>
#include <cutils.h>

#include "ngx_js_com.h"
#include "ngx_js.h"
#include "../http/modules/ngx_http_userid_module.h"


typedef struct {
    ngx_http_userid_conf_t  *ucf;
} ngx_js_userid_opaque_t;


static void
ngx_js_userid_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_userid_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_userid_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_userid_class = {
    "NginxUserid",
    .finalizer = ngx_js_userid_finalizer,
};


static JSValue
ngx_js_userid_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_userid_opaque_t  *op;
    ngx_http_userid_conf_t  *ucf;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_userid_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    ucf = op->ucf;

    switch (magic) {

    case 0: /* enable — "off" / "log" / "v1" / "on" */
    {
        static const char *names[] = { "off", "log", "v1", "on" };
        ngx_uint_t         idx = ucf->enable;

        if (idx < countof(names)) {
            return JS_NewString(ctx, names[idx]);
        }

        return JS_NewString(ctx, "off");
    }

    case 1: /* name */
        return JS_NewStringLen(ctx, (char *) ucf->name.data, ucf->name.len);

    case 2: /* domain — stored as "; domain=<value>", strip prefix */
    {
        static const size_t  prefix_len = sizeof("; domain=") - 1;

        if (ucf->domain.len > prefix_len) {
            return JS_NewStringLen(ctx,
                                   (char *) ucf->domain.data + prefix_len,
                                   ucf->domain.len - prefix_len);
        }

        return JS_NewStringLen(ctx, (char *) ucf->domain.data,
                               ucf->domain.len);
    }

    case 3: /* path — stored as "; path=<value>", strip prefix */
    {
        static const size_t  prefix_len = sizeof("; path=") - 1;

        if (ucf->path.len > prefix_len) {
            return JS_NewStringLen(ctx,
                                   (char *) ucf->path.data + prefix_len,
                                   ucf->path.len - prefix_len);
        }

        return JS_NewStringLen(ctx, (char *) ucf->path.data, ucf->path.len);
    }

    case 4: /* p3p */
        return JS_NewStringLen(ctx, (char *) ucf->p3p.data, ucf->p3p.len);

    case 5: /* expires — seconds; NGX_HTTP_USERID_MAX_EXPIRES means "max" */
        return JS_NewInt64(ctx, (int64_t) ucf->expires);

    case 6: /* mark — single char string, or "" if unset (NUL) */
    {
        char ch[2];

        ch[0] = (char) ucf->mark;
        ch[1] = '\0';

        return JS_NewString(ctx, ucf->mark ? ch : "");
    }

    } /* switch */

    return JS_UNDEFINED;
}


/*
 * Setter for userid properties.
 *   0 — enable  ("off"/"log"/"v1"/"on")
 *   1 — name    (string → pool dup)
 *   2 — domain  (string → pool dup, stored with "; domain=" prefix)
 *   3 — path    (string → pool dup, stored with "; path=" prefix)
 *   4 — p3p     (string → pool dup)
 *   5 — expires (skip — complex time value)
 *   6 — mark    (string: first char stored, or '\0' for empty)
 */
static JSValue
ngx_js_userid_set(JSContext *ctx, JSValueConst this_val, JSValue val,
    int magic)
{
    ngx_js_userid_opaque_t  *op;
    ngx_http_userid_conf_t  *ucf;
    const char              *s;
    size_t                   slen;
    u_char                  *p;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_userid_class_id);
    if (op == NULL) { return JS_EXCEPTION; }

    ucf = op->ucf;

    switch (magic) {

    case 0: /* enable — "off" / "log" / "v1" / "on" */
    {
        static const struct { const char *n; ngx_uint_t v; } tbl[] = {
            { "off", 0 }, { "log", 1 }, { "v1", 2 }, { "on", 3 }
        };
        ngx_uint_t  i;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }

        for (i = 0; i < countof(tbl); i++) {
            if (ngx_strlen(tbl[i].n) == slen
                && ngx_strncasecmp((u_char *) s, (u_char *) tbl[i].n,
                                   slen) == 0)
            {
                ucf->enable = tbl[i].v;
                JS_FreeCString(ctx, s);
                return JS_UNDEFINED;
            }
        }

        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
                    "enable must be \"off\", \"log\", \"v1\", or \"on\"");
    }

    case 1: /* name */
        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        p = ngx_pnalloc(ngx_cycle->pool, slen + 1);
        if (!p) { JS_FreeCString(ctx, s); return JS_EXCEPTION; }
        ngx_memcpy(p, s, slen);
        p[slen] = '\0';
        JS_FreeCString(ctx, s);
        ucf->name.data = p;
        ucf->name.len  = slen;
        return JS_UNDEFINED;

    case 2: /* domain — stored as "; domain=<value>" */
    {
        static const char   prefix[] = "; domain=";
        static const size_t plen     = sizeof(prefix) - 1;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        p = ngx_pnalloc(ngx_cycle->pool, plen + slen);
        if (!p) { JS_FreeCString(ctx, s); return JS_EXCEPTION; }
        ngx_memcpy(p, prefix, plen);
        ngx_memcpy(p + plen, s, slen);
        JS_FreeCString(ctx, s);
        ucf->domain.data = p;
        ucf->domain.len  = plen + slen;
        return JS_UNDEFINED;
    }

    case 3: /* path — stored as "; path=<value>" */
    {
        static const char   prefix[] = "; path=";
        static const size_t plen     = sizeof(prefix) - 1;

        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        p = ngx_pnalloc(ngx_cycle->pool, plen + slen);
        if (!p) { JS_FreeCString(ctx, s); return JS_EXCEPTION; }
        ngx_memcpy(p, prefix, plen);
        ngx_memcpy(p + plen, s, slen);
        JS_FreeCString(ctx, s);
        ucf->path.data = p;
        ucf->path.len  = plen + slen;
        return JS_UNDEFINED;
    }

    case 4: /* p3p */
        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        p = ngx_pnalloc(ngx_cycle->pool, slen + 1);
        if (!p) { JS_FreeCString(ctx, s); return JS_EXCEPTION; }
        ngx_memcpy(p, s, slen);
        p[slen] = '\0';
        JS_FreeCString(ctx, s);
        ucf->p3p.data = p;
        ucf->p3p.len  = slen;
        return JS_UNDEFINED;

    case 6: /* mark — single char, or '\0' for "" */
        s = JS_ToCStringLen(ctx, &slen, val);
        if (!s) { return JS_EXCEPTION; }
        ucf->mark = (slen > 0) ? (u_char) s[0] : '\0';
        JS_FreeCString(ctx, s);
        return JS_UNDEFINED;

    } /* switch */

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_userid_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("enable",  ngx_js_userid_get, ngx_js_userid_set, 0),
    JS_CGETSET_MAGIC_DEF("name",    ngx_js_userid_get, ngx_js_userid_set, 1),
    JS_CGETSET_MAGIC_DEF("domain",  ngx_js_userid_get, ngx_js_userid_set, 2),
    JS_CGETSET_MAGIC_DEF("path",    ngx_js_userid_get, ngx_js_userid_set, 3),
    JS_CGETSET_MAGIC_DEF("p3p",     ngx_js_userid_get, ngx_js_userid_set, 4),
    JS_CGETSET_MAGIC_DEF("expires", ngx_js_userid_get, NULL,              5),
    JS_CGETSET_MAGIC_DEF("mark",    ngx_js_userid_get, ngx_js_userid_set, 6),
};


ngx_int_t
ngx_js_userid_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_userid_class_id, &ngx_js_userid_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_userid_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_userid_proto_funcs,
                               countof(ngx_js_userid_proto_funcs));

    /* JS_SetClassProto takes ownership — no JS_FreeValue needed */
    JS_SetClassProto(ctx, ngx_js_userid_class_id, proto);
    return NGX_OK;
}


JSValue
ngx_js_wrap_userid(JSContext *ctx, ngx_http_userid_conf_t *ucf)
{
    JSValue                 obj;
    ngx_js_userid_opaque_t *op;

    op = js_mallocz(ctx, sizeof(ngx_js_userid_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->ucf = ucf;

    obj = JS_NewObjectClass(ctx, ngx_js_userid_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}
