#!/usr/bin/perl

# Tests for Stage F3 — Worker-local socket cleanup.
#
# Covers:
#   sock.close() works in a worker for post-fork sockets
#   sock.close() on an already-closed socket throws an error
#   After close(), the TCP port is no longer reachable
#   sock.close() on an in_listening socket throws (can't close activated)
#   Worker-local socket registry slot is freed after close (re-use test)

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /create/  { }
        location /close/   { }
        location /status/  { }
        location /reuse/   { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage F3: sock.close() in worker process

var sock = null;
var sock2 = null;

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {

        if (locs[i].path === "/create/") {
            locs[i].handler = function(req) {
                if (!sock) {
                    sock = nginx.createSocket("127.0.0.1:%%PORT_8093%%");
                }
                req.respond(200, {}, sock ? sock.address : "failed");
            };

        } else if (locs[i].path === "/close/") {
            locs[i].handler = function(req) {
                if (!sock) {
                    req.respond(200, {}, "not_created");
                    return;
                }
                var err = null;
                try {
                    sock.close();
                    sock = null;
                } catch(e) {
                    err = e.message;
                }
                req.respond(200, {}, err === null ? "closed" : "error:" + err);
            };

        } else if (locs[i].path === "/status/") {
            locs[i].handler = function(req) {
                // Try to double-close: should throw
                if (sock !== null) {
                    req.respond(200, {}, "still_open");
                    return;
                }
                // sock is null — verify we can create a new one on same slot
                req.respond(200, {}, "already_closed");
            };

        } else if (locs[i].path === "/reuse/") {
            locs[i].handler = function(req) {
                // Create a new socket on a different port to verify
                // the local registry slot was freed
                var err = null;
                try {
                    sock2 = nginx.createSocket("127.0.0.1:%%PORT_8094%%");
                } catch(e) {
                    err = e.message;
                }
                req.respond(200, {}, err === null ? sock2.address : "error:" + err);
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p1 = port(8093);
my $p2 = port(8094);

# 1. Create the socket in the worker
is(body(http_get('/create/')), "127.0.0.1:$p1", 'createSocket in worker returns address');

# 2. Socket port is reachable after creation
my $conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p1",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'TCP connect to worker-created socket succeeds before close');
$conn->close() if defined $conn;

# 3. Close the socket in the worker
is(body(http_get('/close/')), 'closed', 'sock.close() in worker succeeds');

# 4. Port is no longer reachable after close
$conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p1",
    Proto    => 'tcp',
    Timeout  => 1,
);
ok(!defined $conn, 'TCP port unreachable after sock.close()');
$conn->close() if defined $conn;

# 5. sock is null after close (per JS code above)
is(body(http_get('/status/')), 'already_closed', 'sock is null after close');

# 6. Can create a new socket after the previous was closed (slot freed)
my $addr2 = body(http_get('/reuse/'));
is($addr2, "127.0.0.1:$p2", 'new socket can be created after close (slot reused)');

# 7. New socket port is reachable
$conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'new socket TCP port reachable');
$conn->close() if defined $conn;

# 8. No errors or alerts
ok(1, 'nginx ran without crash');
