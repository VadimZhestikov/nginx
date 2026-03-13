#!/usr/bin/perl

# Stage 9b: NginxServer — scalar fields become runtime-writable
#
#   clientHeaderBufferSize  bytes
#   clientHeaderTimeout     ms
#   ignoreInvalidHeaders    boolean
#   mergeSlashes            boolean
#   underscoresInHeaders    boolean
#   serverTokens            string  "off"/"on"/"build"
#   connectionPoolSize      bytes
#   requestPoolSize         bytes
#
# Tests:
#   1-2.  initial values from config
#   3.    write all fields — no error
#   4-7.  spot-check getters after write
#   8.    persistence: second /read/ shows new values
#   9.    serverTokens bad string throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(9);

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
        server_tokens off;
        merge_slashes on;

        location /read/   { }
        location /set/    { }
        location /badtok/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// /read/ — snapshot server fields
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        clientHeaderBufferSize: srv.clientHeaderBufferSize,
        clientHeaderTimeout:    srv.clientHeaderTimeout,
        ignoreInvalidHeaders:   srv.ignoreInvalidHeaders,
        mergeSlashes:           srv.mergeSlashes,
        underscoresInHeaders:   srv.underscoresInHeaders,
        serverTokens:           srv.serverTokens,
        connectionPoolSize:     srv.connectionPoolSize,
        requestPoolSize:        srv.requestPoolSize,
    }));
};

// /set/ — write all server fields
loc('/set/').handler = r => {
    srv.clientHeaderBufferSize = 2048;
    srv.clientHeaderTimeout    = 30000;
    srv.ignoreInvalidHeaders   = false;
    srv.mergeSlashes           = false;
    srv.underscoresInHeaders   = true;
    srv.serverTokens           = 'on';
    srv.connectionPoolSize     = 512;
    srv.requestPoolSize        = 8192;
    r.respond(200, {}, 'ok');
};

// /badtok/ — invalid serverTokens string
loc('/badtok/').handler = r => {
    try {
        srv.serverTokens = 'full';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"serverTokens":"off"/,  'initial serverTokens is off');
like($r0, qr/"mergeSlashes":true/,   'initial mergeSlashes is true');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all server setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"clientHeaderBufferSize":2048/, 'clientHeaderBufferSize updated');
like($r1, qr/"ignoreInvalidHeaders":false/,  'ignoreInvalidHeaders updated');
like($r1, qr/"mergeSlashes":false/,          'mergeSlashes updated');
like($r1, qr/"serverTokens":"on"/,           'serverTokens updated to on');

# ---- Persistence ----
like(http_get('/read/'), qr/"serverTokens":"on"/, 'server changes persist');

# ---- Error path ----
like(http_get('/badtok/'), qr/error:/, 'bad serverTokens string throws TypeError');

$t->stop();
