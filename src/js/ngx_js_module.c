
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_module — NGX_CORE_MODULE that embeds QuickJS into NGINX.
 *
 * Lifecycle:
 *   create_conf  — allocate ngx_js_conf_t in cycle->pool
 *   [ngx_conf_parse runs; js_include directives populate jcf->includes]
 *   init_conf    — create JSRuntime/JSContext, install COM, eval scripts
 *   init_process — each worker creates its own independent runtime
 *   exit_process — free worker runtime
 *   exit_master  — free master runtime
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_js.h"


static void *ngx_js_create_conf(ngx_cycle_t *cycle);
static char *ngx_js_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_js_init_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_master(ngx_cycle_t *cycle);

static char *ngx_js_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static u_char *ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path,
    size_t *len);


static ngx_command_t  ngx_js_commands[] = {

    /*
     * js_include /path/to/script.js;
     *
     * Loads and evaluates a JavaScript file once the full nginx.conf
     * has been parsed (in init_conf).  Relative paths are resolved
     * against the directory of nginx.conf.  Multiple directives are
     * executed in declaration order.
     */
    { ngx_string("js_include"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_js_include,
      0,
      0,
      NULL },

    ngx_null_command
};


static ngx_core_module_t  ngx_js_module_ctx = {
    ngx_string("js"),
    ngx_js_create_conf,
    ngx_js_init_conf
};


ngx_module_t  ngx_js_module = {
    NGX_MODULE_V1,
    &ngx_js_module_ctx,                /* module context */
    ngx_js_commands,                   /* module directives */
    NGX_CORE_MODULE,                   /* module type */
    NULL,                              /* init master */
    NULL,                              /* init module */
    ngx_js_init_process,               /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    ngx_js_exit_process,               /* exit process */
    ngx_js_exit_master,                /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_js_create_conf(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = ngx_pcalloc(cycle->pool, sizeof(ngx_js_conf_t));
    if (jcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&jcf->includes, cycle->pool, 4,
                       sizeof(ngx_str_t)) != NGX_OK)
    {
        return NULL;
    }

    /* rt, ctx, worker are NULL after pcalloc */

    return jcf;
}


static char *
ngx_js_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_str_t      *value, *path;

    value = cf->args->elts;    /* value[0] = "js_include", value[1] = path */

    path = ngx_array_push(&jcf->includes);
    if (path == NULL) {
        return NGX_CONF_ERROR;
    }

    *path = value[1];

    /* Resolve relative path against the directory of nginx.conf */
    if (ngx_conf_full_name(cf->cycle, path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*
 * ngx_js_init_conf — called after ngx_conf_parse() completes.
 *
 * At this point every server{}, location{}, upstream{} in nginx.conf
 * has been parsed and its C config structs are fully populated.  We
 * create the master QuickJS runtime, install the COM, and evaluate
 * every js_include file.  Any mutations JS makes to COM objects
 * (Phases 2+) write directly into cycle-pool memory and are visible
 * to worker processes after fork().
 */
static char *
ngx_js_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_uint_t      i;
    ngx_str_t      *path;
    u_char         *src;
    size_t          src_len;
    JSValue         result;

    if (jcf->includes.nelts == 0) {
        return NGX_CONF_OK;    /* nothing to do — pure static config */
    }

    /* ---- Create the master QuickJS runtime ---- */

    jcf->rt = JS_NewRuntime();
    if (jcf->rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    /* Limit memory to 64 MB for the config-phase runtime */
    JS_SetMemoryLimit(jcf->rt, 64 * 1024 * 1024);

    jcf->ctx = JS_NewContext(jcf->rt);
    if (jcf->ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewContext() failed");
        goto failed_rt;
    }

    /* ---- Install nginx.* COM namespace ---- */

    if (ngx_js_com_init(jcf->ctx, cycle) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: COM initialisation failed");
        goto failed_ctx;
    }

    /* ---- Evaluate each js_include file in declaration order ---- */

    path = jcf->includes.elts;

    for (i = 0; i < jcf->includes.nelts; i++) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "js: executing \"%V\"", &path[i]);

        src = ngx_js_read_file(cycle, &path[i], &src_len);
        if (src == NULL) {
            goto failed_ctx;
        }

        result = JS_Eval(jcf->ctx,
                         (const char *) src,
                         src_len,
                         (const char *) path[i].data,
                         JS_EVAL_TYPE_GLOBAL);

        if (JS_IsException(result)) {
            ngx_js_log_exception(jcf->ctx, cycle->log);
            JS_FreeValue(jcf->ctx, result);
            goto failed_ctx;
        }

        JS_FreeValue(jcf->ctx, result);
    }

    /*
     * The master runtime stays alive until exit_master() so that
     * COM objects remain valid during the lifetime of the master
     * process.  Worker processes create their own runtimes in
     * init_process() and must not use this one.
     */

    return NGX_CONF_OK;

failed_ctx:
    JS_FreeContext(jcf->ctx);
    jcf->ctx = NULL;

failed_rt:
    JS_FreeRuntime(jcf->rt);
    jcf->rt = NULL;

    return NGX_CONF_ERROR;
}


/*
 * ngx_js_init_process — called in each worker after fork().
 *
 * Workers get their own independent JSRuntime.  They re-evaluate
 * all js_include scripts so that any globally-registered handler
 * functions (Phase 4) are available.  The COM installed here points
 * to the same cycle->conf_ctx (read-only in Phase 1).
 */
static ngx_int_t
ngx_js_init_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t   *jcf;
    ngx_js_worker_t *w;
    ngx_uint_t       i;
    ngx_str_t       *path;
    u_char          *src;
    size_t           src_len;
    JSValue          result;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    if (jcf->includes.nelts == 0) {
        return NGX_OK;
    }

    w = ngx_pcalloc(cycle->pool, sizeof(ngx_js_worker_t));
    if (w == NULL) {
        return NGX_ERROR;
    }

    w->rt = JS_NewRuntime();
    if (w->rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: worker JS_NewRuntime() failed");
        return NGX_ERROR;
    }

    JS_SetMemoryLimit(w->rt, 32 * 1024 * 1024);

    w->ctx = JS_NewContext(w->rt);
    if (w->ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: worker JS_NewContext() failed");
        JS_FreeRuntime(w->rt);
        return NGX_ERROR;
    }

    if (ngx_js_com_init(w->ctx, cycle) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: worker COM initialisation failed");
        JS_FreeContext(w->ctx);
        JS_FreeRuntime(w->rt);
        return NGX_ERROR;
    }

    path = jcf->includes.elts;

    for (i = 0; i < jcf->includes.nelts; i++) {
        src = ngx_js_read_file(cycle, &path[i], &src_len);
        if (src == NULL) {
            JS_FreeContext(w->ctx);
            JS_FreeRuntime(w->rt);
            return NGX_ERROR;
        }

        result = JS_Eval(w->ctx,
                         (const char *) src,
                         src_len,
                         (const char *) path[i].data,
                         JS_EVAL_TYPE_GLOBAL);

        if (JS_IsException(result)) {
            ngx_js_log_exception(w->ctx, cycle->log);
            JS_FreeValue(w->ctx, result);
            JS_FreeContext(w->ctx);
            JS_FreeRuntime(w->rt);
            return NGX_ERROR;
        }

        JS_FreeValue(w->ctx, result);
    }

    jcf->worker = w;

    return NGX_OK;
}


static void
ngx_js_exit_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t   *jcf;
    ngx_js_worker_t *w;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    w = jcf->worker;
    if (w == NULL) {
        return;
    }

    if (w->ctx) {
        JS_FreeContext(w->ctx);
        w->ctx = NULL;
    }

    if (w->rt) {
        JS_FreeRuntime(w->rt);
        w->rt = NULL;
    }
}


static void
ngx_js_exit_master(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    if (jcf->ctx) {
        JS_FreeContext(jcf->ctx);
        jcf->ctx = NULL;
    }

    if (jcf->rt) {
        JS_FreeRuntime(jcf->rt);
        jcf->rt = NULL;
    }
}


/*
 * Read an entire file into a NUL-terminated cycle->pool buffer.
 * Returns the buffer on success, NULL on error.
 */
static u_char *
ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path, size_t *out_len)
{
    ngx_fd_t         fd;
    ngx_file_t       file;
    ngx_file_info_t  fi;
    u_char          *buf;
    size_t           size;
    ssize_t          n;

    fd = ngx_open_file(path->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_open_file_n " \"%V\" failed", path);
        return NULL;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.fd   = fd;
    file.name = *path;
    file.log  = cycle->log;

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_fd_info_n " \"%V\" failed", path);
        ngx_close_file(fd);
        return NULL;
    }

    size = ngx_file_size(&fi);

    buf = ngx_palloc(cycle->pool, size + 1);
    if (buf == NULL) {
        ngx_close_file(fd);
        return NULL;
    }

    n = ngx_read_file(&file, buf, size, 0);

    ngx_close_file(fd);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: read \"%V\" failed", path);
        return NULL;
    }

    buf[n]   = '\0';
    *out_len = (size_t) n;

    return buf;
}
