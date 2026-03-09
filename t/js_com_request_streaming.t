#!/usr/bin/perl

# Tests for Stage 31 COM expansion: r.writeHead() + r.write() + r.finish()
#
# Streaming response API:
#   r.writeHead(status[, headers]) — send status + headers (no Content-Length)
#   r.write(chunk)                 — send body chunk
#   r.finish()                     — close response (last_buf)

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

js_include %%TESTDIR%%/init_streaming.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /chunks        { }
        location /write_head    { }
        location /auto_head     { }
        location /finish_only   { }
        location /double_finish { }
    }
}
EOF

$t->write_file('init_streaming.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/chunks',        chunksHandler);
    set('/write_head',    writeHeadHandler);
    set('/auto_head',     autoHeadHandler);
    set('/finish_only',   finishOnlyHandler);
    set('/double_finish', doubleFinishHandler);
})();

/* write three chunks then finish */
function chunksHandler(r) {
    r.writeHead(200, {'content-type': 'text/plain'});
    r.write('chunk1');
    r.write('chunk2');
    r.write('chunk3');
    r.finish();
}

/* writeHead with custom status and header */
function writeHeadHandler(r) {
    r.writeHead(201, {
        'content-type': 'text/plain',
        'x-custom': 'hello'
    });
    r.write('created');
    r.finish();
}

/* write() without writeHead() — auto 200 */
function autoHeadHandler(r) {
    r.write('auto');
    r.finish();
}

/* finish() without write() — empty 200 body */
function finishOnlyHandler(r) {
    r.writeHead(204, {});
    r.finish();
}

/* double finish() throws but first response is already sent */
function doubleFinishHandler(r) {
    r.write('ok');
    r.finish();
    try {
        r.finish();   /* should throw */
    } catch (e) {
        /* silently ignore — first finish already sent headers+body */
    }
}
JS

$t->try_run('no js module')->plan(9);

# three chunks concatenated in the response body
like(http_get('/chunks'), qr/chunk1chunk2chunk3/, 'write: chunks concatenated');

# status from writeHead
like(http_get('/chunks'), qr|200 OK|, 'writeHead: 200 status');

# custom status from writeHead
like(http_get('/write_head'), qr|201|, 'writeHead: custom 201 status');

# custom header from writeHead
like(http_get('/write_head'), qr|x-custom: hello|i, 'writeHead: custom header');

# body from writeHead handler
like(http_get('/write_head'), qr|created|, 'write: body after writeHead(201)');

# auto 200 when write() called without writeHead()
like(http_get('/auto_head'), qr|200 OK|, 'write: auto 200 headers');
like(http_get('/auto_head'), qr|auto|,   'write: body with auto headers');

# finish() with no body — 204
like(http_get('/finish_only'), qr|204|, 'finish: 204 no content');

# double finish throws but doesn't crash
like(http_get('/double_finish'), qr|200 OK|, 'finish: double finish safe');
