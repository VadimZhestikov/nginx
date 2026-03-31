#!/usr/bin/perl

# Tests for Atomics.wait/notify between nginx workers and *dynamic*
# SharedWorkers (created on demand from request handlers, not init_conf).
#
# Dynamic SWs use the wake pipe path fixed by the wake-pipe gap fix.
# This file ensures Atomics-based synchronisation works correctly on that
# path, mirroring js_sw_atomics.t for static SWs.
#
# The SW thread calls JS_SetCanBlock(rt, TRUE) so Atomics.wait() works
# there.  The nginx worker (event-loop) uses only Atomics.notify().
#
# Routes:
#   /dyn_prefork_atomics/  — pre-fork SAB (same VA in master and worker);
#                            worker notifies → dynamic SW wakes → echoes arr[1]
#   /dyn_worker_atomics/   — worker-created SAB (memfd, different VAs, same
#                            physical pages); same notify/wait/echo flow

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

# ---- SharedWorker script ----
#
# Receives a SAB.  Waits on arr[0] (expecting 0); when woken reads arr[1];
# resets arr[0] to 0 for reuse; echoes arr[1] back via postMessage.

$t->write_file('sw_dyn_wait.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var arr = new Int32Array(msg.data);
        var res = Atomics.wait(arr, 0, 0, 5000);
        if (res === 'timed-out') {
            port.postMessage(-1);
            return;
        }
        var val = Atomics.load(arr, 1);
        Atomics.store(arr, 0, 0);   /* reset for next use */
        port.postMessage(val);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_dyn_atomics.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /dyn_prefork_atomics/ { }
        location /dyn_worker_atomics/  { }
    }
}
EOF

# ---- init_sw_dyn_atomics.js ----

$t->write_file('init_sw_dyn_atomics.js', <<"JS");
/*
 * Pre-fork SAB: created before fork so VA is identical in master (SW thread)
 * and every worker process.  Layout:
 *   [0] signal  — worker sets to 1 + notifies; SW resets to 0 after each use
 *   [1] value   — worker writes before notifying; SW reads after waking
 */
var preForkSab  = new SharedArrayBuffer(8);
var preForkArr  = new Int32Array(preForkSab);
preForkArr[0]   = 0;
preForkArr[1]   = 0;

(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /dyn_prefork_atomics/
     *
     * The SharedWorker is created dynamically inside the handler (not in
     * the js_source top-level).  Sends the pre-fork SAB to the SW; the SW
     * calls Atomics.wait on arr[0].  Worker writes arr[1]=42, then signals
     * via Atomics.store+Atomics.notify.  SW wakes and echoes arr[1].
     *
     * Same VA in worker and master → futex resolves to same physical page.
     */
    set('/dyn_prefork_atomics/', async function(req) {
        var sw = new SharedWorker('$dir/sw_dyn_wait.js');

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(preForkSab);
            Atomics.store(preForkArr, 1, 42);
            Atomics.store(preForkArr, 0, 1);
            Atomics.notify(preForkArr, 0, 1);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /dyn_worker_atomics/
     *
     * Worker creates a new SAB post-fork (memfd-backed).  Sends it to the
     * dynamic SW; SW calls Atomics.wait.  Worker writes arr[1]=99, notifies.
     *
     * Futex shared mode (without _PRIVATE) uses physical page identity, so
     * the notify wakes the SW even though VAs differ across processes.
     */
    set('/dyn_worker_atomics/', async function(req) {
        var sw  = new SharedWorker('$dir/sw_dyn_wait.js');
        var sab = new SharedArrayBuffer(8);
        var arr = new Int32Array(sab);
        arr[0] = 0;
        arr[1] = 0;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
            Atomics.store(arr, 1, 99);
            Atomics.store(arr, 0, 1);
            Atomics.notify(arr, 0, 1);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/dyn_prefork_atomics/'), qr/200 OK/, 'dyn SW prefork atomics: 200');
like(http_get('/dyn_prefork_atomics/'), qr/42/,     'dyn SW prefork atomics: SW echoes 42');
like(http_get('/dyn_prefork_atomics/'), qr/42/,     'dyn SW prefork atomics: repeated');

like(http_get('/dyn_worker_atomics/'),  qr/200 OK/, 'dyn SW worker atomics: 200');
like(http_get('/dyn_worker_atomics/'),  qr/99/,     'dyn SW worker atomics: SW echoes 99');
like(http_get('/dyn_worker_atomics/'),  qr/99/,     'dyn SW worker atomics: repeated');
