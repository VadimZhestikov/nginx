/* M1 spike: hand-written C equivalent of a mirror "count + tag" policy.
 * Content handler (parallels the interpreted mirror rule's loc.handler +
 * onRequestHeaders/onResponseHeaders): atomic-incr a shared counter, read the
 * x-tenant request header, emit x-count + x-tenant-seen response headers, send
 * a 3-byte "ok" body. Enable per-location: maxim_spike on; */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct { ngx_flag_t enable; } ngx_http_maxim_loc_conf_t;

static ngx_atomic_t *maxim_counter;
static ngx_int_t maxim_zone_added;
static u_char maxim_body[] = "ok\n";

extern ngx_module_t ngx_http_maxim_spike_module;

static ngx_int_t
ngx_http_maxim_init_zone(ngx_shm_zone_t *zone, void *data)
{
    ngx_slab_pool_t *sp;
    if (data) { maxim_counter = (ngx_atomic_t *) data; return NGX_OK; }
    sp = (ngx_slab_pool_t *) zone->shm.addr;
    maxim_counter = ngx_slab_alloc(sp, sizeof(ngx_atomic_t));
    if (maxim_counter == NULL) return NGX_ERROR;
    *maxim_counter = 0;
    zone->data = (void *) maxim_counter;
    return NGX_OK;
}

static ngx_int_t
ngx_http_maxim_handler(ngx_http_request_t *r)
{
    ngx_list_part_t *part;
    ngx_table_elt_t *h;
    ngx_uint_t i, count;
    ngx_str_t tenant;
    u_char *p;
    ngx_int_t rc;
    ngx_buf_t *b;
    ngx_chain_t out;

    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) return rc;

    count = (ngx_uint_t) ngx_atomic_fetch_add(maxim_counter, 1) + 1;

    ngx_str_set(&tenant, "-");
    part = &r->headers_in.headers.part;
    h = part->elts;
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
    h->hash = 1; ngx_str_set(&h->key, "x-count");
    p = ngx_pnalloc(r->pool, NGX_INT_T_LEN);
    if (p == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    h->value.len = ngx_sprintf(p, "%ui", count) - p;
    h->value.data = p;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    h->hash = 1; ngx_str_set(&h->key, "x-tenant-seen");
    h->value = tenant;

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = sizeof(maxim_body) - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    r->headers_out.content_type_len = r->headers_out.content_type.len;

    rc = ngx_http_send_header(r);
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) return rc;

    b = ngx_calloc_buf(r->pool);
    if (b == NULL) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    b->pos = maxim_body;
    b->last = maxim_body + sizeof(maxim_body) - 1;
    b->memory = 1;
    b->last_buf = 1;
    out.buf = b; out.next = NULL;
    return ngx_http_output_filter(r, &out);
}

static void *
ngx_http_maxim_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_maxim_loc_conf_t *c = ngx_pcalloc(cf->pool, sizeof(*c));
    if (c == NULL) return NULL;
    c->enable = NGX_CONF_UNSET;
    return c;
}

static char *
ngx_http_maxim_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_maxim_loc_conf_t *prev = parent, *conf = child;
    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    return NGX_CONF_OK;
}

static char *
ngx_http_maxim_enable(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *rv;
    ngx_shm_zone_t *zone;
    ngx_http_core_loc_conf_t *clcf;
    ngx_http_maxim_loc_conf_t *lcf = conf;
    ngx_str_t name = ngx_string("maxim_spike_zone");

    rv = ngx_conf_set_flag_slot(cf, cmd, conf);
    if (rv != NGX_CONF_OK) return rv;

    if (lcf->enable) {
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = ngx_http_maxim_handler;
    }
    if (!maxim_zone_added) {
        zone = ngx_shared_memory_add(cf, &name, 16 * 1024,
                                     &ngx_http_maxim_spike_module);
        if (zone == NULL) return NGX_CONF_ERROR;
        zone->init = ngx_http_maxim_init_zone;
        maxim_zone_added = 1;
    }
    return NGX_CONF_OK;
}

static ngx_command_t ngx_http_maxim_commands[] = {
    { ngx_string("maxim_spike"), NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_maxim_enable, NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_maxim_loc_conf_t, enable), NULL },
    ngx_null_command
};

static ngx_http_module_t ngx_http_maxim_module_ctx = {
    NULL, NULL, NULL, NULL, NULL, NULL,
    ngx_http_maxim_create_loc_conf, ngx_http_maxim_merge_loc_conf
};

ngx_module_t ngx_http_maxim_spike_module = {
    NGX_MODULE_V1, &ngx_http_maxim_module_ctx, ngx_http_maxim_commands,
    NGX_HTTP_MODULE, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NGX_MODULE_V1_PADDING
};
