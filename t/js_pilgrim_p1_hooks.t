#!/usr/bin/perl

# Tests for JS-Pilgrim P1 — location.addHook(asyncFn):
#   Pre-content hooks that run before location.handler.
#   Sync and async hooks; hooks that respond vs pass-through.

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

js_source %%TESTDIR%%/hooks_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sync_pass/     { }
        location /sync_cancel/   { }
        location /multi_pass/    { }
        location /multi_cancel/  { }
        location /async_pass/    { }
        location /async_cancel/  { }
        location /hook_only/     { }
    }
}
EOF

$t->write_file('hooks_init.js', <<'JS');
// JS-Pilgrim P1 hook tests — install hooks and handlers in config phase.

(function installAll() {
    const locs = nginx.http.servers[0].locations;

    function loc(path) {
        return locs.find(l => l.path === path);
    }

    // Test 1: sync hook that passes through; handler responds
    {
        const l = loc('/sync_pass/');
        l.addHook(function(req) {
            // passthrough: no req.respond()
        });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'handler');
        };
    }

    // Test 2: sync hook that cancels; handler must not run
    {
        const l = loc('/sync_cancel/');
        l.addHook(function(req) {
            req.respond(403, {'content-type': 'text/plain'}, 'hook-cancel');
        });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'should-not-reach');
        };
    }

    // Test 3: multiple hooks, all pass through; handler responds
    {
        const l = loc('/multi_pass/');
        l.addHook(function(req) { /* pass */ });
        l.addHook(function(req) { /* pass */ });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'multi-handler');
        };
    }

    // Test 4: multiple hooks; first cancels, second must not run
    {
        const l = loc('/multi_cancel/');
        l.addHook(function(req) {
            req.respond(401, {'content-type': 'text/plain'}, 'first-hook-cancel');
        });
        l.addHook(function(req) {
            // This hook should NOT run
            req.respond(200, {'content-type': 'text/plain'}, 'second-hook');
        });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'multi-cancel-handler');
        };
    }

    // Test 5: async hook (uses nginx.setTimeout) that passes through
    {
        const l = loc('/async_pass/');
        l.addHook(async function(req) {
            await nginx.setTimeout(5);
            // no respond — continue to handler
        });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'async-pass-handler');
        };
    }

    // Test 6: async hook that cancels after await
    {
        const l = loc('/async_cancel/');
        l.addHook(async function(req) {
            await nginx.setTimeout(5);
            req.respond(418, {'content-type': 'text/plain'}, 'async-hook-cancel');
        });
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'should-not-reach');
        };
    }

    // Test 7: hook only — no JS handler set; hook always responds
    {
        const l = loc('/hook_only/');
        l.addHook(function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'hook-only-ok');
        });
        // no l.handler
    }

})();
JS

$t->try_run('no js module')->plan(14);

# Test 1: sync hook passes through — handler response
like(http_get('/sync_pass/'), qr/200 OK/,   'sync passthrough hook: 200 OK');
like(http_get('/sync_pass/'), qr/handler/,  'sync passthrough hook: handler body');

# Test 2: sync hook cancels — hook response, not handler
like(http_get('/sync_cancel/'), qr/403/,           'sync cancel hook: 403 status');
like(http_get('/sync_cancel/'), qr/hook-cancel/,   'sync cancel hook: hook body');
unlike(http_get('/sync_cancel/'), qr/should-not-reach/, 'sync cancel hook: handler not called');

# Test 3: multiple pass-through hooks — handler runs
like(http_get('/multi_pass/'), qr/200 OK/,       'multi pass hooks: 200 OK');
like(http_get('/multi_pass/'), qr/multi-handler/, 'multi pass hooks: handler body');

# Test 4: first hook cancels — second hook and handler not called
like(http_get('/multi_cancel/'), qr/401/,               'multi cancel: 401 status');
like(http_get('/multi_cancel/'), qr/first-hook-cancel/, 'multi cancel: first hook body');

# Test 5: async hook passes through — handler runs after timer
like(http_get('/async_pass/'), qr/200 OK/,            'async pass hook: 200 OK');
like(http_get('/async_pass/'), qr/async-pass-handler/, 'async pass hook: handler body');

# Test 6: async hook cancels after timer
like(http_get('/async_cancel/'), qr/418/,             'async cancel hook: 418 status');
like(http_get('/async_cancel/'), qr/async-hook-cancel/, 'async cancel hook: body');

# Test 7: hook-only location (no JS handler) — hook responds
like(http_get('/hook_only/'), qr/hook-only-ok/, 'hook-only location: hook responds');
