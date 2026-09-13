#!/usr/bin/perl

# Tests for Stage 52 Phase C — listener.addServer(srv)
#
# Verifies:
#   - listener.addServer(srv) activates the socket for HTTP connections
#   - Requests on the new port are handled by the server's location handler
#   - Two sockets can share the same server (multi-socket per server)
#   - Two sockets can have different servers (multi-server, one each)
#   - Passing a non-NginxServer throws TypeError
#   - Calling addServer() with no argument throws TypeError

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
        server_name  server_a;

        location /hello/  { }
        location /errarg/ { }
        location /errnone/{ }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  server_b;

        location /world/  { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase C: listener.addServer(srv)

var srv_a = nginx.http.servers[0];
var srv_b = nginx.http.servers[1];

// Socket 1  →  server_a  (basic single-socket case)
var sock1 = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var l1    = nginx.http.attach(sock1);
l1.addServer(srv_a);

// Socket 2  →  server_a  (second socket, same server)
var sock2 = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
var l2    = nginx.http.attach(sock2);
l2.addServer(srv_a);

// Socket 3  →  server_b  (different server)
var sock3 = nginx.createSocket("127.0.0.1:%%PORT_8093%%");
var l3    = nginx.http.attach(sock3);
l3.addServer(srv_b);

// Capture error cases
var errArg  = "none";
var errNone = "none";

try {
    var tmpSock = nginx.createSocket("127.0.0.1:%%PORT_8094%%");
    var tmpL    = nginx.http.attach(tmpSock);
    tmpL.addServer("not a server");
} catch (e) {
    errArg = e.constructor.name;
}

try {
    var tmpSock2 = nginx.createSocket("127.0.0.1:%%PORT_8095%%");
    var tmpL2    = nginx.http.attach(tmpSock2);
    tmpL2.addServer();
} catch (e) {
    errNone = e.constructor.name;
}

// Install location handlers
(function() {
    var locs, i;

    locs = srv_a.locations;
    for (i = 0; i < locs.length; i++) {
        if (locs[i].path === "/hello/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, "hello");
            };
        } else if (locs[i].path === "/errarg/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, errArg);
            };
        } else if (locs[i].path === "/errnone/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, errNone);
            };
        }
    }

    locs = srv_b.locations;
    for (i = 0; i < locs.length; i++) {
        if (locs[i].path === "/world/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, "world");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p1 = port(8091);
my $p2 = port(8092);
my $p3 = port(8093);

# Basic HTTP roundtrip through socket 1  →  server_a
is(body(http_get('/hello/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p1", Proto => 'tcp', Timeout => 2))),
   'hello', 'socket1+server_a: GET /hello/ returns "hello"');

# Same server on socket 2
is(body(http_get('/hello/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2", Proto => 'tcp', Timeout => 2))),
   'hello', 'socket2+server_a (shared): GET /hello/ returns "hello"');

# Different server on socket 3
is(body(http_get('/world/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p3", Proto => 'tcp', Timeout => 2))),
   'world', 'socket3+server_b: GET /world/ returns "world"');

# server_a and server_b are distinct (different response bodies)
isnt(body(http_get('/hello/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p1", Proto => 'tcp', Timeout => 2))),
     body(http_get('/world/', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p3", Proto => 'tcp', Timeout => 2))),
     'server_a and server_b serve different content');

# Error: non-NginxServer argument
is(body(http_get('/errarg/')), 'TypeError',
   'addServer(non-server) throws TypeError');

# Error: no argument
is(body(http_get('/errnone/')), 'TypeError',
   'addServer() with no arg throws TypeError');

# Sanity: nginx started without crash
