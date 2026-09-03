#!/usr/bin/perl

# COMCON increment D3 — POM-node quotations + stone splices. comcon.quote() now
# takes producer `splices`: cap-free STONE data (deep-frozen primitives/records;
# no functions, caps, or accessors — checked at quote time, a stage-0 error at
# the PRODUCER). realize() binds each splice as a JSON LITERAL in an enclosing
# IIFE var, so the quoted code resolves the splice name to escaped DATA — a
# spliced string can never smuggle code (JSON.stringify escaping = the
# parameterized-SQL defense). And a POM node's own quote() (D1) is realizable:
# realize over a real subtree compiles + runs it.

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
        location /sp { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/sp") {
        locs[i].handler = function(req) {
            var out = {};
            var env = comcon.env();

            // a stone splice flows in as data.
            var q1 = comcon.quote("function(){ return LIMIT * 2; }",
                                  { LIMIT: 21 });
            out.scalar = comcon.realize(q1, { imports: [] }, env)();  // 42

            // a nested (record) stone splice.
            var q2 = comcon.quote("function(){ return CFG.max + CFG.min; }",
                                  { CFG: { max: 7, min: 3 } });
            out.record = comcon.realize(q2, { imports: [] }, env)();  // 10

            // INJECTION IS NEUTRALIZED: a string splice binds as escaped data,
            // never as code. If this were textual substitution the fragment
            // would return 999; as a JSON literal it returns the literal string.
            var evil = "x))); return 999; ((0";
            var q3 = comcon.quote("function(){ return TOKEN; }",
                                  { TOKEN: evil });
            out.injected = comcon.realize(q3, { imports: [] }, env)();
            out.safe = (out.injected === evil);   // data, not 999

            // a spliced FUNCTION (a would-be capability) is refused at quote() —
            // a stage-0 error at the producer.
            try { comcon.quote("function(){}", { f: function(){} });
                  out.fnRefused = false; }
            catch (e) { out.fnRefused = /not cap-free|carries a capability/
                                        .test(e.message); }

            // a spliced confined callable (a real cap) is refused too.
            var clo = comcon.include("function(){ return 1; }", {});
            try { comcon.quote("function(){}", { c: clo });
                  out.capRefused = false; }
            catch (e) { out.capRefused = /not cap-free|carries a capability/
                                         .test(e.message); }

            // a getter splice (TOCTOU-unsound) is refused.
            var trap = {}; Object.defineProperty(trap, "x", { enumerable: true,
                get: function(){ return 1; } });
            try { comcon.quote("function(){}", { t: trap });
                  out.getRefused = false; }
            catch (e) { out.getRefused = /accessor/.test(e.message); }

            // a POM node's quote() is realizable: realize over a real subtree.
            var frag = function(a) {
                function twice(x) { return x + x; }
                return twice(a);
            };
            var root  = comcon.pom(frag);
            var child = root.children[0];              // the `twice` node
            var rz    = comcon.realize(child.quote(), { imports: [] }, env);
            out.nodeRealized = rz(6);                  // twice(6) = 12

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(9);

my $body = http_get('/sp');

like($body, qr/"scalar":42/,      'a scalar stone splice flows in as data');
like($body, qr/"record":10/,      'a nested (record) stone splice flows in');
like($body, qr/"safe":true/,      'injection neutralized: a string splice is escaped data');
like($body, qr/"injected":"x\)\)\); return 999; \(\(0"/, 'the splice returns the literal, not 999');
like($body, qr/"fnRefused":true/, 'a spliced function is refused at quote() (producer)');
like($body, qr/"capRefused":true/,'a spliced confined callable is refused at quote()');
like($body, qr/"getRefused":true/,'a getter splice (TOCTOU) is refused at quote()');
like($body, qr/"nodeRealized":12/,'a POM node quote() is realizable (realize over a subtree)');
like($body, qr/HTTP\/1\.1 200/,   'handler responded 200');
