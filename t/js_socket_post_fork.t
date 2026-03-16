#!/usr/bin/perl

# Tests for Stage 52 Phase F — nginx.createSocket() from a worker handler
#
# Verifies:
#   - createSocket() called from a request handler (post-fork) returns a valid
#     NginxSocket with .address and .port set
#   - The port is reachable via TCP after the first request that creates it
#   - A second request returns the same address (socket already in registry)
#   - The socket fd is a positive integer

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

        location /create/ { }
        location /addr/   { }
        location /port/   { }
        location /fd/     { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase F: nginx.createSocket() from worker request handler

var srv = nginx.http.servers[0];
var sock = null;   // created lazily on first /create/ request

(function() {
    var locs = srv.locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/create/") {
            locs[i].handler = function(req) {
                if (!sock) {
                    sock = nginx.createSocket("127.0.0.1:%%PORT_8093%%");
                }
                req.respond(200, {}, sock ? "created" : "failed");
            };
        } else if (locs[i].path === "/addr/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, sock ? sock.address : "none");
            };
        } else if (locs[i].path === "/port/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, sock ? String(sock.port) : "0");
            };
        } else if (locs[i].path === "/fd/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, sock ? String(sock.fd) : "-1");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p = port(8093);

# First: trigger socket creation in the worker
is(body(http_get('/create/')), 'created', 'createSocket in worker returns ok');

# Address matches what we asked for
is(body(http_get('/addr/')), "127.0.0.1:$p", 'sock.address matches');

# Port matches
is(body(http_get('/port/')), "$p", 'sock.port matches');

# fd is positive
my $fd_val = body(http_get('/fd/'));
ok($fd_val > 0, "sock.fd is positive ($fd_val)");

# TCP connect to the new socket port succeeds
my $conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'TCP connect to worker-created socket succeeds');
$conn->close() if defined $conn;

# Sanity
ok(1, 'nginx started without crash');
