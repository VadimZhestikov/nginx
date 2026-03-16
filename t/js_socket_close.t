#!/usr/bin/perl

# Tests for Stage 52 Phase E — sock.close()
#
# Verifies:
#   - sock.close() on an unattached socket closes the OS fd
#   - The port is released: TCP connect to the closed socket fails
#   - sock.close() on an activated socket (already in cycle->listening)
#     throws InternalError
#   - sock.close() called twice (socket already closed) throws InternalError
#   - Sockets NOT closed remain fully functional

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

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

        location /hello/         { }
        location /err_activated/ { }
        location /err_double/    { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase E: sock.close()

var srv = nginx.http.servers[0];

// sock_a: created but NOT attached — close() should succeed
var sock_a = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

// sock_b: created, attached, activated — will serve HTTP
var sock_b = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
var l_b    = nginx.http.attach(sock_b);
l_b.addServer(srv);

// Close sock_a (unattached, unactivated) — must succeed
sock_a.close();

// Error: close activated socket
var errActivated = "none";
try {
    sock_b.close();
} catch (e) {
    errActivated = e.constructor.name;
}

// Error: close already-closed socket (sock_a registry slot is now NULL)
var errDouble = "none";
try {
    sock_a.close();
} catch (e) {
    errDouble = e.constructor.name;
}

// Install handlers on the surviving server (reachable via sock_b / port 8092)
(function() {
    var locs = srv.locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/hello/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, "ok");
            };
        } else if (locs[i].path === "/err_activated/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, errActivated);
            };
        } else if (locs[i].path === "/err_double/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, errDouble);
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p1 = port(8091);
my $p2 = port(8092);

# TCP connect to the closed socket port should fail (Connection refused)
my $conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p1",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(!defined $conn,
   'TCP connect to closed socket port fails');

# The surviving socket (sock_b / 8092) still serves HTTP
is(body(http_get('/hello/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2", Proto => 'tcp', Timeout => 2))),
   'ok', 'surviving socket still serves HTTP');

# close(activated) → InternalError
is(body(http_get('/err_activated/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2", Proto => 'tcp', Timeout => 2))),
   'InternalError', 'close(activated socket) throws InternalError');

# close(already closed) → InternalError
is(body(http_get('/err_double/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2", Proto => 'tcp', Timeout => 2))),
   'InternalError', 'close(already closed) throws InternalError');

# Confirm sock_b port is reachable (sanity)
my $conn2 = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn2, 'TCP connect to open socket port succeeds');
$conn2->close() if defined $conn2;

# Sanity
ok(1, 'nginx started without crash');
