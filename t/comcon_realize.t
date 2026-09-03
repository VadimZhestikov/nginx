#!/usr/bin/perl

# COMCON M-CFG: the quotation half of closure-vs-quotation (FOUNDATION §6).
# comcon.quote(source) is an inert, cap-free DESCRIPTION (zero authority);
# comcon.realize(q, contract, realizerEnv) gives it force under the REALIZER's
# authority — the operator-realizes-a-tenant-proposal path. Least-authority
# realization (R6): the contract is MANDATORY and the realization environment
# is the realizer's grants RESTRICTED to the quotation's declared free-name
# manifest (contract.imports), so a proposal can never reach a name it did not
# declare and the reviewer did not see — the confused-deputy fix. The existing
# admit gate enforces free-names ⊆ imports, charging refusal at the realizer.

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
        location /r { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/r") {
        locs[i].handler = function(req) {
            var out = {};

            // quote(): an inert, frozen, cap-free description (zero authority).
            var q = comcon.quote("function(a){ return a * 2; }");
            out.quoted = (typeof q === "object") && Object.isFrozen(q);

            // realize() REFUSES a closure as arg0 (the closure/quotation bit):
            // a plain function...
            try { comcon.realize(function(){}, {}, comcon.env());
                  out.refusedFn = false; }
            catch (e) { out.refusedFn = /must be a comcon\.quote/.test(e.message); }

            // ...and a bound include() result (a real closure).
            var clo = comcon.include("function(){ return 1; }", {});
            try { comcon.realize(clo, {}, comcon.env());
                  out.refusedClo = false; }
            catch (e) { out.refusedClo = /must be a comcon\.quote/.test(e.message); }

            // realize() requires a contract (mandatory — least authority).
            try { comcon.realize(q, undefined, comcon.env());
                  out.needK = false; }
            catch (e) { out.needK = /contract is mandatory/.test(e.message); }

            // realize() requires a realizer env.
            try { comcon.realize(q, {}, undefined);
                  out.needEnv = false; }
            catch (e) { out.needEnv = /realizer comcon\.env/.test(e.message); }

            // realize a clean quotation under the realizer, threading args.
            var r = comcon.env();
            var doubler = comcon.realize(q, { imports: [] }, r);
            out.realized = doubler(21);

            // Manifest gate (R6): a quotation that references an UNDECLARED
            // free name (not in contract.imports) is refused at realization —
            // the proposal cannot reach anything outside its reviewed manifest,
            // even a name the realizer's session might hold.
            var qh = comcon.quote("function(){ return secretHost.token; }");
            try { comcon.realize(qh, { imports: [] }, r);
                  out.gated = false; }
            catch (e) { out.gated = /admission refused/.test(e.message); }

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(7);

my $body = http_get('/r');

like($body, qr/"quoted":true/,     'quote(): inert frozen cap-free description');
like($body, qr/"refusedFn":true/,  'realize() refuses a plain function (a closure)');
like($body, qr/"refusedClo":true/, 'realize() refuses a bound include() (a closure)');
like($body, qr/"needK":true/,      'realize() requires a contract (least authority)');
like($body, qr/"needEnv":true/,    'realize() requires a realizer env');
like($body, qr/"realized":42/,     'realize() gives a quotation force + threads args');
like($body, qr/"gated":true/,      'R6: an undeclared free name is refused at realization');
