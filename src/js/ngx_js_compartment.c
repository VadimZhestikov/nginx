
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
