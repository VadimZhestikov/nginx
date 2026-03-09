#!/usr/bin/perl

# Tests for the JS Worker class:
#   new Worker(script)   — spawn independent JS thread
#   worker.postMessage() — send message to worker
#   worker.onmessage     — receive message from worker
#   worker.terminate()   — stop worker thread
#
# Each test handler creates a Worker per-request, posts a message, and the
# reply drives req.respond() via the async_pending suspend/resume mechanism.

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

$t->write_file('echo_worker.js', <<'JS');
onmessage = function(e) { postMessage(e.data); };
JS

$t->write_file('add_worker.js', <<'JS');
onmessage = function(e) { postMessage(e.data.a + e.data.b); };
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /worker_echo/ { }
        location /worker_add/  { }
    }
}
EOF

# ---- init.js — embed the test directory path so Worker() gets abs paths ----

$t->write_file('init.js', <<"JS");
(function installHandlers() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    // Echo handler: sends a string to the worker and echoes it back
    set('/worker_echo/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/echo_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage('EchoOK');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    // Add handler: sends an object {a, b} to the worker; worker replies a+b
    set('/worker_add/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/add_worker.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage({a: 20, b: 22});
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(4);

# ---- HTTP assertions ----

like(http_get('/worker_echo/'), qr/200 OK/,  'worker echo responds 200');
like(http_get('/worker_echo/'), qr/EchoOK/,  'worker echo body correct');

like(http_get('/worker_add/'),  qr/200 OK/,  'worker add responds 200');
like(http_get('/worker_add/'),  qr/42/,       'worker add result is 42');
