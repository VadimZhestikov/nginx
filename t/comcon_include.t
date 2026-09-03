#!/usr/bin/perl

# COMCON M-CFG step 2/3: comcon.include(source, contract) — scope isolation.
#
# include compiles a fragment in a confined compartment on its OWN runtime
# (mirrors the tenant compartment, so teardown is clean), holds it C-side, and
# invokes it IN the compartment with JSON data marshaled across the boundary
# (no live object crosses the realm). AUTHORITY confinement: the fragment's
# free names resolve only through curated intrinsics — host authority (nginx)
# is structurally unreachable. RESOURCE confinement: the `meter` bounds CPU.
# (Live-cap grants are a follow-on; this is data-in/data-out.)

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

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /inc { }
    }
}
EOF

# Fragments are included once at config load (stage-0) and invoked per request.
$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/inc") {
        var xform = comcon.include(
            "function(x){ return {sees: typeof nginx, got: x.a * 2}; }");
        var slow = comcon.include(
            "function(){ var x=0; while(true){ x=(x+1)|0; } }",
            { meter: comcon.meter({ timeoutMs: 100 }) });

        locs[i].handler = function(req) {
            var t0 = Date.now(), aborted = false;
            try { slow(); } catch (e) { aborted = true; }
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify({ r: xform({a: 21}),
                                         aborted: aborted, dt: Date.now() - t0 }));
        };
    }
}
JS

$t->try_run('no js module')->plan(4);

my $body = http_get('/inc');

like($body, qr/"sees":"undefined"/,
     'AUTHORITY: a confined fragment cannot reach host authority (nginx)');
like($body, qr/"got":42/,
     'data-in/data-out: the arg is JSON-marshaled and computed on');
like($body, qr/"aborted":true/,
     'RESOURCE: a confined + metered runaway loop is aborted');
$body =~ /"dt":(\d+)/;
my $dt = $1 // 0;
cmp_ok($dt, '>=', 90, "meter fired at ~the budget (dt=${dt}ms)");
