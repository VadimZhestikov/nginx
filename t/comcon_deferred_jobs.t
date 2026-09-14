#!/usr/bin/perl

# EVERY INVOCATION DRAINS TO QUIESCENCE — and why that is an invariant.
#
# A fragment can queue a job and return without awaiting it:
#
#     function(a){ Promise.resolve().then(function(){
#                      out.request('https://a.example.com/LATE'); });
#                  return 'returned'; }
#
# Nothing else in the process drains the compartment runtime — the host's drains
# are a different runtime — so that job used to sit pending until some LATER,
# UNRELATED invocation returned a promise, and then ran inside it.  Measured
# before the fix: the capability was untouched when the fragment returned, and
# exercised during the next fragment's settle loop.
#
#     A({})  ->  'returned'   cap.pending() = []
#     B({})  ->  1            cap.pending() = ["…/LATE"]     <-- A's job, in B
#
# EVERYTHING AN INVOCATION BOUNDS WAS THEREFORE THE WRONG INVOCATION'S.  The
# deferred use ran on a stranger's DEADLINE and MEMORY ALLOWANCE; it was gated at
# a stranger's wall-clock time, so `ttl` and `window` were evaluated at the wrong
# moment; and it ran under a stranger's `onViolation` POSTURE — so a shadowed
# fragment's deferred work could execute under an enforcing binding, or an
# enforced fragment's under audit.  It is also a channel: the first fragment
# spends the second one's job budget.
#
# This was UNREACHABLE until async fragments were admitted (v5.92), because a
# fragment that cannot name `Promise` cannot queue a job.  Widening admission
# opened it, so closing it belongs with that change rather than in a backlog.
#
# The three assertions that carry the weight are not "the job ran" but WHERE it
# ran: attributed to the right fragment (test 2), gated under the right POSTURE
# (test 5), and counted against the right fragment's VOLUME (test 7).

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

        location /defer   { }
        location /posture { }
        location /throw   { }
        location /forever { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

var DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function hhmm(m) {
    m = ((m % 1440) + 1440) % 1440;
    var h = Math.floor(m / 60), mm = m % 60;
    return (h < 10 ? '0' : '') + h + ':' + (mm < 10 ? '0' : '') + mm;
}

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

/* An unrelated async fragment whose promise needs the drain to run.  Before the
 * fix this was the vehicle: whatever a previous fragment left pending ran here. */
function bystander() {
    return comcon.include("async function(b){ return await 1; }", { imports: [] });
}

locs.forEach(function (l) {

if (l.path === '/defer') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            var cap = nginx.outbound();
            var m = comcon.mediate(cap, comcon.allowHosts('https://*.example.com'));
            var A = comcon.include(
                "function(a){ Promise.resolve().then(function(){"
              + " out.request('https://a.example.com/LATE'); });"
              + " return 'returned'; }",
                { imports: ['Promise'], grants: { out: m } });

            o.a = A({});
            o.afterA = cap.pending().requests.map(function (r) { return r.url; });
            o.b = bystander()({});
            o.afterB = cap.pending().requests.map(function (r) { return r.url; });

            /* VOLUME, attributed exactly.  The queue holds 32 records and counts
             * the rest as dropped, so a hundred deferred requests have an exact
             * fingerprint -- and it has to appear against A, not against the
             * fragment that happened to run next. */
            var cap2 = nginx.outbound();
            var m2 = comcon.mediate(cap2,
                         comcon.allowHosts('https://*.example.com'));
            var V = comcon.include(
                "function(a){ var i; for (i = 0; i < 100; i++) {"
              + " Promise.resolve().then(function(){"
              + "   out.request('https://a.example.com/v'); }); }"
              + " return 'queued'; }",
                { imports: ['Promise'], grants: { out: m2 } });
            o.v = V({});
            var q = cap2.pending();
            o.volumeAfterV = { queued: q.requests.length, dropped: q.dropped || 0 };
            bystander()({});
            var q2 = cap2.pending();
            o.volumeAfterB = { queued: q2.requests.length, dropped: q2.dropped || 0 };

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* THE POSTURE THE JOB IS GATED UNDER.
 *
 * A is bound with onViolation:'deny' and holds a capability whose window is
 * CLOSED; its deferred job calls through that capability.  Gated under A's own
 * posture the job is denied and nothing is recorded.  Left pending, it would
 * have run inside the bystander -- which is bound onViolation:'audit', where a
 * closed window is logged and ALLOWED.  So the same deferred call is denied or
 * permitted depending on WHOSE invocation runs it, which is the part of this
 * that is a security property rather than an accounting one. */
if (l.path === '/posture') {
    l.handler = function (req) {
        var o = {};
        try {
            /* THE FLEET IS IN AUDIT and the binding asks for DENY, so the two
             * postures DISAGREE.  With them equal the assertion below cannot
             * tell which one gated the job -- a control that moved the posture
             * restore to before the drain passed, because restoring `enforce`
             * over a `deny` binding changes nothing. */
            comcon.mode('audit');
            var now = new Date();
            var nm = now.getUTCHours() * 60 + now.getUTCMinutes();

            var cap = nginx.outbound();
            var m = comcon.mediate(cap, comcon.allowHosts('https://*.example.com'));
            m = comcon.mediate(m, comcon.window({ days: DAYS[now.getUTCDay()],
                                   from: hhmm(nm - 120), to: hhmm(nm - 60) }));
            var A = comcon.include(
                "function(a){ Promise.resolve().then(function(){"
              + " out.request('https://a.example.com/SHUT'); });"
              + " return 'returned'; }",
                { imports: ['Promise'], grants: { out: m },
                  onViolation: 'deny' });

            var b0 = counts();
            o.a = A({});
            o.firedInA = fired(b0, counts());
            o.afterA = cap.pending().requests.length;

            /* the audit-posture bystander must not resurrect it */
            var B = comcon.include("async function(b){ return await 1; }",
                                   { imports: [], onViolation: 'audit' });
            o.b = B({});
            o.afterB = cap.pending().requests.length;
            o.fleet = nginx.tenantDenials().mode;

            comcon.mode('enforce');    /* the mode is per-process: put it back */

        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* A queued job that THROWS after the fragment returned: the value stands, and
 * the failure is logged rather than swallowed. */
if (l.path === '/throw') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var A = comcon.include(
                "function(a){ Promise.resolve().then(function(){"
              + " throw new Error('late-boom'); }); return 'value-stands'; }",
                { imports: ['Promise'] });
            o.a = A({});
            o.b = bystander()({});
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* A job chain that queues another job forever -- the case that decided the
 * drain's design, after two measurements contradicted two arguments.
 *
 * The first version had NO job cap, on the argument that an invariant with a cap
 * is not an invariant.  The argument is correct and the consequence was not
 * affordable: an uncapped drain over a self-queueing chain runs until a bound
 * the operator set, and MEASUREMENT said which one is reached first.  A `.then`
 * chain exhausts the 16 MB per-invocation MEMORY allowance after 354,885
 * promises -- long before a 300 ms deadline -- while an `await` chain allocates
 * slowly enough to reach the REQUEST deadline instead, turning a fragment that
 * was refused in milliseconds into a ten-second request the client abandoned.
 * That one was caught by the full suite, not by this file.
 *
 * So the drain shares the settle loop's job budget, and quiescence is
 * BEST-EFFORT: every fragment whose continuations are bounded is fully
 * attributed, and one that outruns the budget is REPORTED.  What is asserted
 * below is therefore termination and the report -- not a guarantee the loop
 * cannot make. */
if (l.path === '/forever') {
    l.handler = function (req) {
        var o = {};
        try {
            var A = comcon.include(
                "function(a){ var f = function(){"
              + " Promise.resolve().then(f); }; f(); return 'queued'; }",
                { imports: ['Promise'],
                  meter: comcon.meter({ timeoutMs: 300 }) });
            try { o.a = A({}); o.outcome = 'returned'; }
            catch (e) { o.outcome = 'stopped'; o.msg = String(e.message).substring(0, 80); }
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(14);

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

my $o = get_json('/defer');
is($o->{driverError}, undef, 'the deferred-job probe ran')
    or diag("driverError: $o->{driverError}");

is($o->{a}, 'returned',
   'the fragment returns its value without awaiting the job it queued');

is_deeply($o->{afterA}, ['https://a.example.com/LATE'],
   'THE DEFERRED USE IS ATTRIBUTED TO THE FRAGMENT THAT DEFERRED IT: the job '
   . 'ran before the invocation returned, so the capability was exercised '
   . 'inside A. Before the fix this was EMPTY here and appeared during the next '
   . 'fragment\'s invocation instead')
    or diag('afterA: ' . encode_json($o->{afterA}));

is_deeply($o->{afterB}, ['https://a.example.com/LATE'],
   '...and the unrelated fragment that runs next changes nothing, because there '
   . 'was nothing left pending for it to run')
    or diag('afterB: ' . encode_json($o->{afterB}));

# --- volume ---
is_deeply($o->{volumeAfterV}, { queued => 32, dropped => 68 },
   'VOLUME IS ATTRIBUTED EXACTLY: a hundred deferred requests land against the '
   . 'fragment that queued them -- 32 recorded, 68 counted as dropped -- rather '
   . 'than against whichever fragment ran next')
    or diag('volumeAfterV: ' . encode_json($o->{volumeAfterV}));
is_deeply($o->{volumeAfterB}, { queued => 32, dropped => 68 },
   '...and the bystander adds none of them');

# --- the posture the job is gated under ---
my $p = get_json('/posture');
is($p->{fleet}, 'audit', 'the FLEET is in audit for this probe');
is($p->{afterA}, 0,
   'THE JOB IS GATED UNDER ITS OWN FRAGMENT\'S POSTURE, NOT THE FLEET\'S: the '
   . 'fleet is in AUDIT, A is bound onViolation:deny, and its capability\'s '
   . 'window is CLOSED -- so the deferred call is denied and nothing is '
   . 'recorded. The two postures have to DISAGREE for this to mean anything: a '
   . 'control that restored the posture before the drain passed while they '
   . 'agreed')
    or diag('posture: ' . encode_json($p));
is_deeply($p->{firedInA}, ['cap.window'],
   '...with cap.window firing DURING A. Left pending, the same call would have '
   . 'run inside the bystander, which is bound onViolation:audit -- where a '
   . 'closed window is logged and ALLOWED. The same deferred call was permitted '
   . 'or denied depending on whose invocation happened to run it, which is a '
   . 'security property and not an accounting one')
    or diag('firedInA: ' . encode_json($p->{firedInA}));

# --- a job that throws after the value was computed ---
my $th = get_json('/throw');
is($th->{a}, 'value-stands',
   'a queued job that THROWS after the fragment returned does not fail the '
   . 'invocation: the value was already computed legitimately');
my $log = $t->read_file('error.log');
like($log, qr/queued job threw after the fragment returned \(unhandled rejection\).*late-boom/,
     '...and it is LOGGED rather than swallowed. The report comes from the '
     . 'REJECTION TRACKER, not from the job\'s return value: a promise reaction '
     . 'that throws does not FAIL -- the promise machinery catches it and '
     . 'rejects the derived promise, so the job succeeds and the failure becomes '
     . 'an unhandled rejection. The first version of this checked the job\'s '
     . 'return value, and this assertion is what said so');

# --- the trailing drain is bounded ---
my $t0 = time();
my $fv = get_json('/forever');
my $el = time() - $t0;
my $log2 = $t->read_file('error.log');
diag("forever: outcome=" . ($fv->{outcome} // 'none') . " in ${el}s");
is($fv->{a}, 'queued',
   'A RUNAWAY CONTINUATION DOES NOT FAIL THE FRAGMENT: the fragment finished and '
   . 'returned its value; it was its continuation that ran out of allowance, and '
   . 'those are different events. (A job that cannot RUN at all is fatal to the '
   . 'invocation -- but no reachable job produces that, because every job here is '
   . 'a promise reaction and those catch their own throws.)')
    or diag('forever: ' . encode_json($fv));
like($log2, qr/outran the \d+-job budget and left queued jobs behind/,
     '...and the shortfall is REPORTED, loudly: the fragment outran the job '
     . 'budget, so work is left behind that will run inside a later invocation. '
     . 'Silence here would be the original defect with extra steps, and this is '
     . 'the honest half of a best-effort drain. The structural fix -- binding a '
     . 'capability wrapper to its fragment so a leftover job cannot use '
     . 'authority whenever it runs -- is owed, and named in ASSURANCE G6.16')
    or diag('log tail: ' . substr($log2, -400));
cmp_ok($el, '<', 5,
   'A JOB CHAIN THAT QUEUES ITSELF FOREVER TERMINATES PROMPTLY. Without the '
   . 'shared job budget this drained to a bound the operator set instead -- and '
   . 'which bound that is was measured, not assumed: a `.then` chain exhausts '
   . 'the memory allowance after 354,885 promises, an `await` chain reaches the '
   . 'REQUEST deadline and becomes a ten-second request. If neither the budget '
   . 'nor a bound fired, this assertion would not fail, it would HANG, which is '
   . 'why it is written as a clock');

$t->stop();
