#!/usr/bin/perl

# Stage 48: nginx.broadcast(fn) — per-worker startup callbacks
#
# Verifies:
#   1. fn registered via nginx.broadcast() during init_conf runs in
#      each worker during init_process (after context opaque = worker).
#   2. Multiple broadcast functions all run.
#   3. Broadcast functions can set JS globals readable from request handlers.
#   4. nginx.broadcast(fn) from a request handler (worker context) calls
#      fn immediately in the current worker.
#   5. Static config is unaffected.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(10);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/broadcast.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /check_init  { }
        location /check_multi { }
        location /check_runtime { }
        location /static { return 200 'plain'; }
    }
}
EOF

$t->write_file('broadcast.js', <<'JS');
(function() {
    var srv = nginx.http.servers[0];

    /* Broadcast 1: sets nginx._broadcastA = 'A-ran' in each worker */
    nginx.broadcast(function() {
        nginx._broadcastA = 'A-ran';
    });

    /* Broadcast 2: sets nginx._broadcastB = 'B-ran' in each worker */
    nginx.broadcast(function() {
        nginx._broadcastB = 'B-ran';
    });

    /* /check_init: return both broadcast flags */
    srv.locations.find(function(l) { return l.path === '/check_init'; })
        .handler = function(r) {
            var a = nginx._broadcastA || 'not-run';
            var b = nginx._broadcastB || 'not-run';
            r.respond(200, {}, a + ':' + b);
        };

    /* /check_multi: verify both broadcasts ran */
    srv.locations.find(function(l) { return l.path === '/check_multi'; })
        .handler = function(r) {
            var ok = (nginx._broadcastA === 'A-ran' &&
                      nginx._broadcastB === 'B-ran') ? 'both' : 'missing';
            r.respond(200, {}, ok);
        };

    /* /check_runtime: nginx.broadcast from a request handler (worker ctx) */
    srv.locations.find(function(l) { return l.path === '/check_runtime'; })
        .handler = function(r) {
            nginx._runtimeBroadcast = 'not-yet';
            nginx.broadcast(function() {
                nginx._runtimeBroadcast = 'runtime-ran';
            });
            r.respond(200, {}, nginx._runtimeBroadcast);
        };
})();
JS

$t->run();

# ---- broadcast functions ran during worker init ----
like(http_get('/check_init'), qr/200 OK/,      'check_init: 200');
like(http_get('/check_init'), qr/A-ran:B-ran/,  'check_init: both broadcasts ran');

# ---- multiple broadcasts all run ----
like(http_get('/check_multi'), qr/200 OK/,  'check_multi: 200');
like(http_get('/check_multi'), qr/both/,    'check_multi: both flags set');

# ---- broadcast from request handler calls fn immediately ----
like(http_get('/check_runtime'), qr/200 OK/,       'check_runtime: 200');
like(http_get('/check_runtime'), qr/runtime-ran/,   'check_runtime: fn called immediately');

# ---- static location (return directive) unaffected ----
like(http_get('/static'), qr/200 OK/,  'static: 200');
like(http_get('/static'), qr/plain/,   'static: body');

# ---- repeated calls return same values ----
like(http_get('/check_init'), qr/A-ran:B-ran/, 'check_init repeated: stable');
like(http_get('/check_multi'), qr/both/,       'check_multi repeated: stable');

$t->stop();
