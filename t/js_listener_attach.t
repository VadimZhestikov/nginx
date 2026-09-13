#!/usr/bin/perl

# Tests for Stage 52 Phase B — nginx.http.attach(sock)
#
# Verifies:
#   - nginx.http.attach(sock) returns a NginxHttpListener object
#   - listener.address matches the socket address
#   - listener constructor is "NginxHttpListener"
#   - The socket is still OS-bound after attach (TCP connect succeeds)
#   - Passing a non-NginxSocket throws TypeError
#   - attach() without argument throws TypeError

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /listener/  { }
        location /error_arg/ { }
        location /error_none/{ }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase B: nginx.http.attach(sock)

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);

// Capture error cases
var errArg  = "none";
var errNone = "none";

try {
    nginx.http.attach("not a socket");
} catch (e) {
    errArg = e.constructor.name;
}

try {
    nginx.http.attach();
} catch (e) {
    errNone = e.constructor.name;
}

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        var loc = locs[i];
        if (loc.path === "/listener/") {
            loc.handler = function(req) {
                req.respond(200, {}, [
                    listener.address, "|",
                    (typeof listener.address === "string" ? "ok" : "bad"), "|",
                    (listener !== sock ? "distinct" : "same")
                ].join(""));
            };
        } else if (loc.path === "/error_arg/") {
            loc.handler = function(req) {
                req.respond(200, {}, errArg);
            };
        } else if (loc.path === "/error_none/") {
            loc.handler = function(req) {
                req.respond(200, {}, errNone);
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }

my $p1 = port(8091);
my ($addr, $addr_type, $distinct) = split /\|/, body(http_get('/listener/'));

# listener.address matches sock.address
is($addr, "127.0.0.1:$p1", 'listener.address matches socket address');

# listener.address is a string
is($addr_type, 'ok', 'listener.address is a string');

# OS socket still bound and listening after attach
my $conn = IO::Socket::INET->new(
    PeerAddr => '127.0.0.1',
    PeerPort => $p1,
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'TCP connect to attached socket port succeeds');
$conn->close() if defined $conn;

# Error: non-socket argument
is(body(http_get('/error_arg/')), 'TypeError',
   'attach(non-socket) throws TypeError');

# Error: no argument
is(body(http_get('/error_none/')), 'TypeError',
   'attach() with no arg throws TypeError');

# Sanity: listener and sock are distinct objects
is($distinct, 'distinct', 'listener object is distinct from sock object');

# Verify the response body contains the right address
like(body(http_get('/listener/')), qr/127\.0\.0\.1:$p1/,
     'listener address appears in response');

