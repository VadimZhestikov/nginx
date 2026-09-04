#!/usr/bin/perl

# COMCON increment D5b-1 — the declarative-profile checker (syntax_allowed) +
# descriptor-table normal form. comcon.reviewDeclarative(source) is a SOUND
# rejecter: it accepts only a straight-line sequence of fluent call-chains over
# dotted name paths with literal / nested-chain / free-name-ref arguments (no
# loops, conditionals, operators, assignments, computed access, or functions),
# and returns diffable descriptor tables. realize(q, {profile:'declarative'})
# refuses a proposal that is not in the profile. This is the platform hook the
# config-language pattern needs: an untrusted proposal becomes soundly reviewable.

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
        location /d { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/d") {
        locs[i].handler = function(req) {
            var out = {};

            function ok(src){ try { comcon.reviewDeclarative(src); return true; }
                              catch(e){ return "ERR:"+e.message; } }
            function bad(src){ try { comcon.reviewDeclarative(src); return false; }
                               catch(e){ return /not declarative/.test(e.message); } }

            // --- accepted: fluent call-chains, dotted paths, literal/ref/obj args ---
            var r = comcon.reviewDeclarative(
                'nginx.http.route("/api").header("X-Env","prod")\n' +
                'limit("/api", { rps: 100, burst: 20 })\n' +
                'grant(env, "db", host.cap)');
            out.accepted   = r.declarative === true;
            out.stmtCount  = r.statements.length;                 // 3
            out.firstOp    = r.statements[0][0].op;                // "nginx.http.route"
            out.chainOp    = r.statements[0][1].op;                // "header"
            out.objArg     = r.statements[1][0].args[1].rps;       // 100 (obj literal)
            out.refArg     = r.statements[2][0].args[2].ref;       // "host.cap"

            // more accepted forms
            out.nested   = ok('route(pick("/a","/b"))');           // nested chain arg
            out.bareTrue = ok('flag(true, false, null)');          // literals
            out.arr      = ok('routes(["/a","/b","/c"])');         // array literal

            // --- rejected: not declarative ---
            out.loop     = bad('for (var i=0;i<3;i++) { route(i); }');
            out.cond     = bad('if (x) { route("/a"); }');
            out.assign   = bad('x = route("/a")');
            out.operator = bad('route(1 + 2)');
            out.computed = bad('route(cfg["key"])');
            out.func     = bad('route(function(){ return 1; })');
            out.arrow    = bad('handle(function(){}); use(() => 1)');
            out.keyword  = bad('while (true) noop()');

            // realize({profile:'declarative'}) refuses a non-declarative proposal
            var env2 = comcon.env();
            try { comcon.realize(comcon.quote('for(;;){ evil(); }'),
                                 { profile: 'declarative', imports: [] }, env2);
                  out.realizeRefused = false; }
            catch (e) { out.realizeRefused =
                        /admission refused: not declarative/.test(e.message); }

            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(16);

my $body = http_get('/d');

like($body, qr/"accepted":true/,          'a declarative sentence is accepted');
like($body, qr/"stmtCount":3/,            'descriptor tables: 3 statements');
like($body, qr/"firstOp":"nginx\.http\.route"/, 'dotted path op captured');
like($body, qr/"chainOp":"header"/,       'fluent chain step captured');
like($body, qr/"objArg":100/,             'object-literal argument parsed');
like($body, qr/"refArg":"host\.cap"/,     'free-name ref argument captured');
like($body, qr/"nested":true/,            'nested call-chain argument accepted');
like($body, qr/"bareTrue":true/,          'true/false/null literals accepted');
like($body, qr/"arr":true/,               'array literal accepted');
like($body, qr/"loop":true/,              'a loop is rejected (not declarative)');
like($body, qr/"cond":true/,              'a conditional is rejected');
like($body, qr/"assign":true/,            'an assignment is rejected');
like($body, qr/"operator":true/,          'an operator expression is rejected');
like($body, qr/"computed":true/,          'computed member access is rejected');
like($body, qr/"func":true/,              'a function argument is rejected');
like($body, qr/"realizeRefused":true/,    'realize({profile:declarative}) refuses a non-declarative proposal');
