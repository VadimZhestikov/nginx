#!/usr/bin/perl

# Tests for JS-Pilgrim P3 — location.addResponseHook(fn):
#   Header-filter-phase hooks that run after the content handler sets
#   status/headers but before they are sent to the client.
#   Hooks can read/modify req.status and req.headersOut.
#   Hooks are sync-only.

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

js_source %%TESTDIR%%/p3_hooks_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /add_header/     { }
        location /modify_status/  { }
        location /multi_hooks/    { }
        location /no_respond/     { }
        location /parent/  { }
        location /parent/child/ { }
        location /not_found/ { }
    }
}
EOF

$t->write_file('p3_hooks_init.js', <<'JS');
// JS-Pilgrim P3 response hook tests.

(function installAll() {
    const locs = nginx.http.servers[0].locations;

    function loc(path) {
        return locs.find(l => l.path === path);
    }

    // Test 1: response hook adds a header
    {
        const l = loc('/add_header/');
        l.handler = function(req) {
            req.respond(200, {'x-original': 'yes'}, 'body\n');
        };
        l.addResponseHook(function(req) {
            req.setHeader('x-hook', 'fired');
        });
    }

    // Test 2: response hook modifies status code 200 -> 202
    {
        const l = loc('/modify_status/');
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'accepted\n');
        };
        l.addResponseHook(function(req) {
            req.status = 202;
        });
    }

    // Test 3: multiple response hooks run in order
    {
        const l = loc('/multi_hooks/');
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'multi\n');
        };
        l.addResponseHook(function(req) {
            req.setHeader('x-hook-a', 'first');
        });
        l.addResponseHook(function(req) {
            req.setHeader('x-hook-b', 'second');
        });
    }

    // Test 4: response hook can setHeader after content handler ran
    {
        const l = loc('/no_respond/');
        l.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'original\n');
        };
        l.addResponseHook(function(req) {
            req.setHeader('x-after-hook', 'ok');
        });
    }

    // Test 5: response hook on parent location + child location both work.
    // Since /parent/ and /parent/child/ are sibling locations in nginx.conf,
    // we add the response hook to /parent/ explicitly, and set a separate
    // handler on /parent/child/.  Then verify /parent/ fires its hook
    // and /parent/child/ does NOT (because the hook was only set on /parent/).
    {
        const parent = loc('/parent/');
        parent.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'parent\n');
        };
        parent.addResponseHook(function(req) {
            req.setHeader('x-inherited', 'yes');
        });
        const child = loc('/parent/child/');
        child.handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'child\n');
        };
    }

    // Test 6: response hook fires for non-200 (404) responses
    {
        const l = loc('/not_found/');
        // No JS handler — nginx returns 404 for static file not found.
        // We still add a response hook to modify headers on 404.
        l.addResponseHook(function(req) {
            req.setHeader('x-404-hook', 'yes');
        });
    }

})();
JS

$t->try_run('no js module')->plan(13);

# Test 1: response hook adds a header
my $r1 = http_get('/add_header/');
like($r1, qr/200 OK/,           'add header hook: 200 OK');
like($r1, qr/x-hook:\s*fired/i, 'add header hook: x-hook header present');

# Test 2: response hook modifies status code
my $r2 = http_get('/modify_status/');
like($r2, qr/202/, 'modify status hook: 202 Accepted');

# Test 3: multiple hooks run in order — both headers present
my $r3 = http_get('/multi_hooks/');
like($r3, qr/x-hook-a:\s*first/i,  'multi hooks: first hook header');
like($r3, qr/x-hook-b:\s*second/i, 'multi hooks: second hook header');

# Test 4: response hook can add headers after content handler ran
my $r4 = http_get('/no_respond/');
like($r4, qr/200 OK/,                    'post-handler hook: request completes 200');
like($r4, qr/x-after-hook:\s*ok/i,       'post-handler hook: hook added header');

# Test 5: parent location has response hook; child is separate location without hook
my $r5p = http_get('/parent/');
like($r5p, qr/200 OK/,             'parent loc: 200 OK');
like($r5p, qr/x-inherited:\s*yes/i, 'parent loc: response hook fires');

my $r5c = http_get('/parent/child/');
like($r5c, qr/200 OK/,              'child loc: 200 OK');
unlike($r5c, qr/x-inherited/i,      'child loc: hook not inherited (sibling)');

# Test 6: response hook fires on 404
my $r6 = http_get('/not_found/');
like($r6, qr/404/,                '404 hook: 404 status');
like($r6, qr/x-404-hook:\s*yes/i, '404 hook: hook header present on 404');
