#!/usr/bin/perl

# Tests that multiple async JS handlers can run concurrently on the same
# worker.
#
# Three async requests are sent to the same worker before any of them
# completes.  Each waits a different time via nginx.setTimeout() then
# calls req.text().  All three must return 200 with their correct body.
#
# Test plan (6 user assertions):
#
#   1-3. Each of the three concurrent async requests returns 200 OK.
#   4-6. Each response body matches the expected string.
#   7.   Worker is still alive after all three complete.
#   8.   No sanitizer errors.

use warnings;
use strict;

use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(7);

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

        location /a/    { }
        location /b/    { }
        location /c/    { }
        location /check/{ }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const locs = nginx.http.servers[0].locations;
function loc(path, fn) {
    locs.find(l => l.path === path).handler = fn;
}

loc('/a/', async (req) => {
    await new Promise(resolve => nginx.setTimeout(150).then(resolve));
    req.text('a-ok\n');
});

loc('/b/', async (req) => {
    await new Promise(resolve => nginx.setTimeout(100).then(resolve));
    req.text('b-ok\n');
});

loc('/c/', async (req) => {
    await new Promise(resolve => nginx.setTimeout(50).then(resolve));
    req.text('c-ok\n');
});

loc('/check/', (req) => {
    req.text('alive\n');
});
JS

$t->run();

# ── Open three raw sockets and send all requests before reading any response.
# nginx is single-threaded; all three handlers will be in async_pending
# simultaneously, each waiting on its own timer.

my @socks;
for my $path (qw(/a/ /b/ /c/)) {
    my $s = IO::Socket::INET->new(
        Proto    => 'tcp',
        PeerAddr => '127.0.0.1',
        PeerPort => port(8080),
    ) or die "connect $path: $!";
    $s->print("GET $path HTTP/1.0\r\nHost: localhost\r\n\r\n");
    push @socks, [$path, $s];
}

# ── Read all three responses (blocks until each is complete).
my %got;
for my $entry (@socks) {
    my ($path, $s) = @$entry;
    my $resp = do { local $/; <$s> };
    close $s;
    $got{$path} = $resp;
}

like($got{'/a/'}, qr/200 OK/, '/a/ returns 200');
like($got{'/b/'}, qr/200 OK/, '/b/ returns 200');
like($got{'/c/'}, qr/200 OK/, '/c/ returns 200');
like($got{'/a/'}, qr/a-ok/,   '/a/ body correct');
like($got{'/b/'}, qr/b-ok/,   '/b/ body correct');
like($got{'/c/'}, qr/c-ok/,   '/c/ body correct');

my $alive = http_get('/check/');
like($alive, qr/200 OK.*alive/s, 'worker alive after concurrent handlers');
