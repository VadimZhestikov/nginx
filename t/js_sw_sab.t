#!/usr/bin/perl

# Tests for SharedArrayBuffer passed to SharedWorker via postMessage.
#
# The SAB must be created in the master-scope init script (before fork)
# so that the MAP_SHARED|MAP_ANONYMOUS mapping is inherited by all nginx
# worker processes.  The SharedWorker thread (running in the master
# process) and every worker then share the same physical pages and can
# observe each other's writes.
#
# Two routes:
#
#   /sw_sab_sum/    — worker sends SAB [10, 20, 12] to SW;
#                     SW sums the elements and sends back 42 via postMessage.
#
#   /sw_sab_shared/ — worker sends SAB [5, 7] to SW;
#                     SW doubles each element in place and signals "done";
#                     worker reads the same SAB and responds with "10:14".

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

# ---- SharedWorker scripts ----

# Receives a SharedArrayBuffer, sums its Int32 elements, replies with the sum.
$t->write_file('sw_sum_worker.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var arr = new Int32Array(msg.data);
        var sum = 0;
        for (var i = 0; i < arr.length; i++) { sum += arr[i]; }
        port.postMessage(sum);
    };
};
JS

# Receives a SharedArrayBuffer, doubles each Int32 element in place,
# then sends "done" so the worker knows the writes are complete.
$t->write_file('sw_modify_worker.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var arr = new Int32Array(msg.data);
        for (var i = 0; i < arr.length; i++) { arr[i] *= 2; }
        port.postMessage('done');
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_sw_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_sab_sum/    { }
        location /sw_sab_shared/ { }
    }
}
EOF

# ---- init_sw_sab.js ----

$t->write_file('init_sw_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /sw_sab_sum/ — fill a SAB with [10, 20, 12] and send it to the
     * SharedWorker; SW sums the elements and replies with 42.
     *
     * The SAB is created here in master scope (before fork) so that
     * MAP_SHARED|MAP_ANONYMOUS maps the same physical pages in every
     * nginx worker and in the SW thread.  Per-request the worker sends
     * fresh contents each time.
     */
    var sum_sab = new SharedArrayBuffer(12);   /* 3 × int32 */
    var sum_arr = new Int32Array(sum_sab);
    var sw_sum  = new SharedWorker('$dir/sw_sum_worker.js');

    set('/sw_sab_sum/', async function(req) {
        sum_arr[0] = 10;  sum_arr[1] = 20;  sum_arr[2] = 12;

        var result = await new Promise(function(resolve) {
            sw_sum.onmessage = function(e) { resolve(e.data); };
            sw_sum.postMessage(sum_sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /sw_sab_shared/ — fill a SAB with [5, 7] and send it to the SW;
     * SW doubles each element in place; main reads the shared result.
     *
     * This confirms that the worker can observe writes made by the SW
     * thread without any explicit data transfer back.
     */
    var mod_sab = new SharedArrayBuffer(8);    /* 2 × int32 */
    var mod_arr = new Int32Array(mod_sab);
    var sw_mod  = new SharedWorker('$dir/sw_modify_worker.js');

    set('/sw_sab_shared/', async function(req) {
        mod_arr[0] = 5;  mod_arr[1] = 7;

        await new Promise(function(resolve) {
            sw_mod.onmessage = function(e) { resolve(); };
            sw_mod.postMessage(mod_sab);
        });

        req.respond(200, {'content-type': 'text/plain'},
                    mod_arr[0] + ':' + mod_arr[1]);
    });
})();
JS

$t->try_run('no js module')->plan(4);

# ---- HTTP assertions ----

like(http_get('/sw_sab_sum/'),    qr/200 OK/, 'SW SAB sum responds 200');
like(http_get('/sw_sab_sum/'),    qr/42/,     'SW SAB sum result is 42');

like(http_get('/sw_sab_shared/'), qr/200 OK/, 'SW SAB shared responds 200');
like(http_get('/sw_sab_shared/'), qr/10:14/,  'SW SAB shared values doubled by SW');
