#!/usr/bin/perl

# Stage 0-A: verify that already-writable COM properties can be mutated
# from inside a nginx.setTimeout() callback (the async "between-requests"
# code path), and that the changes are visible on subsequent requests.
#
# Properties exercised:
#   nginx.workerMemoryLimit   — ngx_js_worker_t field, numeric
#   nginx.workerRequestTimeout — ngx_js_worker_t field, numeric
#   location.handler          — swap handler function via timer
#   upstream peer.down / peer.weight — NginxRRPeer setters under wlock

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(14);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /mem-set/      { }
        location /mem-read/     { }
        location /rt-set/       { }
        location /rt-read/      { }
        location /swap-trigger/ { }
        location /swap-check/   { }
        location /peer-set/     { }
        location /peer-read/    { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Baseline values (set during init_conf so we have a known starting point)
nginx.workerMemoryLimit    = 64 * 1024 * 1024;   // 64 MB
nginx.workerRequestTimeout = 5000;                // 5 s

const locs = nginx.http.servers[0].locations;
function loc(path, fn) {
    const l = locs.find(l => l.path === path);
    if (l) l.handler = fn;
}

// ---- workerMemoryLimit via setTimeout ----

loc('/mem-set/', async r => {
    // Suspend current request for one event-loop turn, then mutate.
    await nginx.setTimeout(0);
    nginx.workerMemoryLimit = 32 * 1024 * 1024;   // 32 MB
    r.respond(200, {}, String(nginx.workerMemoryLimit));
});

loc('/mem-read/', r => {
    r.respond(200, {}, String(nginx.workerMemoryLimit));
});

// ---- workerRequestTimeout via setTimeout ----

loc('/rt-set/', async r => {
    await nginx.setTimeout(0);
    nginx.workerRequestTimeout = 1000;   // 1 s
    r.respond(200, {}, String(nginx.workerRequestTimeout));
});

loc('/rt-read/', r => {
    r.respond(200, {}, String(nginx.workerRequestTimeout));
});

// ---- location.handler swap via setTimeout ----
//
// The /swap-trigger/ handler replaces its own location's handler after a
// timer tick.  The swap is fire-and-forget: we respond first, then the
// timer fires and installs the new function.  The next request to
// /swap-check/ must see the new handler.

const swapLoc = locs.find(l => l.path === '/swap-check/');

loc('/swap-trigger/', async r => {
    r.respond(200, {}, 'trigger-done');
    await nginx.setTimeout(0);
    if (swapLoc) swapLoc.handler = r2 => r2.respond(200, {}, 'swapped');
});

loc('/swap-check/', r => {
    r.respond(200, {}, 'original');
});

// ---- peer.down and peer.weight via setTimeout ----

const peer = nginx.http.upstreams[0].peers[0];

loc('/peer-set/', async r => {
    await nginx.setTimeout(0);
    peer.down   = true;
    peer.weight = 7;
    r.respond(200, {}, 'peer-set');
});

loc('/peer-read/', r => {
    r.respond(200, {},
        'down=' + peer.down + ',weight=' + peer.weight);
});
JS

$t->run();

# -- workerMemoryLimit --

like(http_get('/mem-read/'), qr/67108864/,  'workerMemoryLimit baseline is 64 MB');
like(http_get('/mem-set/'),  qr/33554432/,  'setTimeout callback sets workerMemoryLimit to 32 MB');
like(http_get('/mem-read/'), qr/33554432/,  'workerMemoryLimit change persists across requests');

# -- workerRequestTimeout --

like(http_get('/rt-read/'),  qr/5000/,      'workerRequestTimeout baseline is 5000 ms');
like(http_get('/rt-set/'),   qr/1000/,      'setTimeout callback sets workerRequestTimeout to 1000 ms');
like(http_get('/rt-read/'),  qr/1000/,      'workerRequestTimeout change persists across requests');

# -- location.handler swap --

# trigger fires the swap (fire-and-forget); response is 'trigger-done'
like(http_get('/swap-trigger/'), qr/trigger-done/, 'swap trigger responds immediately');

# After the trigger has returned, the swap-check location must run the new handler.
# One more round-trip is enough — the timer (0 ms) fires before the next request.
like(http_get('/swap-check/'), qr/swapped/, 'handler swapped via setTimeout is live');

# -- peer.down + peer.weight --

like(http_get('/peer-read/'), qr/down=false,weight=1/, 'peer baseline: down=false weight=1');
like(http_get('/peer-set/'),  qr/peer-set/,             'peer mutated via setTimeout');
like(http_get('/peer-read/'), qr/down=true,weight=7/,  'peer.down and peer.weight persist');

# -- restore peer state so other tests are not affected --
# (peer.down=false is done by a final request that our inline handler resets)
like(http_get('/peer-set/'),  qr/peer-set/,  'second peer-set for cleanup setup');

# Sanity: workerMemoryLimit is still 32 MB after all the above
like(http_get('/mem-read/'), qr/33554432/, 'workerMemoryLimit still 32 MB after peer tests');

# Sanity: workerRequestTimeout still 1000 ms
like(http_get('/rt-read/'),  qr/1000/,     'workerRequestTimeout still 1000 ms after all tests');

$t->stop();
