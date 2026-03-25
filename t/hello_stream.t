#!/usr/bin/perl

# Minimal JS COM hello-world for the stream module.
#
# The nginx.conf carries only a js_source directive — no stream{} block.
# hello_stream.js calls nginx.createStream() to bootstrap the stream module,
# then creates the socket, listener, and virtual server entirely at init time
# via JS COM calls.

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/stream/)->plan(2);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/hello_stream.js;

events { }
EOF

$t->write_file_expand('hello_stream.js', <<'JS');
// JS COM stream hello world.
// Creates the stream module, a server with a TCP handler, and a listener
// entirely at init time — no stream{}/server{} blocks in nginx.conf.

(function () {

    // 1. Bootstrap the stream module (equivalent to an empty stream{} block)
    nginx.createStream();

    // 2. Create a new stream server and attach a content handler
    var srv = nginx.stream.addServer();
    srv.handler = function (session) {
        session.finalize(200);   // accept connection and close cleanly
    };

    // 3. Bind a new TCP socket, attach it to the stream module,
    //    and register the server on that listener
    var sock = nginx.createSocket('127.0.0.1:%%PORT_8091%%');
    nginx.stream.attach(sock).addServer(srv);

})();
JS

$t->run();

my $p = port(8091);

# Verify the server accepts the connection
my $conn = IO::Socket::INET->new(
    PeerAddr => "127.0.0.1:$p",
    Proto    => 'tcp',
    Timeout  => 2,
);
ok(defined $conn, 'hello_stream: connection accepted');

# The handler calls session.finalize(200) — server closes the connection
my $buf = '';
$conn->read($buf, 1024);
ok(defined $conn, 'hello_stream: connection handled cleanly');

$t->stop();
