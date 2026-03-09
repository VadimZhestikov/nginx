#!/usr/bin/perl

# Tests for dynamic SharedWorker creation from nginx worker request handlers.
#
# Unlike static SharedWorkers (created in js_source init code), these are
# created the first time a request handler calls new SharedWorker(url).  The
# SW thread is started in the master process by the SW manager thread, and the
# worker receives its channel fds via SCM_RIGHTS over the pre-fork command
# socket.
#
# Two routes:
#
#   /sw_dynamic_echo/   — handler creates SW on first request;
#                         sends "hello" to SW; SW echoes back.
#
#   /sw_dynamic_dedup/  — second request for the same URL reuses the
#                         cached stub; SW echoes "world".

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

$t->write_file('sw_echo.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        port.postMessage(msg.data);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_dynamic.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_dynamic_echo/  { }
        location /sw_dynamic_dedup/ { }
    }
}
EOF

# ---- init_sw_dynamic.js ----
# The SharedWorker is NOT created here.  It is created lazily from the
# request handler the first time a request arrives.

$t->write_file('init_sw_dynamic.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    set('/sw_dynamic_echo/', async function(req) {
        /* SW created on first request; reused on subsequent ones */
        var sw = new SharedWorker('$dir/sw_echo.js');

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('hello');
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    set('/sw_dynamic_dedup/', async function(req) {
        /* Same URL as echo route — must deduplicate per-worker */
        var sw = new SharedWorker('$dir/sw_echo.js');

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('world');
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/sw_dynamic_echo/'),  qr/200 OK/, 'dynamic SW echo responds 200');
like(http_get('/sw_dynamic_echo/'),  qr/hello/,  'dynamic SW echoes hello');

# Second request reuses stub (no new SW created)
like(http_get('/sw_dynamic_echo/'),  qr/hello/,  'dynamic SW echo second request');

# Different message on same SW (dedup)
like(http_get('/sw_dynamic_dedup/'), qr/200 OK/, 'dedup SW responds 200');
like(http_get('/sw_dynamic_dedup/'), qr/world/,  'dedup SW echoes world');

# Both routes work together
like(http_get('/sw_dynamic_echo/'),  qr/hello/,  'dynamic SW echo still works');
