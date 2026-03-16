#!/usr/bin/perl

# Stage 44 DYN: nginx.http.addServer / removeServer — dynamic virtual host
#
# Verifies that new virtual servers can be created and removed at runtime:
#
#   1. addServer('name') creates a new NginxServer reachable via Host header
#      after rebuildVhostDispatch() is called.
#   2. addLocation() on the new server installs a JS content handler.
#   3. rebuildVhostDispatch() makes the new server live.
#   4. removeServer('name') removes the server; subsequent vhost-dispatched
#      requests no longer reach it (fall through to the default server).
#   5. removeServer('nonexistent') returns false (no crash).
#   6. Static servers are unaffected by dynamic server add/remove.
#
# Note: two static servers on the same address are required so that
# nginx creates a vhost dispatch hash at config time (addr.servers.nelts > 1),
# which addServer/removeServer can then mutate.

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

js_source %%TESTDIR%%/dyn_server.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name static1.local;

        location /s1          { }
        location /probe_remove { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name static2.local;

        location /s2 { }
    }
}
EOF

$t->write_file('dyn_server.js', <<'JS');
(function() {
    var http = nginx.http;
    var servers = http.servers;

    function findSrv(name) {
        return servers.find(function(s) { return s.name === name; });
    }

    /* static server handlers */
    findSrv('static1.local').locations
        .find(function(l) { return l.path === '/s1'; })
        .handler = function(r) { r.respond(200, {}, 'static1-ok'); };

    findSrv('static2.local').locations
        .find(function(l) { return l.path === '/s2'; })
        .handler = function(r) { r.respond(200, {}, 'static2-ok'); };

    /* add dynamic1.local */
    var dyn1 = http.addServer('dynamic1.local');
    dyn1.addLocation('/').handler = function(r) {
        r.respond(200, {}, 'dynamic1-ok');
    };

    /* add dynamic2.local */
    var dyn2 = http.addServer('dynamic2.local');
    dyn2.addLocation('/').handler = function(r) {
        r.respond(200, {}, 'dynamic2-ok');
    };

    /* /probe_remove: removes dynamic1.local, checks nonexistent */
    findSrv('static1.local').locations
        .find(function(l) { return l.path === '/probe_remove'; })
        .handler = function(r) {
            var removed = http.removeServer('dynamic1.local');
            var missing = http.removeServer('nonexistent.local');
            http.rebuildVhostDispatch();
            r.respond(200, {}, (removed ? 'removed' : 'not-removed')
                               + ':' + (missing ? 'found' : 'false'));
        };

    http.rebuildVhostDispatch();
})();
JS

$t->run();

sub vhost {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# ---- static servers are alive ----
like(vhost('static1.local', '/s1'), qr/200 OK/,    'static1: 200');
like(vhost('static1.local', '/s1'), qr/static1-ok/, 'static1: body');

like(vhost('static2.local', '/s2'), qr/200 OK/,    'static2: 200');
like(vhost('static2.local', '/s2'), qr/static2-ok/, 'static2: body');

# ---- dynamic servers live after addServer + rebuildVhostDispatch ----
like(vhost('dynamic1.local', '/'), qr/200 OK/,     'dynamic1: 200');
like(vhost('dynamic1.local', '/'), qr/dynamic1-ok/, 'dynamic1: body');

like(vhost('dynamic2.local', '/'), qr/200 OK/,     'dynamic2: 200');
like(vhost('dynamic2.local', '/'), qr/dynamic2-ok/, 'dynamic2: body');

# ---- removeServer via probe ----
my $r = vhost('static1.local', '/probe_remove');
like($r, qr/200 OK/,   'probe_remove: 200');
like($r, qr/removed/,  'probe_remove: removeServer returned true');
like($r, qr/false/,    'probe_remove: removeServer on missing returned false');

# ---- dynamic1 is gone — falls to default server; dynamic1-ok body no longer served ----
unlike(vhost('dynamic1.local', '/'), qr/dynamic1-ok/, 'dynamic1 gone: no longer routing there after removal');

# ---- dynamic2 still alive ----
like(vhost('dynamic2.local', '/'), qr/dynamic2-ok/, 'dynamic2 still live after static1 removal');

# ---- static servers unaffected ----
like(vhost('static2.local', '/s2'), qr/static2-ok/, 'static2 still live after all ops');

$t->stop();
