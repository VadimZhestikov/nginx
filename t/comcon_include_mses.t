#!/usr/bin/perl

# COMCON CONVERGE P4 / P6 lockdown-test strategy: the M-SES escape suite of
# comcon_mses.t, re-expressed on include. The M-SES lockdown (ngx_js_tenant_
# lockdown) is SHARED code — it runs in both tenant_ctx and comcon_ctx — so the
# engine-escape resistance and curated-intrinsic behavior hold identically for a
# confined include fragment. Proving it here is the basis for retiring the tenant
# harness at P6 (the lockdown stays; its probes move to include).

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
        location /m { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var h = comcon.include(
    "function(req){" +
    "  function closed(fn){ try { fn(); return 'open'; } catch(e){ return 'closed'; } }" +
    "  var r = [" +
    "    closed(function(){ return [].constructor.constructor('return 1')(); })," +
    "    closed(function(){ return Object.constructor('return 1')(); })," +
    "    closed(function(){ return Object.getPrototypeOf(function*(){}).constructor('x'); })," +
    "    closed(function(){ return Object.getPrototypeOf(async function(){}).constructor('x'); })" +
    "  ].join(',');" +
    "  var std = [" +
    "    JSON.stringify({a:[1,2].map(function(x){return x*2;})})," +
    "    String(Math.max(3,4))," +
    "    String(/ab/.test('zaby'))," +
    "    String(new Map([['k',7]]).get('k'))," +
    "    String(new Set([1,1,2]).size)," +
    "    (typeof Promise)," +
    "    (typeof new Uint8Array(2).length)," +
    "    (typeof new Date().getTime())" +
    "  ].join(' ');" +
    "  return { status:200, body: 'routes=' + r + ' | std=' + std }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/m") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method });
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(2);

my $body = http_get('/m');

like($body, qr/routes=closed,closed,closed,closed/,
     'M-SES: every dynamic-code escape route stays tamed in the confined fragment');
like($body, qr{std=\{"a":\[2,4\]\} 4 true 7 2 function number number},
     'curated standard JS all works (Array/JSON/Math/RegExp/Map/Set/Promise/TypedArray/Date)');
