#!/usr/bin/perl

# COMCON CONVERGE P4: learn mode on the include compartment. In learn mode the
# compartment seeds recorders for the withheld host surface, so a confined
# fragment's references to ungranted host names are HARVESTED (the wishlist)
# instead of failing, and admission is relaxed (non-enforcing discovery). Same
# B0 behavior the tenant compartment gives — now on the one include primitive,
# configured entirely via operators (comcon.mode('learn'), no js_tenant_*).

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;

        location /go    { }
        location /learn { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
comcon.mode('learn');

// Written against a host surface it was never granted — legal in learn mode:
// the references are harvested, not fatal, and admission is relaxed.
var h = comcon.include(
    "function(req){" +
    "  nginx.http.addServer({});" +   // wants nginx.http.addServer
    "  createSocket('127.0.0.1:9');" + // wants createSocket()
    "  fetch('http://x/');" +          // wants fetch()
    "  return { status:200, body:'handled ' + req.uri }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/go") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method, uri: req.uri });
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
    if (locs[i].path === "/learn") {
        locs[i].handler = function(req) {
            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(nginx.tenantLearning()));
        };
    }
}
JS

$t->try_run('no js module')->plan(6);

like(http_get('/go'), qr/handled \/go/,
     'learn mode: the confined fragment runs to completion (reaches recorded, not fatal)');

my $h = http_get('/learn');

like($h, qr/"mode":"learn"/, 'report: learn mode');
like($h, qr/"path":"nginx\.http\.addServer"/,
     'harvest: nginx.http.addServer wanted (the reach path)');
like($h, qr/"path":"createSocket\(\)"/,
     'harvest: createSocket() wanted (a direct call)');
like($h, qr/"path":"fetch\(\)"/,
     'harvest: fetch() wanted');

like($t->read_file('error.log'), qr/js learn: comp=1 wants "createSocket\(\)"/,
     'the wishlist is also logged for the operator');
