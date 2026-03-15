#!/usr/bin/perl

# Smoke-test for the Phase-1 snapshot infrastructure:
#
#   * ngx_js_req_ctx_t is allocated per-request in r->pool.
#   * w->current_request is bracketed around every JS call site.
#   * ngx_js_body_done restores ngx_js_req_ctx_t after body reading.
#
# These are internal C-level invariants that cannot be tested directly
# from JS.  Instead this file verifies that all three code paths
# (sync handler, async-sleep handler, async-readBody handler) still
# produce correct responses after the infrastructure was introduced.
#
# If any of the allocations or pointer restorations were broken the
# worker would crash / return 500 rather than 200.

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

js_source %%TESTDIR%%/snap_handlers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sync        { }
        location /async_sleep { }
        location /async_body  { }
    }
}
EOF

$t->write_file('snap_handlers.js', <<'JS');
(function installHandlers() {
    const locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    // Synchronous handler
    set('/sync', function syncHandler(r) {
        r.respond(200, {}, "sync ok");
    });

    // Async handler using r.sleep()
    set('/async_sleep', async function asyncSleepHandler(r) {
        await r.sleep(1);
        r.respond(200, {}, "sleep ok");
    });

    // Async handler using r.readBody()
    set('/async_body', async function asyncBodyHandler(r) {
        const body = await r.readBody();
        r.respond(200, {}, "body:" + body);
    });
}());
JS

$t->try_run('no js module')->plan(4);

like(http_get('/sync'),        qr/200.*sync ok/s,  'sync handler works');
like(http_get('/async_sleep'), qr/200.*sleep ok/s, 'async sleep handler works');

my $body_resp = http(
    "POST /async_body HTTP/1.0\r\n" .
    "Host: localhost\r\n" .
    "Content-Length: 5\r\n" .
    "\r\nhello"
);
like($body_resp, qr/200/,        'async readBody returns 200');
like($body_resp, qr/body:hello/, 'async readBody echoes body');
