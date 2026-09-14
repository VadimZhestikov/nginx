
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
    "sock.mutate",
    /*
     * The budget axis. MANUAL §3.2 named an `E_BUDGET_*` family alongside the
     * refusal codes and [TBD-2] left it empty for want of anything to refuse.
     * Building the mediation showed why it was empty and where it belongs: a
     * budget is exhausted at RUN time, by a gate, which is the denial axis --
     * not at admission. So it is a denial code, and the refusal family stays
     * empty on purpose rather than by omission.
     */
    "budget.uses",
    /*
     * The lifetime axis. A capability may be granted "for the next N seconds",
     * which is what makes a SESSION LEASE bite on authority that has already
     * been handed out: TM-2's mapping expires on its own, but a fragment binds
     * its grants at admission and would otherwise hold them forever.
     */
    "cap.expired",
    /* M-LIB `allowHosts` — the outbound capability's two gates: the glob
       refusing a destination, and the reach gate on the host's drain half. */
    "out.host",
    "out.drain",
    /* M-LIB `window` — a recurring lifetime; the sibling of cap.expired */
    "cap.window",
    /* M-LIB `cosign` — one signature short, not forbidden (see the header) */
    "cap.cosign",
    /* M-LIB `protocol` — out of the declared operation order (see the header) */
    "cap.protocol",
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


/*
 * Switch the EFFECTIVE mode at runtime, without touching the counters.
 *
 * The mode that gates lives in the static below; policy_init() sets it once, at
 * the end of config load.  So comcon.mode() -- which writes jcf->tenant_mode --
 * took effect during the host eval and was SILENTLY INERT afterwards, which is
 * precisely when an operator runs it: audit-first rollout is a live session's
 * verb.  An operator calling enforce() on a running server got "ok" and kept
 * auditing, i.e. kept ALLOWING what they believed they had started denying.
 * Found by M-LIB step 2, because std.ops' shadow()/enforce() read the mode back
 * through the denial report and the two disagreed.
 *
 * The counters are deliberately NOT reset here: switching audit -> enforce must
 * not destroy the audit evidence that justified the switch.  policy_init keeps
 * zeroing them, since a config load is a new cycle.
 *
 * PER PROCESS.  This sets the mode in THIS worker only; the others keep theirs.
 * There is no fleet-wide mode fan-out (it would want the class-F transport, like
 * bindShared), and std.ops reports the scope rather than implying otherwise.
 */
ngx_js_tenant_mode_e
ngx_js_compartment_mode_get(void)
{
    return ngx_js_tenant_mode;
}


void
ngx_js_compartment_mode_set(ngx_js_tenant_mode_e mode)
{
    ngx_js_tenant_mode = mode;
}


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


/* ------------------------------------------------------------------ */
/* COMCON [TBD-2]: the refusal codes (admission-time; see the header)  */
/* ------------------------------------------------------------------ */

/*
 * Index-parallel to ngx_js_refusal_code_t. The NONE row is spelled "" and is
 * not a code: it is the success value, and naming it would put a thirteenth
 * entry in front of every tenant that can never be refused with.
 */
static const char  *ngx_js_refusal_codes[NGX_JS_REFUSAL_LAST] = {
    "",
    "E_ADMIT_ARG",
    "E_ADMIT_NOTBYTECODE",
    "E_ADMIT_SOURCE",
    "E_ADMIT_DYNCODE",
    "E_ADMIT_FREENAME",
    "E_ADMIT_INTRINSIC",
    "E_ADMIT_SCHEMA",
    "E_ADMIT_TEST",
    "E_ADMIT_CONTRACT",
    "E_ADMIT_DEP",
    "E_CAP_GRANT",
    /*
     * The capability layer's own two, thrown in the JS bootstrap rather than
     * here -- which is why they waited a tranche. They are policy outcomes, not
     * argument checks: FLAVOR is the closed vocabulary refusing a word (a typo
     * once meant FULL authority), and ESCALATE is every composition that cannot
     * be SHOWN to narrow -- a meet that widened, a glob or a budget with no
     * computable meet, a realization env that is not a sub-map of the
     * realizer's. One code, because they are one rule.
     */
    "E_CAP_FLAVOR",
    "E_CAP_ESCALATE",
    /*
     * The third, added with `cosign`. It is deliberately NOT folded into
     * E_CAP_FLAVOR: the flavour is known and spelled correctly, and it is not
     * an ESCALATE either -- nothing composed and nothing widened. What happened
     * is that the policy is incoherent on its own terms, and a tenant's CI
     * wants to tell "I typo'd a word" from "I asked for a two-person rule and
     * never said who the people are".
     */
    "E_CAP_PRINCIPAL",
    "E_PIN_IDENTITY",
    "E_EPOCH_STALE",
    "E_INVOKE_PENDING",
};


const char *
ngx_js_refusal_name(ngx_js_refusal_code_t code)
{
    if (code <= NGX_JS_REFUSAL_NONE || code >= NGX_JS_REFUSAL_LAST) {
        return "";
    }

    return ngx_js_refusal_codes[code];
}
