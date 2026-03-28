#!/usr/bin/perl

# Tests for JS-Pilgrim P5: addBodyFilter with async generator functions.
# Tests single-arg auto-detect form and explicit 'generator' mode string.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(14);

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

        location /passthru/       { }
        location /multi_yield/    { }
        location /req_method/     { }
        location /async_yield/    { }
        location /empty_yield/    { }
        location /explicit_mode/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /passthru/ -- generator yields the body unchanged (auto-detect form) */
    by['/passthru/'].addBodyFilter(async function*(body, req) {
        yield body;
    });
    by['/passthru/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /multi_yield/ -- generator yields multiple chunks (auto-detect form) */
    by['/multi_yield/'].addBodyFilter(async function*(body, req) {
        yield 'prefix:';
        yield body;
        yield ':suffix';
    });
    by['/multi_yield/'].handler = function(r) { r.respond(200, {}, 'world'); };

    /* /req_method/ -- generator uses req.method (auto-detect form) */
    by['/req_method/'].addBodyFilter(async function*(body, req) {
        yield req.method + ':';
        yield body;
    });
    by['/req_method/'].handler = function(r) { r.respond(200, {}, 'data'); };

    /* /async_yield/ -- generator with await (auto-detect form) */
    by['/async_yield/'].addBodyFilter(async function*(body, req) {
        await nginx.setTimeout(10);
        yield 'async:';
        yield body;
    });
    by['/async_yield/'].handler = function(r) { r.respond(200, {}, 'value'); };

    /* /empty_yield/ -- generator yields nothing → empty body (auto-detect) */
    by['/empty_yield/'].addBodyFilter(async function*(body, req) {
        // yields nothing
    });
    by['/empty_yield/'].handler = function(r) { r.respond(200, {}, 'gone'); };

    /* /explicit_mode/ -- explicit 'generator' mode string */
    by['/explicit_mode/'].addBodyFilter('generator', async function*(body, req) {
        yield body;
        yield '\n';
    });
    by['/explicit_mode/'].handler = function(r) { r.respond(200, {}, 'line'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /passthru/ -- body unchanged
$r = http_get('/passthru/');
like($r, qr{HTTP/1\.1 200}, 'passthru: 200 OK');
is(body($r), 'hello', 'passthru: body passes through unchanged');

# /multi_yield/ -- multiple yielded chunks are concatenated
$r = http_get('/multi_yield/');
like($r, qr{HTTP/1\.1 200}, 'multi_yield: 200 OK');
is(body($r), 'prefix:world:suffix', 'multi_yield: chunks concatenated in order');

# /req_method/ -- generator can access request object
$r = http_get('/req_method/');
like($r, qr{HTTP/1\.1 200}, 'req_method: 200 OK');
is(body($r), 'GET:data', 'req_method: req.method accessible in generator');

# /async_yield/ -- generator with await works via async suspension
$r = http_get('/async_yield/');
like($r, qr{HTTP/1\.1 200}, 'async_yield: 200 OK');
is(body($r), 'async:value', 'async_yield: await inside generator works');

# /empty_yield/ -- generator that yields nothing produces empty body
$r = http_get('/empty_yield/');
like($r, qr{HTTP/1\.1 200}, 'empty_yield: 200 OK');
is(body($r), '', 'empty_yield: no yield produces empty body');

# /explicit_mode/ -- explicit 'generator' mode string works
$r = http_get('/explicit_mode/');
like($r, qr{HTTP/1\.1 200}, 'explicit_mode: 200 OK');
is(body($r), "line\n", 'explicit_mode: explicit generator mode appends newline');

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts');
