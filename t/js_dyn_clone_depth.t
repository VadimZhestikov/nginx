#!/usr/bin/perl

# Stage 47 DYN: clone depth — srv.clone({depth:N}) + loc.clone()
#
# Verifies depth-limited cloning for servers and location-level cloning:
#
#   srv.clone(name, {depth:0})     → empty server (= addServer())
#   srv.clone(name, {depth:1})     → top-level locations only
#   srv.clone(name)                → full tree (depth:Infinity, current default)
#
#   loc.clone(newPattern)          → copies location only (depth:0 default)
#   loc.clone(newPattern,{depth:1})→ location + direct children (repathied)
#
# Two static servers are required so nginx builds a vhost dispatch hash.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(22);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/clone_depth.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name source.local;

        location /api      { }
        location /api/v1   { }
        location /api/v2   { }
        location /other    { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name dummy.local;

        location /dummy { }
    }
}
EOF

$t->write_file('clone_depth.js', <<'JS');
(function() {
    var http = nginx.http;

    function findSrv(name) {
        return http.servers.find(function(s) { return s.name === name; });
    }

    function findLoc(srv, path) {
        return srv.locations.find(function(l) { return l.path === path; });
    }

    var src = findSrv('source.local');

    /* Assign handlers to source locations */
    findLoc(src, '/api').handler    = function(r) { r.respond(200, {}, 'api-ok');   };
    findLoc(src, '/api/v1').handler = function(r) { r.respond(200, {}, 'v1-ok');    };
    findLoc(src, '/api/v2').handler = function(r) { r.respond(200, {}, 'v2-ok');    };
    findLoc(src, '/other').handler  = function(r) { r.respond(200, {}, 'other-ok'); };

    /* srv.clone depth:0 → empty server */
    src.clone('d0.local', {depth: 0});

    /* srv.clone depth:1 → top-level only (/api, /other); /api/v1, /api/v2 excluded */
    src.clone('d1.local', {depth: 1});

    /* srv.clone depth:Infinity (default) → full tree */
    src.clone('full.local');

    /* loc.clone — no opts → depth:0 (location only, no children) */
    var newApi = findLoc(src, '/api').clone('/newapi');
    newApi.handler = function(r) { r.respond(200, {}, 'newapi-ok'); };

    /* loc.clone with depth:1 → /newapi2 + /newapi2/v1 + /newapi2/v2 */
    findLoc(src, '/api').clone('/newapi2', {depth: 1});
    /* handlers for /newapi2, /newapi2/v1, /newapi2/v2 are inherited from source */

    http.rebuildVhostDispatch();
})();
JS

$t->run();

sub vhost {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# ---- source server intact ----
like(vhost('source.local', '/api'),    qr/api-ok/,   'source: /api');
like(vhost('source.local', '/api/v1'), qr/v1-ok/,    'source: /api/v1');
like(vhost('source.local', '/api/v2'), qr/v2-ok/,    'source: /api/v2');
like(vhost('source.local', '/other'),  qr/other-ok/, 'source: /other');

# ---- depth:0 clone — no locations → fallback to default (403 or similar) ----
unlike(vhost('d0.local', '/api'),   qr/api-ok/,   'd0: /api not present');
unlike(vhost('d0.local', '/other'), qr/other-ok/, 'd0: /other not present');

# ---- depth:1 clone — top-level present, nested absent ----
like(vhost('d1.local', '/api'),    qr/api-ok/,  'd1: /api present');
like(vhost('d1.local', '/other'),  qr/other-ok/,'d1: /other present');
# /api/v1 falls back to /api (parent inclusive match) — NOT v1-ok
unlike(vhost('d1.local', '/api/v1'), qr/v1-ok/, 'd1: /api/v1 not present');
unlike(vhost('d1.local', '/api/v2'), qr/v2-ok/, 'd1: /api/v2 not present');

# ---- full clone — all locations ----
like(vhost('full.local', '/api'),    qr/api-ok/,   'full: /api');
like(vhost('full.local', '/api/v1'), qr/v1-ok/,    'full: /api/v1');
like(vhost('full.local', '/api/v2'), qr/v2-ok/,    'full: /api/v2');
like(vhost('full.local', '/other'),  qr/other-ok/, 'full: /other');

# ---- loc.clone (depth:0 default) — /newapi has custom handler, no children ----
like(vhost('source.local', '/newapi'),    qr/newapi-ok/, 'loc.clone: /newapi handler');
# /newapi/v1 falls to /newapi (parent) → newapi-ok, NOT v1-ok
unlike(vhost('source.local', '/newapi/v1'), qr/v1-ok/,  'loc.clone: no children');

# ---- loc.clone depth:1 — /newapi2 + /newapi2/v1 + /newapi2/v2 ----
# /newapi2 inherits /api handler → api-ok
like(vhost('source.local', '/newapi2'),    qr/api-ok/, 'loc.clone d1: /newapi2 ok');
# /newapi2/v1 inherits /api/v1 handler → v1-ok
like(vhost('source.local', '/newapi2/v1'), qr/v1-ok/,  'loc.clone d1: /newapi2/v1 ok');
# /newapi2/v2 inherits /api/v2 handler → v2-ok
like(vhost('source.local', '/newapi2/v2'), qr/v2-ok/,  'loc.clone d1: /newapi2/v2 ok');

# ---- source unaffected after all ops ----
like(vhost('source.local', '/api'),    qr/api-ok/,   'source intact: /api');
like(vhost('source.local', '/api/v1'), qr/v1-ok/,    'source intact: /api/v1');
like(vhost('source.local', '/other'),  qr/other-ok/, 'source intact: /other');

$t->stop();
