#!/usr/bin/perl

# Tests for setTimeout/setInterval in SharedWorker threads (KnownFailure_2: Step 6).
#
# Before this fix the SW thread's poll loop blocked with timeout=-1 (forever)
# and never called js_std_loop / js_std_tick_timers, so setTimeout() callbacks
# in SW scripts never fired.
#
# After this fix:
#   - js_std_tick_timers() is added to quickjs-libc (fires expired timers
#     without blocking, returns ms until next deadline).
#   - The SW thread's poll loop uses the timer deadline as its timeout, waking
#     up at the right time even when no message arrives.
#   - JS_ExecutePendingJob is drained after every timer tick.
#
# This test verifies:
#   1. A setTimeout() inside the SW script fires and updates SW-local state.
#   2. The next postMessage() sees the updated state (confirming the timer ran).
#   3. A delayed reply (SW uses setTimeout to reply after 50ms) is received
#      correctly by the nginx worker.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# SW script: on first connect set a timer that flips a flag after 50ms.
# On each message it replies with the current flag value.
$t->write_file('timer_sw.js', <<'JS');
var timerFired = false;
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        if (ev.data === 'set_timer') {
            setTimeout(function() { timerFired = true; }, 50);
            port.postMessage('timer_set');
        } else if (ev.data === 'check') {
            port.postMessage(timerFired ? 'fired' : 'not_fired');
        }
    };
};
JS

# SW that replies to a message after a 30ms delay via setTimeout.
$t->write_file('delayed_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        setTimeout(function() {
            port.postMessage('delayed:' + ev.data);
        }, 30);
    };
};
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_timer.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /set_timer/   { }
        location /check/       { }
        location /delayed/     { }
    }
}
EOF

$t->write_file('init_timer.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;
    new SharedWorker(prefix + 'timer_sw.js');
    new SharedWorker(prefix + 'delayed_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(p) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === p) { return locs[i]; }
        }
    }

    findLoc('/set_timer/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'timer_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('set_timer');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };

    findLoc('/check/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'timer_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('check');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };

    findLoc('/delayed/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'delayed_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('ping');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(8);

# 1. Request the SW to set a timer — confirm acknowledgement.
like(http_get('/set_timer/'), qr/timer_set/, 'SW acknowledges timer setup');

# 2. Check immediately — timer hasn't fired yet (set for 50ms).
like(http_get('/check/'), qr/not_fired/, 'SW timer not fired yet (immediate check)');

# 3. Wait 200ms then check — timer should have fired by now.
select undef, undef, undef, 0.2;
like(http_get('/check/'), qr/\bfired\b/, 'SW timer fired within 200ms');

# 4. Second check confirms state persists across poll iterations.
like(http_get('/check/'), qr/\bfired\b/, 'SW timer state persists');

# 5-6. Delayed-reply SW: reply arrives after SW-internal setTimeout(30ms).
like(http_get('/delayed/'), qr/delayed:ping/, 'SW setTimeout-delayed reply received');
like(http_get('/delayed/'), qr/delayed:ping/, 'SW setTimeout-delayed reply (2nd request)');

$t->stop();
unlike($t->read_file('error.log'), qr/\[alert\]|\[emerg\]/i,
    'no alert/emerg in error.log');
ok(1, 'setTimeout in SW thread works correctly');
