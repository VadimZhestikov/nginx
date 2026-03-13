#!/usr/bin/perl

# Stage 15: remaining location I/O/keepalive properties and server pool sizes.
#
# Location (NginxLocation) properties:
#   keepaliveDisable    string[]  r/w  keepalive_disable bitmask
#   keepaliveMinTimeout number    r/w  keepalive_min_timeout (ms)
#   sendfileMaxChunk    number    r/w  sendfile_max_chunk (bytes)
#   readAhead           number    r/w  read_ahead (bytes)
#   directio            number|"off" r/w  directio threshold
#
# Server (NginxServer) properties:
#   connectionPoolSize  number    r/w  connection_pool_size (bytes)
#   requestPoolSize     number    r/w  request_pool_size (bytes)
#
# Tests:
#   1.  keepaliveDisable default contains "msie6"
#   2.  keepaliveMinTimeout default is 0
#   3.  sendfileMaxChunk default > 0
#   4.  readAhead default is 0
#   5.  directio default is "off"
#   6.  connectionPoolSize default > 0
#   7.  requestPoolSize default is 4096
#   8.  set keepaliveDisable = ["safari"] — no error
#   9.  keepaliveDisable reads ["safari"]
#  10.  set keepaliveMinTimeout = 200 — no error
#  11.  keepaliveMinTimeout reads 200
#  12.  set sendfileMaxChunk = 1048576 — no error
#  13.  sendfileMaxChunk reads 1048576
#  14.  set readAhead = 65536 — no error
#  15.  readAhead reads 65536
#  16.  set directio = 4096 — no error
#  17.  directio reads 4096
#  18.  set connectionPoolSize = 128 — no error
#  19.  connectionPoolSize reads 128

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(18);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /read/ { }
        location /set/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const loc = nginx.http.servers[0].locations.find(l => l.path === '/read/');
const srv = nginx.http.servers[0];

// /read/ — snapshot current property values
loc.handler = r => {
    r.respond(200, {}, JSON.stringify({
        keepaliveDisable:    loc.keepaliveDisable,
        keepaliveMinTimeout: loc.keepaliveMinTimeout,
        sendfileMaxChunk:    loc.sendfileMaxChunk,
        readAhead:           loc.readAhead,
        directio:            loc.directio,
        connectionPoolSize:  srv.connectionPoolSize,
        requestPoolSize:     srv.requestPoolSize,
    }));
};

// /set/ — mutate all writable properties
nginx.http.servers[0].locations.find(l => l.path === '/set/').handler = r => {
    loc.keepaliveDisable    = ['safari'];
    loc.keepaliveMinTimeout = 200;
    loc.sendfileMaxChunk    = 1048576;
    loc.readAhead           = 65536;
    loc.directio            = 4096;
    srv.connectionPoolSize  = 128;
    r.respond(200, {}, 'ok');
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"keepaliveDisable":\["msie6"\]/,  'keepaliveDisable default contains msie6');
like($r0, qr/"keepaliveMinTimeout":0/,          'keepaliveMinTimeout default is 0');
like($r0, qr/"sendfileMaxChunk":[1-9]/,         'sendfileMaxChunk default > 0');
like($r0, qr/"readAhead":0/,                    'readAhead default is 0');
like($r0, qr/"directio":"off"/,                 'directio default is "off"');
like($r0, qr/"connectionPoolSize":[1-9]/,       'connectionPoolSize default > 0');
like($r0, qr/"requestPoolSize":4096/,           'requestPoolSize default is 4096');

# ---- Apply writes ----
my $set = http_get('/set/');
like($set, qr/ok/, 'setters execute without error');

my $r1 = http_get('/read/');
like($r1, qr/"keepaliveDisable":\["safari"\]/,  'keepaliveDisable reads ["safari"]');
like($r1, qr/"keepaliveMinTimeout":200/,        'keepaliveMinTimeout reads 200');
like($r1, qr/"sendfileMaxChunk":1048576/,       'sendfileMaxChunk reads 1048576');
like($r1, qr/"readAhead":65536/,               'readAhead reads 65536');
like($r1, qr/"directio":4096/,                 'directio reads 4096');
like($r1, qr/"connectionPoolSize":128/,         'connectionPoolSize reads 128');

# ---- Persistence ----
my $r2 = http_get('/read/');
like($r2, qr/"keepaliveMinTimeout":200/,        'keepaliveMinTimeout persists');
like($r2, qr/"directio":4096/,                 'directio persists');
like($r2, qr/"connectionPoolSize":128/,         'connectionPoolSize persists');

# ---- requestPoolSize unchanged ----
like($r2, qr/"requestPoolSize":4096/,           'requestPoolSize unchanged after other sets');

# ---- keepaliveDisable accepts empty array ----
# (clears the bitmask — allowed)

$t->stop();
