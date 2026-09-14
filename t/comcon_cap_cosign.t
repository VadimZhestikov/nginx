#!/usr/bin/perl

# M-LIB `cosign` — the two-person rule, and the ninth vocabulary word.
#
# `ttl` and `window` bound WHEN a capability may be used and `uses` bounds HOW
# OFTEN.  `cosign` bounds WHO, and it is the only one of them that cannot be
# satisfied by the holder alone: the operation runs when two DISTINCT principals
# have asked for it.
#
# THE HARD PART IS NOT THE COUNTER, IT IS WHO IS COUNTING.  COMCON does not
# authenticate (TM-2) -- the host asserts the principal and COMCON maps it.  So
# `as` is written by the operator's own configuration on the trusted side, and
# there is no path from inside a compartment that sets or changes it.  A fragment
# therefore holds exactly ONE identity per invocation and can cast exactly one
# vote: DISTINCTNESS IS STRUCTURAL, not checked.  Had `as` been a string the
# fragment could write, the word would be theatre -- one fragment voting twice
# under two names.
#
# Which is why the quorum assembles ACROSS INVOCATIONS: alice runs the policy and
# is denied pending a cosignature, bob runs the same policy and it executes.
# There is no approve() verb because THE ATTEMPT IS THE CONSENT.
#
# And so the surprising fact this file pins down: a cap.cosign DENIAL IS NOT
# "NOTHING HAPPENED".  The denied attempt recorded consent.  Test 5 measures that
# directly, because a reader has every right to disbelieve it.
#
# THE CONTROLS THAT HAVE TO FIRE, or nothing here is evidence:
#   * the SAME principal attempting twice must still be denied (otherwise the
#     rule counts attempts, which is a one-person rule with extra steps)
#   * a DIFFERENT quorum must meet by MAX, and the direction is measured by
#     behaviour -- a meet that took the smaller number would pass a test that
#     only checked "it did not throw"
#   * consent must NOT be recorded for an operation another gate refuses, or an
#     operator could gather signatures against a destination this capability can
#     never reach and spend them on the one it can
#   * consent must EXPIRE, and the `within` meet must take the shorter one

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

worker_processes 4;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /cosign { }
        location /expire { }
        location /fleet  { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (Object.prototype.hasOwnProperty.call(d, k)) { c[k] = d[k]; } }
    return c;
}
function fired(a, b) {
    var out = [], k;
    for (k in b) {
        if (!Object.prototype.hasOwnProperty.call(b, k)) { continue; }
        if ((b[k] || 0) > (a[k] || 0)) { out.push(k); }
    }
    return out.sort();
}

var PROBE = "function(a){ return out.request(a.url); }";

/* One arming helper: every difference in outcome below is a difference in the
 * MEDIATION, never in the probe. */
function arm(spec, extra) {
    var c = nginx.outbound();
    var m = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
    m = comcon.mediate(m, comcon.cosign(spec));
    if (extra) { m = comcon.mediate(m, extra); }
    var f = comcon.include(PROBE, { imports: [], grants: { out: m } });
    return { cap: c, f: f };
}

function attempt(a, url) {
    var b = counts();
    var r = a.f({ url: url || 'https://api.example.com/x' });
    return { result: r, fired: fired(b, counts()),
             queued: a.cap.pending().requests.length };
}

/* A fresh key per armed capability, so one assertion below cannot be satisfied
 * by consent another one gathered.  (The key names a DECISION; sharing one is a
 * deliberate act, and an accidentally shared one would make this file pass for
 * the wrong reason.) */
var seq = 0;
function k() { return 'probe' + (++seq); }

locs.forEach(function (l) {

if (l.path === '/cosign') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* --- the rule itself: two principals, one decision --- */
            var K = k();
            var alice = arm({ key: K, quorum: 2, within: 60, as: 'alice' });
            var bob   = arm({ key: K, quorum: 2, within: 60, as: 'bob' });
            o.first  = attempt(alice);
            o.second = attempt(bob);

            /* --- THE distinctness control: one principal, twice --- */
            var K2 = k();
            var a2 = arm({ key: K2, quorum: 2, within: 60, as: 'alice' });
            var b2 = arm({ key: K2, quorum: 2, within: 60, as: 'bob' });
            o.twiceA = attempt(a2);
            o.twiceB = attempt(a2);
            /* ...and now a DIFFERENT principal, which proves the two denials
             * above recorded exactly one consent between them. */
            o.thenBob = attempt(b2);

            /* --- THE RETRY, which is the actual ops-room sequence ---
             *
             * alice tries, is told to find a cosigner, bob cosigns, and ALICE
             * RETRIES.  Every assertion above has the SECOND principal perform
             * the operation, which is the one shape that works if the record's
             * membership test is wrong -- and it was: the first implementation
             * compared the quorum against the matched principal's POSITION in the
             * record rather than the record's LENGTH, so once the quorum was met
             * alice was still denied, forever, because she is first in the list.
             * Found by a test for a different word entirely. */
            var KR = k();
            var ra = arm({ key: KR, quorum: 2, within: 60, as: 'alice' });
            var rb = arm({ key: KR, quorum: 2, within: 60, as: 'bob' });
            o.retry = [attempt(ra).result, attempt(rb).result,
                       attempt(ra).result];

            /* --- quorum 3 --- */
            var K3 = k();
            var t3 = ['alice', 'bob', 'carol'].map(function (p) {
                return arm({ key: K3, quorum: 3, within: 60, as: p });
            });
            o.three = t3.map(function (a) { return attempt(a).result; });

            /* --- THE MEET'S DIRECTION, measured rather than asserted.
             * quorum 2 MEET quorum 3 must be 3: needing MORE signatures is
             * narrower.  So bob is denied and carol is what executes it.  A meet
             * that had taken the smaller number would pass a test that only
             * checked that the composition did not throw. */
            var K4 = k();
            function armMeet(p) {
                var c = nginx.outbound();
                var m = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
                m = comcon.mediate(m, comcon.cosign({ key: K4, quorum: 2,
                                                      within: 60, as: p }));
                m = comcon.mediate(m, comcon.cosign({ key: K4, quorum: 3,
                                                      within: 60, as: p }));
                var f = comcon.include(PROBE, { imports: [], grants: { out: m } });
                return { cap: c, f: f };
            }
            o.meetQuorum = ['alice', 'bob', 'carol'].map(function (p) {
                return attempt(armMeet(p)).result;
            });

            /* --- GATE ORDER: no consent for an operation another gate refuses.
             * alice aims at a host the glob forbids; her attempt is denied by
             * out.host and must record NOTHING, so bob aiming at a permitted
             * host is still one signature short. */
            var K5 = k();
            var a5 = arm({ key: K5, quorum: 2, within: 60, as: 'alice' });
            var b5 = arm({ key: K5, quorum: 2, within: 60, as: 'bob' });
            o.offGlob  = attempt(a5, 'https://api.evil.invalid/x');
            o.afterOff = attempt(b5);

            /* --- the meet refuses what it cannot narrow --- */
            function meet(s1, s2) {
                try {
                    comcon.mediate(comcon.mediate(nginx.outbound(),
                        comcon.cosign(s1)), comcon.cosign(s2));
                    return 'ALLOWED';
                } catch (e) { return e.code || e.name; }
            }
            var base = { key: 'm', quorum: 2, within: 60, as: 'alice' };
            function w(over) {
                var x = {}, p;
                for (p in base) { x[p] = base[p]; }
                for (p in over) { x[p] = over[p]; }
                return x;
            }
            o.meet = {
                same:      meet(base, w({})),
                quorum:    meet(base, w({ quorum: 3 })),
                within:    meet(base, w({ within: 30 })),
                otherKey:  meet(base, w({ key: 'other' })),
                otherAs:   meet(base, w({ as: 'bob' }))
            };

            /* --- nothing is defaulted --- */
            o.bad = {};
            var cases = {
                noKey:    { quorum: 2, within: 60, as: 'alice' },
                quorum1:  { key: 'x', quorum: 1, within: 60, as: 'alice' },
                quorum9:  { key: 'x', quorum: 9, within: 60, as: 'alice' },
                noQuorum: { key: 'x', within: 60, as: 'alice' },
                noWithin: { key: 'x', quorum: 2, as: 'alice' },
                noAs:     { key: 'x', quorum: 2, within: 60 },
                commaAs:  { key: 'x', quorum: 2, within: 60, as: 'a,b' }
            };
            Object.keys(cases).forEach(function (kk) {
                try { comcon.cosign(cases[kk]); o.bad[kk] = 'ACCEPTED'; }
                catch (e) { o.bad[kk] = e.code || e.name; }
            });

            /* --- a HAND-BUILT descriptor cannot slip past the producer --- */
            o.handBuilt = 'ACCEPTED';
            try {
                comcon.mediate(nginx.outbound(),
                               { flavor: 'cosign', key: 'x', quorum: 2 });
            } catch (e) { o.handBuilt = e.code || e.name; }

            /* --- it composes with the other four axes at once --- */
            var K6 = k();
            var now = new Date();
            var nowMin = now.getUTCHours() * 60 + now.getUTCMinutes();
            var DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
            function hhmm(m) {
                m = ((m % 1440) + 1440) % 1440;
                var h = Math.floor(m / 60), mm = m % 60;
                return (h < 10 ? '0' : '') + h + ':' + (mm < 10 ? '0' : '') + mm;
            }
            function armAll(p) {
                var c = nginx.outbound();
                var m = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
                m = comcon.mediate(m, comcon.cosign({ key: K6, quorum: 2,
                                                      within: 60, as: p }));
                m = comcon.mediate(m, comcon.ttl(3600));
                m = comcon.mediate(m, comcon.uses('cos:probe', 1, 60));
                m = comcon.mediate(m, comcon.window({ days: DAYS[now.getUTCDay()],
                          from: hhmm(nowMin - 60), to: hhmm(nowMin + 60) }));
                var f = comcon.include(PROBE, { imports: [], grants: { out: m } });
                return { cap: c, f: f };
            }
            o.composedA = attempt(armAll('alice')).result;
            o.composedB = attempt(armAll('bob')).result;

            /* --- a SOCKET capability, not only the outbound one: cosign lives
             * on the same gate path as every other mediation, so a masked field
             * read is cosigned too. */
            var K7 = k();
            function armSock(p) {
                var m = comcon.mediate(sock, comcon.allow(['port']));
                m = comcon.mediate(m, comcon.cosign({ key: K7, quorum: 2,
                                                      within: 60, as: p }));
                return comcon.include("function(a){ return s.port; }",
                                      { imports: [], grants: { s: m } });
            }
            var sb = counts();
            o.sockFirst  = armSock('alice')({});
            o.sockFired  = fired(sb, counts());
            o.sockSecond = armSock('bob')({});

            /* --- audit mode logs and ALLOWS, like every gate --- */
            comcon.mode('audit');
            o.audited = attempt(arm({ key: k(), quorum: 2, within: 60,
                                      as: 'alice' })).result;
            comcon.mode('enforce');

            o.refusalCodes = comcon.refusalCodes();

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* Consent EXPIRES, and the `within` meet takes the SHORTER window.
 *
 * Driven from Perl across a sleep, because the record's clock is nginx's and no
 * amount of arithmetic inside one request can advance it.  The capability is
 * mediated within=3600 and THEN within=1: if the meet had taken the larger
 * number, alice's signature would still be waiting after the sleep and bob would
 * execute the operation. */
if (l.path === '/expire') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            /* req.args is the RAW QUERY STRING, not a parsed object -- reading
             * it as req.args.as made both requests vote as alice, so this probe
             * passed identically whether the `within` meet took the shorter
             * window or the longer one.  The control below is what caught it. */
            var p = String(req.args || '').indexOf('as=bob') >= 0
                    ? 'bob' : 'alice';
            var c = nginx.outbound();
            var m = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
            m = comcon.mediate(m, comcon.cosign({ key: 'expiring', quorum: 2,
                                                  within: 3600, as: p }));
            m = comcon.mediate(m, comcon.cosign({ key: 'expiring', quorum: 2,
                                                  within: 1, as: p }));
            var f = comcon.include(PROBE, { imports: [], grants: { out: m } });
            var b = counts();
            o.result = f({ url: 'https://api.example.com/x' });
            o.fired = fired(b, counts());
            o.as = p;
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* FLEET-WIDE, proved without naming a worker.
 *
 * The principal is the WORKER INDEX, so a given worker can cast exactly one vote
 * no matter how many requests it serves.  A SUCCESS anywhere is therefore itself
 * the proof that the record crossed workers: two votes exist, and no single
 * worker could have cast them both.  There is no worker to look up and no pid to
 * compare, which is what made an earlier fan-out test flaky 4% of the time.
 *
 * A per-worker record would make this unreachable no matter how many requests
 * arrive -- which is the whole reason the record lives in nginx.shared, and the
 * same defect class as the per-process mode switch (v5.56).
 */
if (l.path === '/fleet') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var c = nginx.outbound();
            var m = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
            m = comcon.mediate(m, comcon.cosign({ key: 'fleetwide', quorum: 2,
                                  within: 300, as: 'w' + nginx.workerIdx }));
            var f = comcon.include(PROBE, { imports: [], grants: { out: m } });
            o.result = f({ url: 'https://api.example.com/x' });
            o.worker = nginx.workerIdx;
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(26);

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

my $o = get_json('/cosign');
is($o->{driverError}, undef, 'cosign() is usable on a capability')
    or diag("driverError: $o->{driverError}");

# --- the rule ---
is($o->{first}{result}, undef,
   'the FIRST principal is denied: one signature is not a quorum');
is_deeply($o->{first}{fired}, ['cap.cosign'],
   '...with cap.cosign, its own code -- the only denial in the set that means '
   . '"go find a colleague" rather than "no"');
is($o->{first}{queued}, 0, '...and the denied intent reached nothing');

cmp_ok($o->{second}{result}, '>', 0,
   'the SECOND, DIFFERENT principal executes it -- which is only possible if '
   . "the first, DENIED attempt had recorded alice's consent");
is_deeply($o->{second}{fired}, [], '...with nothing denied');

# --- distinctness ---
is($o->{twiceA}{result}, undef, 'one principal attempting twice: first denied');
is($o->{twiceB}{result}, undef,
   '...and STILL denied on the retry -- the record is the SET of principals, '
   . 'not a count of attempts, or a two-person rule would be a one-person rule '
   . 'with extra steps');
cmp_ok($o->{thenBob}{result}, '>', 0,
   '...and a different principal then executes it, so those two denials had '
   . 'recorded exactly one consent between them');

# --- the retry ---
is_deeply($o->{retry}, [undef, 1, 1],
   'THE RETRY: alice is denied, bob cosigns, and ALICE RETRIES SUCCESSFULLY -- '
   . 'which is the sequence an operations room actually performs. The first '
   . 'implementation denied it forever, because it compared the quorum against '
   . "the matched principal's POSITION in the record rather than the record's "
   . 'LENGTH, and alice is first. Every other assertion here has the SECOND '
   . 'principal perform the operation, which is exactly the shape that passes '
   . 'with that bug present')
    or diag('retry: ' . encode_json($o->{retry}));

# --- quorum 3 ---
is_deeply($o->{three}, [undef, undef, 1],
   'quorum 3 needs three distinct principals')
    or diag('three: ' . encode_json($o->{three}));

# --- the meet's direction, measured ---
is_deeply($o->{meetQuorum}, [undef, undef, 1],
   'quorum 2 MEET quorum 3 is 3, not 2: needing MORE signatures is the narrower '
   . 'authority, so the meet takes the MAX -- measured by which principal gets '
   . 'the operation, because a meet taking the smaller number would pass a test '
   . 'that only checked the composition did not throw')
    or diag('meetQuorum: ' . encode_json($o->{meetQuorum}));

# --- gate order ---
is_deeply($o->{offGlob}{fired}, ['out.host'],
   'a destination outside allowHosts is refused by out.host, before cosign');
is($o->{afterOff}{result}, undef,
   '...and recorded NO consent: otherwise an operator could gather signatures '
   . 'against a host this capability can never reach and spend them on the one '
   . 'it can');

# --- the meet's refusals ---
is_deeply($o->{meet},
   { same => 'ALLOWED', quorum => 'ALLOWED', within => 'ALLOWED',
     otherKey => 'E_CAP_ESCALATE', otherAs => 'E_CAP_ESCALATE' },
   'quorum and within compose (both are ordered); a different KEY or a '
   . 'different ACTING PRINCIPAL is refused -- merging keys would let consent '
   . 'for one decision authorize another, and two principals on one capability '
   . 'would have to vote as somebody')
    or diag('meet: ' . encode_json($o->{meet}));

# --- nothing defaulted ---
is_deeply($o->{bad},
   { noKey => 'E_CAP_FLAVOR', quorum1 => 'E_CAP_FLAVOR',
     quorum9 => 'E_CAP_FLAVOR', noQuorum => 'E_CAP_FLAVOR',
     noWithin => 'E_CAP_FLAVOR', noAs => 'E_CAP_PRINCIPAL',
     commaAs => 'E_CAP_PRINCIPAL' },
   'every malformed cosign is REFUSED: no key, quorum 1 (which is no rule at '
   . 'all), quorum past the record bound, no quorum, no window -- and the two '
   . 'about WHO get their own code, E_CAP_PRINCIPAL, because nothing was '
   . 'misspelled and nothing composed')
    or diag('bad: ' . encode_json($o->{bad}));

is($o->{handBuilt}, 'E_CAP_FLAVOR',
   'a HAND-BUILT cosign descriptor is refused at the producer, not carried as '
   . 'far as include()');

ok(grep({ $_ eq 'E_CAP_PRINCIPAL' } @{ $o->{refusalCodes} || [] }),
   'E_CAP_PRINCIPAL is in comcon.refusalCodes(), so a CI can pin to it');

# --- composition ---
is_deeply([$o->{composedA}, $o->{composedB}], [undef, 1],
   'cosign composes with allowHosts AND ttl AND uses AND window at once')
    or diag('composed: ' . encode_json([$o->{composedA}, $o->{composedB}]));

# --- the socket kind ---
is($o->{sockFirst}, undef,
   'a cosigned SOCKET capability denies a masked field read to one principal');
cmp_ok($o->{sockSecond}, '>', 0,
   '...and yields it to the second, so cosign is on the shared gate path and '
   . 'not an outbound-only feature');

# --- audit ---
cmp_ok($o->{audited}, '>', 0,
   'in AUDIT mode a missing cosignature logs and ALLOWS, so an operator can '
   . 'see which operations WOULD need a second pair of hands before it bites');

# --- expiry, and the within meet ---
my $e1 = get_json('/expire?as=alice');
is($e1->{result}, undef, 'consent expiry: the first principal is denied');
select(undef, undef, undef, 2.2);
my $e2 = get_json('/expire?as=bob');
is($e2->{result}, undef,
   'after the window passes the second principal is ALSO denied: the record '
   . 'expired, so bob is now the first signature -- and since the capability '
   . 'was mediated within=3600 and THEN within=1, this also shows the `within` '
   . 'meet took the SHORTER window')
    or diag('expire: ' . encode_json([$e1, $e2]));

# --- fleet-wide ---
my ($fleet_ok, @workers);
for (1 .. 60) {
    my $f = get_json('/fleet');
    push @workers, $f->{worker};
    if (($f->{result} || 0) > 0) { $fleet_ok = $f; last; }
}
my %seen; $seen{$_}++ for @workers;
diag('fleet: ' . scalar(@workers) . ' requests over workers '
     . join(',', sort { $a <=> $b } keys %seen));
ok(scalar(@workers) > 1,
   'the first worker to attempt is denied, so more than one request is needed');
ok(defined $fleet_ok,
   'the consent record is FLEET-WIDE: the principal is the worker index, so no '
   . 'single worker can cast two votes -- a success anywhere is therefore proof '
   . 'that the quorum assembled across processes, with no worker to look up and '
   . 'no pid to compare');

$t->stop();
