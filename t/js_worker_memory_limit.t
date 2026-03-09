#!/usr/bin/perl

# Tests for nginx.workerMemoryLimit:
#   - can be set from js_source; reads back the configured bytes
#   - workers respect the limit (OOM throws, worker survives)

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

        location /read/  { }
        location /oom/   { }
        location /alive/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Set workerMemoryLimit to 64 MB before workers fork.
nginx.workerMemoryLimit = 64 * 1024 * 1024;

const locs = nginx.http.servers[0].locations;
function loc(path, fn) {
    const l = locs.find(l => l.path === path);
    if (l) l.handler = fn;
}

// Read back the configured limit.
loc('/read/', r => {
    r.respond(200, {}, String(nginx.workerMemoryLimit));
});

// Try to allocate 512 MB — must throw OOM under the 64 MB cap.
loc('/oom/', r => {
    try {
        const buf = new ArrayBuffer(512 * 1024 * 1024);
        r.respond(200, {}, 'no-oom');
    } catch (e) {
        r.respond(200, {}, 'oom-caught:' + e.constructor.name);
    }
});

// Confirm the worker is still functional after the OOM.
loc('/alive/', r => {
    r.respond(200, {}, 'alive');
});
JS

$t->run();

like(http_get('/read/'),  qr/200.*67108864/s,  'workerMemoryLimit reads back correct bytes');
like(http_get('/oom/'),   qr/200.*oom-caught/s, 'OOM exception caught under limit');
like(http_get('/alive/'), qr/200.*alive/s,      'worker still alive after OOM');
like(http_get('/read/'),  qr/200.*67108864/s,   'limit unchanged after OOM');

$t->stop();
