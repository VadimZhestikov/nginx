
/*
 * Copyright (C) nginx JS contributors
 *
 * COMCON compartment identity — increment A, unit A1.0. See the header.
 */

#include "ngx_js_compartment.h"


/*
 * Per-worker current compartment. JS dispatch on a worker runs on the single
 * main event-loop thread, so a plain static is race-free here: the accessor is
 * only ever called from COM getters/handlers on that thread. (Worker /
 * SharedWorker threads have their own runtimes and never read this.)
 */
static ngx_js_compartment_t  ngx_js_cur_compartment
    = NGX_JS_COMPARTMENT_HOST_ROOT;


ngx_js_compartment_t
ngx_js_current_compartment(void)
{
    return ngx_js_cur_compartment;
}


ngx_js_compartment_t
ngx_js_compartment_enter(ngx_js_compartment_t c)
{
    ngx_js_compartment_t  previous;

    previous = ngx_js_cur_compartment;
    ngx_js_cur_compartment = c;

    return previous;
}


void
ngx_js_compartment_leave(ngx_js_compartment_t previous)
{
    ngx_js_cur_compartment = previous;
}


ngx_flag_t
ngx_js_compartment_may_reach(ngx_js_compartment_t owner)
{
    ngx_js_compartment_t  cur;

    cur = ngx_js_cur_compartment;

    return cur == NGX_JS_COMPARTMENT_HOST_ROOT || cur == owner;
}


/* ------------------------------------------------------------------ */
/* COMCON A4: denial log, TM-1 quotas, audit mode                      */
/* ------------------------------------------------------------------ */

static const char  *ngx_js_denial_names[NGX_JS_DENIAL_LAST] = {
    "sock.listener",
    "listener.read",
    "listener.serverByName",
    "enum.sockets",
};

/* Per-process state (single-threaded main loop; see the note above). */
static ngx_js_tenant_mode_e  ngx_js_tenant_mode;
static ngx_uint_t  ngx_js_denial_counts[NGX_JS_DENIAL_LAST];
static ngx_uint_t  ngx_js_denials_total;
static ngx_uint_t  ngx_js_denial_records;    /* full records written */

/* B0: the learning record — distinct harvested access paths, deduped. */
static u_char      ngx_js_learn_paths[NGX_JS_LEARN_MAX][128];
static ngx_uint_t  ngx_js_learn_path_hits[NGX_JS_LEARN_MAX];
static ngx_uint_t  ngx_js_learn_n;


void
ngx_js_compartment_policy_init(ngx_js_tenant_mode_e mode)
{
    ngx_js_tenant_mode = mode;
    ngx_js_denials_total = 0;
    ngx_js_denial_records = 0;
    ngx_memzero(ngx_js_denial_counts, sizeof(ngx_js_denial_counts));

    ngx_js_learn_n = 0;
    ngx_memzero(ngx_js_learn_path_hits, sizeof(ngx_js_learn_path_hits));
}


void
ngx_js_learn_record(const char *path)
{
    size_t      len;
    ngx_uint_t  i;

    for (i = 0; i < ngx_js_learn_n; i++) {
        if (ngx_strcmp(ngx_js_learn_paths[i], path) == 0) {
            ngx_js_learn_path_hits[i]++;
            return;
        }
    }

    if (ngx_js_learn_n >= NGX_JS_LEARN_MAX) {
        return;                          /* bounded; the wishlist is capped */
    }

    len = ngx_strlen(path);
    if (len > sizeof(ngx_js_learn_paths[0]) - 1) {
        len = sizeof(ngx_js_learn_paths[0]) - 1;
    }

    ngx_memcpy(ngx_js_learn_paths[ngx_js_learn_n], path, len);
    ngx_js_learn_paths[ngx_js_learn_n][len] = '\0';
    ngx_js_learn_path_hits[ngx_js_learn_n] = 1;
    ngx_js_learn_n++;

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                  "js learn: comp=%ui wants \"%s\"",
                  (ngx_uint_t) ngx_js_cur_compartment, path);
}


ngx_flag_t
ngx_js_compartment_learn_mode(void)
{
    return ngx_js_tenant_mode == NGX_JS_TENANT_LEARN;
}


ngx_uint_t
ngx_js_learn_count(void)
{
    return ngx_js_learn_n;
}


const char *
ngx_js_learn_path(ngx_uint_t i)
{
    return i < ngx_js_learn_n ? (const char *) ngx_js_learn_paths[i] : "";
}


ngx_uint_t
ngx_js_learn_hits(ngx_uint_t i)
{
    return i < ngx_js_learn_n ? ngx_js_learn_path_hits[i] : 0;
}


const char *
ngx_js_tenant_mode_name(void)
{
    switch (ngx_js_tenant_mode) {
    case NGX_JS_TENANT_LEARN:   return "learn";
    case NGX_JS_TENANT_AUDIT:   return "audit";
    default:                    return "enforce";
    }
}


ngx_flag_t
ngx_js_compartment_denial(ngx_js_denial_code_t code, const char *obj)
{
    const char  *mode;

    ngx_js_denial_counts[code]++;      /* exact, always (TM-1) */
    ngx_js_denials_total++;

    mode = ngx_js_tenant_mode_name();

    if (ngx_js_denial_records < NGX_JS_DENIAL_QUOTA) {
        ngx_js_denial_records++;

        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js denial: comp=%ui op=%s obj=\"%s\" mode=%s n=%ui",
                      (ngx_uint_t) ngx_js_cur_compartment,
                      ngx_js_denial_names[code],
                      obj ? obj : "-", mode, ngx_js_denials_total);

        if (ngx_js_denial_records == NGX_JS_DENIAL_QUOTA) {
            ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                          "js denial: comp=%ui quota exceeded "
                          "(%ui full records); sampling 1/%ui, "
                          "counters stay exact",
                          (ngx_uint_t) ngx_js_cur_compartment,
                          (ngx_uint_t) NGX_JS_DENIAL_QUOTA,
                          (ngx_uint_t) NGX_JS_DENIAL_SAMPLE);
        }

    } else if (ngx_js_denials_total % NGX_JS_DENIAL_SAMPLE == 0) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js denial: comp=%ui op=%s obj=\"%s\" mode=%s n=%ui "
                      "sampled=1",
                      (ngx_uint_t) ngx_js_cur_compartment,
                      ngx_js_denial_names[code],
                      obj ? obj : "-", mode, ngx_js_denials_total);
    }

    /* enforce denies; audit and learn log-and-allow */
    return ngx_js_tenant_mode == NGX_JS_TENANT_ENFORCE ? 1 : 0;
}


ngx_uint_t
ngx_js_compartment_denial_total(void)
{
    return ngx_js_denials_total;
}


ngx_uint_t
ngx_js_compartment_denial_count(ngx_js_denial_code_t code)
{
    return ngx_js_denial_counts[code];
}


const char *
ngx_js_denial_code_name(ngx_js_denial_code_t code)
{
    return ngx_js_denial_names[code];
}
