#!/usr/bin/perl

# Regression test: worker must not crash when a second async request arrives
# while another async request is already pending on the same worker.
#
# The crash scenario (now fixed):
#   1. Worker has w->async_pending set (first async request is suspended).
#   2. A second async request arrives on the same worker.
#   3. The handler runs to its first `await`, registering epoll events
#      (e.g. a suspendAllWorkers reply_fd).
#   4. Content handler detects the conflict, frees result/req_obj, returns 500.
#   5. Nginx finalises the second request, freeing r.
#   6. The reply_fd event fires later, resumes the abandoned continuation.
#   7. Continuation calls req.respond() → accesses freed r → SIGSEGV (old).
#
# Fix: null op->r in the error path; guard ngx_js_request_respond against
# op->r == NULL so the continuation discards the call safely.
#
# How we guarantee the conflict:
#   /slow/  — async handler that never resolves (holds async_pending).
#   /probe/ — synchronous handler; responding 200 proves that /slow/ was
#             already processed (nginx is single-threaded; connections are
#             served in accept-queue order, so when /probe/ responds the
#             worker must have already set async_pending for /slow/).
#   /fast_async/ — async handler calling suspendAllWorkers(); sent after
#                 /probe/ confirms /slow/ is pending.  Must get 500 and
#                 must NOT crash the worker.

use warnings;
use strict;

use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(5);

# The /slow/ handler never resolves — its nginx connection is intentionally
# left open until shutdown, which produces the standard "open socket left in
# connection" alert.  This is expected behaviour in this test.
$t->todo_alerts();

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/apc_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /slow/        { }
        location /probe/       { }
        location /fast_async/  { }
        location /check/       { }
    }
}
EOF

$t->write_file('apc_init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* Async handler that never settles — holds async_pending forever. */
    set('/slow/', async function(req) {
        await new Promise(function() { /* intentionally never resolves */ });
        req.respond(200, {}, 'slow');
    });

    /*
     * Synchronous probe — used as a FIFO barrier: nginx's event loop is
     * single-threaded and serves connections in accept-queue order.  When
     * /probe/ responds, /slow/ was necessarily processed (and async_pending
     * set) in the same worker before /probe/ was accepted.
     */
    set('/probe/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'probe ok');
    });

    /*
     * Async handler that calls suspendAllWorkers() (registers an epoll
     * event for the reply_fd) then responds.  When it lands on a worker
     * with async_pending already set it must get 500 — and crucially the
     * worker must not crash when the reply_fd fires later.
     */
    set('/fast_async/', async function(req) {
        await nginx.suspendAllWorkers();
        await nginx.resumeAllWorkers();
        req.respond(200, {'content-type': 'text/plain'}, 'fast ok');
    });

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'alive');
    });
})();
JS

$t->run();

# Open a raw socket for /slow/.  Do NOT read its response — the connection
# keeps the async request alive so async_pending stays set.
my $slow = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => port(8080),
    Proto    => 'tcp',
) or die "slow connect: $!";

syswrite $slow, "GET /slow/ HTTP/1.0\r\nHost: localhost\r\n\r\n";

# /probe/ is synchronous and nginx serves connections FIFO.  By the time
# http_get('/probe/') returns, /slow/ was already processed and
# w->async_pending is set on the single worker.
http_get('/probe/');

# Now send a conflicting async request.  It must get an error (500) but
# must NOT crash the worker (was a SIGSEGV before the fix).
my $r = http_get('/fast_async/');
like($r, qr/500/, 'conflict: second async request gets 500');

# Worker must still be alive and serving after the conflict.
like(http_get('/check/'), qr/200 OK/,  'worker alive after conflict');
like(http_get('/check/'), qr/alive/,   'check body ok');

close $slow;

# After the slow connection closes the worker should fully recover.
select undef, undef, undef, 0.1;
like(http_get('/check/'), qr/200 OK/,  'worker alive after slow close');
like(http_get('/check/'), qr/alive/,   'check body after slow close');
