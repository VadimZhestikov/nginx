#!/usr/bin/perl

# COMCON increment B (B0): learning mode — the onboarding harvest.
#
# `js_tenant_mode learn;` seeds the tenant's global with a recorder for each
# withheld host-authority name. A fragment written against a richer host than
# it was granted then RUNS to completion instead of failing, and every access
# path it walks into that surface is captured. The host reads
# nginx.tenantLearning() to get the exact wishlist — grant the safe subset,
# then switch to enforce.

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

js_tenant_mode learn;
js_source        %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /go    { js_tenant_handler; }
        location /learn { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/learn") {
        locs[i].handler = function(req) {
            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(nginx.tenantLearning()));
        };
    }
}
JS

# Written against a host it was never granted — legal to run in learn mode.
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    nginx.http.addServer({});   // wants nginx.http.addServer
    createSocket("127.0.0.1:9");  // wants createSocket()
    fetch("http://x/");         // wants fetch()
    return "handled " + req.uri;
});
JS

$t->try_run('no js module')->plan(6);

like(http_get('/go'), qr/handled \/go/,
     'learn mode: the fragment runs to completion (reaches recorded, not fatal)');

my $h = http_get('/learn');

like($h, qr/"mode":"learn"/, 'report: learn mode');
like($h, qr/"path":"nginx\.http\.addServer"/,
     'harvest: nginx.http.addServer wanted (the reach path)');
like($h, qr/"path":"createSocket\(\)"/,
     'harvest: createSocket() wanted (a direct call)');
like($h, qr/"path":"fetch\(\)"/,
     'harvest: fetch() wanted');

# enforce is unchanged: the same names are simply absent (not recorded)
like($t->read_file('error.log'), qr/js learn: comp=1 wants "createSocket\(\)"/,
     'the wishlist is also logged for the operator');
