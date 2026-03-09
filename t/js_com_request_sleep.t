#!/usr/bin/perl

# Tests for Stage 32 COM expansion: r.sleep(ms)
#
# r.sleep(ms) suspends the async request handler for ms milliseconds,
# returning control to the nginx event loop.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sleep.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /sleep_basic   { }
        location /sleep_zero    { }
        location /sleep_chain   { }
    }
}
EOF

$t->write_file('init_sleep.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/sleep_basic',  sleepBasic);
    set('/sleep_zero',   sleepZero);
    set('/sleep_chain',  sleepChain);
})();

/* basic: sleep 10ms then respond */
async function sleepBasic(r) {
    await r.sleep(10);
    r.respond(200, {'content-type': 'text/plain'}, 'after sleep');
}

/* zero ms sleep — timer fires on next event loop iteration */
async function sleepZero(r) {
    await r.sleep(0);
    r.respond(200, {'content-type': 'text/plain'}, 'zero');
}

/* chain two sleeps */
async function sleepChain(r) {
    await r.sleep(5);
    await r.sleep(5);
    r.respond(200, {'content-type': 'text/plain'}, 'chain');
}
JS

$t->try_run('no js module')->plan(5);

like(http_get('/sleep_basic'), qr/after sleep/, 'r.sleep: responds after delay');
like(http_get('/sleep_basic'), qr/200 OK/,      'r.sleep: 200 status');
like(http_get('/sleep_zero'),  qr/zero/,         'r.sleep(0): fires next iteration');
like(http_get('/sleep_chain'), qr/chain/,        'r.sleep: two awaited sleeps');
like(http_get('/sleep_chain'), qr/200 OK/,       'r.sleep: chain responds 200');
