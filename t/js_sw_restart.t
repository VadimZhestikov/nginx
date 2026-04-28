#!/usr/bin/perl

# Tests for SharedWorker health-pipe monitoring and restart-on-death.
#
# The health pipe is written by the SW thread on every poll() wakeup and
# read (drained) by the manager thread.  When the SW thread exits normally,
# health_pipe[1] is closed and the manager sees POLLHUP and attempts a
# pthread restart.
#
# This test verifies that the health pipe plumbing does not interfere with
# normal SharedWorker operation, and that the manager's dynamic pollfd array
# (which now includes health_pipe[0] entries alongside sw_cmd_fds and
# sw_term_fds) is built and walked correctly.
#
# The counter SW tracks message count across wakeups so we can confirm that
# multiple poll() iterations (each one writing a heartbeat byte) still deliver
# messages correctly.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# ---- SharedWorker script: count messages and echo with count ----

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

# ---- SharedWorker script: plain echo ----

$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage(ev.data);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_restart.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /counter/ { }
        location /echo/    { }
    }
}
EOF

# ---- init_restart.js ----
# Create both SharedWorkers statically in master (init_conf phase).
# Each handler re-looks up the SW by URL from the worker process.

$t->write_file('init_restart.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;

    // Create static SharedWorkers in master (threads start after daemonize).
    new SharedWorker(prefix + 'counter_sw.js');
    new SharedWorker(prefix + 'echo_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(path) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === path) { return locs[i]; }
        }
        return null;
    }

    // /echo/   - send fixed message, return echoed value
    findLoc('/echo/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('HealthOK');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };

    // /counter/ - send message, return echoed value with count
    findLoc('/counter/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'counter_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('msg');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(4);

# ---- Tests ----

# 1. Basic echo: verify health pipe does not break message delivery.
like(http_get('/echo/'), qr/HealthOK/,
    'echo SW: basic delivery works with health pipe');

# 2. Second echo request: the SW thread woke up again and wrote another
#    heartbeat byte; the manager must drain it without blocking.
like(http_get('/echo/'), qr/HealthOK/,
    'echo SW: second request works (heartbeat drained by manager)');

# 3. Counter SW: first message → count=1.
like(http_get('/counter/'), qr/msg:1/,
    'counter SW: first message, count=1');

# 4. Counter SW: second message → count=2.
#    Confirms state persists across multiple poll() wakeups and heartbeats.
like(http_get('/counter/'), qr/msg:2/,
    'counter SW: second message, count=2 (state persists across wakeups)');
