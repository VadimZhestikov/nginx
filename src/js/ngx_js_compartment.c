
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
static ngx_flag_t  ngx_js_audit_mode;
static ngx_uint_t  ngx_js_denial_counts[NGX_JS_DENIAL_LAST];
static ngx_uint_t  ngx_js_denials_total;
static ngx_uint_t  ngx_js_denial_records;    /* full records written */


void
ngx_js_compartment_policy_init(ngx_flag_t audit)
{
    ngx_js_audit_mode = audit;
    ngx_js_denials_total = 0;
    ngx_js_denial_records = 0;
    ngx_memzero(ngx_js_denial_counts, sizeof(ngx_js_denial_counts));
}


ngx_flag_t
ngx_js_compartment_denial(ngx_js_denial_code_t code, const char *obj)
{
    const char  *mode;

    ngx_js_denial_counts[code]++;      /* exact, always (TM-1) */
    ngx_js_denials_total++;

    mode = ngx_js_audit_mode ? "audit" : "enforce";

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

    return ngx_js_audit_mode ? 0 : 1;
}


ngx_flag_t
ngx_js_compartment_audit_mode(void)
{
    return ngx_js_audit_mode;
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
