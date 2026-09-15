#!/usr/bin/perl

# G6.16, THE ACCOUNTING HALF — a fragment's leftovers are charged to NOBODY.
#
# Every invocation drains the compartment to quiescence before it returns, so a
# fragment's continuations are charged to, and gated at, the fragment that
# created them.  That drain is BEST-EFFORT: a fragment which outruns the
# 10,000-job budget leaves work queued, and nothing can un-queue ordinary JS.
#
# The AUTHORITY half of what remained is closed — `cap.owner` refuses a
# capability to anyone but the fragment it was granted to.  THE ACCOUNTING HALF
# WAS NOT, and measuring it turned up something worse than the two costs the
# backlog named.
#
# WHAT THE BACKLOG NAMED — a stranger pays:
#   * THE BUDGET.  Leftovers were drained by the next invocation's trailing loop,
#     out of the next fragment's job budget.  The queue is FIFO
#     (`list_add_tail` / `job_list.next` in quickjs.c), so leftovers go FIRST:
#     with 12,000 queued behind it, B's entire 10,000-job allowance goes on a
#     stranger's work and B's own continuations NEVER RUN.  That is the original
#     escape's shape with the arrow reversed — instead of one fragment reaching
#     into the next invocation, one fragment SPENDS the next invocation.
#   * THE CLOCK and THE REPORT.  They ran on B's deadline, and the
#     unhandled-rejection counter is reset per invocation, so a leftover that
#     rejected was logged as "this fragment's queued jobs" against a fragment that
#     had never seen it.  A log line naming the wrong author is worse than none.
#
# WHAT MEASURING IT FOUND — A LEFTOVER'S AUTHORITY DEPENDED ON WHO ARRIVED NEXT.
# `cap.owner` compares the capability's owner against the fragment NOW RUNNING.
# So a leftover drained inside a DIFFERENT fragment was denied, and the very same
# leftover drained inside ANOTHER INVOCATION OF ITS OWN FRAGMENT was ALLOWED —
# with `ttl` and `window` evaluated at that later moment, under that invocation's
# posture, on its clock.  Whether unfinished work kept its authority was decided
# by traffic order.  Test 4 is that, and it is the one that changes behaviour:
# drained as nobody, a leftover obtains nothing from ANY invocation, including
# its own author's next one.  An invocation's work belongs to that invocation.
#
# THE RESIDUAL IS IN THESE NUMBERS RATHER THAN ARGUED AWAY: the leading drain has
# a budget too, so a fragment that leaves more than 10,000 behind still pushes the
# remainder into the next invocation's trailing drain — bounded and reported, not
# zero.  And reaching this file's condition at all takes FOUR invocations,
# because one cannot leave more than the next fragment's budget behind: A's own
# 16 MB allowance caps how many jobs it can queue and its own drain burns 10,000.

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

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /leftover { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

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
function owner(c) { return c['cap.owner'] || 0; }
function vol(cap) {
    var q = cap.pending();
    return { queued: q.requests.length, dropped: q.dropped || 0 };
}

/* A fragment that queues N deferred uses of its own capability and returns
 * without awaiting any of them.  N above the 10,000-job budget is the only way
 * to leave work behind at all, which is why the shape is deliberately
 * pathological: this path does not exist for a fragment that finishes. */
function queuer(n, host) {
    return "function(a){ var i; for (i = 0; i < " + n + "; i++) {"
         + " Promise.resolve().then(function(){"
         + "   out.request('https://" + host + ".example.com/x'); }); }"
         + " return 'queued'; }";
}

locs.forEach(function (l) {

if (l.path === '/leftover') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            var capA = nginx.outbound();
            var A = comcon.include(queuer(13000, 'a'),
                      { imports: ['Promise'],
                        grants: { out: comcon.mediate(capA,
                                    comcon.allowHosts('https://*.example.com')) } });

            var capB = nginx.outbound();
            var B = comcon.include(queuer(100, 'b'),
                      { imports: ['Promise'],
                        grants: { out: comcon.mediate(capB,
                                    comcon.allowHosts('https://*.example.com')) } });

            /* FOUR invocations of A, at 13,000 jobs each: 10,000 run inside each
             * one and 3,000 are left behind, so by the fourth the backlog is past
             * B's whole budget.  Fewer calls cannot reach the defect. */
            o.a = [A({})];
            var d1 = counts();
            o.a.push(A({}));
            o.ownerDeltaA2 = owner(counts()) - owner(d1);
            o.a.push(A({}));
            o.a.push(A({}));
            o.aVolume = vol(capA);

            /* B: its 100 own continuations must all run. */
            var b0 = counts();
            try { o.b = B({}); }
            catch (e) { o.b = 'stopped: ' + String(e.message).substring(0, 60); }
            o.firedInB = fired(b0, counts());
            o.ownerDeltaB = owner(counts()) - owner(b0);
            o.bVolume = vol(capB);
            o.aVolumeAfterB = vol(capA);

            /* A third, trivial fragment: the deadline must still be its own, and
             * nothing new may be attributed to anybody. */
            var C = comcon.include("function(a){ return 'plain'; }",
                                   { imports: [] });
            o.c = C({});
            o.bVolumeAfterC = vol(capB);

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(9);

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

my $o = get_json('/leftover');

is($o->{driverError}, undef, 'the leftover-accounting probe ran')
    or diag("driverError: $o->{driverError}");
diag('aVolume=' . encode_json($o->{aVolume} || {})
     . ' ownerDeltaA2=' . ($o->{ownerDeltaA2} // '?')
     . ' ownerDeltaB=' . ($o->{ownerDeltaB} // '?')
     . ' bVolume=' . encode_json($o->{bVolume} || {}));

is_deeply($o->{a}, ['queued', 'queued', 'queued', 'queued'],
   'the queuing fragment returns without awaiting its jobs, four times');

is_deeply($o->{aVolume}, { queued => 32, dropped => 39968 },
   'A SPENDS ITS OWN BUDGET ON ITS OWN WORK: 10,000 of each call\'s 13,000 '
   . 'deferred uses run inside that call (40,000 total: 32 recorded, 39,968 '
   . 'counted as dropped), and 3,000 per call are left queued')
    or diag('aVolume: ' . encode_json($o->{aVolume} || {}));

cmp_ok($o->{ownerDeltaA2} // -1, '==', 3000,
   'A LEFTOVER\'S AUTHORITY NO LONGER DEPENDS ON WHO ARRIVES NEXT. cap.owner '
   . 'compares against the fragment NOW RUNNING, so before the fix A\'s own '
   . '3,000 leftovers were ALLOWED when A happened to be invoked again (this was '
   . '0) and DENIED when anyone else was -- the same unfinished work kept or lost '
   . 'its authority by traffic order, with ttl and window evaluated at whatever '
   . 'moment that turned out to be. Drained as nobody it is denied either way')
    or diag('ownerDeltaA2: ' . ($o->{ownerDeltaA2} // 'undef'));

is_deeply($o->{bVolume}, { queued => 32, dropped => 68 },
   'AND B\'S BUDGET IS STILL B\'S: all 100 of B\'s own deferred uses run inside '
   . 'B. Before the fix this was {queued:0,dropped:0} -- the job queue is FIFO, '
   . 'so A\'s 12,000 leftovers went first and consumed B\'s entire 10,000-job '
   . 'allowance, and B\'s continuations never ran at all. One fragment could '
   . 'silence the next one\'s')
    or diag('bVolume: ' . encode_json($o->{bVolume} || {}));

is($o->{b}, 'queued',
   '...and B is not STOPPED either: the leading drain narrows the deadline to its '
   . 'own 50 ms and RESTORES it, so B gets the full meter it asked for. Forget '
   . 'that restore and every invocation after a leftover drain dies on a deadline '
   . 'that had already passed')
    or diag('b: ' . ($o->{b} // 'undef'));

is_deeply($o->{firedInB}, ['cap.owner'],
   'THE LEFTOVERS OBTAIN NOTHING, which is the half that was already closed and '
   . 'must stay closed: drained as NOBODY, every capability A was granted is '
   . 'foreign (`owner != 0 && owner != cur_frag`), so cap.owner denies it. The '
   . 'authority outcome is the one it always was for a stranger -- only the bill '
   . 'moved, and the answer stopped depending on who is running')
    or diag('firedInB: ' . encode_json($o->{firedInB} || []));

is_deeply($o->{bVolumeAfterC}, { queued => 32, dropped => 68 },
   'a third, trivial fragment runs normally and attributes nothing further: the '
   . 'leading drain is a no-op once the compartment is quiescent');

like($t->read_file('error.log'),
     qr/drained \d+ leftover job\(s\) from an earlier fragment/,
   'AND THEY ARE REPORTED AS LEFTOVERS, naming what they are. The '
   . 'unhandled-rejection counter is reset per invocation, so before the fix a '
   . 'leftover that rejected was logged as "this fragment\'s queued jobs" against '
   . 'a fragment that had never seen it -- a line naming the wrong author sends '
   . 'an operator to the wrong place');
