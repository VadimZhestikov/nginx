#!/usr/bin/perl

# COMCON M-CFG step 2: the host-JS capability layer — env / grant / mediate /
# meter / bind. env() is a fresh deny-by-default environment; grant places a
# capability; meter/mediate build the interceptor structure; bind associates
# the env with a fragment and ENFORCES the `meter` mediation (resource
# confinement) by tightening the worker's gas deadline around the call.
# (Scope isolation — a confined compartment — is a later slice.)

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
        location /ops { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/ops") {
        locs[i].handler = function(req) {
            var out = {};

            // env() + grant(): build a deny-by-default env, place a cap in it
            var e = comcon.env();
            comcon.grant(e, "lookup", { tag: "cap" });
            out.granted = !!(e.grants.lookup && e.grants.lookup.tag === "cap");

            // grant on a non-env rejects
            try { comcon.grant({}, "x", 1); out.nonEnvThrew = false; }
            catch (err) { out.nonEnvThrew = true; }

            // bind(env, SOURCE) compiles the source in the confined compartment
            // under the env (the real kernel bind = the env-first spelling of
            // include, NOT a metered host closure). A fresh grant-free env here.
            var be = comcon.env();
            var fast = comcon.bind(be, "function(a){ return 'fast:' + a; }");
            out.fast = fast(7);

            // bind WITH a meter bounds a runaway loop (resource confinement)
            var slow = comcon.bind(be,
                "function(){ var x = 0; while (true) { x = (x + 1) | 0; } }",
                { meter: comcon.meter({ timeoutMs: 100 }) });
            var t0 = Date.now(), aborted = false;
            try { slow(); } catch (err) { aborted = true; }
            out.aborted = aborted;
            out.dt = Date.now() - t0;

            // bind CONFINES (it is the env-first spelling of include, not a
            // metered host closure): the bound fragment cannot reach the host.
            var probe = comcon.bind(be, "function(){ return typeof nginx; }");
            out.confined = probe({});

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(7);

my $body = http_get('/ops');

like($body, qr/"granted":true/,        'env()/grant(): capability placed in the env');
like($body, qr/"nonEnvThrew":true/,    'grant() rejects a non-env first arg');
like($body, qr/"fast":"fast:7"/,       'bind(env, source) compiles + threads args');
like($body, qr/"aborted":true/,        'bind() with a meter aborts a runaway loop');
like($body, qr/"confined":"undefined"/,'bind() CONFINES: the fragment cannot reach the host (nginx)');
$body =~ /"dt":(\d+)/;
my $dt = $1 // 0;
cmp_ok($dt, '>=', 90, "meter fired at ~the budget (dt=${dt}ms, not early)");
cmp_ok($dt, '<', 2000, "meter bounded the loop (dt=${dt}ms < 2s, not a hang)");
