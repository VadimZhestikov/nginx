#!/usr/bin/perl

# Stage 8a: NginxSsi — all six properties become runtime-writable
#
#   enable                 boolean
#   silentErrors           boolean
#   ignoreRecycledBuffers  boolean
#   lastModified           boolean
#   minFileChunk           number (bytes)
#   valueLen               number (bytes)
#
# Tests:
#   1.  Initial enable=true from config
#   2.  Initial silentErrors=false from config
#   3.  Write enable=false — getter reflects change
#   4.  Write silentErrors=true — getter reflects change
#   5.  Write ignoreRecycledBuffers=true — getter reflects change
#   6.  Write lastModified=true — getter reflects change
#   7.  Write minFileChunk=8192 — getter reflects change
#   8.  Write valueLen=4096 — getter reflects change
#   9.  Persistence: second /read/ shows all new values
#  10.  Negative minFileChunk throws RangeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http ssi/)->plan(10);

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

        location /ssi/ {
            ssi on;
        }

        location /read/   { }
        location /set/    { }
        location /badnum/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const ssiLoc = loc('/ssi/');

// /read/ — return all current ssi property values
loc('/read/').handler = r => {
    const s = ssiLoc.ssi;
    r.respond(200, {}, JSON.stringify({
        enable:                s.enable,
        silentErrors:          s.silentErrors,
        ignoreRecycledBuffers: s.ignoreRecycledBuffers,
        lastModified:          s.lastModified,
        minFileChunk:          s.minFileChunk,
        valueLen:              s.valueLen,
    }));
};

// /set/ — write new values
loc('/set/').handler = r => {
    const s = ssiLoc.ssi;
    s.enable                = false;
    s.silentErrors          = true;
    s.ignoreRecycledBuffers = true;
    s.lastModified          = true;
    s.minFileChunk          = 8192;
    s.valueLen              = 4096;
    r.respond(200, {}, JSON.stringify({
        enable:                s.enable,
        silentErrors:          s.silentErrors,
        ignoreRecycledBuffers: s.ignoreRecycledBuffers,
        lastModified:          s.lastModified,
        minFileChunk:          s.minFileChunk,
        valueLen:              s.valueLen,
    }));
};

// /badnum/ — negative minFileChunk
loc('/badnum/').handler = r => {
    try {
        ssiLoc.ssi.minFileChunk = -1;
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values from config ----
my $r0 = http_get('/read/');
like($r0, qr/"enable":true/,         'initial ssi.enable is true');
like($r0, qr/"silentErrors":false/,  'initial ssi.silentErrors is false');

# ---- Apply writes ----
my $r1 = http_get('/set/');
like($r1, qr/"enable":false/,                  'enable set to false');
like($r1, qr/"silentErrors":true/,             'silentErrors set to true');
like($r1, qr/"ignoreRecycledBuffers":true/,    'ignoreRecycledBuffers set to true');
like($r1, qr/"lastModified":true/,             'lastModified set to true');
like($r1, qr/"minFileChunk":8192/,             'minFileChunk set to 8192');
like($r1, qr/"valueLen":4096/,                 'valueLen set to 4096');

# ---- Persistence ----
like(http_get('/read/'), qr/"enable":false/, 'changes persist on next request');

# ---- Error path ----
like(http_get('/badnum/'), qr/error:/, 'negative minFileChunk throws RangeError');

$t->stop();
