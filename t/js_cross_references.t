#!/usr/bin/perl

# Tests for Stage F2 — cross-references between NginxSocket, NginxHttpListener,
# and NginxStreamListener.
#
# Covers:
#   NginxSocket.listener        — NginxHttpListener | NginxStreamListener | null
#   NginxHttpListener.socket    — NginxSocket back-reference
#   NginxHttpListener.serverNames[]  — array of server name strings
#   NginxHttpListener.serverByName(name)  — case-insensitive lookup
#   NginxStreamListener.socket  — NginxSocket back-reference
#   NginxStreamListener.serverNames[]    — array of server name strings
#   NginxStreamListener.serverByName(name)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/xref.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  mysite.example;
        location / { }
    }
}

stream {
    server {
        listen      127.0.0.1:%%PORT_8093%%;
        # no server_name in stream servers — uses stream default
    }
}
EOF

$t->write_file_expand('xref.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

/* ================================================================ */
/* HTTP cross-reference                                              */
/* ================================================================ */

/* Create a socket and verify sock.listener === null before attach */
var httpSock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

check("http_sock_listener_null_before_attach",
      httpSock.listener === null,
      String(httpSock.listener));

/* Attach the socket to the HTTP pipeline */
var httpListener = nginx.http.attach(httpSock);

/* sock.listener — should now return the NginxHttpListener */
var gotListener = httpSock.listener;

check("http_sock_listener_not_null",
      gotListener !== null && gotListener !== undefined,
      String(gotListener));

/* duck-type check: NginxHttpListener has addServer method */
check("http_sock_listener_class",
      gotListener !== null &&
      gotListener !== undefined &&
      typeof gotListener.addServer === "function",
      gotListener && typeof gotListener.addServer);

/* The listener from sock.listener should have the same address */
check("http_sock_listener_address",
      gotListener !== null &&
      gotListener !== undefined &&
      gotListener.address === httpSock.address,
      gotListener && gotListener.address);

/* listener.socket — back-reference to the NginxSocket */
var gotSocket = httpListener.socket;

check("http_listener_socket_not_null",
      gotSocket !== null && gotSocket !== undefined,
      String(gotSocket));

/* duck-type check: NginxSocket has port (number) */
check("http_listener_socket_class",
      gotSocket !== null &&
      gotSocket !== undefined &&
      typeof gotSocket.port === "number",
      gotSocket && typeof gotSocket.port);

check("http_listener_socket_address",
      gotSocket !== null &&
      gotSocket !== undefined &&
      gotSocket.address === httpSock.address,
      gotSocket && gotSocket.address);

/* listener.serverNames[] — before addServer() should be empty array */
var namesBeforeAdd = httpListener.serverNames;
check("http_listener_serverNames_before_add_array",
      Array.isArray(namesBeforeAdd),
      typeof namesBeforeAdd);

check("http_listener_serverNames_before_add_empty",
      namesBeforeAdd.length === 0,
      namesBeforeAdd.length);

/* Now add the HTTP server */
var httpSrv = nginx.http.servers[0];
httpListener.addServer(httpSrv);

/* listener.serverNames[] — after addServer() should contain the server names */
var namesAfterAdd = httpListener.serverNames;
check("http_listener_serverNames_after_add_array",
      Array.isArray(namesAfterAdd),
      typeof namesAfterAdd);

check("http_listener_serverNames_after_add_nonempty",
      namesAfterAdd.length > 0,
      namesAfterAdd.length);

check("http_listener_serverNames_has_mysite",
      namesAfterAdd.indexOf("mysite.example") !== -1,
      JSON.stringify(namesAfterAdd));

/* serverByName() — found */
var byName = httpListener.serverByName("mysite.example");
check("http_listener_serverByName_found",
      byName !== null && byName !== undefined && typeof byName === "object",
      String(byName));

/* serverByName() — case insensitive */
var byNameUC = httpListener.serverByName("MYSITE.EXAMPLE");
check("http_listener_serverByName_case_insensitive",
      byNameUC !== null && byNameUC !== undefined && typeof byNameUC === "object",
      String(byNameUC));

/* serverByName() — unknown returns null */
var byNameMissing = httpListener.serverByName("no-such-host.example");
check("http_listener_serverByName_missing_null",
      byNameMissing === null,
      String(byNameMissing));

/* ================================================================ */
/* Stream cross-reference                                            */
/* ================================================================ */

/* Create a stream socket and verify sock.listener === null before attach */
var streamSock = nginx.createSocket("127.0.0.1:%%PORT_8092%%");

check("stream_sock_listener_null_before_attach",
      streamSock.listener === null,
      String(streamSock.listener));

/* Attach to stream pipeline */
var streamListener = nginx.stream.attach(streamSock);

/* sock.listener — should return NginxStreamListener */
var gotStreamListener = streamSock.listener;

check("stream_sock_listener_not_null",
      gotStreamListener !== null && gotStreamListener !== undefined,
      String(gotStreamListener));

/* duck-type check: NginxStreamListener has addServer method */
check("stream_sock_listener_class",
      gotStreamListener !== null &&
      gotStreamListener !== undefined &&
      typeof gotStreamListener.addServer === "function",
      gotStreamListener && typeof gotStreamListener.addServer);

check("stream_sock_listener_address",
      gotStreamListener !== null &&
      gotStreamListener !== undefined &&
      gotStreamListener.address === streamSock.address,
      gotStreamListener && gotStreamListener.address);

/* listener.socket — back-reference */
var gotStreamSocket = streamListener.socket;

check("stream_listener_socket_not_null",
      gotStreamSocket !== null && gotStreamSocket !== undefined,
      String(gotStreamSocket));

/* duck-type check: NginxSocket has port (number) */
check("stream_listener_socket_class",
      gotStreamSocket !== null &&
      gotStreamSocket !== undefined &&
      typeof gotStreamSocket.port === "number",
      gotStreamSocket && typeof gotStreamSocket.port);

check("stream_listener_socket_address",
      gotStreamSocket !== null &&
      gotStreamSocket !== undefined &&
      gotStreamSocket.address === streamSock.address,
      gotStreamSocket && gotStreamSocket.address);

/* serverNames[] before addServer */
var streamNamesBeforeAdd = streamListener.serverNames;
check("stream_listener_serverNames_before_add_array",
      Array.isArray(streamNamesBeforeAdd),
      typeof streamNamesBeforeAdd);

check("stream_listener_serverNames_before_add_empty",
      streamNamesBeforeAdd.length === 0,
      streamNamesBeforeAdd.length);

/* Add the stream server */
var streamSrv = nginx.stream.servers[0];
streamListener.addServer(streamSrv);

/* serverNames[] after addServer — stream servers may have empty name */
var streamNamesAfterAdd = streamListener.serverNames;
check("stream_listener_serverNames_after_add_array",
      Array.isArray(streamNamesAfterAdd),
      typeof streamNamesAfterAdd);

/* serverByName() with empty string (stream server's default name) */
var streamByEmpty = streamListener.serverByName("");
check("stream_listener_serverByName_empty_not_error",
      streamByEmpty !== undefined,
      typeof streamByEmpty);

/* serverByName() — unknown */
var streamByMissing = streamListener.serverByName("no-such-host.example");
check("stream_listener_serverByName_missing_null",
      streamByMissing === null,
      String(streamByMissing));
JS

$t->try_run('no js module or stream module')->plan(28);

# Trigger nginx startup; the JS runs at init_conf time.
sleep(1);

my $log = $t->read_file('error.log');

# HTTP cross-references
like($log, qr/JSTEST PASS http_sock_listener_null_before_attach/, 'sock.listener is null before attach');
like($log, qr/JSTEST PASS http_sock_listener_not_null/,           'sock.listener is not null after attach');
like($log, qr/JSTEST PASS http_sock_listener_class/,              'sock.listener is NginxHttpListener');
like($log, qr/JSTEST PASS http_sock_listener_address/,            'sock.listener.address matches sock');
like($log, qr/JSTEST PASS http_listener_socket_not_null/,         'listener.socket is not null');
like($log, qr/JSTEST PASS http_listener_socket_class/,            'listener.socket is NginxSocket');
like($log, qr/JSTEST PASS http_listener_socket_address/,          'listener.socket.address matches sock');
like($log, qr/JSTEST PASS http_listener_serverNames_before_add_array/, 'serverNames[] is array before addServer');
like($log, qr/JSTEST PASS http_listener_serverNames_before_add_empty/, 'serverNames[] is empty before addServer');
like($log, qr/JSTEST PASS http_listener_serverNames_after_add_array/,  'serverNames[] is array after addServer');
like($log, qr/JSTEST PASS http_listener_serverNames_after_add_nonempty/, 'serverNames[] nonempty after addServer');
like($log, qr/JSTEST PASS http_listener_serverNames_has_mysite/,  'serverNames contains mysite.example');
like($log, qr/JSTEST PASS http_listener_serverByName_found/,      'serverByName finds exact match');
like($log, qr/JSTEST PASS http_listener_serverByName_case_insensitive/, 'serverByName is case-insensitive');
like($log, qr/JSTEST PASS http_listener_serverByName_missing_null/, 'serverByName returns null for unknown');

# Stream cross-references
like($log, qr/JSTEST PASS stream_sock_listener_null_before_attach/, 'stream sock.listener null before attach');
like($log, qr/JSTEST PASS stream_sock_listener_not_null/,           'stream sock.listener not null after attach');
like($log, qr/JSTEST PASS stream_sock_listener_class/,              'stream sock.listener is NginxStreamListener');
like($log, qr/JSTEST PASS stream_sock_listener_address/,            'stream sock.listener.address matches sock');
like($log, qr/JSTEST PASS stream_listener_socket_not_null/,         'stream listener.socket not null');
like($log, qr/JSTEST PASS stream_listener_socket_class/,            'stream listener.socket is NginxSocket');
like($log, qr/JSTEST PASS stream_listener_socket_address/,          'stream listener.socket.address matches sock');
like($log, qr/JSTEST PASS stream_listener_serverNames_before_add_array/, 'stream serverNames[] array before addServer');
like($log, qr/JSTEST PASS stream_listener_serverNames_before_add_empty/, 'stream serverNames[] empty before addServer');
like($log, qr/JSTEST PASS stream_listener_serverNames_after_add_array/,  'stream serverNames[] array after addServer');
like($log, qr/JSTEST PASS stream_listener_serverByName_empty_not_error/, 'stream serverByName("") no crash');
like($log, qr/JSTEST PASS stream_listener_serverByName_missing_null/,    'stream serverByName unknown == null');

# Basic non-crash check
unlike($log, qr/JSTEST FAIL/, 'no JS test failures');
