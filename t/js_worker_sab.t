#!/usr/bin/perl

# Tests for SharedArrayBuffer by-reference passing in Worker messages.
#
# Verifies that:
#   1. A SharedArrayBuffer can be sent main → worker; worker computes
#      on the data and replies with a plain value.
#   2. The SAB is shared memory: writes made by the worker are visible
#      to the main thread after the worker signals completion.

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

# Receives a SharedArrayBuffer, sums its Int32 elements, replies with the sum.
$t->write_file('sab_sum_worker.js', <<'JS');
onmessage = function(e) {
    var arr = new Int32Array(e.data);
    var sum = 0;
    for (var i = 0; i < arr.length; i++) { sum += arr[i]; }
    postMessage(sum);
};
JS

# Receives a SharedArrayBuffer, doubles each Int32 element in-place,
# then sends "done" so the main thread knows the writes are complete.
$t->write_file('sab_modify_worker.js', <<'JS');
onmessage = function(e) {
    var arr = new Int32Array(e.data);
    for (var i = 0; i < arr.length; i++) { arr[i] *= 2; }
    postMessage('done');
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sab_sum/    { }
        location /sab_shared/ { }
    }
}
EOF

# ---- init_sab.js ----

$t->write_file('init_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /sab_sum/ — fill a SharedArrayBuffer with 10, 20, 12 and send it
     * to the worker; worker sums the elements and replies with 42.
     */
    set('/sab_sum/', async function(req) {
        var sab = new SharedArrayBuffer(12);   /* 3 × int32 */
        var arr = new Int32Array(sab);
        arr[0] = 10;  arr[1] = 20;  arr[2] = 12;

        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/sab_sum_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /sab_shared/ — fill a SharedArrayBuffer with 5, 7; worker doubles
     * every element in-place; main reads the same memory and verifies
     * the values are now 10, 14.  This confirms by-reference semantics.
     */
    set('/sab_shared/', async function(req) {
        var sab = new SharedArrayBuffer(8);    /* 2 × int32 */
        var arr = new Int32Array(sab);
        arr[0] = 5;  arr[1] = 7;

        await new Promise(function(resolve) {
            var w = new Worker('$dir/sab_modify_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(); };
            w.postMessage(sab);
        });

        /* Worker multiplied each element by 2 in the shared buffer */
        req.respond(200, {'content-type': 'text/plain'},
                    arr[0] + ':' + arr[1]);
    });
})();
JS

$t->try_run('no js module')->plan(4);

# ---- HTTP assertions ----

like(http_get('/sab_sum/'),    qr/200 OK/, 'SAB sum responds 200');
like(http_get('/sab_sum/'),    qr/42/,     'SAB sum result is 42');

like(http_get('/sab_shared/'), qr/200 OK/, 'SAB shared responds 200');
like(http_get('/sab_shared/'), qr/10:14/,  'SAB shared values doubled by worker');
