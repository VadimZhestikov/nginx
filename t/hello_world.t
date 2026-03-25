#!/usr/bin/perl

# Minimal JS COM hello-world.
#
# The nginx.conf carries only a js_source directive and an empty http{} block.
# hello_world.js creates the socket, listener, virtual server, and
# /hello_world location entirely at init time via JS COM calls.

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(2);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/hello_world.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
}
EOF

$t->write_file_expand('hello_world.js', <<'JS');
// JS COM hello world.
// Creates a socket, listener, virtual server, and /hello_world location
// entirely at init time — no server{}/location{} blocks in nginx.conf.

(function () {

    // 1. New virtual server (nginx builds a default template when servers[] is empty)
    var srv = nginx.http.addServer('hello-world.local');

    // 2. Add /hello_world location with an inline JS content handler
    srv.addLocation('/hello_world').handler = function (req) {
        req.respond(200, { 'content-type': 'text/plain' },
                    'JS COM: Hello World!');
    };

    // 3. Bind a new TCP socket, attach it to the HTTP module,
    //    and register the server on that listener
    var sock = nginx.createSocket('127.0.0.1:%%PORT_8091%%');
    nginx.http.attach(sock).addServer(srv);

})();
JS

$t->run();

my $p = port(8091);
my $resp = http_get('/hello_world', socket => IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p",
    Proto    => 'tcp',
    Timeout  => 2,
));

like($resp, qr/200 OK/,               'hello_world: status 200');
like($resp, qr/JS COM: Hello World!/, 'hello_world: response body');

$t->stop();
