#!/usr/bin/perl

# Tests for SharedArrayBuffer + Atomics in Worker messages.
#
# Two synchronization patterns, side-by-side:
#
#   /atomics_sync/  — true blocking synchronization:
#     worker calls Atomics.wait() and blocks until the main thread
#     calls Atomics.store() + Atomics.notify() via nginx.setTimeout().
#
#   /no_atomics/    — postMessage-only synchronization:
#     worker modifies the SAB freely, signals completion with
#     postMessage(); main reads the shared result after receiving it.
#
# Both yield 42 so the correctness of the shared-memory path is clear.

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

# ---- Worker scripts ----

# Worker blocks on Atomics.wait() until the main thread signals.
# When woken (status 'ok' = notified, 'not-equal' = flag already set),
# it computes arr[1] * 6 and stores the result in arr[2].
$t->write_file('wait_worker.js', <<'JS');
onmessage = function(e) {
    var arr    = new Int32Array(e.data);
    var status = Atomics.wait(arr, 0, 0, 2000);   /* block; 2 s timeout */
    arr[2] = (status === 'ok' || status === 'not-equal') ? arr[1] * 6 : -1;
    postMessage('done');
};
JS

# Worker reads two SAB values, stores their sum back into arr[0],
# then signals completion via postMessage — no Atomics.wait/notify.
$t->write_file('noatomics_worker.js', <<'JS');
onmessage = function(e) {
    var arr = new Int32Array(e.data);
    arr[0] = arr[0] + arr[1];   /* 15 + 27 = 42 */
    postMessage('done');
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_atomics.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /atomics_sync/ { }
        location /no_atomics/   { }
    }
}
EOF

# ---- init_atomics.js ----

$t->write_file('init_atomics.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /atomics_sync/ — blocking synchronization via Atomics.
     *
     * Layout: Int32Array over a 12-byte (3 × int32) SharedArrayBuffer:
     *   arr[0]  flag   — worker does Atomics.wait(arr, 0, 0)
     *   arr[1]  input  — value set by main before sending the SAB
     *   arr[2]  result — written by worker after wakeup (arr[1] * 6)
     *
     * Main sends the SAB to the worker, then after 30 ms:
     *   1. Atomics.store(arr, 0, 1)   — change flag so wait returns
     *   2. Atomics.notify(arr, 0, 1)  — wake the blocked worker
     *
     * If the worker happened to call Atomics.wait after the store,
     * it returns 'not-equal' immediately — the result is still correct.
     */
    set('/atomics_sync/', async function(req) {
        var sab = new SharedArrayBuffer(12);
        var arr = new Int32Array(sab);
        arr[0] = 0;   /* flag  — worker waits on this */
        arr[1] = 7;   /* input */
        arr[2] = 0;   /* result placeholder */

        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/wait_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(arr[2]); };
            w.postMessage(sab);

            /*
             * Give the worker time to reach Atomics.wait(), then
             * signal it.  30 ms is generous; the worker typically
             * reaches the wait within a few µs of receiving the message.
             */
            nginx.setTimeout(30).then(function() {
                Atomics.store(arr, 0, 1);
                Atomics.notify(arr, 0, 1);
            });
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /no_atomics/ — postMessage-only synchronization, no Atomics.wait/notify.
     *
     * Main fills arr[0]=15, arr[1]=27 and sends the SAB.
     * Worker computes arr[0]+arr[1]=42 in-place and signals via postMessage.
     * Main reads arr[0] after receiving the message.
     */
    set('/no_atomics/', async function(req) {
        var sab = new SharedArrayBuffer(8);
        var arr = new Int32Array(sab);
        arr[0] = 15;
        arr[1] = 27;

        await new Promise(function(resolve) {
            var w = new Worker('$dir/noatomics_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(); };
            w.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(arr[0]));
    });
})();
JS

$t->try_run('no js module')->plan(4);

# ---- HTTP assertions ----

like(http_get('/atomics_sync/'), qr/200 OK/, 'Atomics notify+wait responds 200');
like(http_get('/atomics_sync/'), qr/42/,     'Atomics notify+wait result is 42');

like(http_get('/no_atomics/'),   qr/200 OK/, 'no-Atomics SAB responds 200');
like(http_get('/no_atomics/'),   qr/42/,     'no-Atomics SAB result is 42');
