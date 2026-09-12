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
            /* Every out.* is computed before the single respond(), so one throw
             * would take down every unrelated assertion with it. Calls that are
             * EXPECTED to succeed go through this, so a regression fails its own
             * check instead of blanking the whole report. */
            function review(src) {
                try { return comcon.reviewCalls(src, grants); }
                catch (e) { return { ok: false, checked: [], unchecked: [],
                                     threw: String(e.message || e) }; }
            }

            /* --- accepted: real member, correct arity --------------------- */
            var r = review('loc.addHook(fn)');
            out.ok_result   = !!(r && r.ok === true);
            out.ok_checked  = (r.checked.indexOf('loc.addHook') >= 0);

            /* clearHandler takes no arguments (sig has an empty param list) */
            var r0 = review('loc.clearHandler()');
            out.ok_zeroarity = (r0.checked.indexOf('loc.clearHandler') >= 0);

            /* optional parameter: removeLocation is 1..2 */
            var r2 = review('loc.removeLocation("/x")');
            var r3 = review('loc.removeLocation("/x", { hard: true })');
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
            var u = review('somethingUngranted.doThing("x")');
            out.soft_ungranted = (u.ok === true &&
                                  u.unchecked.indexOf('somethingUngranted.doThing') >= 0 &&
                                  u.checked.length === 0);

            /* chain after a NON-handle return: addHook returns void, so the
             * next step's receiver type is unknown and must not be guessed */
            var c = review('loc.addHook(fn).whateverNext()');
            out.soft_chained = (c.ok === true &&
                                c.checked.indexOf('loc.addHook') >= 0 &&
                                c.unchecked.indexOf('whateverNext') >= 0);

            /* --- M4 RETURN-TYPE BINDING ----------------------------------- */
            /* addLocation returns handle<NginxLocation>, so the NEXT step is
             * checkable against NginxLocation with no location in existence. */
            var ch = review('loc.addLocation({}).addHook(fn)');
            out.m4_chain_checked = (ch.ok === true &&
                                    ch.checked.indexOf('loc.addLocation') >= 0 &&
                                    ch.checked.indexOf('addHook') >= 0 &&
                                    ch.unchecked.length === 0);

            /* the point of the whole thing: a typo PAST the first call used to
             * be admitted silently and fail at request time. Now refused. */
            var mc = refused('loc.addLocation({}).nopeNope()');
            out.m4_chain_typo = (mc !== false) &&
                                /unknown member 'nopeNope' on NginxLocation/.test(mc);

            /* arity is enforced on chained steps too */
            var ma = refused('loc.addLocation({}).addHook()');
            out.m4_chain_arity = (ma !== false) && /expects 1\.\.1 argument/.test(ma);

            /* a DOTTED chained step needs namespace typing on top of the
             * return type, so it stays unchecked rather than guessed */
            var dn = review('loc.addLocation({}).ssl.setCiphers("X")');
            out.m4_dotted_soft = (dn.ok === true &&
                                  dn.unchecked.indexOf('ssl.setCiphers') >= 0);

            /* describeType itself: by TYPE NAME, no instance needed.
             * Guarded so a null/!absent result fails ONE assertion instead of
             * throwing and taking every unrelated check down with it. */
            function dtype(t, m) {
                try { return nginx.describeType(t, m); } catch (e) { return undefined; }
            }
            var dt = dtype('NginxLocation', 'addHook');
            out.dt_member = !!(dt && dt.callable === true);
            var dta = dtype('NginxLocation');
            out.dt_all    = !!(dta && dta.length > 5);
            out.dt_bogus  = (dtype('NoSuchClass', 'addHook') === null);
            var dtr = dtype('NginxLocation', 'addLocation');
            out.dt_returns = !!(dtr && dtr.returns === 'handle<NginxLocation>');

            /* --- still a sound rejecter: non-declarative source throws ----- */
            var nd = refused('if (x) { loc.addHook(fn); }');
            out.bad_nondeclarative = (nd !== false) && /not declarative/.test(nd);

            req.respond(200, {"Content-Type":"application/json"}, JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(18);

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
like($r, qr/"soft_chained":true/,     'chain after a void return stays unchecked');

# M4 return-type binding: the chain's receiver TYPE is carried across steps.
like($r, qr/"m4_chain_checked":true/,
     'M4: a step after handle<NginxLocation> is CHECKED, not unchecked');
like($r, qr/"m4_chain_typo":true/,
     'M4: a typo past the first call is now refused, naming the type');
like($r, qr/"m4_chain_arity":true/,
     'M4: arity is enforced on chained steps too');
like($r, qr/"m4_dotted_soft":true/,
     'M4 soundness: a DOTTED chained step is still unchecked, not guessed');
like($r, qr/"dt_member":true/,        'describeType(type, member) resolves by name');
like($r, qr/"dt_all":true/,           'describeType(type) lists the classified table');
like($r, qr/"dt_bogus":true/,         'describeType on an unknown type yields null');
like($r, qr/"dt_returns":true/,       'describeType carries the M2b return type');
like($r, qr/"bad_nondeclarative":true/, 'non-declarative source still rejected');
