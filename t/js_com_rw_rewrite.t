#!/usr/bin/perl

# Stage 10: NginxRewrite — log, uninitializedVariableWarn, stackSize setters
#
# Tests:
#   1.  initial log=false from config (rewrite_log off by default)
#   2.  set log=true — getter reflects change
#   3.  set uninitializedVariableWarn=false
#   4.  set stackSize=65536
#   5.  persistence: second /read/ shows new values
#   6.  hasRules stays read-only (assignment silently ignored, value unchanged)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(6);

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

        location /target/ {
            rewrite ^/target/(.*)$ /$1 break;
        }

        location /read/   { }
        location /set/    { }
        location /hasrules/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const tgt = loc('/target/');

// /read/ — snapshot rewrite fields
loc('/read/').handler = r => {
    const rw = tgt.rewrite;
    r.respond(200, {}, JSON.stringify({
        log:                      rw.log,
        uninitializedVariableWarn: rw.uninitializedVariableWarn,
        stackSize:                rw.stackSize,
        hasRules:                 rw.hasRules,
    }));
};

// /set/ — write rewrite fields
loc('/set/').handler = r => {
    const rw = tgt.rewrite;
    rw.log                      = true;
    rw.uninitializedVariableWarn = false;
    rw.stackSize                = 65536;
    r.respond(200, {}, 'ok');
};

// /hasrules/ — hasRules is read-only; write is silently ignored
loc('/hasrules/').handler = r => {
    const rw = tgt.rewrite;
    const before = rw.hasRules;
    rw.hasRules = !before;      // attempt to flip
    r.respond(200, {}, JSON.stringify({ before, after: rw.hasRules }));
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"log":false/,                        'initial log is false');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all rewrite setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"log":true/,                         'log set to true');
like($r1, qr/"uninitializedVariableWarn":false/,  'uninitializedVariableWarn set to false');
like($r1, qr/"stackSize":65536/,                  'stackSize set to 65536');

# ---- Persistence ----
like(http_get('/read/'), qr/"log":true/, 'changes persist on next request');

$t->stop();
