#!/usr/bin/perl

# Tests for wake_pipe EOF handling in the SW thread (KnownFailure_2: Step 3).
#
# When all write-ends of wake_pipe[1] are closed (master + all workers have
# exited), poll(wake_pipe[0]) returns POLLHUP and read() returns 0 (EOF).
# Before this fix the SW thread did `continue` on nwake == 0, causing a
# 100% CPU busy-spin; after the fix it sets terminate=1 and breaks, writing
# to health_pipe so the manager can detect the exit.
#
# We verify normal request delivery and a timely graceful stop.

use warnings;
use strict;
use Test::More;
use Time::HiRes qw(time);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) { port.postMessage(ev.data); };
};
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_eof.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /echo/ { }
    }
}
EOF

$t->write_file('init_eof.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;
    new SharedWorker(prefix + 'echo_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(p) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === p) { return locs[i]; }
        }
    }

    findLoc('/echo/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('ping');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(4);

# 1-2. Verify normal delivery works.
like(http_get('/echo/'), qr/ping/, 'echo SW delivers first request');
like(http_get('/echo/'), qr/ping/, 'echo SW delivers second request');

# 3. Graceful stop must complete quickly (no busy-spin in SW thread).
my $t0 = time();
$t->stop();
my $elapsed = time() - $t0;
ok($elapsed < 5, sprintf('nginx stops within 5s after graceful shutdown (%.1fs)', $elapsed));

# 4. No error-level alerts in error.log.
unlike($t->read_file('error.log'), qr/\[alert\]|\[emerg\]/i,
    'no alert/emerg in error.log');
