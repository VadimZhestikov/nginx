#!/usr/bin/perl

# Tests for nginx.workerMemoryLimit:
#   - can be set from js_source; reads back the configured bytes
#   - can be reconfigured from a request handler at runtime
#   - workers respect the limit (OOM throws, worker survives)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

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

        location /read/      { }
        location /set-tight/ { }
        location /oom/       { }
        location /alive/     { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Set workerMemoryLimit to 64 MB from js_source.
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

// Reconfigure memory limit to 1 MB from a request handler.
loc('/set-tight/', r => {
    nginx.workerMemoryLimit = 1024 * 1024;
    r.respond(200, {}, String(nginx.workerMemoryLimit));
});

// Try to allocate 64 MB — must throw OOM under the 1 MB cap.
loc('/oom/', r => {
    try {
        const buf = new ArrayBuffer(64 * 1024 * 1024);
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

like(http_get('/read/'),      qr/200.*67108864/s,   'workerMemoryLimit reads back 64 MB from js_source');
like(http_get('/set-tight/'), qr/200.*1048576/s,    'request handler sets workerMemoryLimit to 1 MB');
like(http_get('/read/'),      qr/200.*1048576/s,    'new limit visible on next request');
like(http_get('/oom/'),       qr/200.*oom-caught/s, 'OOM exception caught under 1 MB cap');
like(http_get('/alive/'),     qr/200.*alive/s,      'worker still alive after OOM');
like(http_get('/read/'),      qr/200.*1048576/s,    'limit unchanged after OOM');

$t->stop();
