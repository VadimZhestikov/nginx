#!/usr/bin/perl

# Tests for mixed Mode B: a location with both wholeBody* and streaming*
# filters.  When any wholeBody* filter is present the body is accumulated
# first; streaming* filters in that list receive the full accumulated body
# as a single chunk (flags.last = true) and are dispatched in registration
# order alongside the wholeBody* filters.

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /wb_then_stream/ { }
        location /stream_then_wb/ { }
        location /wb_stream_wb/   { }
        location /stream_drop/    { }
        location /async_wb_stream/{ }
        location /async_stream_wb/{ }
        location /stream_then_async_wb/{ }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* wholeBodySync then streamingSync — WB uppercases, streaming brackets */
    by['/wb_then_stream/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body.toUpperCase();
    });
    by['/wb_then_stream/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/wb_then_stream/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* streamingSync then wholeBodySync — streaming brackets, WB uppercases */
    by['/stream_then_wb/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/stream_then_wb/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body.toUpperCase();
    });
    by['/stream_then_wb/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* wholeBodySync, streamingSync, wholeBodySync — three interleaved */
    by['/wb_stream_wb/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body.toUpperCase();
    });
    by['/wb_stream_wb/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/wb_stream_wb/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body + '!';
    });
    by['/wb_stream_wb/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* streamingSync with no sendBuffer → drop (body becomes empty) */
    by['/stream_drop/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body.toUpperCase();
    });
    by['/stream_drop/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        /* no sendBuffer: drop */
    });
    by['/stream_drop/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* wholeBodyAsync then streamingSync */
    by['/async_wb_stream/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body.toUpperCase();
    });
    by['/async_wb_stream/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/async_wb_stream/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* streamingAsync then wholeBodySync */
    by['/async_stream_wb/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return chunk.toUpperCase();
    });
    by['/async_stream_wb/'].addBodyFilter('wholeBodySync', function(r, body) {
        return '[' + body + ']';
    });
    by['/async_stream_wb/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* streamingSync then wholeBodyAsync */
    by['/stream_then_async_wb/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('<' + chunk + '>');
    });
    by['/stream_then_async_wb/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body.toUpperCase();
    });
    by['/stream_then_async_wb/'].handler = function(r) { r.respond(200, {}, 'hello'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# wb then stream: 'hello' → WB→'HELLO' → stream→'[HELLO]'
$r = http_get('/wb_then_stream/');
like($r, qr{HTTP/1\.1 200}, 'wb_then_stream: 200 OK');
is(body($r), '[HELLO]', 'wb_then_stream: WB then streaming in order');

# stream then wb: 'hello' → stream→'[hello]' → WB→'[HELLO]'
$r = http_get('/stream_then_wb/');
like($r, qr{HTTP/1\.1 200}, 'stream_then_wb: 200 OK');
is(body($r), '[HELLO]', 'stream_then_wb: streaming then WB in order');

# wb_stream_wb: 'hello' → WB→'HELLO' → stream→'[HELLO]' → WB→'[HELLO]!'
$r = http_get('/wb_stream_wb/');
like($r, qr{HTTP/1\.1 200}, 'wb_stream_wb: 200 OK');
is(body($r), '[HELLO]!', 'wb_stream_wb: three interleaved filters');

# stream_drop: WB uppercases, streaming drops
$r = http_get('/stream_drop/');
like($r, qr{HTTP/1\.1 200}, 'stream_drop: 200 OK');
is(body($r), '', 'stream_drop: streaming filter with no sendBuffer drops body');

# async_wb_stream: WB async uppercase, then streaming brackets
$r = http_get('/async_wb_stream/');
like($r, qr{HTTP/1\.1 200}, 'async_wb_stream: 200 OK');
is(body($r), '[HELLO]', 'async_wb_stream: async WB then streaming filter');

# async_stream_wb: streaming async uppercase, then WB brackets
$r = http_get('/async_stream_wb/');
like($r, qr{HTTP/1\.1 200}, 'async_stream_wb: 200 OK');
is(body($r), '[HELLO]', 'async_stream_wb: async streaming then WB filter');

# stream_then_async_wb: streaming angle-brackets, then WB async uppercase
$r = http_get('/stream_then_async_wb/');
like($r, qr{HTTP/1\.1 200}, 'stream_then_async_wb: 200 OK');
is(body($r), '<HELLO>', 'stream_then_async_wb: streaming then async WB filter');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts');
