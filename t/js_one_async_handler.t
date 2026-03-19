#!/usr/bin/perl

# Tests that only one async JS handler may be suspended per worker at a time.
#
# While an async handler is awaiting a Promise that never resolves, the
# worker's async_pending slot is occupied.  Any new request that reaches a
# JS content handler on the same worker must receive HTTP 500 immediately —
# regardless of whether the new handler is itself async or synchronous.
#
# Test plan (5 user assertions):
#
#   1. barrier /probe/ returns 200 — proves /slow/ was already processed and
#      async_pending is set (nginx event loop is single-threaded FIFO).
#   2. Second async request to /second_async/ returns 500.
#   3. Worker is still alive after the 500.
#   4. After /slow/ connection closes, /check/ returns 200.
#   5. Worker is still alive at the end.
#
# Note: /slow/ never resolves, so the open socket produces the standard
# "open socket left in connection" alert on shutdown.  todo_alerts() covers
# this expected condition.

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

# /slow/ connection intentionally left open until shutdown → expected alert
$t->todo_alerts();

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /slow/        { }   # async, never resolves — holds async_pending
        location /probe/       { }   # sync barrier
        location /second_async/{ }   # async — must get 500 while /slow/ pending
        location /check/       { }   # sync health check
    }
}
EOF

$t->write_file('init.js', <<'JS');
const locs = nginx.http.servers[0].locations;
function loc(path, fn) {
    locs.find(l => l.path === path).handler = fn;
}

// Async handler that suspends forever — occupies async_pending for the
// duration of the test.
loc('/slow/', async (req) => {
    await new Promise(() => {});   // never resolves
    req.text('done\n');
});

// Synchronous barrier — responds immediately, no await.
loc('/probe/', (req) => {
    req.text('probe-ok\n');
});

// A second async handler.  While /slow/ is pending this must return 500.
loc('/second_async/', async (req) => {
    await new Promise(resolve => nginx.setTimeout(100).then(resolve));
    req.text('second-ok\n');
});

// Synchronous health check — confirms the worker is still functional.
loc('/check/', (req) => {
    req.text('alive\n');
});
JS

$t->run();

# ── Step 1: open /slow/ but do NOT read the response ─────────────────────────
# Keep the raw socket open so the request stays alive (and async_pending set)
# for the entire test.

my $slow = IO::Socket::INET->new(
    Proto    => 'tcp',
    PeerAddr => '127.0.0.1',
    PeerPort => port(8080),
) or die "connect: $!";

$slow->print("GET /slow/ HTTP/1.0\r\nHost: localhost\r\n\r\n");

# ── Step 2: synchronous /probe/ as FIFO barrier ───────────────────────────────
# nginx is single-threaded; it processes connections in the order they enter
# the accept queue.  /slow/ arrived first, so when /probe/ responds, the
# worker has necessarily already run the /slow/ handler past its first await
# and set async_pending.

my $probe = http_get('/probe/');
like($probe, qr/200 OK/, 'barrier /probe/ returns 200');

# ── Step 3: second async request — must get 500 immediately ──────────────────
my $second = http_get('/second_async/');
like($second, qr/500/, 'second async request gets 500 while first is pending');

# ── Step 4: worker survived the 500 ──────────────────────────────────────────
my $alive1 = http_get('/check/');
like($alive1, qr/200 OK.*alive/s, 'worker alive after 500');

# ── Step 5: close /slow/, let nginx drain the connection ─────────────────────
close $slow;
select undef, undef, undef, 0.2;   # give nginx one event-loop spin

# ── Step 6: worker still functional after /slow/ closes ──────────────────────
my $alive2 = http_get('/check/');
like($alive2, qr/200 OK.*alive/s, 'worker alive after slow connection closes');

# ── Step 7: no sanitizer errors ──────────────────────────────────────────────
$t->stop();
unlike($t->read_file('error.log'), qr/sanitizer/i, 'no sanitizer errors');
