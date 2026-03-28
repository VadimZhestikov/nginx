#!/usr/bin/perl

# Tests for JS-Pilgrim P2 — nginx.http.addHook() and server.addHook():
#   Global (http-level) and per-server access-phase hooks.
#   These fire before the content handler for ALL requests, EXCEPT those
#   served by nginx's `return` directive (which runs in the rewrite phase
#   before the access phase).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p2_hooks_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  server1;

        location /pass/      { }
        location /cancel/    { }
        location /srv1only/  { }
        location /order/     { }
        location /p1_after/  { }
    }

    server {
        listen       127.0.0.1:%%PORT_8081%%;
        server_name  server2;

        location /pass/        { }
        location /srv2only/    { }
        location /global_fires/ { }
        location /cancel/       { }
    }
}
EOF

$t->write_file('p2_hooks_init.js', <<'JS');
// JS-Pilgrim P2 hook tests — all locations use JS content handlers.

(function installAll() {
    const s1 = nginx.http.servers[0];
    const s2 = nginx.http.servers[1];

    function loc1(path) {
        return s1.locations.find(l => l.path === path);
    }

    function loc2(path) {
        return s2.locations.find(l => l.path === path);
    }

    // Install JS content handlers for all locations
    loc1('/pass/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'js-pass');
    };
    loc1('/cancel/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'js-cancel-body');
    };
    loc1('/srv1only/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'srv1-content');
    };
    loc2('/pass/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'js-pass-s2');
    };
    loc2('/srv2only/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'srv2-content');
    };
    loc2('/global_fires/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'global-fires-body');
    };
    loc2('/cancel/').handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'js-cancel-body-s2');
    };

    // Shared log for order test
    const orderLog = [];

    // Global hook: cancels /cancel/, logs order for /order/,
    // passes through for everything else.
    nginx.http.addHook(function globalHook(req) {
        if (req.uri === '/cancel/') {
            req.respond(403, {'content-type': 'text/plain'}, 'global-cancel');
            return;
        }
        if (req.uri === '/order/') {
            orderLog.push('global');
        }
    });

    // Server1 hook: adds header on /srv1only/, logs order on /order/,
    // sets header on /p1_after/.
    s1.addHook(function srv1Hook(req) {
        if (req.uri === '/srv1only/') {
            req.setHeader('X-SrvHook', 's1');
        }
        if (req.uri === '/order/') {
            orderLog.push('server');
        }
        if (req.uri === '/p1_after/') {
            req.setHeader('X-Phase', 'p2');
        }
    });

    // /order/ location handler: respond with the log contents
    const orderLoc = loc1('/order/');
    orderLoc.handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, orderLog.join(','));
    };

    // /p1_after/ location handler (P1): runs after P2 server hook
    const p1Loc = loc1('/p1_after/');
    p1Loc.addHook(function p1Hook(req) {
        req.setHeader('X-Phase2', 'p1');
    });
    p1Loc.handler = function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'done');
    };
})();
JS

$t->try_run('no js module')->plan(15);

# -------------------------------------------------------------------------
# Test 1: global hook passes through — JS handler serves response
# -------------------------------------------------------------------------
like(http_get('/pass/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/200 OK/, 'global pass: 200 OK');
like(http_get('/pass/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/js-pass/, 'global pass: JS handler body');

# -------------------------------------------------------------------------
# Test 2: global hook cancels — returns 403, JS handler never runs
# -------------------------------------------------------------------------
like(http_get('/cancel/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/403/, 'global cancel: 403 status');
like(http_get('/cancel/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/global-cancel/, 'global cancel: hook body');
unlike(http_get('/cancel/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
       qr/js-cancel-body/, 'global cancel: JS handler not called');

# -------------------------------------------------------------------------
# Test 3: server1 hook fires for server1 — adds X-SrvHook header
# -------------------------------------------------------------------------
like(http_get('/srv1only/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/X-SrvHook:\s*s1/i, 'srv1 hook fires for srv1');

# -------------------------------------------------------------------------
# Test 4: server1 hook does NOT fire for server2
# -------------------------------------------------------------------------
unlike(http_get('/srv2only/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
       qr/X-SrvHook/i, 'srv1 hook not fired for srv2');

# -------------------------------------------------------------------------
# Test 5: global hook fires for server2 requests too
# -------------------------------------------------------------------------
like(http_get('/global_fires/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
     qr/200 OK/, 'global hook fires for srv2: 200 OK');

# -------------------------------------------------------------------------
# Test 6: execution order: global hook runs before server hook
# -------------------------------------------------------------------------
like(http_get('/order/', PeerAddr => '127.0.0.1', PeerPort => port(8080)),
     qr/global,server/, 'execution order: global before server');

# -------------------------------------------------------------------------
# Test 7: P1 location hook runs after P2 server hook
# -------------------------------------------------------------------------
my $r7 = http_get('/p1_after/', PeerAddr => '127.0.0.1', PeerPort => port(8080));
like($r7, qr/X-Phase:\s*p2/i,   'p2 server hook ran (X-Phase: p2)');
like($r7, qr/X-Phase2:\s*p1/i,  'p1 location hook ran (X-Phase2: p1)');

# -------------------------------------------------------------------------
# Test 8: global cancel also works for server2
# -------------------------------------------------------------------------
like(http_get('/cancel/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
     qr/403/, 'global cancel fires for srv2 too: 403');
like(http_get('/cancel/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
     qr/global-cancel/, 'global cancel fires for srv2: body');

# -------------------------------------------------------------------------
# Test 9: global pass + srv1only is served by JS handler
#         (confirms global hook actually ran but passed through)
# -------------------------------------------------------------------------
my $r9 = http_get('/srv1only/', PeerAddr => '127.0.0.1', PeerPort => port(8080));
like($r9, qr/200 OK/,     'srv1only: 200 OK');
like($r9, qr/srv1-content/, 'srv1only: JS handler body');
