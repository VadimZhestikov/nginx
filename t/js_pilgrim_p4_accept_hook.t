#!/usr/bin/perl

# Tests for JS-Pilgrim P4 — listener.on('accept', fn(conn)):
#   Accept-phase hooks on a JS-managed HTTP listener.
#   Fires before ngx_http_init_connection.
#   conn.remoteAddr, conn.remotePort, conn.reject().

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p4_accept_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /status/     { }
        location /api/        { }
    }
}
EOF

$t->write_file_expand('p4_accept_init.js', <<'JS');
// JS-Pilgrim P4 — accept hook tests.

var acceptCount  = 0;
var lastAddr     = '';
var lastPort     = 0;
var rejectCount  = 0;
var hook2Count   = 0;

var srv = nginx.http.servers[0];

// === JS-managed listener on port 8091: normal accept hook ===
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(srv);

listener.on('accept', function(conn) {
    acceptCount++;
    lastAddr = conn.remoteAddr;
    lastPort = conn.remotePort;
});

// === JS-managed listener on port 8092: reject all connections ===
var sockR = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
var rejectListener = nginx.http.attach(sockR);
rejectListener.addServer(srv);

rejectListener.on('accept', function(conn) {
    rejectCount++;
    conn.reject();
});

// === JS-managed listener on port 8093: two hooks run in order ===
var sock2 = nginx.createSocket("127.0.0.1:%%PORT_8093%%");
var twoHookListener = nginx.http.attach(sock2);
twoHookListener.addServer(srv);

twoHookListener.on('accept', function(conn) {
    acceptCount++;
    hook2Count++;
});
twoHookListener.on('accept', function(conn) {
    hook2Count++;
});

// Status handler — returns accept counters
var locs = srv.locations;
var statusLoc = locs.find(function(l) { return l.path === '/status/'; });
statusLoc.handler = function(req) {
    req.respond(200, {'content-type': 'text/plain'},
        'accept=' + acceptCount + '\n' +
        'addr=' + lastAddr + '\n' +
        'port=' + lastPort + '\n' +
        'reject=' + rejectCount + '\n' +
        'hook2=' + hook2Count + '\n');
};

var apiLoc = locs.find(function(l) { return l.path === '/api/'; });
apiLoc.handler = function(req) {
    req.respond(200, {}, 'ok\n');
};
JS

$t->try_run('no js module')->plan(11);

sub req_to {
    my ($port, $request) = @_;
    my $s = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto    => 'tcp',
        Timeout  => 3,
    );
    return '' unless defined $s;
    print $s $request;
    local $/ = undef;
    my $resp = <$s>;
    close $s;
    return $resp // '';
}

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }
sub status_val {
    my ($key) = @_;
    my $r = http_get('/status/');
    my ($val) = body($r) =~ /^$key=(.*)$/m;
    return $val // '';
}

my $p1 = port(8091);
my $p2 = port(8092);
my $p3 = port(8093);

# Test 1: before any connection, accept count is 0
is(status_val('accept'), '0', 'initial accept count is 0');

# Make a connection to port 8091 (triggers accept hook)
my $resp1 = req_to($p1, "GET /api/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($resp1, qr/200 OK/, 'connection to port 8091 succeeds');

# Test 3: accept count is now 1
is(status_val('accept'), '1', 'accept hook fired: count is 1');

# Test 4: remoteAddr is 127.0.0.1
is(status_val('addr'), '127.0.0.1', 'conn.remoteAddr is 127.0.0.1');

# Test 5: remotePort is a positive integer
my $port_val = status_val('port');
ok($port_val > 0, "conn.remotePort is positive ($port_val)");

# Test 6: second connection increments count to 2
req_to($p1, "GET /api/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
is(status_val('accept'), '2', 'second accept hook fires: count is 2');

# Test 7: reject() closes connection immediately
my $s = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => $p2,
    Proto    => 'tcp',
    Timeout  => 3,
);
ok(defined $s, 'TCP connect to reject listener succeeds initially');
if (defined $s) {
    print $s "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    local $/ = undef;
    my $resp = <$s>;
    ok(!defined $resp || $resp eq '', 'rejected connection returns EOF');
    close $s;
} else {
    ok(0, 'rejected connection returns EOF');
}

# Test 9: reject hook ran once
is(status_val('reject'), '1', 'reject hook fired once');

# Test 10: two hooks on port 8093 both run
req_to($p3, "GET /api/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
is(status_val('hook2'), '2', 'two hooks on listener both run');

# Test 11: port 8080 connections don't affect port 8091 counter
# (acceptCount is now 3 due to the 8093 connection incrementing it)
http_get('/status/');
is(status_val('accept'), '3', 'port 8080 connections do not trigger 8091 hook');
