#!/usr/bin/perl

# Tests for manager recvmsg() timeout fix (KnownFailure_2: Step 5).
#
# Every blocking recvmsg() call that waits for the master manager to reply
# now has a SO_RCVTIMEO of NGX_JS_MGR_RECV_TIMEOUT_S (5 s).  This prevents
# a hung or slow manager from blocking the nginx event loop indefinitely.
#
# Under normal operation the manager replies in microseconds, so the timeout
# is never triggered and all existing dynamic-SW semantics are preserved.
# This test verifies that:
#   1. Dynamic SharedWorker creation (worker→manager request) still works.
#   2. Multiple sequential requests to a dynamic SW all succeed (the reply
#      arrives well within the 5 s window).

use warnings;
use strict;
use Test::More;

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

js_source %%TESTDIR%%/init_timeout.js;

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

$t->write_file('init_timeout.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;

    var locs = nginx.http.servers[0].locations;
    function findLoc(p) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === p) { return locs[i]; }
        }
    }

    // Dynamic SW: created on first request, not during init_conf.
    findLoc('/echo/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('timeout-ok');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(6);

# Four sequential requests — each completes well within the 5 s timeout.
like(http_get('/echo/'), qr/timeout-ok/, 'dynamic SW request 1');
like(http_get('/echo/'), qr/timeout-ok/, 'dynamic SW request 2 (dedup)');
like(http_get('/echo/'), qr/timeout-ok/, 'dynamic SW request 3');
like(http_get('/echo/'), qr/timeout-ok/, 'dynamic SW request 4');

# No warnings about timeouts in error log.
$t->stop();
unlike($t->read_file('error.log'), qr/timeout|EAGAIN/i,
    'no timeout errors in error.log');
unlike($t->read_file('error.log'), qr/\[alert\]|\[emerg\]/i,
    'no alert/emerg in error.log');
