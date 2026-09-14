#!/usr/bin/perl

# The POSTURE fields — `onViolation` and `profile`.
#
# MANUAL has written `bind(e, frag, { profile:'restrictive', onViolation:'audit' })`
# since v5.0, and INCREMENT_MLIB §4 recorded both as NOT SHIPPED for a good
# reason: nothing read them, and *a posture assembled from ignored keys would
# read like a policy and do nothing, which is worse than its absence — it would
# be believed, and by exactly the reader least able to check.*
#
# What changed is that there is now something to be a posture OF.  Ten mediation
# words enforce, and the audit/enforce switch exists — but only FLEET-WIDE, and
# that is the wrong granularity for the rollout MANUAL describes:
#
#     SHADOWING ONE TENANT'S NEW POLICY BY PUTTING THE FLEET IN AUDIT ALSO STOPS
#     ENFORCING EVERY OTHER TENANT'S.
#
# Which is a strictly worse posture than the one the operator is carefully trying
# to reach.  Observe-first has to be a property of the BINDING, and test 4 below
# is the whole point of this file: two fragments in one request, one shadowed and
# one enforced, at the same time.
#
# `profile` is read by being REFUSED where it cannot be honoured. 'restrictive'
# is what every mediation here already is — the vocabulary attenuates and never
# transforms — so it is accepted and means what it says.  'adaptive' is refused
# rather than ignored: accepting the word would make "this program runs standalone
# without COMCON" unfalsifiable for exactly the fragments where it matters.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

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
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /posture { }
        location /leak    { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

/* A capability whose gate always denies, so the MODE is the only variable.
 *
 * A CLOSED WINDOW rather than an elapsed `ttl`, and the reason is the trap `ttl`
 * left behind: its clock starts when the capability CROSSES into the
 * compartment, so a capability built and called inside one request is never
 * expired, and the probe would have measured nothing at all.  A window that has
 * already passed today is closed the instant it is built. */
var DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function hhmm(m) {
    m = ((m % 1440) + 1440) % 1440;
    var h = Math.floor(m / 60), mm = m % 60;
    return (h < 10 ? '0' : '') + h + ':' + (mm < 10 ? '0' : '') + mm;
}
function closed() {
    var now = new Date(), nm = now.getUTCHours() * 60 + now.getUTCMinutes();
    return comcon.mediate(comcon.mediate(sock, comcon.allow(['port'])),
               comcon.window({ days: DAYS[now.getUTCDay()],
                               from: hhmm(nm - 120), to: hhmm(nm - 60) }));
}

function frag(contract) {
    var c = { imports: [], grants: { s: closed() } }, k;
    for (k in contract) { c[k] = contract[k]; }
    return comcon.include("function(a){ var v=s.port;"
                        + " return v===undefined?'denied':'allowed'; }", c);
}

locs.forEach(function (l) {

if (l.path === '/posture') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* The capability must actually be expired before anything is asked
             * of it: `ttl` starts its clock when the capability CROSSES, so the
             * fragments are built first and the clock is read after. */
            var shadowed = frag({ onViolation: 'audit' });
            var enforced = frag({ onViolation: 'deny' });
            var inherits = frag({});

            /* --- the fleet is in ENFORCE --- */
            o.fleetEnforce = { shadowed: shadowed({}), enforced: enforced({}),
                               inherits: inherits({}) };

            /* --- and now the fleet is in AUDIT --- */
            comcon.mode('audit');
            o.fleetAudit = { shadowed: shadowed({}), enforced: enforced({}),
                             inherits: inherits({}) };
            comcon.mode('enforce');

            /* --- the mode does not LEAK past the invocation --- */
            o.modeAfter = nginx.tenantDenials().mode;

            /* --- profile: read, not ignored --- */
            o.profile = {}; o.why = {};
            ['restrictive', 'declarative', 'adaptive', 'sideways'].forEach(
                function (p) {
                    try {
                        var f = frag({ profile: p });
                        o.profile[p] = f.profile;
                    } catch (e) { o.profile[p] = e.code || e.name; }
                    /* The CODE is the same for both refusals, so the reason has
                     * to be asserted separately: 'adaptive' is refused because
                     * the transforming half does not exist, and an unknown word
                     * because it is unknown. A control that disabled only the
                     * adaptive branch left the unknown-profile branch refusing
                     * it anyway, and the test could not tell. */
                    try { frag({ profile: p }); o.why[p] = 'ACCEPTED'; }
                    catch (e) {
                        o.why[p] = /not implemented/.test(e.message)
                                   ? 'unimplemented'
                                   : (/unknown profile/.test(e.message)
                                      ? 'unknown' : 'other'); }
                });

            /* an unknown onViolation is refused rather than read as "enforce" */
            o.badOnViolation = 'ACCEPTED';
            try { frag({ onViolation: 'maybe' }); }
            catch (e) { o.badOnViolation = e.code || e.name; }

            /* the default profile reads back as restrictive: every mediation
             * here attenuates, so that is not a guess */
            o.defaultProfile = frag({}).profile;
            o.defaultOnViolation = frag({}).onViolation;

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* A fragment that THROWS under a shadow posture must not leave the worker in
 * audit mode: the restore has to happen on the exception path too. */
if (l.path === '/leak') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var boom = comcon.include(
                "function(a){ throw new Error('boom'); }",
                { imports: [], onViolation: 'audit' });
            try { boom({}); o.threw = 'NO'; } catch (e) { o.threw = 'yes'; }
            o.modeAfterThrow = nginx.tenantDenials().mode;

            /* THE POSTURE COVERS THE MARSHALLING, because the marshalling is part
             * of the invocation: SR-1 deliberately materializes the result INSIDE
             * the tenant compartment, so a getter on the returned object is
             * fragment code and its gates are the fragment's.  The fleet is in
             * ENFORCE and this binding asks for AUDIT, so the getter's closed
             * window must be logged and ALLOWED -- which it can only be if the
             * posture had not already been restored when the getter ran. */
            var g = comcon.include(
                "function(a){ return { get v(){ return s.port; } }; }",
                { imports: [], grants: { s: closed() }, onViolation: 'audit' });
            o.marshalAudit = g({}).v;

            /* ...and the same shape with the binding in DENY is undefined, so the
             * assertion above is the posture doing it and not the window being
             * open after all. */
            var g2 = comcon.include(
                "function(a){ return { get v(){ return s.port; } }; }",
                { imports: [], grants: { s: closed() }, onViolation: 'deny' });
            o.marshalDeny = g2({}).v;
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(12);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {};
    };
    return $o;
}

my $o = get_json('/posture');
is($o->{driverError}, undef, 'the posture fields are usable')
    or diag("driverError: $o->{driverError}");

# --- THE POINT OF THE FILE ---
is_deeply($o->{fleetEnforce},
   { shadowed => 'allowed', enforced => 'denied', inherits => 'denied' },
   'ONE REQUEST, TWO POSTURES: with the fleet in ENFORCE, the binding that asked '
   . 'for onViolation:audit is SHADOWED (its denial is logged and allowed) while '
   . 'the binding beside it still enforces. Before this, shadowing one tenant '
   . "meant putting the fleet in audit -- which stops enforcing everyone else's "
   . 'policy, a strictly worse posture than the one being carefully reached')
    or diag('fleetEnforce: ' . encode_json($o->{fleetEnforce}));

is_deeply($o->{fleetAudit},
   { shadowed => 'allowed', enforced => 'denied', inherits => 'allowed' },
   '...and the other direction: with the FLEET in audit, a binding that asked '
   . 'for onViolation:deny still ENFORCES, while a binding with no opinion '
   . 'inherits the fleet posture. The per-binding value wins in both '
   . 'directions, because which way is stricter is the operator\'s call')
    or diag('fleetAudit: ' . encode_json($o->{fleetAudit}));

is($o->{modeAfter}, 'enforce',
   'the per-binding mode does not LEAK: it is restored in C after the '
   . 'invocation, so it cannot reach the next request');

# --- profile ---
is_deeply($o->{profile},
   { restrictive => 'restrictive', declarative => 'declarative',
     adaptive => 'E_ADMIT_CONTRACT', sideways => 'E_ADMIT_CONTRACT' },
   '`profile` is READ, by being refused where it cannot be honoured: '
   . 'restrictive is what every mediation here already is, and adaptive is '
   . 'refused rather than ignored -- accepting the word would make "runs '
   . 'standalone without COMCON" unfalsifiable for exactly the fragments where '
   . 'it matters')
    or diag('profile: ' . encode_json($o->{profile}));

is_deeply($o->{why},
   { restrictive => 'ACCEPTED', declarative => 'ACCEPTED',
     adaptive => 'unimplemented', sideways => 'unknown' },
   '...and for the right REASON, which the shared code cannot show: adaptive is '
   . 'refused because the transforming half does not exist, `sideways` because '
   . 'it is not a profile at all. Asserted separately because a control that '
   . 'disabled only the adaptive branch left the unknown-profile branch refusing '
   . 'it anyway, and nothing noticed')
    or diag('why: ' . encode_json($o->{why}));

is($o->{badOnViolation}, 'E_ADMIT_CONTRACT',
   'an unknown onViolation is refused, not read as the stricter value: a '
   . 'posture word nobody enforces is worse than its absence, because it would '
   . 'be believed');

is($o->{defaultProfile}, 'restrictive',
   'the default profile reads back as restrictive -- not a guess: every word in '
   . 'the vocabulary attenuates and none transforms');
is($o->{defaultOnViolation}, 0,
   '...and with no onViolation the binding INHERITS the fleet posture, rather '
   . 'than silently picking one');

# --- the exception path ---
my $l = get_json('/leak');
cmp_ok($l->{marshalAudit}, '>', 0,
   'THE POSTURE COVERS THE MARSHALLING. SR-1 materializes the result inside the '
   . 'tenant compartment, so a getter on the returned object is fragment code: '
   . 'with the fleet in ENFORCE and this binding in AUDIT, its closed window is '
   . 'logged and ALLOWED -- which it can only be if the posture had not already '
   . 'been restored when the getter ran. The identity and the allowance end at '
   . 'the same boundary, and an SR-1 assertion caught it when they did not')
    or diag('marshalAudit: ' . ($l->{marshalAudit} // 'undef'));
is($l->{marshalDeny}, undef,
   '...and the same shape with the binding in DENY reads undefined, so what is '
   . 'being measured is the POSTURE and not a window that was open anyway');

is_deeply([$l->{threw}, $l->{modeAfterThrow}], ['yes', 'enforce'],
   'a fragment that THROWS under a shadow posture does not leave the worker in '
   . 'audit mode: the restore happens on the exception path too, or one bad '
   . 'fragment would quietly unshield every later request in that worker')
    or diag('leak: ' . encode_json($l));

$t->stop();
