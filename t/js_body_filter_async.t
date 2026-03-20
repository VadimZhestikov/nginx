#!/usr/bin/perl

# Tests for wholeBodyAsync body filter mode — async body transformation
# using nginx.setTimeout to simulate async operations.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(11);

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

        location /async_upper/  { }
        location /async_chain/  { }
        location /async_nostr/  { }
        location /async_reject/ { }
        location /async_mixed/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /async_upper/ -- single wholeBodyAsync filter uppercases after a delay */
    by['/async_upper/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body.toUpperCase();
    });
    by['/async_upper/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_chain/ -- two wholeBodyAsync filters chained: upper then append */
    by['/async_chain/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body.toUpperCase();
    });
    by['/async_chain/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body + '!';
    });
    by['/async_chain/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /async_nostr/ -- async filter that resolves with undefined (pass-through) */
    by['/async_nostr/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return undefined;
    });
    by['/async_nostr/'].handler = function(r) { r.respond(200, {}, 'original'); };

    /* /async_reject/ -- async filter that rejects; connection is closed */
    by['/async_reject/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        throw new Error('intentional rejection');
    });
    by['/async_reject/'].handler = function(r) { r.respond(200, {}, 'data'); };

    /* /async_mixed/ -- sync filter then async filter */
    by['/async_mixed/'].addBodyFilter('wholeBodySync', function(r, body) {
        return '[' + body + ']';
    });
    by['/async_mixed/'].addBodyFilter('wholeBodyAsync', async function(r, body) {
        await nginx.setTimeout(10);
        return body + '!';
    });
    by['/async_mixed/'].handler = function(r) { r.respond(200, {}, 'hello'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /async_upper/
$r = http_get('/async_upper/');
like($r, qr{HTTP/1\.1 200}, 'async_upper: 200 OK');
is(body($r), 'HELLO', 'async_upper: body uppercased after timer');

# /async_chain/
$r = http_get('/async_chain/');
like($r, qr{HTTP/1\.1 200}, 'async_chain: 200 OK');
is(body($r), 'HELLO!', 'async_chain: both async filters applied in order');

# /async_nostr/
$r = http_get('/async_nostr/');
like($r, qr{HTTP/1\.1 200}, 'async_nostr: 200 OK');
is(body($r), 'original', 'async_nostr: undefined return is pass-through');

# /async_reject/ -- filter rejects; connection is closed or response incomplete
$r = http_get('/async_reject/');
ok(!defined($r) || $r !~ /HTTP\/1\.1 200.*\r\n\r\n.+/s,
   'async_reject: body not delivered after rejection');

# /async_mixed/ -- sync then async
$r = http_get('/async_mixed/');
like($r, qr{HTTP/1\.1 200}, 'async_mixed: 200 OK');
is(body($r), '[hello]!', 'async_mixed: sync filter then async filter both applied');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts');
