#!/usr/bin/perl

# Tests for Stage 52 Phase D — listener.addVirtualServer(srv)
#
# Verifies:
#   - listener.addVirtualServer(srv) installs Host-header dispatch
#   - Requests with matching Host route to the named virtual server
#   - Requests with unmatched/absent Host fall back to default_server
#   - Multiple virtual servers on one listener all route correctly
#   - Calling addVirtualServer before addServer() throws an error
#   - Passing a non-NginxServer argument throws TypeError

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

    # server_a — default server on port 8080
    server {
        listen       127.0.0.1:8080;
        server_name  server_a;

        location /who/   { }
        location /early/ { }
        location /errpre/{ }
        location /errarg/{ }
    }

    # server_b — virtual host "server_b"
    server {
        listen       127.0.0.1:8080;
        server_name  server_b;

        location /who/ { }
    }

    # server_c — virtual host "server_c"
    server {
        listen       127.0.0.1:8080;
        server_name  server_c;

        location /who/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase D: listener.addVirtualServer(srv)

var srv_a = nginx.http.servers[0];  // default: server_a
var srv_b = nginx.http.servers[1];  // virtual: server_b
var srv_c = nginx.http.servers[2];  // virtual: server_c

// Create the listener on port 8091
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(srv_a);           // default_server
listener.addVirtualServer(srv_b);    // Host: server_b → srv_b
listener.addVirtualServer(srv_c);    // Host: server_c → srv_c

// Error: addVirtualServer before addServer
var errPre = "none";
try {
    var earlyL = nginx.http.attach(nginx.createSocket("127.0.0.1:%%PORT_8092%%"));
    earlyL.addVirtualServer(srv_b);  // listener not yet activated
} catch (e) {
    errPre = e.constructor.name;
}

// Error: non-NginxServer argument
var errArg = "none";
try {
    listener.addVirtualServer("not a server");
} catch (e) {
    errArg = e.constructor.name;
}

// Install location handlers on each server
(function() {
    function setWhoHandler(srv, name) {
        var locs = srv.locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === "/who/") {
                (function(n) {
                    locs[i].handler = function(req) {
                        req.respond(200, {}, n);
                    };
                })(name);
            } else if (locs[i].path === "/early/") {
                locs[i].handler = function(req) {
                    req.respond(200, {}, errPre);
                };
            } else if (locs[i].path === "/errarg/") {
                locs[i].handler = function(req) {
                    req.respond(200, {}, errArg);
                };
            }
        }
    }

    setWhoHandler(srv_a, "server_a");
    setWhoHandler(srv_b, "server_b");
    setWhoHandler(srv_c, "server_c");
})();
JS

$t->run();

my $p1 = port(8091);

sub req {
    my ($port, $host, $path) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => "127.0.0.1:$port",
        Proto    => 'tcp',
        Timeout  => 2,
    ) or return '';

    print $sock "GET $path HTTP/1.0\r\nHost: $host\r\n\r\n";
    local $/;
    my $resp = <$sock>;
    $resp =~ s/.*\r\n\r\n//s;
    return $resp;
}

# Default server — Host not matching any virtual host
is(req($p1, 'unknown_host', '/who/'), 'server_a',
   'unmatched Host falls back to default server_a');

# Virtual host server_b
is(req($p1, 'server_b', '/who/'), 'server_b',
   'Host: server_b routes to server_b');

# Virtual host server_c
is(req($p1, 'server_c', '/who/'), 'server_c',
   'Host: server_c routes to server_c');

# Default server still reachable by its own name via the default fallback
is(req($p1, 'server_a', '/who/'), 'server_a',
   'Host: server_a (not in vnames hash) falls back to default server_a');

# Error: addVirtualServer before addServer → InternalError (not TypeError)
is(req($p1, 'server_a', '/early/'), 'InternalError',
   'addVirtualServer before addServer throws InternalError');

# Error: non-NginxServer argument → TypeError
is(req($p1, 'server_a', '/errarg/'), 'TypeError',
   'addVirtualServer(non-server) throws TypeError');

# listener.address is still correct after addVirtualServer calls
like(req($p1, 'server_a',
         '/__nonexistent__'  # triggers nginx 404 handler; not our concern
     ) . req($p1, 'server_b', '/who/'),
     qr/server_b/,
     'listener still routes after multiple addVirtualServer calls');

# Sanity
