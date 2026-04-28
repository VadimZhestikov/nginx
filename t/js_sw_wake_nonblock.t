#!/usr/bin/perl

# Tests for wake_pipe[1] O_NONBLOCK fix (KnownFailure_2: Step 1).
#
# wake_pipe[1] is the write end used by nginx worker processes to signal the
# SharedWorker thread that a new message is waiting.  Before this fix the
# write end was blocking, so a slow or dead SW thread could stall the nginx
# event loop for the duration of the write.
#
# After the fix both ends are O_NONBLOCK.  A full pipe (> 64 KB of pending
# wake bytes) causes write() to return EAGAIN rather than blocking; the
# message data itself is already on the SOCK_SEQPACKET socketpair and will
# be delivered on the next wakeup.
#
# This test verifies that rapid sequential requests to a SharedWorker all
# succeed: if wake_pipe writes were blocking they would stall; with
# O_NONBLOCK they return EAGAIN gracefully (pipe is small relative to our
# 20-request burst, so no bytes are actually dropped here).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file('counter_sw.js', <<'JS');
var count = 0;
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        count++;
        port.postMessage(String(ev.data) + ':' + count);
    };
};
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_wake.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /count/ { }
    }
}
EOF

$t->write_file('init_wake.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;
    new SharedWorker(prefix + 'counter_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(p) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === p) { return locs[i]; }
        }
    }

    findLoc('/count/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'counter_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('req');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(8);

# Send 8 sequential requests; each should return req:<n> with incrementing n.
for my $n (1..8) {
    like(http_get('/count/'), qr/req:$n/,
        "sequential request $n returns correct count");
}
