#!/usr/bin/perl

# Tests for nginx.workerRequestTimeout:
#   - can be set from js_source; reads back the configured value
#   - interrupt fires on a tight loop, handler returns 500
#   - worker survives the timeout and serves next request normally

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(4);

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

        location /read/    { }
        location /loop/    { }
        location /alive/   { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Set workerRequestTimeout to 200 ms.
nginx.workerRequestTimeout = 200;

const locs = nginx.http.servers[0].locations;
function loc(path, fn) {
    const l = locs.find(l => l.path === path);
    if (l) l.handler = fn;
}

// Read back the configured timeout.
loc('/read/', r => {
    r.respond(200, {}, String(nginx.workerRequestTimeout));
});

// Infinite loop — must be interrupted within the timeout.
loc('/loop/', r => {
    let i = 0;
    for (;;) { i++; }   // tight loop — QuickJS interrupt handler aborts this
    r.respond(200, {}, 'no-interrupt');
});

// Confirm worker is still alive after the timeout.
loc('/alive/', r => {
    r.respond(200, {}, 'alive');
});
JS

$t->run();

like(http_get('/read/'),  qr/200.*200/s,  'workerRequestTimeout reads back 200 ms');
like(http_get('/loop/'),  qr/500/s,       'infinite loop interrupted with 500');
like(http_get('/alive/'), qr/200.*alive/s, 'worker alive after timeout');
like(http_get('/read/'),  qr/200.*200/s,  'timeout value unchanged after interrupt');

$t->stop();
