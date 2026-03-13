#!/usr/bin/perl

# Stage 12: location.errorPage setter — runtime-writable error page rules
#
# Tests:
#   1.  initial errorPage.length == 1 (error_page 404 /404.html)
#   2.  initial errorPage[0].status == 404
#   3.  initial errorPage[0].uri == "/404.html"
#   4.  set new errorPage — no error
#   5.  after set: errorPage.length == 2
#   6.  after set: errorPage[0].status == 404, uri == "/50x.html"
#   7.  after set: errorPage[1].status == 500, overwrite == 503
#   8.  persistence: second /read/ shows new rules
#   9.  set [] clears error pages
#  10.  non-array throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(10);

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
            error_page 404 /404.html;
        }

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

const tgtLoc = loc('/target/');

// /read/ — snapshot errorPage
loc('/read/').handler = r => {
    const ep = tgtLoc.errorPage;
    r.respond(200, {}, JSON.stringify({
        len: ep.length,
        ep:  ep,
    }));
};

// /set/ — replace error pages
loc('/set/').handler = r => {
    tgtLoc.errorPage = [
        { status: 404, uri: '/50x.html' },
        { status: 500, overwrite: 503, uri: '/50x.html' },
    ];
    r.respond(200, {}, 'ok');
};

// /clear/ — set empty
loc('/clear/').handler = r => {
    tgtLoc.errorPage = [];
    r.respond(200, {}, JSON.stringify({ len: tgtLoc.errorPage.length }));
};

// /bad/ — non-array triggers TypeError
loc('/bad/').handler = r => {
    try {
        tgtLoc.errorPage = 'notanarray';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"len":1/,           'initial errorPage.length is 1');
like($r0, qr/"status":404/,      'initial errorPage[0].status is 404');
like($r0, qr/"uri":"\/404\.html"/, 'initial errorPage[0].uri is /404.html');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/,  'errorPage setter executes without error');

my $r1 = http_get('/read/');
like($r1, qr/"len":2/,                    'after set: errorPage.length is 2');
like($r1, qr/\{"status":404,"overwrite":0,"uri":"\/50x\.html"\}/,
                                          'after set: entry[0] is 404→/50x.html');
like($r1, qr/\{"status":500,"overwrite":503,"uri":"\/50x\.html"\}/,
                                          'after set: entry[1] is 500→overwrite 503');

# ---- Persistence ----
like(http_get('/read/'), qr/"uri":"\/50x\.html"/, 'changes persist');

# ---- Clear ----
like(http_get('/clear/'), qr/"len":0/, 'set [] clears error pages');

# ---- Error path ----
like(http_get('/bad/'), qr/error:/, 'non-array throws TypeError');

$t->stop();
