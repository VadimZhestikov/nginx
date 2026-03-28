#!/usr/bin/perl

# Tests for JS-Pilgrim P8: async P2 hooks (server-scope and global-scope
# access-phase hooks that return a Promise / use await).
#
# nginx.http.addHook(async fn(req)) — global scope, runs before every request
# server.addHook(async fn(req))     — server scope, runs before every request
#
# Both now support await inside the hook body.  The request is suspended
# (r->main->count++) and resumed when the Promise settles, then the content
# handler runs normally.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(16);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p8_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sync_global/    { }
        location /async_global/   { }
        location /async_server/   { }
        location /async_block/    { }
        location /async_error/    { }
        location /async_respond/  { }
    }
}
EOF

$t->write_file_expand('p8_init.js', <<'JS');
// JS-Pilgrim P8 — async P2 hook test init
//
// Each hook uses a URI guard so it only acts on its specific endpoint.
// State is stored in module-level variables (not req properties), because
// a new JS req wrapper is created on each re-entry after an async resume,
// so any property set on the old wrapper would be lost.

var locs = nginx.http.servers[0].locations;

// Module-level flags reset by each content handler after reading.
var syncOkFlag     = false;
var asyncGlobalFlag = false;
var asyncServerFlag = false;
var blockCount     = 0;

// --- global hook 0: sync, fires only for /sync_global/ ---
nginx.http.addHook(function(req) {
    if (req.uri.indexOf('/sync_global/') === 0) {
        syncOkFlag = true;
    }
});

// loc[5] = /sync_global/ (locs[] is alphabetically ordered)
locs[5].handler = function(req) {
    var v = syncOkFlag;
    syncOkFlag = false;
    req.respond(200, {}, 'sync-global:' + (v ? 'ok' : 'missing') + '\n');
};

// --- global hook 1: async, fires only for /async_global/ ---
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/async_global/') === 0) {
        await nginx.setTimeout(5);
        asyncGlobalFlag = true;
    }
});

// loc[2] = /async_global/
locs[2].handler = function(req) {
    var v = asyncGlobalFlag ? 'done' : 'missing';
    asyncGlobalFlag = false;
    req.respond(200, {}, 'async-global:' + v + '\n');
};

// --- server hook 0: async, fires only for /async_server/ ---
nginx.http.servers[0].addHook(async function(req) {
    if (req.uri.indexOf('/async_server/') === 0) {
        await nginx.setTimeout(5);
        asyncServerFlag = true;
    }
});

// loc[4] = /async_server/
locs[4].handler = function(req) {
    var v = asyncServerFlag ? 'done' : 'missing';
    asyncServerFlag = false;
    req.respond(200, {}, 'async-server:' + v + '\n');
};

// --- global hooks 2 & 3: two sequential async hooks for /async_block/ ---
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/async_block/') === 0) {
        await nginx.setTimeout(5);
        blockCount++;
    }
});
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/async_block/') === 0) {
        await nginx.setTimeout(5);
        blockCount++;
    }
});

// loc[0] = /async_block/
locs[0].handler = function(req) {
    var v = blockCount;
    blockCount = 0;
    req.respond(200, {}, 'block:' + v + '\n');
};

// --- global hook 4: async hook that throws for /async_error/ ---
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/async_error/') === 0) {
        await nginx.setTimeout(1);
        throw new Error('async hook error');
    }
});

// loc[1] = /async_error/
locs[1].handler = function(req) {
    req.respond(200, {}, 'should-not-reach\n');
};

// --- global hook 5: async hook that calls req.respond() for /async_respond/ ---
nginx.http.addHook(async function(req) {
    if (req.uri.indexOf('/async_respond/') === 0) {
        await nginx.setTimeout(1);
        req.respond(403, {}, 'blocked-by-hook\n');
    }
});

// loc[3] = /async_respond/
locs[3].handler = function(req) {
    req.respond(200, {}, 'should-not-reach\n');
};
JS

$t->run();

# -----------------------------------------------------------------------
# 1–2: Sync global hook still works (regression test)
# -----------------------------------------------------------------------

my $r = http_get('/sync_global/');
like($r, qr{200 OK},          'sync global hook: 200 OK');
like($r, qr{sync-global:ok},  'sync global hook: _syncOk property set');

# -----------------------------------------------------------------------
# 3–4: Async global hook (await nginx.setTimeout)
# -----------------------------------------------------------------------

$r = http_get('/async_global/');
like($r, qr{200 OK},              'async global hook: 200 OK');
like($r, qr{async-global:done},   'async global hook: property set after await');

# -----------------------------------------------------------------------
# 5–6: Async server-scope hook
# -----------------------------------------------------------------------

$r = http_get('/async_server/');
like($r, qr{200 OK},              'async server hook: 200 OK');
like($r, qr{async-server:done},   'async server hook: property set after await');

# -----------------------------------------------------------------------
# 7–8: Two sequential async hooks — both must complete
# -----------------------------------------------------------------------

$r = http_get('/async_block/');
like($r, qr{200 OK},      'two async hooks: 200 OK');
like($r, qr{block:2},     'two async hooks: both ran (count=2)');

# -----------------------------------------------------------------------
# 9–10: Async hook that throws → 500
# -----------------------------------------------------------------------

$r = http_get('/async_error/');
like($r, qr{500},          'async hook throw: 500 status');
unlike($r, qr{should-not-reach}, 'async hook throw: content handler not reached');

# -----------------------------------------------------------------------
# 11–12: Async hook calling req.respond() → short-circuits content handler
# -----------------------------------------------------------------------

$r = http_get('/async_respond/');
like($r, qr{403},               'async hook respond: 403 status');
like($r, qr{blocked-by-hook},   'async hook respond: hook body returned');

# -----------------------------------------------------------------------
# 13–14: Repeated requests — state not carried over between requests
# -----------------------------------------------------------------------

$r = http_get('/async_global/');
like($r, qr{async-global:done}, 'async global hook: second request ok');

$r = http_get('/async_server/');
like($r, qr{async-server:done}, 'async server hook: second request ok');

# -----------------------------------------------------------------------
# 15–16: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
