#!/usr/bin/perl

# Regression test: worker must not crash when multiple async requests run
# concurrently on the same worker.
#
# The historical crash scenario (pre-fix):
#   1. Worker has w->async_pending set (first async request is suspended).
#   2. A second async request arrives; handler runs to its first `await`,
#      registering epoll events (e.g. a suspendAllWorkers reply_fd).
#   3. Content handler rejected the second request with 500, freed req_obj.
#   4. The reply_fd event fired later, resumed the abandoned continuation.
#   5. Continuation called req.respond() → accessed freed r → SIGSEGV.
#
# With concurrent async support, both requests now succeed.  This test
# verifies the worker stays alive and serves correct responses throughout.
#
# /slow/        — async, never resolves (keeps one slot in async_pending list)
# /probe/       — sync FIFO barrier
# /fast_async/  — async + suspendAllWorkers; now gets 200, not 500

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

# The /slow/ handler never resolves — its connection is left open until
# shutdown, producing the expected "open socket left in connection" alert.
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

    /* Async handler that never settles — stays in async_pending list. */
    set('/slow/', async function(req) {
        await new Promise(function() { /* intentionally never resolves */ });
        req.respond(200, {}, 'slow');
    });

    /*
     * Synchronous probe — FIFO barrier: when /probe/ responds, /slow/ was
     * already processed and is in the async_pending list.
     */
    set('/probe/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'probe ok');
    });

    /*
     * Async handler that calls suspendAllWorkers() then responds.
     * Previously this got 500 when async_pending was already set;
     * now it runs concurrently and must return 200.
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

# Open a raw socket for /slow/ and keep it open.
my $slow = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => port(8080),
    Proto    => 'tcp',
) or die "slow connect: $!";

syswrite $slow, "GET /slow/ HTTP/1.0\r\nHost: localhost\r\n\r\n";

# /probe/ confirms async_pending list is non-empty.
http_get('/probe/');

# Second async request — now runs concurrently, must succeed with 200.
my $r = http_get('/fast_async/');
like($r, qr/200.*fast ok/s, 'concurrent: second async request succeeds');

like(http_get('/check/'), qr/200 OK/,  'worker alive after concurrent async');
like(http_get('/check/'), qr/alive/,   'check body ok');

close $slow;

select undef, undef, undef, 0.1;
like(http_get('/check/'), qr/200 OK/,  'worker alive after slow close');
like(http_get('/check/'), qr/alive/,   'check body after slow close');
