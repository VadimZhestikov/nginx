#!/usr/bin/perl

# Tests for Atomics.wait/notify between JS Worker threads.
#
# Three scenarios:
#
#   /jw_worker_to_worker/
#       Worker A creates a fresh SAB (memfd-backed, post-fork), posts it to
#       the main thread, then calls Atomics.wait(arr, 0, 0, 5000).
#       Main receives the SAB and forwards it to Worker B.
#       Worker B stores 42 in arr[0] and calls Atomics.notify(arr, 0, 1).
#       Worker A wakes ("ok" or "not-equal"), reads arr[0] = 42, echoes it.
#       Both Workers are pthreads inside the same nginx worker process.
#
#   /jw_event_loop_notify/
#       Worker creates a SAB, posts it to main, then calls Atomics.wait.
#       Main (event loop) receives the SAB, stores 99 in arr[0], and calls
#       Atomics.notify via nginx.setTimeout(0).
#       Worker wakes, reads arr[0] = 99, echoes it.
#       Demonstrates that the nginx event loop can notify a waiting Worker.
#
#   /jw_prefork_atomics/
#       Pre-fork SAB: created before fork so all nginx worker processes share
#       the same physical pages (MAP_SHARED|MAP_ANONYMOUS, same VA everywhere).
#       Main spawns a Worker, posts the pre-fork SAB to it.
#       Worker calls Atomics.wait(arr, 0, 0, 5000).
#       Main stores arr[1]=77, sets arr[0]=1, and calls Atomics.notify.
#       Worker wakes ("ok" or "not-equal"), reads arr[1] = 77, echoes it.
#       Main resets arr[0]=0 for the next request.
#       With worker_processes > 1 a notify from any worker process would wake
#       a waiter in any other worker process: the Linux futex uses physical
#       page identity (FUTEX_WAIT without _PRIVATE) rather than virtual addr.
#
# Atomics.wait semantics:
#   "ok"        — notified while blocking (arr matched expected)
#   "not-equal" — arr was already != expected when wait was called (also ok)
#   "timed-out" — 5-second timeout expired; Worker posts -1 (must not happen)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $dir = $t->testdir();

# ---- Worker A (waiter, Worker-to-Worker scenario) ----
#
# Creates a SAB, posts it to main so main can pass it to Worker B, then
# blocks in Atomics.wait.  On wake echoes arr[0]; on timeout echoes -1.

$t->write_file('jwa_waiter.js', <<'JS');
onmessage = function(e) {
    var sab  = new SharedArrayBuffer(8);
    var arr  = new Int32Array(sab);
    arr[0] = 0;
    postMessage(sab);                      /* hand SAB to main */
    var res = Atomics.wait(arr, 0, 0, 5000);
    if (res === 'timed-out') { postMessage(-1); return; }
    postMessage(Atomics.load(arr, 0));     /* echo the stored value */
};
JS

# ---- Worker B (notifier, Worker-to-Worker scenario) ----
#
# Receives a SAB; stores 42 in arr[0] and notifies one waiter.

$t->write_file('jwa_notifier.js', <<'JS');
onmessage = function(e) {
    var arr = new Int32Array(e.data);
    Atomics.store(arr, 0, 42);
    Atomics.notify(arr, 0, 1);
};
JS

# ---- Worker (event-loop notify scenario) ----
#
# Creates a SAB with arr[0]=0, posts it to main, then blocks in Atomics.wait.
# On wake echoes arr[0]; on timeout echoes -1.

$t->write_file('jwa_el_waiter.js', <<'JS');
onmessage = function(e) {
    var sab  = new SharedArrayBuffer(4);
    var arr  = new Int32Array(sab);
    arr[0] = 0;
    postMessage(sab);                      /* send SAB to event loop */
    var res = Atomics.wait(arr, 0, 0, 5000);
    if (res === 'timed-out') { postMessage(-1); return; }
    postMessage(Atomics.load(arr, 0));
};
JS

# ---- Worker (pre-fork SAB waiter) ----
#
# Receives a pre-fork SAB; calls Atomics.wait on arr[0] (expected 0).
# On wake echoes arr[1]; on timeout echoes -1.

$t->write_file('jwa_pf_waiter.js', <<'JS');
onmessage = function(e) {
    var arr = new Int32Array(e.data);
    var res = Atomics.wait(arr, 0, 0, 5000);
    if (res === 'timed-out') { postMessage(-1); return; }
    postMessage(Atomics.load(arr, 1));
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_jw_atomics.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /jw_worker_to_worker/  { }
        location /jw_event_loop_notify/ { }
        location /jw_prefork_atomics/   { }
    }
}
EOF

# ---- init_jw_atomics.js ----

$t->write_file('init_jw_atomics.js', <<"JS");
/*
 * Pre-fork SAB used by /jw_prefork_atomics/.
 * arr[0] = signal (0→1 triggers wake; reset to 0 after each request).
 * arr[1] = value the Worker echoes after waking.
 */
var preForkSab = new SharedArrayBuffer(8);
var preForkArr = new Int32Array(preForkSab);
preForkArr[0] = 0;
preForkArr[1] = 0;

(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /jw_worker_to_worker/
     *
     * Worker A creates a fresh SAB (memfd-backed post-fork), posts it,
     * then blocks in Atomics.wait(arr, 0, 0, 5000).
     * Main receives the SAB and spawns Worker B with it.
     * Worker B stores 42 in arr[0] and calls Atomics.notify.
     * Worker A wakes ("ok" or "not-equal"), reads arr[0] = 42, posts 42.
     *
     * Both Workers are pthreads in the same nginx worker process.
     * The race between Worker B's store/notify and Worker A's wait is
     * harmless: if Worker B runs first, Atomics.wait returns "not-equal"
     * and arr[0] is still 42.
     */
    set('/jw_worker_to_worker/', async function(req) {
        var result = await new Promise(function(resolve) {
            var wa = new Worker('$dir/jwa_waiter.js');
            var wb;

            wa.onmessage = function(e) {
                if (e.data instanceof SharedArrayBuffer) {
                    /* Worker A handed us the SAB — start the notifier */
                    wb = new Worker('$dir/jwa_notifier.js');
                    wb.postMessage(e.data);
                } else {
                    /* Worker A sent us the echoed value */
                    wa.terminate();
                    if (wb) { wb.terminate(); }
                    resolve(e.data);
                }
            };

            wa.postMessage(null);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_event_loop_notify/
     *
     * Worker creates a SAB, posts it to the event loop, then blocks in
     * Atomics.wait(arr, 0, 0, 5000).
     * Event loop receives the SAB, schedules a setTimeout(0) to store
     * arr[0]=99 and notify, ensuring the Worker is already waiting.
     * Worker wakes, reads arr[0] = 99, posts 99 to main.
     *
     * JS Worker threads run with JS_SetCanBlock(rt, TRUE) so Atomics.wait
     * is allowed in them; the nginx event loop may only call Atomics.notify
     * (non-blocking from the event-loop perspective).
     */
    set('/jw_event_loop_notify/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jwa_el_waiter.js');

            w.onmessage = function(e) {
                if (e.data instanceof SharedArrayBuffer) {
                    var arr = new Int32Array(e.data);
                    /* Defer the notify so the Worker has time to call wait */
                    nginx.setTimeout(0).then(function() {
                        Atomics.store(arr, 0, 99);
                        Atomics.notify(arr, 0, 1);
                    });
                } else {
                    w.terminate();
                    resolve(e.data);
                }
            };

            w.postMessage(null);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_prefork_atomics/
     *
     * Main posts the pre-fork SAB to a freshly-spawned Worker.
     * Worker calls Atomics.wait(arr, 0, 0, 5000).
     * Main stores arr[1]=77, sets arr[0]=1, calls Atomics.notify.
     * Worker wakes ("ok" or "not-equal"), reads arr[1] = 77, posts 77.
     * Main resets arr[0]=0 for the next request.
     *
     * The pre-fork SAB (MAP_SHARED|MAP_ANONYMOUS) has the same physical pages
     * in all nginx worker processes.  A notify from any worker process wakes
     * a waiter in any other worker process: Linux futex uses physical-page
     * identity as the key in non-private (cross-process) mode.
     */
    set('/jw_prefork_atomics/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jwa_pf_waiter.js');

            w.onmessage = function(e) {
                w.terminate();
                Atomics.store(preForkArr, 0, 0);    /* reset for next request */
                resolve(e.data);
            };

            w.postMessage(preForkSab);              /* Worker waits on this */
            Atomics.store(preForkArr, 1, 77);       /* value to echo */
            Atomics.store(preForkArr, 0, 1);        /* signal: 0 → 1 */
            Atomics.notify(preForkArr, 0, 1);       /* wake one waiter */
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(9);

# ---- HTTP assertions ----

like(http_get('/jw_worker_to_worker/'),  qr/200 OK/, 'Worker-to-Worker Atomics: 200');
like(http_get('/jw_worker_to_worker/'),  qr/42/,     'Worker-to-Worker Atomics: echoes 42');
like(http_get('/jw_worker_to_worker/'),  qr/42/,     'Worker-to-Worker Atomics: repeated request');

like(http_get('/jw_event_loop_notify/'), qr/200 OK/, 'event-loop notifies Worker: 200');
like(http_get('/jw_event_loop_notify/'), qr/99/,     'event-loop notifies Worker: echoes 99');
like(http_get('/jw_event_loop_notify/'), qr/99/,     'event-loop notifies Worker: repeated request');

like(http_get('/jw_prefork_atomics/'),   qr/200 OK/, 'prefork SAB Worker Atomics: 200');
like(http_get('/jw_prefork_atomics/'),   qr/77/,     'prefork SAB Worker Atomics: echoes 77');
like(http_get('/jw_prefork_atomics/'),   qr/77/,     'prefork SAB Worker Atomics: repeated request');
