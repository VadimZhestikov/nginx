#!/usr/bin/perl

# Tests for JS-Pilgrim P13: L4 server→client direction.
#
# listener.addL4SendFilter(async function*(source) { ... }) intercepts raw
# outbound bytes after HTTP processing, before they reach the client socket.
# Generator receives Uint8Array chunks via 'for await', yields transformed bytes.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(16);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p13_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /plain/ {
            return 200 "hello\n";
        }
    }
}
EOF

$t->write_file_expand('p13_init.js', <<'JS');
// JS-Pilgrim P13 — L4 server->client send filter tests

var srv = nginx.http.servers[0];

// ----------------------------------------------------------------
// Port %%PORT_8081%%: passthrough — forward bytes unchanged
// ----------------------------------------------------------------
var passSock = nginx.createSocket("127.0.0.1:%%PORT_8081%%");
var passListener = nginx.http.attach(passSock);
passListener.addServer(srv);

passListener.addL4SendFilter(async function*(source) {
    for await (var chunk of source) {
        yield chunk;
    }
});

// ----------------------------------------------------------------
// Port %%PORT_8082%%: transform — replace "hello" with "world"
// ----------------------------------------------------------------
var xformSock = nginx.createSocket("127.0.0.1:%%PORT_8082%%");
var xformListener = nginx.http.attach(xformSock);
xformListener.addServer(srv);

xformListener.addL4SendFilter(async function*(source) {
    for await (var chunk of source) {
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        yield s.replace(/hello/g, 'world');
    }
});

// ----------------------------------------------------------------
// Port %%PORT_8083%%: chained — uppercase then prepend prefix
// ----------------------------------------------------------------
var chainSock = nginx.createSocket("127.0.0.1:%%PORT_8083%%");
var chainListener = nginx.http.attach(chainSock);
chainListener.addServer(srv);

chainListener.addL4SendFilter(async function*(source) {
    for await (var chunk of source) {
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        yield s.toUpperCase();
    }
});

chainListener.addL4SendFilter(async function*(source) {
    for await (var chunk of source) {
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        yield s.replace(/^HELLO/m, 'FILTERED:HELLO');
    }
});
JS

$t->run();

# -----------------------------------------------------------------------
# 1–4: Passthrough filter — response body unchanged
# -----------------------------------------------------------------------

my $r = http_get('/plain/');
like($r, qr{200 OK},    'no-filter port: 200 OK');
like($r, qr{^hello\n}m, 'no-filter port: body correct');

$r = http_get('/plain/', PeerAddr => '127.0.0.1:' . port(8081));
like($r, qr{200 OK},    'passthrough: 200 OK');
like($r, qr{^hello\n}m, 'passthrough: body unchanged');

# -----------------------------------------------------------------------
# 5–8: Transform filter — "hello" → "world"
# -----------------------------------------------------------------------

$r = http_get('/plain/', PeerAddr => '127.0.0.1:' . port(8082));
like($r, qr{200 OK},    'transform: 200 OK');
like($r, qr{^world\n}m, 'transform: hello replaced with world');

$r = http_get('/plain/', PeerAddr => '127.0.0.1:' . port(8082));
like($r, qr{200 OK},    'transform 2: 200 OK');
like($r, qr{^world\n}m, 'transform 2: replacement persists');

# -----------------------------------------------------------------------
# 9–12: No-filter port — bytes not transformed
# -----------------------------------------------------------------------

$r = http_get('/plain/');
like($r, qr{200 OK},    'no-filter port 2: 200 OK');
like($r, qr{^hello\n}m, 'no-filter port 2: body correct');

$r = http_get('/plain/', PeerAddr => '127.0.0.1:' . port(8082));
like($r, qr{200 OK},    'transform 3: 200 OK');
like($r, qr{^world\n}m, 'transform 3: third request still filtered');

# -----------------------------------------------------------------------
# 13–14: Chained filters — uppercase then add prefix
# -----------------------------------------------------------------------

$r = http_get('/plain/', PeerAddr => '127.0.0.1:' . port(8083));
like($r, qr{200 OK},             'chain: 200 OK');
like($r, qr{FILTERED:HELLO\n}m, 'chain: both filters applied');

# -----------------------------------------------------------------------
# 15–16: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:' . port(8081)), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
