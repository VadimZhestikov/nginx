#!/usr/bin/perl

# COMCON step-4 (directive retirement): js_tenant_handler needs NO operator.
#
# A confined request handler is just a callable — and comcon.include(source)
# already returns one (marshal arg in, run the fragment confined, marshal result
# out). The EXISTING js_com location.handler setter carries it: the root script
# marshals the request to data, calls the confined fragment, and responds with
# its {status, body} result. No js_tenant_handler directive, no new operator —
# the confinement comes from the include callable, the binding from location.handler
# (the recursive-inclusion fundament: reuse the js_com primitive).

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# Only js_source; the location is a plain location {} — no js_tenant_handler.
$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
// A confined request handler: sees only marshaled request data, returns a
// data-only {status, body} — no host authority (typeof nginx === "undefined").
var handle = comcon.include(
    "function(req){ return {" +
    "  status: 200," +
    "  body: 'tenant saw ' + req.method + ' ' + req.uri" +
    "         + ' nginx=' + (typeof nginx) }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/t") {
        // The existing js_com location.handler carries the confined callable.
        locs[i].handler = function(req) {
            var out = handle({ method: req.method, uri: req.uri });
            req.respond(out.status, {'content-type': 'text/plain'}, out.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(3);

my $resp = http_get('/t');

like($resp, qr/HTTP\/1\.1 200 /,
     'the fragment\'s {status} shaped the response (data-out contract)');
like($resp, qr/tenant saw GET \/t/,
     'a confined include fragment serves a request via location.handler');
like($resp, qr/nginx=undefined/,
     'the request handler is CONFINED (host authority unreachable in the fragment)');
