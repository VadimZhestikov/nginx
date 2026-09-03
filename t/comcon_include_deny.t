#!/usr/bin/perl

# COMCON CONVERGE P4: the deny-by-default scenario of comcon_tenant_deny.t,
# re-expressed on include. A confined include fragment runs in a reduced
# compartment: it cannot name the nginx global (typeof === "undefined") and a
# withheld capability (createSocket) is unreachable — the same deny-by-default
# the tenant compartment gives, now on the one include primitive.

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
        location /d { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
// control: the host js_source has full authority (nginx is an object).
if (typeof nginx === "object" && nginx !== null) {
    nginx.log(6, "JSTEST PASS host_sees_nginx");
}

var h = comcon.include(
    "function(req){" +
    "  var r = [];" +
    "  r.push('nginx=' + typeof nginx);" +
    "  try { nginx.createSocket('127.0.0.1:1'); r.push('createSocket=reachable'); }" +
    "  catch (e) { r.push('createSocket=denied'); }" +
    "  return { status:200, body: r.join(' ') }; }",
    { imports: ['nginx'] });

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/d") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method });
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(3);

like($t->read_file('error.log'), qr/JSTEST PASS host_sees_nginx/,
     'control: host js_source sees the nginx global');

my $body = http_get('/d');
like($body, qr/nginx=undefined/,
     'deny-by-default: the confined fragment cannot name the nginx global');
like($body, qr/createSocket=denied/,
     'deny-by-default: a withheld capability is unreachable in the fragment');
