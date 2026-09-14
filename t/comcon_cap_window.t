#!/usr/bin/perl

# M-LIB `window` — a RECURRING capability lifetime (office hours).
#
# `ttl` says "for the next N seconds"; `window` says "on these days, between
# these hours".  THREATS.md wants exactly this for the signing key, where the
# useful attenuation is a schedule and not a countdown.
#
# TIMES ARE UTC, and that is a decision.  "Office hours" is a local-time idea,
# but a gate whose behaviour depends on the host's TZ cannot be tested
# identically on two machines and shifts under a daylight-saving transition with
# nothing edited.  So the operator converts once, where they can see it.
#
# THE HARD PART IS NOT "IS IT 3PM".  It is the two shapes a schedule gate gets
# wrong, both tested below against a window computed FROM THE CURRENT TIME so the
# test means the same thing at every hour of the day:
#
#   WRAPPING     22:00-02:00 is a real shift pattern.  A naive `now >= from &&
#                now < to` is empty for it, which would silently deny a
#                capability its operator believed was open all night.
#   WHOLE DAY    from === to means the whole of an allowed day, not "never".  An
#                operator who writes 00:00-00:00 far more plausibly means "all
#                day on these days" -- and a window that is never open is spelled
#                by granting nothing at all.
#
# The day mask is tested by its complement: a window naming every day EXCEPT
# today, with hours that are open right now, must still deny.  Otherwise a test
# that passes could be ignoring the day list entirely.

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

        location /win { }
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

var DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function hhmm(mins) {
    mins = ((mins % 1440) + 1440) % 1440;
    var h = Math.floor(mins / 60), m = mins % 60;
    return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m;
}

/* One probe, run against differently-windowed capabilities: the arm is the
 * mediation, so a difference in outcome is a difference in the membrane. */
var PROBE = "function(a){ return out.request('https://api.example.com/x'); }";

locs.forEach(function (l) {
    if (l.path !== '/win') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* Everything below is derived from the CURRENT UTC time, so this
             * file asserts the same thing whenever it runs. */
            var now = new Date();
            var nowMin = now.getUTCHours() * 60 + now.getUTCMinutes();
            var today = DAYS[now.getUTCDay()];
            var notToday = DAYS.filter(function (d) { return d !== today; }).join(',');
            o.utc = { day: today, min: nowMin };

            function arm(spec) {
                var cap = nginx.outbound();
                var med = comcon.mediate(cap, comcon.allowHosts('*.example.com'));
                med = comcon.mediate(med, comcon.window(spec));
                var f = comcon.include(PROBE, { imports: [], grants: { out: med } });
                var b = counts();
                var r = f({});
                return { result: r, fired: fired(b, counts()),
                         queued: cap.pending().requests.length };
            }

            /* open now: today, a window comfortably around the current minute */
            o.open = arm({ days: today,
                           from: hhmm(nowMin - 60), to: hhmm(nowMin + 60) });

            /* closed now: today, but a window that has passed */
            o.closedHours = arm({ days: today,
                                  from: hhmm(nowMin - 120), to: hhmm(nowMin - 60) });

            /* the DAY MASK, by its complement: every day but today, hours open */
            o.wrongDay = arm({ days: notToday,
                               from: hhmm(nowMin - 60), to: hhmm(nowMin + 60) });

            /* WRAPPING past midnight: a window whose `from` is later in the day
             * than its `to`, arranged so that NOW is inside it. */
            o.wrapOpen = arm({ days: today,
                               from: hhmm(nowMin - 30), to: hhmm(nowMin - 60) });

            /* and a wrapping window that excludes now */
            o.wrapClosed = arm({ days: today,
                                 from: hhmm(nowMin + 30), to: hhmm(nowMin - 30) });

            /* WHOLE DAY: from === to */
            o.wholeDay = arm({ days: today, from: '00:00', to: '00:00' });

            /* composition: a window AND a lifetime AND a budget */
            var cap2 = nginx.outbound();
            var m2 = comcon.mediate(cap2, comcon.allowHosts('*.example.com'));
            m2 = comcon.mediate(m2, comcon.window({ days: today,
                     from: hhmm(nowMin - 60), to: hhmm(nowMin + 60) }));
            m2 = comcon.mediate(m2, comcon.ttl(3600));
            m2 = comcon.mediate(m2, comcon.uses('win:probe', 1, 60));
            var g = comcon.include(
                "function(a){ return [out.request('https://a.example.com/1'),"
              + " out.request('https://a.example.com/2')]; }",
                { imports: [], grants: { out: m2 } });
            o.composed = g({});

            /* two DIFFERENT windows do not compose */
            try {
                comcon.mediate(
                    comcon.mediate(nginx.outbound(),
                        comcon.window({ days: 'Mon', from: '09:00', to: '17:00' })),
                    comcon.window({ days: 'Tue', from: '09:00', to: '17:00' }));
                o.meet = 'ALLOWED';
            } catch (e) { o.meet = e.code || e.name; }

            /* the same window composes */
            try {
                comcon.mediate(
                    comcon.mediate(nginx.outbound(),
                        comcon.window({ days: 'Mon', from: '09:00', to: '17:00' })),
                    comcon.window({ days: 'Mon', from: '09:00', to: '17:00' }));
                o.meetSame = 'ok';
            } catch (e) { o.meetSame = e.code || e.name; }

            /* nothing is defaulted */
            o.bad = {};
            var cases = { noDays:    { from: '09:00', to: '17:00' },
                          noHours:   { days: 'Mon' },
                          badDay:    { days: 'Funday', from: '09:00', to: '17:00' },
                          badTime:   { days: 'Mon', from: '9am', to: '17:00' },
                          hour99:    { days: 'Mon', from: '99:00', to: '17:00' } };
            Object.keys(cases).forEach(function (k) {
                try { comcon.window(cases[k]); o.bad[k] = 'ACCEPTED'; }
                catch (e) { o.bad[k] = e.code || e.name; }
            });

            /* audit mode logs and allows, like every gate */
            comcon.mode('audit');
            o.audited = arm({ days: today,
                              from: hhmm(nowMin - 120), to: hhmm(nowMin - 60) });
            comcon.mode('enforce');

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(16);

my $raw = http_get('/win');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 400)); $o = {};
};

is($o->{driverError}, undef, 'window() is usable on a capability');
diag("UTC now: $o->{utc}{day} minute $o->{utc}{min}");

# --- open and closed ---
cmp_ok($o->{open}{result}, '>', 0, 'inside the window, the capability works');
is_deeply($o->{open}{fired}, [], '...and nothing is denied');

is($o->{closedHours}{result}, undef, 'outside the hours, the operation is denied');
is_deeply($o->{closedHours}{fired}, ['cap.window'],
   '...with cap.window, its own code -- not cap.expired, because "run out" and '
   . '"outside its hours" are different operational facts');
is($o->{closedHours}{queued}, 0, '...and the denied intent reached nothing');

# --- the day mask, by its complement ---
is($o->{wrongDay}{result}, undef,
   'a window naming every day EXCEPT today denies, even with the hours open '
   . 'right now -- so the day list is really being read');
is_deeply($o->{wrongDay}{fired}, ['cap.window'], '...as cap.window');

# --- wrapping past midnight ---
cmp_ok($o->{wrapOpen}{result}, '>', 0,
   'a window that WRAPS midnight (from > to) is open when now is inside it: a '
   . 'naive from<=now<to would make a 22:00-02:00 shift permanently closed');
is($o->{wrapClosed}{result}, undef,
   '...and closed when now is in the gap it excludes');

# --- whole day ---
cmp_ok($o->{wholeDay}{result}, '>', 0,
   'from === to means the WHOLE of an allowed day, not "never": an operator '
   . 'writing 00:00-00:00 means all day, and never-open is spelled by granting '
   . 'nothing');

# --- composition ---
is_deeply($o->{composed}, [1, undef],
   'a window composes with a lifetime AND a budget: the first request is '
   . 'recorded, the second exhausts uses(1)')
    or diag("composed: " . encode_json($o->{composed}));

# --- the meet ---
is($o->{meet}, 'E_CAP_ESCALATE',
   'two DIFFERENT windows are refused: two schedules do not intersect in one '
   . 'schedule, so a meet would have to guess, and guessing widens');
is($o->{meetSame}, 'ok', 'the same window composes, being the same attenuation');

# --- nothing defaulted ---
is_deeply($o->{bad},
   { noDays  => 'E_CAP_FLAVOR', noHours => 'E_CAP_FLAVOR',
     badDay  => 'E_CAP_FLAVOR', badTime => 'E_CAP_FLAVOR',
     hour99  => 'E_CAP_FLAVOR' },
   'every malformed window is REFUSED, not defaulted: no days, no hours, an '
   . 'unknown day, a time that is not HH:MM, and an hour past 23')
    or diag("bad: " . encode_json($o->{bad}));

# --- audit ---
cmp_ok($o->{audited}{result}, '>', 0,
   'in AUDIT mode a closed window logs and ALLOWS, so a schedule can be watched '
   . 'before it bites');

$t->stop();
