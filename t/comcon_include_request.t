#!/usr/bin/perl

# COMCON CONVERGE P4: the confined-request-handler scenario of
# comcon_tenant_request.t, re-expressed on the ONE include primitive +
# location.handler (no js_tenant_source / js_tenant_handler / onRequest).
#
# A confined include fragment holds a live-cap grant (a socket), serves real
# requests via location.handler, and the response body carries the confinement
# proof: host authority is unreachable (typeof nginx === "undefined") and the A1
# reach gate isolates a granted socket (.listener stays null) during a request.
# This is the interpreted-tier parity that lets js_tenant_* retire (P6).

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# Only js_source — no js_tenant_* directives.
$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;

        location /t    { }
        location /host { return 200 "host-ok\n"; }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

// The confined handler: a live-cap grant (granted) + admission (nginx must be
// listed to even be referenced — here it is, to probe it reads as undefined).
var h = comcon.include(
    "function(req){" +
    "  var iso = (typeof granted === 'object' && granted.listener === null)" +
    "            ? 'isolated' : 'LEAK';" +
    "  return { status: 200," +
    "           body: 'tenant ' + req.method + ' ' + req.uri + ' ' + iso" +
    "                 + ' nginx=' + (typeof nginx) }; }",
    { grants: { granted: sock }, imports: ['nginx'] });

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/t") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method, uri: req.uri });
            req.respond(o.status, {'content-type': 'text/plain'}, o.body);
        };
    }
}
nginx.log(6, "JSTEST include handler ready");
JS

$t->try_run('no js module')->plan(6);

my $r1 = http_get('/t');
like($r1, qr/ 200 /, 'confined include fragment serves a real request (200)');
like($r1, qr/tenant GET \/t /,
     'the marshaled request carries the real method + uri on the request path');
like($r1, qr/isolated/,
     'A1 gate isolates during a live request: granted socket .listener null');
like($r1, qr/nginx=undefined/,
     'host authority is unreachable in the confined fragment');

my $r2 = http_get('/t');
like($r2, qr/tenant GET \/t isolated/,
     'second request served (fragment JSValue lifecycle stable across calls)');

like(http_get('/host'), qr/host-ok/,
     'host locations are unaffected by the confined include path');
