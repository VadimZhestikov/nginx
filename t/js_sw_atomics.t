#!/usr/bin/perl

# Tests for SharedWorker Atomics.wait/notify with nginx workers.
#
# The SW thread calls JS_SetCanBlock(rt, TRUE) so Atomics.wait() works
# there.  The nginx worker (event loop) can only call Atomics.notify()
# (non-blocking).  Two memory-sharing paths are tested:
#
#   /sw_prefork_atomics/
#       Pre-fork SAB (MAP_SHARED|MAP_ANONYMOUS, same VA in all processes).
#       Worker notifies → SW wakes → SW echoes arr[1] back via postMessage.
#
#   /sw_worker_atomics/
#       Worker-created SAB (memfd, different VAs but same physical pages).
#       Futex cross-process shared mode works because both mappings refer
#       to the same physical page at the same page-offset.
#       Same notify/wait/echo flow as above.
#
# Atomics.wait semantics used:
#   Atomics.wait(arr, 0, 0, 5000)
#     → "ok"        if notified while blocking (arr[0] was 0)
#     → "not-equal" if arr[0] was already != 0 when wait was called
#     → "timed-out" on timeout (should never happen in these tests)
# Both "ok" and "not-equal" are success — the race is harmless.

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
# Receives a SharedArrayBuffer from the worker.
# Waits on arr[0] (expected value 0); when woken reads arr[1]; resets
# arr[0] to 0 for reuse; echoes arr[1] back via postMessage.

$t->write_file('sw_wait.js', <<'JS');
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

js_source %%TESTDIR%%/init_sw_atomics.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_prefork_atomics/ { }
        location /sw_worker_atomics/  { }
    }
}
EOF

# ---- init_sw_atomics.js ----

$t->write_file('init_sw_atomics.js', <<"JS");
/*
 * Pre-fork SAB: created before fork so the same VA is valid in the master
 * (SW thread) and every worker process.  arr layout:
 *   [0] signal  — worker sets to 1 + notifies; SW resets to 0 after read
 *   [1] value   — worker writes before notifying; SW reads after waking
 */
var preForkSab  = new SharedArrayBuffer(8);
var preForkArr  = new Int32Array(preForkSab);
preForkArr[0] = 0;
preForkArr[1] = 0;

(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /sw_prefork_atomics/
     *
     * Sends the pre-fork SAB to the SW.  The SW calls Atomics.wait on
     * arr[0]; the worker writes arr[1]=42 and then signals via
     * Atomics.store + Atomics.notify.
     *
     * The SAB has the same virtual address in the worker and in the SW
     * thread (master), so the futex FUTEX_WAIT/FUTEX_WAKE pair resolves
     * to the same physical page and works across processes.
     */
    set('/sw_prefork_atomics/', async function(req) {
        var sw = new SharedWorker('$dir/sw_wait.js');

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(preForkSab);       /* SW will Atomics.wait on it */
            Atomics.store(preForkArr, 1, 42); /* put value before signal */
            Atomics.store(preForkArr, 0, 1);  /* signal: change from 0→1 */
            Atomics.notify(preForkArr, 0, 1); /* wake one waiter */
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /sw_worker_atomics/
     *
     * Worker creates a new SAB post-fork (memfd-backed).  The SAB is sent
     * to the SW via the channel (SCM_RIGHTS carries the memfd fd; receiver
     * mmap's at a different VA but the same physical pages).
     *
     * Futex shared mode (FUTEX_WAIT without _PRIVATE) uses the physical
     * page identity rather than the virtual address, so notify in the
     * worker wakes the waiting SW thread even though their VAs differ.
     */
    set('/sw_worker_atomics/', async function(req) {
        var sw  = new SharedWorker('$dir/sw_wait.js');
        var sab = new SharedArrayBuffer(8);
        var arr = new Int32Array(sab);
        arr[0] = 0;
        arr[1] = 0;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);             /* SW will Atomics.wait on it */
            Atomics.store(arr, 1, 99);       /* put value before signal */
            Atomics.store(arr, 0, 1);        /* signal: change from 0→1 */
            Atomics.notify(arr, 0, 1);       /* wake one waiter */
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/sw_prefork_atomics/'), qr/200 OK/, 'prefork SAB: SW atomics wait responds 200');
like(http_get('/sw_prefork_atomics/'), qr/42/,     'prefork SAB: SW echoes notified value 42');
like(http_get('/sw_prefork_atomics/'), qr/42/,     'prefork SAB: repeated use (resets correctly)');

like(http_get('/sw_worker_atomics/'),  qr/200 OK/, 'worker SAB: SW atomics wait responds 200');
like(http_get('/sw_worker_atomics/'),  qr/99/,     'worker SAB: SW echoes notified value 99');
like(http_get('/sw_worker_atomics/'),  qr/99/,     'worker SAB: repeated use (new SAB each time)');
