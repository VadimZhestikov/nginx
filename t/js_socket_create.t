#!/usr/bin/perl

# Tests for Stage 52 Phase A — nginx.createSocket('host:port')
#
# Verifies:
#   - nginx.createSocket() returns a NginxSocket object during init_conf
#   - sock.address returns the requested address string
#   - sock.port returns the port as a number
#   - sock.fd returns a non-negative integer
#   - The OS socket is actually bound (TCP connect succeeds)
#   - Creating two sockets on different ports works
#   - Invalid argument throws TypeError

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(10);

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

        location /sock1/   { }
        location /sock2/   { }
        location /error/   { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase A: nginx.createSocket()

// Create two sockets during init_conf (master process, before fork)
var sock1 = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var sock2 = nginx.createSocket("127.0.0.1:%%PORT_8092%%");

// Capture error-case result
var sockErrorName = "none";
try {
    nginx.createSocket("not-valid-format");
} catch (e) {
    sockErrorName = e.constructor.name;
}

// Install request handlers that return socket properties
(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        var loc = locs[i];
        if (loc.path === "/sock1/") {
            loc.handler = function(req) {
                req.respond(200, {}, [
                    sock1.address, "|",
                    String(sock1.port), "|",
                    String(sock1.fd)
                ].join(""));
            };
        } else if (loc.path === "/sock2/") {
            loc.handler = function(req) {
                req.respond(200, {}, [
                    sock2.address, "|",
                    String(sock2.port), "|",
                    String(sock2.fd)
                ].join(""));
            };
        } else if (loc.path === "/error/") {
            loc.handler = function(req) {
                req.respond(200, {}, sockErrorName);
            };
        }
    }
})();
JS

$t->run();

# ---- helpers ----

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p1 = port(8091);
my $p2 = port(8092);

# ---- sock1 property tests ----

my ($addr1, $port1_str, $fd1_str) = split /\|/, body(http_get('/sock1/'));

is($addr1,    "127.0.0.1:$p1", 'sock1.address matches requested');
is($port1_str, $p1,            'sock1.port matches requested port');
ok($fd1_str >= 0,              'sock1.fd is non-negative');

# ---- sock2 property tests ----

my ($addr2, $port2_str, $fd2_str) = split /\|/, body(http_get('/sock2/'));

is($addr2,    "127.0.0.1:$p2", 'sock2.address matches requested');
is($port2_str, $p2,            'sock2.port matches requested port');
ok($fd2_str >= 0,              'sock2.fd is non-negative');

# ---- sockets have different fds ----

isnt($fd1_str, $fd2_str, 'sock1 and sock2 have different fds');

# ---- OS sockets are actually bound and listening ----

my $conn1 = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => $p1,
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn1, 'TCP connect to sock1 port succeeds');
$conn1->close() if defined $conn1;

my $conn2 = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => $p2,
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn2, 'TCP connect to sock2 port succeeds');
$conn2->close() if defined $conn2;

# ---- invalid argument throws TypeError ----

is(body(http_get('/error/')), 'TypeError', 'invalid addr throws TypeError');
