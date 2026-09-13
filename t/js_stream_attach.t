#!/usr/bin/perl

# Tests for Stage 52 Phase G — nginx.stream.attach(sock) / addServer(srv)

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)->plan(5);

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

        location /debug/    { }
        location /attached/ { }
        location /addr/     { }
    }
}

stream {
    server {
        listen  127.0.0.1:%%PORT_8091%%;
        return  "stream-ok\n";
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase G: nginx.stream.attach(sock)

var httpSrv = nginx.http.servers[0];
var strmSrvs = nginx.stream.servers;
var strmSrv  = strmSrvs.length > 0 ? strmSrvs[0] : null;

var listener = null;
var debugInfo = "servers.length=" + strmSrvs.length
              + " strmSrv=" + (strmSrv ? strmSrv.constructor.name : "null");

if (strmSrv) {
    var sock = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
    listener = nginx.stream.attach(sock);
    listener.addServer(strmSrv);
}

(function() {
    var locs = httpSrv.locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/debug/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, debugInfo);
            };
        } else if (locs[i].path === "/attached/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, listener ? "yes" : "no");
            };
        } else if (locs[i].path === "/addr/") {
            locs[i].handler = function(req) {
                req.respond(200, {}, listener ? listener.address : "none");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

sub stream_read {
    my ($addr) = @_;
    my $s = IO::Socket::INET->new(
        PeerAddr => $addr,
        Proto    => 'tcp',
        Timeout  => 2,
    ) or return undef;
    my $buf = '';
    $s->recv($buf, 64, 0);
    $s->close();
    return $buf;
}

my $p1 = port(8091);
my $p2 = port(8092);

my $debug = body(http_get('/debug/'));
diag("debug: $debug");

is(body(http_get('/attached/')), 'yes', 'listener was created');
is(body(http_get('/addr/')),     "127.0.0.1:$p2", 'listener.address matches socket');

# Static stream server still works
my $r1 = stream_read("127.0.0.1:$p1");
is($r1, "stream-ok\n", 'static stream server serves correctly');

# JS-attached stream listener uses the same server conf → same return value
my $r2 = stream_read("127.0.0.1:$p2");
is($r2, "stream-ok\n", 'JS-attached stream server serves correctly');

# TCP connect to JS-attached port succeeds
my $conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p2",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'TCP connect to JS stream listener succeeds');
$conn->close() if defined $conn;

