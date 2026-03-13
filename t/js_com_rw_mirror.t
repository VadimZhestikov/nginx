#!/usr/bin/perl

# Stage 13: mirror.uris setter — runtime-writable mirror target URIs
#
# Tests:
#   1.  initial uris.length == 1 (mirror /backend/)
#   2.  initial uris[0] == "/backend/"
#   3.  set new uris — no error
#   4.  after set: uris.length == 2
#   5.  after set: uris[0] == "/m1/"
#   6.  after set: uris[1] == "/m2/"
#   7.  persistence: second /read/ still shows new uris
#   8.  set [] clears mirror uris
#   9.  non-array throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http mirror/)->plan(9);

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

        location /src/ {
            mirror /backend/;
        }

        location /backend/ { return 200 ok; }
        location /m1/      { return 200 ok; }
        location /m2/      { return 200 ok; }

        location /read/   { }
        location /set/    { }
        location /clear/  { }
        location /bad/    { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const srcLoc = loc('/src/');

// /read/ — snapshot uris
loc('/read/').handler = r => {
    const uris = srcLoc.mirror.uris;
    r.respond(200, {}, JSON.stringify({
        len:  uris.length,
        uris: uris,
    }));
};

// /set/ — replace uris
loc('/set/').handler = r => {
    srcLoc.mirror.uris = ['/m1/', '/m2/'];
    r.respond(200, {}, 'ok');
};

// /clear/ — set empty
loc('/clear/').handler = r => {
    srcLoc.mirror.uris = [];
    r.respond(200, {}, JSON.stringify({ len: srcLoc.mirror.uris.length }));
};

// /bad/ — non-array throws TypeError
loc('/bad/').handler = r => {
    try {
        srcLoc.mirror.uris = '/not-an-array/';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"len":1/,              'initial uris.length is 1');
like($r0, qr/"\/backend\/"/,        'initial uris[0] is /backend/');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/,     'uris setter executes without error');

my $r1 = http_get('/read/');
like($r1, qr/"len":2/,              'after set: uris.length is 2');
like($r1, qr/"\/m1\/"/,             'after set: uris[0] is /m1/');
like($r1, qr/"\/m2\/"/,             'after set: uris[1] is /m2/');

# ---- Persistence ----
like(http_get('/read/'), qr/"\/m1\/"/, 'changes persist');

# ---- Clear ----
like(http_get('/clear/'), qr/"len":0/, 'set [] clears uris');

# ---- Error path ----
like(http_get('/bad/'), qr/error:/, 'non-array throws TypeError');

$t->stop();
