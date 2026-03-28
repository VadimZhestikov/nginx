#!/usr/bin/perl

# Tests for JS-Pilgrim P15: multiple addBodyFilter calls on the same location.
# Each filter's output becomes the next filter's input.
#
# The body_filters array supports an unlimited number of entries; the runner
# threads body_val through all of them in registration order (modulo priority).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(18);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p15_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /two_gen/      { }
        location /three_gen/    { }
        location /gen_wb/       { }
        location /wb_gen/       { }
        location /async_chain/  { }
        location /priority/     { }
        location /empty_mid/    { }
    }
}
EOF

$t->write_file('p15_init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /two_gen/ — two generator filters; second sees output of first */
    by['/two_gen/'].handler = function(r) { r.respond(200, {}, 'hello'); };
    by['/two_gen/'].addBodyFilter(async function*(body, req) {
        yield body.toUpperCase();
    });
    by['/two_gen/'].addBodyFilter(async function*(body, req) {
        yield '[' + body + ']';
    });

    /* /three_gen/ — three generators chained */
    by['/three_gen/'].handler = function(r) { r.respond(200, {}, 'x'); };
    by['/three_gen/'].addBodyFilter(async function*(body, req) {
        yield 'A' + body;
    });
    by['/three_gen/'].addBodyFilter(async function*(body, req) {
        yield body + 'B';
    });
    by['/three_gen/'].addBodyFilter(async function*(body, req) {
        yield body + body;   /* doubles the string */
    });

    /* /gen_wb/ — generator then whole-body-sync */
    by['/gen_wb/'].handler = function(r) { r.respond(200, {}, 'foo'); };
    by['/gen_wb/'].addBodyFilter(async function*(body, req) {
        yield body + '!';
    });
    by['/gen_wb/'].addBodyFilter('wholeBodySync', function(req, body) {
        return body.repeat(2);
    });

    /* /wb_gen/ — whole-body-sync then generator */
    by['/wb_gen/'].handler = function(r) { r.respond(200, {}, 'bar'); };
    by['/wb_gen/'].addBodyFilter('wholeBodySync', function(req, body) {
        return body.toUpperCase();
    });
    by['/wb_gen/'].addBodyFilter(async function*(body, req) {
        yield '<' + body + '>';
    });

    /* /async_chain/ — async generator (await) followed by sync generator */
    by['/async_chain/'].handler = function(r) { r.respond(200, {}, 'val'); };
    by['/async_chain/'].addBodyFilter(async function*(body, req) {
        await nginx.setTimeout(5);
        yield 'async:' + body;
    });
    by['/async_chain/'].addBodyFilter(async function*(body, req) {
        yield body + ':done';
    });

    /* /priority/ — lower priority number runs first; default priority is 50 */
    by['/priority/'].handler = function(r) { r.respond(200, {}, 'p'); };
    by['/priority/'].addBodyFilter('generator', async function*(body, req) {
        yield body + 'Z';          /* registered first, priority 50 (default) */
    }, { name: 'second', priority: 50 });
    by['/priority/'].addBodyFilter('generator', async function*(body, req) {
        yield body + 'A';          /* registered second, but lower priority → runs first */
    }, { name: 'first', priority: 10 });

    /* /empty_mid/ — middle filter yields nothing; third filter sees empty string */
    by['/empty_mid/'].handler = function(r) { r.respond(200, {}, 'data'); };
    by['/empty_mid/'].addBodyFilter(async function*(body, req) {
        yield body;               /* pass through */
    });
    by['/empty_mid/'].addBodyFilter(async function*(body, req) {
        /* yields nothing — swallows the body */
    });
    by['/empty_mid/'].addBodyFilter(async function*(body, req) {
        yield 'empty:' + body;    /* body is '' because previous yielded nothing */
    });
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /two_gen/ — first filter uppercases, second wraps in brackets
$r = http_get('/two_gen/');
like($r, qr{HTTP/1\.1 200}, 'two_gen: 200 OK');
is(body($r), '[HELLO]', 'two_gen: second filter sees uppercased output of first');

# /three_gen/ — three generators: A+x, Ax+B, AXB+AxB
$r = http_get('/three_gen/');
like($r, qr{HTTP/1\.1 200}, 'three_gen: 200 OK');
is(body($r), 'AxBAxB', 'three_gen: three filters chained correctly');

# /gen_wb/ — generator output fed into whole-body-sync
$r = http_get('/gen_wb/');
like($r, qr{HTTP/1\.1 200}, 'gen_wb: 200 OK');
is(body($r), 'foo!foo!', 'gen_wb: WB_SYNC receives generator output');

# /wb_gen/ — whole-body-sync output fed into generator
$r = http_get('/wb_gen/');
like($r, qr{HTTP/1\.1 200}, 'wb_gen: 200 OK');
is(body($r), '<BAR>', 'wb_gen: generator receives WB_SYNC output');

# /async_chain/ — async generator followed by sync generator
$r = http_get('/async_chain/');
like($r, qr{HTTP/1\.1 200}, 'async_chain: 200 OK');
is(body($r), 'async:val:done', 'async_chain: second filter sees resolved async output');

$r = http_get('/async_chain/');
like($r, qr{HTTP/1\.1 200}, 'async_chain 2: 200 OK');
is(body($r), 'async:val:done', 'async_chain 2: repeatable');

# /priority/ — lower priority number runs first
$r = http_get('/priority/');
like($r, qr{HTTP/1\.1 200}, 'priority: 200 OK');
is(body($r), 'pAZ', 'priority: filter with priority 10 runs before priority 50');

# /empty_mid/ — middle filter yields nothing → third filter sees ''
$r = http_get('/empty_mid/');
like($r, qr{HTTP/1\.1 200}, 'empty_mid: 200 OK');
is(body($r), 'empty:', 'empty_mid: third filter receives empty string from second');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
