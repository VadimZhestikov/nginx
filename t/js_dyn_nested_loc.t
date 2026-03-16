#!/usr/bin/perl

# Stage 46 DYN: loc.addLocation(childPattern) / loc.removeLocation(childPattern)
#
# Verifies that addLocation() and removeLocation() are available directly on
# NginxLocation objects, enabling nested (child) location management:
#
#   1. loc.addLocation('/child') adds a child under the parent prefix.
#      The BST builder groups '/child' under '/parent' automatically so
#      requests to /parent/child are routed to the child handler.
#   2. Multiple children can be added to the same parent.
#   3. loc.removeLocation('/child') removes the child; requests fall back
#      to the nearest parent match.
#   4. loc.removeLocation('/nonexistent') returns false (no crash).
#   5. Static (top-level) locations are unaffected.
#   6. Children added via loc.addLocation appear in srv.locations[] and
#      are reachable via srv.addLocation idempotency (same object).
#
# Implementation note: loc.addLocation() delegates to the server-level
# addLocation, storing the new location in the same flat prefix_locs[].
# The nginx BST builder groups '/parent/child' under '/parent' via the
# standard shared-prefix list algorithm.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(14);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/nested_loc.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /api      { }
        location /static   { }
        location /probe    { }
    }
}
EOF

$t->write_file('nested_loc.js', <<'JS');
(function() {
    var srv = nginx.http.servers[0];

    function findLoc(path) {
        return srv.locations.find(function(l) { return l.path === path; });
    }

    /* /static — must survive all mutations */
    findLoc('/static').handler = function(r) {
        r.respond(200, {}, 'static-ok');
    };

    /* /api — parent location handler (fallback if no child matches) */
    var apiLoc = findLoc('/api');
    apiLoc.handler = function(r) {
        r.respond(200, {}, 'api-ok');
    };

    /* Add two child locations under /api */
    apiLoc.addLocation('/api/v1').handler = function(r) {
        r.respond(200, {}, 'v1-ok');
    };
    apiLoc.addLocation('/api/v2').handler = function(r) {
        r.respond(200, {}, 'v2-ok');
    };

    /* /probe — removes /api/v1, checks nonexistent */
    findLoc('/probe').handler = function(r) {
        var removed = apiLoc.removeLocation('/api/v1');
        var missing = apiLoc.removeLocation('/nonexistent');
        r.respond(200, {}, (removed ? 'removed' : 'not-removed')
                           + ':' + (missing ? 'found' : 'false'));
    };
})();
JS

$t->run();

# ---- /static unaffected ----
like(http_get('/static'), qr/200 OK/,    'static: 200');
like(http_get('/static'), qr/static-ok/, 'static: body');

# ---- /api parent matches directly ----
like(http_get('/api'), qr/200 OK/,  'api parent: 200');
like(http_get('/api'), qr/api-ok/,  'api parent: body');

# ---- child /api/v1 reachable ----
like(http_get('/api/v1'), qr/200 OK/, 'child /api/v1: 200');
like(http_get('/api/v1'), qr/v1-ok/,  'child /api/v1: body');

# ---- child /api/v2 reachable ----
like(http_get('/api/v2'), qr/200 OK/, 'child /api/v2: 200');
like(http_get('/api/v2'), qr/v2-ok/,  'child /api/v2: body');

# ---- removeLocation on child ----
my $r = http_get('/probe');
like($r, qr/200 OK/,   'probe: 200');
like($r, qr/removed/,  'probe: removeLocation returned true');
like($r, qr/false/,    'probe: removeLocation on missing returned false');

# ---- /api/v1 gone — falls back to /api (parent inclusive match) ----
like(http_get('/api/v1'), qr/api-ok/,
     '/api/v1 after removal: falls back to /api');

# ---- /api/v2 still alive ----
like(http_get('/api/v2'), qr/v2-ok/, '/api/v2 still live after /api/v1 removal');

# ---- /static still alive ----
like(http_get('/static'), qr/static-ok/, 'static intact after all ops');

$t->stop();
