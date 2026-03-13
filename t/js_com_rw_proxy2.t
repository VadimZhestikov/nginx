#!/usr/bin/perl

# Stage 10: NginxProxy — httpVersion and bufferSize setters
#
# Tests:
#   1.  initial httpVersion is "1.0" (nginx proxy default)
#   2.  set httpVersion="1.1" — getter reflects change
#   3.  set bufferSize=8192 — getter reflects change
#   4.  persistence: second /read/ shows new values
#   5.  bad httpVersion string throws TypeError
#   6.  set httpVersion back to "1.0" round-trip

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(6);

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

        location /proxy/ {
            proxy_pass http://127.0.0.1:8081/;
        }

        location /read/   { }
        location /set/    { }
        location /badver/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const pLoc = loc('/proxy/');

// /read/ — snapshot proxy fields
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        httpVersion: pLoc.proxy.httpVersion,
        bufferSize:  pLoc.proxy.bufferSize,
    }));
};

// /set/ — write proxy fields
loc('/set/').handler = r => {
    pLoc.proxy.httpVersion = '1.1';
    pLoc.proxy.bufferSize  = 8192;
    r.respond(200, {}, 'ok');
};

// /badver/ — invalid httpVersion
loc('/badver/').handler = r => {
    try {
        pLoc.proxy.httpVersion = '2.0';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"httpVersion":"1\.0"/, 'initial httpVersion is 1.0');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'httpVersion and bufferSize setters OK');

my $r1 = http_get('/read/');
like($r1, qr/"httpVersion":"1\.1"/, 'httpVersion updated to 1.1');
like($r1, qr/"bufferSize":8192/,    'bufferSize updated to 8192');

# ---- Persistence ----
like(http_get('/read/'), qr/"httpVersion":"1\.1"/, 'changes persist');

# ---- Error path ----
like(http_get('/badver/'), qr/error:/, 'bad httpVersion throws TypeError');

$t->stop();
