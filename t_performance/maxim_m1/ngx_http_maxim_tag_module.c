/* M1 reduced spike: directive-expressible "tag" policy in hand-C.
 * Content handler: read x-tenant request header (default "-"), emit
 * x-tenant-seen response header, send 3-byte "ok" body. No shared state
 * (that is the part pure directives can also do). Enable: maxim_tag on; */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct { ngx_flag_t enable; } ngx_http_maxim_tag_loc_conf_t;
static u_char maxim_tag_body[] = "ok\n";
extern ngx_module_t ngx_http_maxim_tag_module;

static ngx_int_t
ngx_http_maxim_tag_handler(ngx_http_request_t *r)
{
    ngx_list_part_t *part; ngx_table_elt_t *h; ngx_uint_t i;
    ngx_str_t tenant; ngx_int_t rc; ngx_buf_t *b; ngx_chain_t out;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) return rc;

    ngx_str_set(&tenant, "-");
    part = &r->headers_in.headers.part; h = part->elts;
    for (i = 0; ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) break;
            part = part->next; h = part->elts; i = 0;
        }
        if (h[i].key.len == 8
            && ngx_strncasecmp(h[i].key.data, (u_char *) "x-tenant", 8) == 0)
        { tenant = h[i].value; break; }
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    h->hash = 1; ngx_str_set(&h->key, "x-tenant-seen"); h->value = tenant;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(maxim_tag_body) - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_type_len = r->headers_out.content_type.len;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) return rc;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    b->pos = maxim_tag_body; b->last = maxim_tag_body + sizeof(maxim_tag_body) - 1;
    b->memory = 1; b->last_buf = 1; out.buf = b; out.next = NULL;
    return ngx_http_output_filter(r, &out);
}

static void *
ngx_http_maxim_tag_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_maxim_tag_loc_conf_t *c = ngx_pcalloc(cf->pool, sizeof(*c));
    if (c == NULL) return NULL;
    c->enable = NGX_CONF_UNSET;
    return c;
}
static char *
ngx_http_maxim_tag_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_maxim_tag_loc_conf_t *prev = parent, *conf = child;
    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    return NGX_CONF_OK;
}
static char *
ngx_http_maxim_tag_enable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *rv;
    ngx_http_core_loc_conf_t *clcf;
    ngx_http_maxim_tag_loc_conf_t *lcf = conf;
    rv = ngx_conf_set_flag_slot(cf, cmd, conf);
    if (rv != NGX_CONF_OK) return rv;
    if (lcf->enable) {
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = ngx_http_maxim_tag_handler;
    }
    return NGX_CONF_OK;
}
static ngx_command_t ngx_http_maxim_tag_commands[] = {
    { ngx_string("maxim_tag"), NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_maxim_tag_enable, NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_maxim_tag_loc_conf_t, enable), NULL },
    ngx_null_command
};
static ngx_http_module_t ngx_http_maxim_tag_module_ctx = {
    NULL, NULL, NULL, NULL, NULL, NULL,
    ngx_http_maxim_tag_create_loc_conf, ngx_http_maxim_tag_merge_loc_conf
};
ngx_module_t ngx_http_maxim_tag_module = {
    NGX_MODULE_V1, &ngx_http_maxim_tag_module_ctx, ngx_http_maxim_tag_commands,
    NGX_HTTP_MODULE, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};
