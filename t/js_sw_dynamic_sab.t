#!/usr/bin/perl

# Tests for SharedArrayBuffer transfer through a *dynamic* SharedWorker.
#
# Dynamic SWs are created on demand from request handlers (not in js_source
# init code).  Their wake pipe is received from the manager via SCM_RIGHTS
# — the fix that made this path work.  These tests verify that SAB transfers
# work correctly on that path.
#
# Routes:
#   /dyn_worker_sab/   — nginx worker creates SAB{42} post-fork (memfd),
#                        sends to dynamic SW; SW reads buf[0] and echoes it
#   /dyn_sw_sab/       — nginx worker triggers dynamic SW to create a fresh
#                        SAB{99}; worker reads buf[0] from the returned SAB

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

# Receives a SAB, reads buf[0], echoes the value.
$t->write_file('sw_dyn_echo_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var view = new Int32Array(msg.data);
        port.postMessage(view[0]);
    };
};
JS

# Receives a trigger value, creates a new SAB, writes the value, sends SAB.
$t->write_file('sw_dyn_make_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var sab  = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0]  = msg.data;
        port.postMessage(sab);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_dyn_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /dyn_worker_sab/ { }
        location /dyn_sw_sab/     { }
    }
}
EOF

# ---- init_sw_dyn_sab.js ----

$t->write_file('init_sw_dyn_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /dyn_worker_sab/
     *
     * Dynamic SW is created inside the request handler (not during init).
     * The manager thread allocates channels and replies with [worker_fd,
     * wake_pipe[1]] so the worker can wake the SW thread.
     *
     * Worker creates a post-fork SAB (memfd-backed), fills buf[0]=42, and
     * sends it to the SW.  SW echoes buf[0]; worker responds with that value.
     */
    set('/dyn_worker_sab/', async function(req) {
        var sw   = new SharedWorker('$dir/sw_dyn_echo_sab.js');
        var sab  = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0]  = 42;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /dyn_sw_sab/
     *
     * Worker triggers the dynamic SW to create a fresh SAB with buf[0]=99.
     * Worker receives the SAB, reads buf[0], and responds with that value.
     */
    set('/dyn_sw_sab/', async function(req) {
        var sw = new SharedWorker('$dir/sw_dyn_make_sab.js');

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) {
                var view = new Int32Array(e.data);
                resolve(view[0]);
            };
            sw.postMessage(99);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/dyn_worker_sab/'), qr/200 OK/, 'dyn SW worker SAB: 200');
like(http_get('/dyn_worker_sab/'), qr/42/,     'dyn SW worker SAB: echoes 42');
like(http_get('/dyn_worker_sab/'), qr/42/,     'dyn SW worker SAB: repeated request');

like(http_get('/dyn_sw_sab/'),     qr/200 OK/, 'dyn SW creates SAB: 200');
like(http_get('/dyn_sw_sab/'),     qr/99/,     'dyn SW creates SAB: worker reads 99');
like(http_get('/dyn_sw_sab/'),     qr/99/,     'dyn SW creates SAB: repeated request');
