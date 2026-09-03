#!/usr/bin/perl

# COMCON: admit phase (iii) — the test-phase. include(source, {tests}) runs the
# contract's tests against the compiled fragment IN the confined compartment,
# refusing admission if any test throws. The tests run with ZERO BLAST RADIUS
# (no host authority in scope) — a test that reaches for the host fails, which
# is exactly how a host admits an untrusted / AI-generated fragment by verifying
# its BEHAVIOR before granting it authority.

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
        location /a { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/a") {
        locs[i].handler = function(req) {
            var r = {};
            function tri(fn){ try { fn(); return "ok"; } catch(e){ return ""+e; } }

            // a passing test admits the fragment
            r.pass = tri(function(){
                comcon.include("function(x){ return x.a * 2; }",
                    { tests: "function(f){ if (f({a:21}) !== 42) throw new Error('math'); }" });
            });

            // a failing test refuses admission
            r.fail = tri(function(){
                comcon.include("function(x){ return x.a * 2; }",
                    { tests: "function(f){ if (f({a:21}) !== 999) throw new Error('want999'); }" });
            });

            // a test that throws directly refuses admission
            r.thrown = tri(function(){
                comcon.include("function(x){ return 1; }",
                    { tests: "function(f){ throw new Error('nope'); }" });
            });

            // zero blast radius: a test that reaches for the host fails (nginx
            // is unreachable in the compartment) -> admission refused
            r.confined = tri(function(){
                comcon.include("function(x){ return 1; }",
                    { tests: "function(f){ return nginx.version; }" });
            });

            req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
        };
    }
}
JS

$t->try_run('no js module')->plan(4);

my $body = http_get('/a');

like($body, qr/"pass":"ok"/,
     'a fragment whose contract tests PASS is admitted');
like($body, qr/"fail":"[^"]*admission refused: test failed[^"]*want999/,
     'a fragment whose contract tests FAIL is refused (reproducible reason)');
like($body, qr/"thrown":"[^"]*admission refused: test failed[^"]*nope/,
     'a test that throws refuses admission');
like($body, qr/"confined":"[^"]*admission refused: test failed/,
     'zero blast radius: the test runs confined (host unreachable) -> refused');
