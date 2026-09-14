
/*
 * Copyright (C) nginx JS contributors
 *
 * COMCON compartment identity — increment A, unit A1.0 (the seam).
 *
 * A "compartment" is the owner of the objects and registry entries a fragment
 * creates: the host root, or a confined tenant fragment. It is the identity
 * that deny-by-default environments (the primary control) and the process-wide
 * handle-registry owner checks (defense-in-depth) are keyed on.
 *
 * Today there is exactly one compartment — NGX_JS_COMPARTMENT_HOST_ROOT — and
 * every accessor returns it, so behaviour is byte-identical to pre-COMCON
 * nginx. The value only diverges when per-tenant environments arrive (A2), at
 * which point the owner checks (A1.1) and the reduced tenant environments begin
 * to isolate for real. This file exists so that the many creation and lookup
 * sites can be wired to the token now, once, while it is a no-op.
 *
 * See js_comcon/docs-v5.0/INCREMENT_A.md and SPEC.md.
 */

#ifndef _NGX_JS_COMPARTMENT_H_INCLUDED_
#define _NGX_JS_COMPARTMENT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_js_compartment_t;

#define NGX_JS_COMPARTMENT_HOST_ROOT   ((ngx_js_compartment_t) 0)

/* The single MVP tenant compartment (increment A). Multi-tenant = later. */
#define NGX_JS_COMPARTMENT_TENANT      ((ngx_js_compartment_t) 1)


/*
 * The compartment currently executing on this worker's main event loop.
 * HOST_ROOT unless a confined fragment dispatch is in progress. It is set and
 * cleared around a handler's JS_Call, exactly as the per-request execution
 * deadline already is (ngx_js_http_module.c content handler). Worker /
 * SharedWorker pthreads run separate runtimes and never touch this state.
 */
ngx_js_compartment_t ngx_js_current_compartment(void);


/*
 * Enter a compartment for the duration of a dispatch; returns the previous
 * compartment so the caller restores it on leave. Dispatches nest (a handler
 * may drive a subrequest handler), so callers must save/restore, not assume
 * HOST_ROOT on leave.
 */
ngx_js_compartment_t ngx_js_compartment_enter(ngx_js_compartment_t c);
void ngx_js_compartment_leave(ngx_js_compartment_t previous);


/*
 * Reach check (defense-in-depth for a leaked handle): may the compartment
 * currently executing reach an object/registry entry owned by `owner`?
 *
 * Rule: HOST_ROOT reaches everything; any other compartment reaches only what
 * it owns. Today the current compartment is always HOST_ROOT, so this always
 * returns true and behaviour is unchanged; it begins to isolate once confined
 * fragments run (A2). Front-line control remains the deny-by-default
 * environment (a tenant should never hold the handle in the first place);
 * this guards the case where one leaks.
 */
ngx_flag_t ngx_js_compartment_may_reach(ngx_js_compartment_t owner);


/*
 * COMCON A4: the denial log + the audit→enforce loop.
 *
 * A denial event fires where a REACH GATE denies (name-level denials are
 * structural — the withheld name simply does not exist in the tenant
 * environment — that is the primary control working silently; the gates are
 * the observable layer). Per TM-1 (THREATS.md): counters are EXACT per code,
 * always; full log records are written up to a quota; above quota, records
 * are sampled; quota-exceeded is itself reported (once). This keeps a tenant
 * looping on a denied operation from exhausting disk or drowning the audit
 * signal while losing no counting precision.
 *
 * Audit mode (js_tenant_mode audit;): every gate consults
 * ngx_js_compartment_denial() — in audit mode it logs the event and ALLOWS,
 * so an operator observes the full would-be-denied reach before enforcing.
 */

typedef enum {
    NGX_JS_DENIAL_SOCK_LISTENER = 0,   /* sock.listener reach edge          */
    NGX_JS_DENIAL_LISTENER_READ,       /* listener getter (socket/serverNames) */
    NGX_JS_DENIAL_SERVER_BY_NAME,      /* listener.serverByName escalation  */
    NGX_JS_DENIAL_ENUM_SOCKETS,        /* cycle.sockets / http.sockets enum */
    NGX_JS_DENIAL_SOCK_MUTATE,         /* sock.close / sock.broadcast (SR-1)  */
    NGX_JS_DENIAL_BUDGET_USES,         /* a `uses` budget is exhausted (M-LIB) */
    NGX_JS_DENIAL_CAP_EXPIRED,         /* a `ttl` capability lifetime has passed  */
    /*
     * M-LIB `allowHosts`: the outbound capability.  `out.host` is the glob
     * refusing a destination -- the mediation biting.  `out.drain` is the A1
     * reach gate: reading or clearing the recorded intents is the HOST's half
     * of this capability, and a fragment that could drain the queue would read
     * what a sibling fragment sharing the same cap had recorded, which is a
     * channel rather than an outbound request.
     */
    NGX_JS_DENIAL_OUT_HOST,            /* destination outside allowHosts()       */
    NGX_JS_DENIAL_OUT_DRAIN,           /* pending()/clear() from a compartment   */
    /*
     * M-LIB `window`: a RECURRING lifetime -- office hours rather than an
     * absolute expiry.  `ttl` and `window` are siblings and deliberately
     * separate codes: "your capability has run out" and "your capability is
     * outside its hours" are different operational facts, and an operator paged
     * at 02:00 needs to know which one they are looking at.
     */
    NGX_JS_DENIAL_CAP_WINDOW,          /* outside an allowed time-of-day window  */
    NGX_JS_DENIAL_LAST
} ngx_js_denial_code_t;

#define NGX_JS_DENIAL_QUOTA    100     /* full records per process          */
#define NGX_JS_DENIAL_SAMPLE   100     /* above quota: log every Nth event  */


/*
 * COMCON [TBD-2]: the REFUSAL codes — the other half of MANUAL §3.2's promise.
 *
 * Two axes, deliberately not merged. A DENIAL code (above) names the gate that
 * fired while a fragment was RUNNING; a REFUSAL code names why a fragment was
 * never admitted in the first place. A tenant's CI needs both, and needs them
 * to be different things: "my policy tripped sock.listener at request 41" and
 * "my policy will not load at all" are different failures with different fixes.
 *
 * Until this enum existed, every admission refusal was MESSAGE TEXT. MANUAL
 * §3.2 tells tenants "pin your CI to codes, not to message text" and then left
 * them nothing to pin to for the whole admission surface — the gap V12 dated
 * (t/tools/golden-denials.js). The code travels two ways: bracketed in the
 * thrown message (so the audit trail is greppable) and as `.code` on the Error
 * object (so a deny-suite reads it without parsing prose). `admit()`'s verdict
 * object carries it as `code` beside `reject`.
 *
 * CLOSED AND FROZEN. Appending is allowed; renaming or renumbering is the
 * breakage this exists to prevent. Every code here must appear in the V12
 * golden corpus with a probe or a written reason it is unreachable —
 * check [5] of t/tools/check-enumerations.py fails otherwise.
 *
 * NOT every throw gets a code: a host fault ("no conf", "compartment failed",
 * "no compartment") is not a refusal — nothing the tenant wrote caused it and
 * there is nothing for them to fix, so coding it would invite a deny-suite to
 * assert on our bugs. MANUAL's E_BUDGET_* family has no member here yet for a
 * related reason: the deadline abort is the engine's interrupt, which carries
 * no refusal of ours to label. Recorded rather than invented.
 */

typedef enum {
    NGX_JS_REFUSAL_NONE = 0,           /* no refusal (success)              */
    NGX_JS_REFUSAL_ADMIT_ARG,          /* arg0 is not a function            */
    NGX_JS_REFUSAL_ADMIT_NOTBYTECODE,  /* a function, but not bytecode      */
    NGX_JS_REFUSAL_ADMIT_SOURCE,       /* include: source is not a function expr */
    NGX_JS_REFUSAL_ADMIT_DYNCODE,      /* C3: direct eval / with            */
    NGX_JS_REFUSAL_ADMIT_FREENAME,     /* C3: free name not in `imports`    */
    NGX_JS_REFUSAL_ADMIT_INTRINSIC,    /* `intrinsics` names a non-intrinsic */
    NGX_JS_REFUSAL_ADMIT_SCHEMA,       /* C3: request field outside the seal */
    NGX_JS_REFUSAL_ADMIT_TEST,         /* a contract test threw             */
    NGX_JS_REFUSAL_ADMIT_CONTRACT,     /* a contract field is present but unusable */
    NGX_JS_REFUSAL_ADMIT_DEP,          /* a pinned dependency failed to load */
    NGX_JS_REFUSAL_CAP_GRANT,          /* a grant is not a mediatable cap   */
    NGX_JS_REFUSAL_CAP_FLAVOR,         /* a mediation flavor outside the closed set */
    NGX_JS_REFUSAL_CAP_ESCALATE,       /* a composition that cannot be shown to narrow */
    NGX_JS_REFUSAL_PIN_IDENTITY,       /* artifact identity pin mismatch    */
    NGX_JS_REFUSAL_EPOCH_STALE,        /* the fragment was freed (old epoch) */
    NGX_JS_REFUSAL_LAST
} ngx_js_refusal_code_t;

const char *ngx_js_refusal_name(ngx_js_refusal_code_t code);


/*
 * Tenant enforcement mode (A4 + B0):
 *   ENFORCE — gates deny (default).
 *   AUDIT   — gates log-and-allow (observe-then-enforce).
 *   LEARN   — audit + the withheld host surface is recorded (onboarding
 *             harvest): references to ungranted host names are captured into
 *             the learning record instead of failing, so the operator sees
 *             what the fragment wants and grants the safe subset.
 */
typedef enum {
    NGX_JS_TENANT_ENFORCE = 0,
    NGX_JS_TENANT_AUDIT,
    NGX_JS_TENANT_LEARN
} ngx_js_tenant_mode_e;

#define NGX_JS_LEARN_MAX       64      /* distinct harvested paths / process */


/*
 * Reset counters + learning record and set the mode for this cycle (called
 * from init_conf before the tenant evaluates; workers inherit by fork).
 */
void ngx_js_compartment_policy_init(ngx_js_tenant_mode_e mode);
/* Runtime mode switch (audit-first rollout), counters preserved, PER PROCESS.
 * policy_init only runs at config load, so without this comcon.mode() was
 * silently inert at request time -- see the definition. */
void ngx_js_compartment_mode_set(ngx_js_tenant_mode_e mode);

/* B0: record a harvested access path (deny-by-default wishlist entry). */
void         ngx_js_learn_record(const char *path);
ngx_flag_t   ngx_js_compartment_learn_mode(void);
ngx_uint_t   ngx_js_learn_count(void);
const char  *ngx_js_learn_path(ngx_uint_t i);
ngx_uint_t   ngx_js_learn_hits(ngx_uint_t i);
const char  *ngx_js_tenant_mode_name(void);

/*
 * Record a denial event at a gate. Returns 1 = DENY (enforce mode: the gate
 * must refuse), 0 = ALLOW (audit mode: log only, let it through). `obj` is a
 * short object identifier for the record (e.g. the socket address), or NULL.
 */
ngx_flag_t ngx_js_compartment_denial(ngx_js_denial_code_t code,
    const char *obj);

/* Introspection for the host-side report (nginx.tenantDenials()). */
ngx_uint_t   ngx_js_compartment_denial_total(void);
ngx_uint_t   ngx_js_compartment_denial_count(ngx_js_denial_code_t code);
const char  *ngx_js_denial_code_name(ngx_js_denial_code_t code);


#endif /* _NGX_JS_COMPARTMENT_H_INCLUDED_ */
