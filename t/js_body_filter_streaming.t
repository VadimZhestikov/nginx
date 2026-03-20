#!/usr/bin/perl

# Tests for streamingSync body filter mode:
#   - addBodyFilter('streamingSync', fn(req, chunk, flags))
#   - req.sendBuffer(data) emits output for the current chunk
#   - no sendBuffer call = chunk is silently dropped
#   - req.sendBuffer() in a wholeBodySync context logs a warning (no-op)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(10);

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

        location /upper/      { }
        location /drop/       { }
        location /chain/      { }
        location /wb_warn/    { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /upper/ -- streamingSync filter uppercases and re-emits */
    by['/upper/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer(chunk.toUpperCase());
    });
    by['/upper/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /drop/ -- streamingSync filter that never calls sendBuffer; chunk dropped */
    by['/drop/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        /* intentionally no sendBuffer: chunk is dropped */
    });
    by['/drop/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /chain/ -- two streamingSync filters chained */
    by['/chain/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer('[' + chunk + ']');
    });
    by['/chain/'].addBodyFilter('streamingSync', function(r, chunk, flags) {
        r.sendBuffer(chunk.toUpperCase());
    });
    by['/chain/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /wb_warn/ -- req.sendBuffer inside a wholeBodySync context: no-op + warn */
    by['/wb_warn/'].addBodyFilter('wholeBodySync', function(r, body) {
        r.sendBuffer('IGNORED');
        return body.toUpperCase();
    });
    by['/wb_warn/'].handler = function(r) { r.respond(200, {}, 'hello'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /upper/
$r = http_get('/upper/');
like($r, qr{HTTP/1\.1 200}, 'streaming upper: 200 OK');
is(body($r), 'HELLO', 'streaming upper: chunk uppercased via sendBuffer');

# /drop/
$r = http_get('/drop/');
like($r, qr{HTTP/1\.1 200}, 'streaming drop: 200 OK');
is(body($r), '', 'streaming drop: no sendBuffer = chunk dropped');

# /chain/
$r = http_get('/chain/');
like($r, qr{HTTP/1\.1 200}, 'streaming chain: 200 OK');
is(body($r), '[HELLO]', 'streaming chain: both filters applied in order');

# /wb_warn/ -- sendBuffer inside wholeBody is no-op; filter still transforms body
$r = http_get('/wb_warn/');
like($r, qr{HTTP/1\.1 200}, 'wb_warn: 200 OK');
is(body($r), 'HELLO', 'wb_warn: wholeBodySync filter result used, sendBuffer ignored');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts');
