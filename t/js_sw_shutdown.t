#!/usr/bin/perl

# Tests for graceful shutdown (SIGQUIT) when async requests are suspended
# waiting for a SharedWorker reply.
#
# Without the drain fix, a worker with a pending async request would hang
# forever on SIGQUIT because:
#   - r->main->count > 0 keeps the event loop alive
#   - The SW thread never replies (simulated here by never calling port.postMessage)
#   - The master waits for all workers, deadlocking the shutdown
#
# With the fix, ngx_js_async_drain_503 sends 503 to every suspended request
# during exit_process, allowing the worker to exit promptly.
#
# Test strategy:
#   1. Verify normal (responding) SW works: prove the setup is valid.
#   2. Send a request to the hanging-SW endpoint; the worker suspends.
#   3. Send SIGQUIT and measure how long nginx takes to exit.
#   4. Assert it exits within a short timeout (5 s).
#   5. Optionally check the error log for the drain warning.

use warnings;
use strict;
use Test::More;
use POSIX qw(WNOHANG);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/shutdown_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /echo/  { }
        location /hang/  { }
    }
}
EOF

# A well-behaved echo SharedWorker (used to verify normal operation).
$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage('pong:' + ev.data);
    };
};
JS

# A SharedWorker that never replies — simulates a dead/stuck SW thread.
$t->write_file('hang_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        /* intentionally never reply */
    };
};
JS

$t->write_file('shutdown_init.js', <<'JS');
(function() {
    var prefix = nginx.cycle.prefix;

    /* Pre-create both SWs in master so workers can connect in handlers. */
    var echoSW = new SharedWorker(prefix + 'echo_sw.js');
    var hangSW = new SharedWorker(prefix + 'hang_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(path) {
        return locs.find(function(l) { return l.path === path; });
    }

    /* /echo/ — normal round-trip; used to verify the module works. */
    findLoc('/echo/').handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('hello');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };

    /* /hang/ — sends a message to the never-replying SW; the request stays
     * async-suspended until the worker exits.  The drain fix should send 503
     * and let the worker exit cleanly during graceful shutdown. */
    findLoc('/hang/').handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'hang_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('block');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(4);

# --- baseline: normal SW round-trip works ---
like(http_get('/echo/'), qr/200 OK/,      'echo SW: status 200');
like(http_get('/echo/'), qr/pong:hello/,  'echo SW: correct body');

# --- graceful-shutdown test ---
#
# Fire the hang request in the background (it will never get a reply from the
# SW, so the worker stays suspended).  Then send SIGQUIT and measure how long
# it takes nginx to exit.  With the drain fix the worker sends 503 and exits
# promptly; without it the worker hangs forever.

my $pid_file = $t->testdir() . '/nginx.pid';
my $nginx_pid = do { local $/; open my $f, '<', $pid_file or die; <$f> };
chomp $nginx_pid;

# Send the hang request in a forked child so we don't block the test process.
my $req_pid = fork();
if ($req_pid == 0) {
    # child: fire the request and ignore the response (we expect 503 or hangup)
    eval { http_get('/hang/') };
    exit 0;
}

# Give the worker a moment to suspend on the hang request.
select undef, undef, undef, 0.3;

# Send SIGQUIT (graceful shutdown).
kill 'QUIT', $nginx_pid;

# Wait up to 5 seconds for nginx to exit.
my $exited = 0;
for (1 .. 50) {
    $exited = (waitpid($nginx_pid, WNOHANG) != 0);
    last if $exited;
    select undef, undef, undef, 0.1;
}

# Reap the request child regardless.
kill 'TERM', $req_pid;
waitpid($req_pid, 0);

ok($exited, 'nginx exits cleanly within 5 s after SIGQUIT with pending async request');

# Prevent Test::Nginx DESTROY from calling stop() on the already-dead server.
$t->{_started} = 0;

# Confirm the drain warning appears in the error log.
my $log = $t->read_file('error.log');
like($log, qr/drain async request with 503 on worker exit/,
     'error log contains drain-503 warning');
