#!/usr/bin/perl

# Tests for a SharedArrayBuffer created by a JS Worker thread, forwarded
# by the nginx worker main thread to a SharedWorker.
#
# The existing js_sw_worker_sab.t tests the nginx worker main thread creating
# a SAB and sending it directly to a SharedWorker.  Here the SAB is created
# inside a JS Worker thread (a pthread in the same nginx worker process), which
# posts it to the main thread; the main thread then forwards it to the SW.
#
# The two-hop path:
#   JS Worker (memfd alloc, post-fork worker process)
#     → main thread via QuickJS intra-process worker pipe (sab_dup)
#     → SharedWorker via channel_send/SCM_RIGHTS (sab_get_fd + mmap in master)
#
# Routes:
#   /jw_to_sw/        — JS Worker creates SAB{42}; main forwards; SW echoes 42
#   /jw_to_sw_modify/ — JS Worker creates SAB{21}; main doubles buf[0] to 42;
#                       main forwards modified SAB; SW echoes 42 (confirms true
#                       shared memory: SW sees main's mutation)

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
# Receives a SAB via port.onmessage; echoes buf[0] back to the worker.

$t->write_file('sw_read_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var view = new Int32Array(msg.data);
        port.postMessage(view[0]);
    };
};
JS

# ---- JS Worker script ----
#
# Receives a trigger value; allocates a SharedArrayBuffer, stores the value
# in buf[0], and posts the SAB back to the main thread.

$t->write_file('jw_sab_maker.js', <<'JS');
onmessage = function(e) {
    var val  = e.data;
    var sab  = new SharedArrayBuffer(16);
    var view = new Int32Array(sab);
    view[0]  = val;
    postMessage(sab);
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_jw_to_sw_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /jw_to_sw/        { }
        location /jw_to_sw_modify/ { }
    }
}
EOF

# ---- init_jw_to_sw_sab.js ----

$t->write_file('init_jw_to_sw_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /jw_to_sw/
     *
     * Step 1: main spawns a JS Worker and asks it to create SAB{42}.
     * Step 2: Worker posts SAB back; main receives it (sab_dup).
     * Step 3: main forwards the SAB to the SharedWorker (SCM_RIGHTS).
     * Step 4: SW reads buf[0] and echoes it.
     */
    set('/jw_to_sw/', async function(req) {
        /* Step 1-2: get SAB from JS Worker */
        var sab = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sab_maker.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(42);
        });

        /* Step 3-4: forward to SharedWorker, wait for echo */
        var sw = new SharedWorker('$dir/sw_read_sab.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_to_sw_modify/
     *
     * Same two-hop path, but the main thread mutates buf[0] (21 → 42)
     * between receiving it from the Worker and forwarding it to the SW.
     * The SW must see 42, confirming the SAB is truly shared memory across
     * all three contexts (Worker thread, main thread, SW thread).
     */
    set('/jw_to_sw_modify/', async function(req) {
        /* Step 1-2: JS Worker creates SAB{21} */
        var sab = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sab_maker.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(21);
        });

        /* Main doubles buf[0]: 21 → 42 */
        var view = new Int32Array(sab);
        view[0] *= 2;

        /* Step 3-4: forward to SharedWorker */
        var sw = new SharedWorker('$dir/sw_read_sab.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/jw_to_sw/'),        qr/200 OK/, 'JS Worker SAB → SW: 200');
like(http_get('/jw_to_sw/'),        qr/42/,     'JS Worker SAB → SW: SW echoes 42');
like(http_get('/jw_to_sw/'),        qr/42/,     'JS Worker SAB → SW: repeated request');

like(http_get('/jw_to_sw_modify/'), qr/200 OK/, 'JS Worker SAB → main mutate → SW: 200');
like(http_get('/jw_to_sw_modify/'), qr/42/,     'JS Worker SAB → main mutate → SW: SW sees 42');
like(http_get('/jw_to_sw_modify/'), qr/42/,     'JS Worker SAB → main mutate → SW: repeated request');
