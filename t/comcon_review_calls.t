#!/usr/bin/perl

# COMCON M3 — the admission-time CALL check, comcon.reviewCalls(source, grants).
#
# reviewDeclarative() proves a proposal has the declarative SHAPE, but the admit
# path discarded its descriptor table, so nothing verified the calls were real:
# a typo'd member or a wrong-arity call was admitted and only failed later, at
# request time, inside the tenant. reviewCalls walks that table and validates
# each call against the typed describe() registry (the M2b signatures), so the
# refusal happens at admission with the offending call path named.
#
# SOUNDNESS is the property under test: it must never guess. A receiver it
# cannot resolve statically (a chained step, an un-granted root) is reported in
# `unchecked` rather than rejected, so the check can only convert runtime
# failures into admission failures — never reject a valid proposal.

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
        location /rc { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/rc") {
        locs[i].handler = function(req) {
            var out = {};
            var loc = nginx.http.servers[0].locations[0];
            var grants = { loc: loc };

            function refused(src) {
                try { comcon.reviewCalls(src, grants); return false; }
                catch (e) { return String(e.message || e); }
            }

            /* --- accepted: real member, correct arity --------------------- */
            var r = comcon.reviewCalls('loc.addHook(fn)', grants);
            out.ok_result   = (r && r.ok === true);
            out.ok_checked  = (r.checked.indexOf('loc.addHook') >= 0);

            /* clearHandler takes no arguments (sig has an empty param list) */
            var r0 = comcon.reviewCalls('loc.clearHandler()', grants);
            out.ok_zeroarity = (r0.checked.indexOf('loc.clearHandler') >= 0);

            /* optional parameter: removeLocation is 1..2 */
            var r2 = comcon.reviewCalls('loc.removeLocation("/x")', grants);
            var r3 = comcon.reviewCalls('loc.removeLocation("/x", { hard: true })', grants);
            out.ok_optional = (r2.checked.length === 1 && r3.checked.length === 1);

            /* --- refused: member that does not exist ---------------------- */
            var m = refused('loc.addLocationTypo("/x")');
            out.bad_unknown = (m !== false) && /unknown member 'addLocationTypo'/.test(m)
                                            && /admission refused/.test(m);

            /* --- refused: wrong arity ------------------------------------- */
            var m2 = refused('loc.addHook()');
            out.bad_arity = (m2 !== false) && /expects 1\.\.1 argument/.test(m2);

            var m3 = refused('loc.clearHandler("nope")');
            out.bad_arity_zero = (m3 !== false) && /expects 0\.\.0 argument/.test(m3);

            /* --- SOUNDNESS: unresolvable receivers are UNCHECKED, not refused --- */
            /* un-granted root: that is the free-name gate's job, not ours */
            var u = comcon.reviewCalls('somethingUngranted.doThing("x")', grants);
            out.soft_ungranted = (u.ok === true &&
                                  u.unchecked.indexOf('somethingUngranted.doThing') >= 0 &&
                                  u.checked.length === 0);

            /* chained step: its receiver is the previous call's RESULT, which
             * needs return-type binding (M4) — must not be guessed at */
            var c = comcon.reviewCalls('loc.addHook(fn).whateverNext()', grants);
            out.soft_chained = (c.ok === true &&
                                c.checked.indexOf('loc.addHook') >= 0 &&
                                c.unchecked.indexOf('whateverNext') >= 0);

            /* --- still a sound rejecter: non-declarative source throws ----- */
            var nd = refused('if (x) { loc.addHook(fn); }');
            out.bad_nondeclarative = (nd !== false) && /not declarative/.test(nd);

            req.respond(200, {"Content-Type":"application/json"}, JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(10);

###############################################################################

my $r = http_get('/rc');

like($r, qr/"ok_result":true/,        'reviewCalls accepts a valid proposal');
like($r, qr/"ok_checked":true/,       'the validated call is reported in checked[]');
like($r, qr/"ok_zeroarity":true/,     'zero-argument method accepted');
like($r, qr/"ok_optional":true/,      'optional parameter accepted with and without it');
like($r, qr/"bad_unknown":true/,      'unknown member refused at admission, named');
like($r, qr/"bad_arity":true/,        'wrong arity refused (too few)');
like($r, qr/"bad_arity_zero":true/,   'wrong arity refused (too many)');
like($r, qr/"soft_ungranted":true/,   'un-granted root is unchecked, not refused');
like($r, qr/"soft_chained":true/,     'chained receiver is unchecked, not refused');
like($r, qr/"bad_nondeclarative":true/, 'non-declarative source still rejected');
