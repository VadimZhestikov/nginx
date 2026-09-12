#!/usr/bin/perl

# COMCON M-LIB — the audit/enforce mode switch must reach EVERY worker.
#
# `comcon.mode()` sets a per-process static, so std.ops' rollout verbs switched
# only the worker that served the request. Measured on 4 workers before the fix:
# one shadow() call, then 24 requests -> 16 report audit and 8 report enforce.
# THE FLEET SITS IN MIXED MODES, nondeterministically, and the dangerous
# direction is the common one: an operator calls enforce(), gets "enforce" back,
# and some workers keep AUDITING -- still allowing what they believe they have
# begun denying.
#
# It survived review twice: the doc note said "per process" (true, and read as a
# limitation rather than a hole), and t/comcon_std_ops.t runs with ONE worker,
# which is the default when a fixture does not say otherwise. A single-worker
# fixture cannot see a fan-out bug.
#
# The fix rides D4b's transport exactly: the mode lives in nginx.shared as
# {epoch, mode}, and each worker reconciles lazily -- one shared read before a
# fragment runs -- so a switch in any worker reaches all of them without a
# broadcast. Same lazy-pull shape as bindShared, no new mechanism.
#
# THE COUNTER TRICK IS LOAD-BEARING: root.js is evaluated pre-fork, so every
# worker inherits its own copy of `n` and counts only the requests it served.
# REPEATED values across the run prove more than one worker answered -- without
# that, "all 24 said audit" is equally consistent with one worker answering all
# 24, which is exactly how a single-worker fixture hides this class of bug.

# NEGATIVE CONTROLS (run 2026-09-12, rebuilt and re-passed after each):
#
#   reconcile on the fragment invoke path      -> tests 8-9 fail
#   reconcile in denials() (the report path)   -> tests 4, 11 fail
#   the switch publishes the shared epoch      -> tests 4, 6, 8, 11 fail
#   a reconcile applies locally, never publishes -> test 10 fails
#
# THE INSTRUMENT TOOK THREE TRIES, and that is the part worth remembering,
# because every version of it was green while measuring nothing:
#
#   1. "several workers" inferred from REPEATED per-worker counters. Repeats
#      happen only when two workers coincidentally reach the same count, so it
#      reported "several" for a run served by one worker.
#   2. A per-process id from Math.random(). The JS runtime is created in the
#      MASTER, pre-fork, so every worker inherited the same RNG state and
#      returned the same "identity" -- now four workers looked like one.
#   3. nginx.shared.incr(), which is genuinely per-process: each worker takes
#      the next number the first time it serves a request.
#
# And the requests themselves had to become CONCURRENT: under light sequential
# load one worker wins nearly every accept, so 24 sequential http_get() calls
# measured a single process. Every connection is opened before any request is
# written, which forces the accept queue to be shared.
#
# Until all of that was true, the invoke-path control PASSED -- the fix looked
# unnecessary because the test could not see the workers it was meant to be
# about.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use IO::Socket::INET;

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /ctl  { }
        location /mode { }
        location /gate { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode });

/* A FRAGMENT must run under the FLEET's mode, not under whatever its worker
 * last saw -- which is a stronger claim than "the report agrees", and the one
 * that matters: the A1 reach gate denies this read in enforce and log-and-allows
 * it in audit, so the fragment's own answer tells us which mode it ran under.
 * Without the reconcile on the invoke path, a worker that never served a /mode
 * request would keep enforcing while the fleet audits. */
var sock = nginx.createSocket("127.0.0.1:8091");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);
var reach = comcon.include(
    "function(){ return (s.listener === null) ? 'denied' : 'allowed'; }",
    { grants: { s: sock } });

/* A PER-PROCESS IDENTITY. root.js is evaluated pre-fork, so every worker
 * inherits `wid === null` and mints its own on first use -- after the fork, so
 * the values differ per process.
 *
 * The first version of this counted requests per worker and inferred "several
 * workers" from REPEATED counter values, which only occur when two workers
 * happen to reach the same count: it reported "several" for runs served by one
 * worker and could have reported "one" for runs served by four. A test whose
 * multi-worker precondition is itself unreliable cannot testify about fan-out,
 * which is the only thing this file is for. */
var n = 0, wid = null;
/* Hold the worker briefly, so 24 CONCURRENT connections cannot all be served by
 * one process: a worker busy in a handler is not accepting. Concurrency alone
 * was not enough -- the multi-worker precondition failed intermittently, and a
 * flaky precondition on a fan-out test is worse than no test, because the run
 * that passes is the run that measured one worker. */
function hold(ms) {
    var t = Date.now();
    while (Date.now() - t < ms) { /* deliberate */ }
}

function workerId() {
    /* Math.random() was the first attempt and is NOT per-process: the JS runtime
     * is created in the master, pre-fork, so every worker inherits the same RNG
     * state and returns the same "identity" -- the detector then reported four
     * workers as one. A shared counter is genuinely per-process: each worker
     * takes the next number the first time it serves a request. */
    if (wid === null) { wid = nginx.shared.incr('__widseq__', 1); }
    return wid;
}

locs.find(function (l) { return l.path === "/ctl"; }).handler = function (req) {
    var op = req.queryParams.op, r = { n: ++n, w: workerId() };
    try {
        if (op === 'epoch') {
            var raw = nginx.shared.get('__comconMode__');
            r.epoch = raw ? JSON.parse(raw).epoch : -1;
            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(r));
            return;
        }
        r.switched = (op === 'enforce') ? ops.enforce()
                   : (op === 'learn')   ? ops.learnMode()
                   :                      ops.shadow();
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};

locs.find(function (l) { return l.path === "/gate"; }).handler = function (req) {
    hold(4);
    req.respond(200, {'content-type':'application/json'},
                JSON.stringify({ gate: reach({}), w: workerId(), n: ++n }));
};

locs.find(function (l) { return l.path === "/mode"; }).handler = function (req) {
    hold(4);
    /* reading the mode is itself a reconcile point: a worker that has not run a
     * fragment since the switch must still report the fleet's mode. */
    req.respond(200, {'content-type':'application/json'},
                JSON.stringify({ mode: ops.denials().mode, w: workerId(), n: ++n }));
};
JS

$t->try_run('no js module')->plan(11);

###############################################################################

# CONCURRENTLY, because sequential requests do not spread. Under light
# sequential load one worker wins nearly every accept, so 24 http_get() calls
# measured ONE process and reported it as four. Opening every connection first
# and only then writing the requests forces the accept queue to be shared.
sub concurrent_get {
    my ($path, $count) = @_;
    my (@socks, @out);
    for (1 .. $count) {
        my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1:8080',
                                      Proto => 'tcp', Timeout => 5);
        push @socks, $s if $s;
    }
    $_->print("GET $path HTTP/1.0\r\nHost: localhost\r\n\r\n") for @socks;
    for my $s (@socks) {
        local $/;
        push @out, (<$s> // '');
        $s->close;
    }
    return @out;
}

sub tally {
    my ($label, $path, $key) = @_;
    my (%v, %w);
    for my $r (concurrent_get($path, 24)) {
        $v{$1}++ if $r =~ /"$key":"(\w+)"/;
        $w{$1}++ if $r =~ /"w":(\d+)/;
    }
    my $workers = scalar(keys %w);
    diag("$label: " . join(", ", map { "$_=$v{$_}" } sort keys %v)
         . "  (served by $workers distinct worker(s))");
    return (\%v, $workers);
}

sub sweep      { return tally($_[0], '/mode', 'mode'); }
sub gate_sweep { return tally($_[0], '/gate', 'gate'); }

# The instrument first: if a single worker answers everything, this file cannot
# detect a fan-out bug at all and must say so rather than pass.
my ($m0, $w0) = sweep('before any switch');
cmp_ok($w0, '>', 1,
   "the sweep was served by $w0 DISTINCT workers -- otherwise this test cannot "
   . 'see a fan-out bug at all, and a green run would mean nothing');
is_deeply([sort keys %$m0], ['enforce'],
   'every worker starts in the default mode (enforce)');

like(http_get('/ctl?op=shadow'), qr/"switched":"audit"/,
     'shadow() reports the switch from the worker that served it');

my ($m1) = sweep('after shadow()');
is_deeply([sort keys %$m1], ['audit'],
   'THE POINT: EVERY worker reports audit -- one call, whole fleet. Before the '
   . 'fan-out this split 16/8 across four workers');

like(http_get('/ctl?op=enforce'), qr/"switched":"enforce"/, 'enforce() switches back');

my ($m2) = sweep('after enforce()');
is_deeply([sort keys %$m2], ['enforce'],
   '...and every worker reports enforce again');

# The fragment's OWN behaviour, which is what the mode is for. Reading the
# report only proves the reporter reconciled; invoking a fragment proves the
# invoke path did.
http_get('/ctl?op=shadow');
my ($gate, $gw) = gate_sweep('A1 gate under audit');
cmp_ok($gw, '>', 1,
   "the FRAGMENT sweep was served by $gw distinct workers -- without this the "
   . 'next assertion can pass with one worker answering all 24, which is '
   . 'exactly how a single-worker fixture hid this bug in the first place');
is_deeply([sort keys %$gate], ['allowed'],
   'EVERY worker runs FRAGMENTS under the fleet mode too: in audit the A1 reach '
   . 'gate log-and-allows in all of them. This is what the reconcile on the '
   . 'invoke path buys -- a worker that never served a report request would '
   . 'otherwise keep enforcing while the fleet audits');

http_get('/ctl?op=enforce');
my ($gate2) = gate_sweep('A1 gate under enforce');
is_deeply([sort keys %$gate2], ['denied'],
   '...and back to denied in every worker after enforce()');

# A reconcile must APPLY the fleet mode, never republish it: republishing would
# bump the epoch on every fragment invocation -- a shared write on the hot path,
# and a fleet chasing its own tail.
my $e0 = http_get('/ctl?op=epoch');
gate_sweep('epoch stability sweep');
my $e1 = http_get('/ctl?op=epoch');
my ($v0) = $e0 =~ /"epoch":(\d+)/;
my ($v1) = $e1 =~ /"epoch":(\d+)/;
is($v1, $v0,
   "24 fragment invocations across the fleet left the mode epoch at $v0 -- a "
   . 'reconcile applies the mode locally and publishes nothing');

# learn is the third mode and travels the same path
http_get('/ctl?op=learn');
my ($m3) = sweep('after learnMode()');
is_deeply([sort keys %$m3], ['learn'], 'learn mode fans out too');
