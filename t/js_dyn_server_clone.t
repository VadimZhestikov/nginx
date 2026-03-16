#!/usr/bin/perl

# Stage 45 DYN: srv.clone(newName) — deep-copy virtual server
#
# Verifies that srv.clone(newName) creates an independent copy of the
# server's current location tree under a new server_name:
#
#   1. The clone inherits all prefix locations from the source, including
#      their handlers.
#   2. addLocation() on the clone produces an independent entry that does
#      NOT appear in the original server.
#   3. The clone is reachable via Host header after rebuildVhostDispatch().
#   4. removeServer() works on the clone; the original is unaffected.
#   5. Cloning a server that already has dynamic locations (added via
#      addLocation) preserves those locations in the clone.
#
# Two static servers on the same address are required so that nginx creates
# a vhost dispatch hash at config time (addr.servers.nelts > 1).

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

js_source %%TESTDIR%%/srv_clone.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name static1.local;

        location /s1           { }
        location /probe_remove { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name static2.local;

        location /s2 { }
    }
}
EOF

$t->write_file('srv_clone.js', <<'JS');
(function() {
    var http = nginx.http;

    function findSrv(name) {
        return http.servers.find(function(s) { return s.name === name; });
    }

    /* static server handlers */
    findSrv('static1.local').locations
        .find(function(l) { return l.path === '/s1'; })
        .handler = function(r) { r.respond(200, {}, 'static1-ok'); };

    findSrv('static2.local').locations
        .find(function(l) { return l.path === '/s2'; })
        .handler = function(r) { r.respond(200, {}, 'static2-ok'); };

    /* add a dynamic location to static1 before cloning, so the clone
     * gets it too */
    findSrv('static1.local').addLocation('/dyn_before_clone').handler =
        function(r) { r.respond(200, {}, 'dyn-before-clone'); };

    /* clone static1 → cloned1.local */
    var cloned = findSrv('static1.local').clone('cloned1.local');

    /* add a location on the clone AFTER cloning — must NOT appear on source */
    cloned.addLocation('/clone_only').handler =
        function(r) { r.respond(200, {}, 'clone-only'); };

    /* probe_remove: removes cloned1.local */
    findSrv('static1.local').locations
        .find(function(l) { return l.path === '/probe_remove'; })
        .handler = function(r) {
            var removed = http.removeServer('cloned1.local');
            http.rebuildVhostDispatch();
            r.respond(200, {}, removed ? 'removed' : 'not-removed');
        };

    http.rebuildVhostDispatch();
})();
JS

$t->run();

sub vhost {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# ---- static servers unaffected ----
like(vhost('static1.local', '/s1'), qr/200 OK/,    'static1: 200');
like(vhost('static1.local', '/s1'), qr/static1-ok/, 'static1: body');

like(vhost('static2.local', '/s2'), qr/200 OK/,    'static2: 200');
like(vhost('static2.local', '/s2'), qr/static2-ok/, 'static2: body');

# ---- clone inherits /s1 handler from source ----
like(vhost('cloned1.local', '/s1'), qr/200 OK/,    'clone: /s1 200');
like(vhost('cloned1.local', '/s1'), qr/static1-ok/, 'clone: /s1 inherited handler');

# ---- clone inherits dynamic location added before clone call ----
like(vhost('cloned1.local', '/dyn_before_clone'), qr/dyn-before-clone/,
     'clone: dynamic location added before clone is inherited');

# ---- location added on clone after cloning is visible on clone ----
like(vhost('cloned1.local', '/clone_only'), qr/200 OK/,   'clone: /clone_only 200');
like(vhost('cloned1.local', '/clone_only'), qr/clone-only/, 'clone: /clone_only body');

# ---- location added on clone is NOT visible on original ----
like(vhost('static1.local', '/clone_only'), qr/404/,
     'original: /clone_only not present (independent tree)');

# ---- remove clone ----
my $r = vhost('static1.local', '/probe_remove');
like($r, qr/200 OK/,  'probe_remove: 200');
like($r, qr/removed/, 'probe_remove: removeServer returned true');

# ---- clone gone after removal — /clone_only (only on clone) is now 404 ----
# (fallback is static1.local which has no /clone_only location)
unlike(vhost('cloned1.local', '/clone_only'), qr/clone-only/,
       'clone gone: clone-only location no longer served after removal');

# ---- static1 unaffected after clone removal ----
like(vhost('static1.local', '/s1'), qr/static1-ok/,
     'static1 intact after clone removal');

$t->stop();
