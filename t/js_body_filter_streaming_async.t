#!/usr/bin/perl

# Tests for streamingAsync body filter mode — per-chunk async transformation.
# The filter receives (r, chunk, flags) and returns a Promise:
#   - resolve with a string → that string becomes the output chunk
#   - resolve with '' (empty string) → chunk is dropped
#   - resolve with undefined (no return) → pass-through (keep original chunk)

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /async_upper/   { }
        location /async_drop/    { }
        location /async_chain/   { }
        location /async_reject/  { }
        location /async_mixed/   { }
        location /async_nostr/   { }
        location /async_passthru/{ }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /async_upper/ -- resolve with uppercased string */
    by['/async_upper/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return chunk.toUpperCase();
    });
    by['/async_upper/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_drop/ -- resolve with '' drops the chunk */
    by['/async_drop/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return '';
    });
    by['/async_drop/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_chain/ -- sync brackets then async uppercase (composing) */
    by['/async_chain/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/async_chain/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return chunk.toUpperCase();
    });
    by['/async_chain/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_reject/ -- filter rejects; connection is closed */
    by['/async_reject/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        throw new Error('intentional rejection');
    });
    by['/async_reject/'].handler = function(r) { r.respond(200, {}, 'data'); };

    /* /async_mixed/ -- async uppercase then sync brackets (composing) */
    by['/async_mixed/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return chunk.toUpperCase();
    });
    by['/async_mixed/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/async_mixed/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_nostr/ -- no return (undefined) means pass-through */
    by['/async_nostr/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        /* return undefined — pass-through */
    });
    by['/async_nostr/'].handler = function(r) { r.respond(200, {}, 'original'); };

    /* /async_passthru/ -- two chained async filters */
    by['/async_passthru/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return chunk.toUpperCase();
    });
    by['/async_passthru/'].addBodyFilter('streamingAsync', async function(r, chunk, flags) {
        await nginx.setTimeout(10);
        return '<' + chunk + '>';
    });
    by['/async_passthru/'].handler = function(r) { r.respond(200, {}, 'hello'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /async_upper/
$r = http_get('/async_upper/');
like($r, qr{HTTP/1\.1 200}, 'async_upper: 200 OK');
is(body($r), 'HELLO', 'async_upper: chunk uppercased via resolved string');

# /async_drop/
$r = http_get('/async_drop/');
like($r, qr{HTTP/1\.1 200}, 'async_drop: 200 OK');
is(body($r), '', 'async_drop: empty string resolve drops chunk');

# /async_chain/ -- sync first, then async (composing)
$r = http_get('/async_chain/');
like($r, qr{HTTP/1\.1 200}, 'async_chain: 200 OK');
is(body($r), '[HELLO]', 'async_chain: sync then async filters composing');

# /async_reject/
$r = http_get('/async_reject/');
ok(!defined($r) || $r !~ /HTTP\/1\.1 200.*\r\n\r\n.+/s,
   'async_reject: body not delivered after rejection');

# /async_mixed/ -- async first, then sync (composing)
$r = http_get('/async_mixed/');
like($r, qr{HTTP/1\.1 200}, 'async_mixed: 200 OK');
is(body($r), '[HELLO]', 'async_mixed: async then sync filters composing');

# /async_nostr/ -- undefined resolve = pass-through
$r = http_get('/async_nostr/');
like($r, qr{HTTP/1\.1 200}, 'async_nostr: 200 OK');
is(body($r), 'original', 'async_nostr: undefined resolve is pass-through');

# /async_passthru/ -- two chained async filters
$r = http_get('/async_passthru/');
like($r, qr{HTTP/1\.1 200}, 'async_passthru: 200 OK');
is(body($r), '<HELLO>', 'async_passthru: two async filters chain correctly');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts');
