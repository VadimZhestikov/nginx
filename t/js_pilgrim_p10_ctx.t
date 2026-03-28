#!/usr/bin/perl

# Tests for JS-Pilgrim P10: req.ctx — persistent per-request context.
#
# req.ctx is a plain JS object that:
#  - is lazily created on first access
#  - survives async suspend/resume (the req wrapper is replaced but ctx persists)
#  - survives the P2→P1 phase transition (same object visible in P2 hook AND handler)
#  - is independent per-request (not shared across requests)
#  - is freed when the nginx request pool is freed

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(15);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p10_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sync/    { }
        location /async/   { }
        location /p2p1/    { }
        location /isolate/ { }
    }
}
EOF

# Location alphabetical order:
#   locs[0] = /async/
#   locs[1] = /isolate/
#   locs[2] = /p2p1/
#   locs[3] = /sync/

$t->write_file_expand('p10_init.js', <<'JS');
// JS-Pilgrim P10 — req.ctx persistent per-request context tests

var locs = nginx.http.servers[0].locations;

// ----------------------------------------------------------------
// /sync/ (locs[3]): sync hook sets ctx property, handler reads it
// ----------------------------------------------------------------
locs[3].addHook(function(req) {
    req.ctx.token = 'sync-set';
});

locs[3].handler = function(req) {
    req.respond(200, {}, 'token:' + (req.ctx.token || 'missing') + '\n');
};

// ----------------------------------------------------------------
// /async/ (locs[0]): async hook sets ctx after await, handler reads it
// ----------------------------------------------------------------
locs[0].addHook(async function(req) {
    await nginx.setTimeout(2);
    req.ctx.token = 'async-set';
    // req wrapper is replaced on re-entry, but req.ctx persists
});

locs[0].handler = function(req) {
    req.respond(200, {}, 'token:' + (req.ctx.token || 'missing') + '\n');
};

// ----------------------------------------------------------------
// /p2p1/ (locs[2]): P2 global hook sets ctx, P1 handler reads it
// ----------------------------------------------------------------
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/p2p1/') !== 0) return;
    await nginx.setTimeout(2);
    req.ctx.source = 'p2-hook';
});

locs[2].handler = function(req) {
    req.respond(200, {}, 'source:' + (req.ctx.source || 'missing') + '\n');
};

// ----------------------------------------------------------------
// /isolate/ (locs[1]): two sequential requests must not share ctx
// ----------------------------------------------------------------
locs[1].addHook(function(req) {
    // Set a value only if it wasn't already there (it shouldn't be)
    if (req.ctx.seen === undefined) {
        req.ctx.seen = 1;
    } else {
        req.ctx.seen++;
    }
});

locs[1].handler = function(req) {
    var v = req.ctx.seen;
    req.respond(200, {}, 'seen:' + v + '\n');
};
JS

$t->run();

# -----------------------------------------------------------------------
# 1–2: Sync hook sets ctx.token, handler reads it
# -----------------------------------------------------------------------

my $r = http_get('/sync/');
like($r, qr{200 OK},          'sync ctx: 200 OK');
like($r, qr{token:sync-set},  'sync ctx: property survives hook-to-handler');

# -----------------------------------------------------------------------
# 3–4: Async hook sets ctx.token after await, handler reads it
#       (req wrapper replaced on re-entry — ctx is the bridge)
# -----------------------------------------------------------------------

$r = http_get('/async/');
like($r, qr{200 OK},           'async ctx: 200 OK');
like($r, qr{token:async-set},  'async ctx: property survives async boundary');

# -----------------------------------------------------------------------
# 5–6: P2 global async hook sets ctx.source, P1 handler reads it
#       (ctx spans access → content phase transition)
# -----------------------------------------------------------------------

$r = http_get('/p2p1/');
like($r, qr{200 OK},           'P2→P1 ctx: 200 OK');
like($r, qr{source:p2-hook},   'P2→P1 ctx: property set in P2 visible in P1 handler');

# -----------------------------------------------------------------------
# 7–8: ctx is not shared across requests (isolation)
# -----------------------------------------------------------------------

$r = http_get('/isolate/');
like($r, qr{200 OK},           'isolate: first request 200 OK');
like($r, qr{seen:1},           'isolate: first request ctx.seen == 1');

$r = http_get('/isolate/');
like($r, qr{200 OK},           'isolate: second request 200 OK');
like($r, qr{seen:1},           'isolate: second request gets fresh ctx (seen:1)');

# -----------------------------------------------------------------------
# 11–14: req.ctx is the same object across multiple accesses per request
# -----------------------------------------------------------------------

$r = http_get('/sync/');
like($r, qr{token:sync-set},   'sync ctx: second request ok (no state leak)');

$r = http_get('/async/');
like($r, qr{token:async-set},  'async ctx: second request ok');

$r = http_get('/p2p1/');
like($r, qr{source:p2-hook},   'P2→P1 ctx: second request ok');

# -----------------------------------------------------------------------
# 15–16: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
