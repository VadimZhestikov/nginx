#!/usr/bin/perl

# Tests for Stage 52 Phase H — stream listener.addVirtualServer(srv)
#
# Full SNI-based dispatch requires SSL; here we verify:
#   - addVirtualServer succeeds and builds the virtual_names hash
#   - The listener still serves correctly (default server) after adding vservers
#   - Error: addVirtualServer before addServer throws InternalError
#   - Error: non-NginxStreamServer argument throws TypeError

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)->plan(6);

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

        location /result/ { }
    }
}

stream {
    # srv_a — default server (server_name deliberately empty)
    server {
        listen  127.0.0.1:%%PORT_8091%%;
        return  "srv_a\n";
    }

    # srv_b — virtual server for SNI "srv_b"
    server {
        listen  127.0.0.1:%%PORT_8091%%;
        server_name  srv_b;
        return  "srv_b\n";
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52 Phase H: stream listener.addVirtualServer(srv)

var strmSrvs = nginx.stream.servers;
var srv_a    = strmSrvs.length > 0 ? strmSrvs[0] : null;
var srv_b    = strmSrvs.length > 1 ? strmSrvs[1] : null;

var results = {
    activated: 'no',
    errPre:    'none',
    errArg:    'none',
    vbAdded:   'no',
};

if (srv_a && srv_b) {
    var sock = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
    var listener = nginx.stream.attach(sock);
    listener.addServer(srv_a);
    results.activated = 'yes';

    // addVirtualServer with valid NginxStreamServer
    try {
        listener.addVirtualServer(srv_b);
        results.vbAdded = 'yes';
    } catch (e) {
        results.vbAdded = 'err:' + e.message;
    }

    // Error: addVirtualServer before addServer
    try {
        var earlyL = nginx.stream.attach(
            nginx.createSocket("127.0.0.1:%%PORT_8093%%"));
        earlyL.addVirtualServer(srv_b);
    } catch (e) {
        results.errPre = e.constructor.name;
    }

    // Error: non-NginxStreamServer argument
    try {
        listener.addVirtualServer("not a server");
    } catch (e) {
        results.errArg = e.constructor.name;
    }
}

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/result/") {
            locs[i].handler = function(req) {
                var key = req.args.replace(/^key=/, '');
                req.respond(200, {}, results[key] !== undefined
                    ? results[key] : "undefined");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }
sub prop { body(http_get("/result/?key=$_[0]")) }

sub stream_read {
    my ($addr) = @_;
    my $s = IO::Socket::INET->new(
        PeerAddr => $addr, Proto => 'tcp', Timeout => 2,
    ) or return undef;
    my $buf = '';
    $s->recv($buf, 64, 0);
    $s->close();
    return $buf;
}

my $p1 = port(8091);
my $p2 = port(8092);

# Static stream server still works
is(stream_read("127.0.0.1:$p1"), "srv_a\n", 'static stream server ok');

# listener was activated
is(prop('activated'), 'yes', 'listener activated via addServer');

# addVirtualServer succeeded
is(prop('vbAdded'),   'yes', 'addVirtualServer(srv_b) succeeded');

# JS-attached listener (default server) still responds after addVirtualServer
is(stream_read("127.0.0.1:$p2"), "srv_a\n",
   'JS listener still serves after addVirtualServer');

# Error cases
is(prop('errPre'), 'InternalError',
   'addVirtualServer before addServer throws InternalError');
is(prop('errArg'), 'TypeError',
   'addVirtualServer(non-server) throws TypeError');

