
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


#endif /* _NGX_JS_COMPARTMENT_H_INCLUDED_ */
