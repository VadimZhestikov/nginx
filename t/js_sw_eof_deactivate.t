#!/usr/bin/perl

# Tests for channel-EOF deactivation in ngx_js_sw_recv_handler
# (KnownFailure_2: Step 8).
#
# When nginx reloads (SIGHUP) the master retires old SW threads by closing
# their sw_fd ends.  Each worker holds the other end (worker_fd) registered
# with epoll.  Once sw_fd is closed the kernel sets EPOLLRDHUP, which fires
# ngx_js_sw_recv_handler with ev->eof = 1.
#
# Before the fix: recv_handler returned without removing the event; the
# level-triggered epoll re-fired it on every event-loop iteration
# (100 % CPU busy-spin), degrading or blocking normal request handling.
#
# After the fix: recv_handler calls ngx_del_event / ngx_free_connection,
# silencing the fd; subsequent reloads and requests continue normally.
#
# Observable: after two successive reloads, requests are still served
# promptly and no [alert]/[emerg] messages appear in error.log.

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

js_source %%TESTDIR%%/init_eof_ch.js;

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

$t->write_file('init_eof_ch.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;
    new SharedWorker(prefix + 'echo_sw.js');

    var locs = nginx.http.servers[0].locations;
    var loc;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/echo/') { loc = locs[i]; break; }
    }

    loc.handler = async function(req) {
        var sw = new SharedWorker(prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('ping');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(8);

sub wait_ready {
    for (1 .. 50) {
        my $r = http_get('/echo/');
        return $r if defined $r && $r =~ /200 OK/;
        select undef, undef, undef, 0.1;
    }
    return undef;
}

# 1-2. Baseline: SW delivers requests before any reload.
like(http_get('/echo/'), qr/200 OK/, 'pre-reload: status 200');
like(http_get('/echo/'), qr/ping/,   'pre-reload: body matches');

# First reload: causes channel EOF (EPOLLRDHUP) on every worker's worker_fd.
# Without the fix recv_handler busy-spins; with the fix it deactivates once.
$t->reload();
my $r1 = wait_ready();
like($r1, qr/200 OK/, 'post-reload-1: status 200');
like($r1, qr/ping/,   'post-reload-1: body matches');

# Second reload stacks another layer of EOF events.  If the first reload
# left stale epoll entries they compound here; the fix prevents both.
$t->reload();
my $r2 = wait_ready();
like($r2, qr/200 OK/, 'post-reload-2: status 200');
like($r2, qr/ping/,   'post-reload-2: body matches');

# 7-8. No alert or emerg: confirms no open-socket leaks and no assertion
# failures were triggered by the busy-spin or stale state.
my $log = $t->read_file('error.log');
unlike($log, qr/\[alert\]/i, 'no [alert] in error.log after two reloads');
unlike($log, qr/\[emerg\]/i, 'no [emerg] in error.log after two reloads');
