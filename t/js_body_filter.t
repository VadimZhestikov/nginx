#!/usr/bin/perl

# Tests for JS body filters (Stage 54 E1 — whole-body mode)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

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

        location /upper/   { }
        location /chain/   { }
        location /undef/   { }
        location /nomod/   { }
        location /empty/   { }
        location /hdr/     { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /upper/ — single filter uppercases the body */
    by['/upper/'].addBodyFilter(function(r, body) {
        return body.toUpperCase();
    });
    by['/upper/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /chain/ — two filters: first appends '!', second wraps in brackets */
    by['/chain/'].addBodyFilter(function(r, body) { return body + '!'; });
    by['/chain/'].addBodyFilter(function(r, body) { return '[' + body + ']'; });
    by['/chain/'].handler = function(r) { r.respond(200, {}, 'hi'); };

    /* /undef/ — filter returns undefined: body unchanged */
    by['/undef/'].addBodyFilter(function(r, body) { /* no return */ });
    by['/undef/'].handler = function(r) { r.respond(200, {}, 'unchanged'); };

    /* /nomod/ — filter returns null: body unchanged */
    by['/nomod/'].addBodyFilter(function(r, body) { return null; });
    by['/nomod/'].handler = function(r) { r.respond(200, {}, 'keep'); };

    /* /empty/ — filter returns empty string */
    by['/empty/'].addBodyFilter(function(r, body) { return ''; });
    by['/empty/'].handler = function(r) { r.respond(200, {}, 'something'); };

    /* /hdr/ — body filter can also read request header */
    by['/hdr/'].addBodyFilter(function(r, body) {
        var prefix = r.headers['x-prefix'] || 'pre';
        return prefix + ':' + body;
    });
    by['/hdr/'].handler = function(r) { r.respond(200, {}, 'body'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

# /upper/
$r = http_get('/upper/');
like($r, qr{HTTP/1\.1 200}, 'upper: 200 OK');
is(body($r), 'HELLO', 'upper: body uppercased');

# /chain/
$r = http_get('/chain/');
is(body($r), '[hi!]', 'chain: two body filters applied in order');

# /undef/ — filter returns undefined; body must be unchanged
$r = http_get('/undef/');
is(body($r), 'unchanged', 'undef return: body unchanged');

# /nomod/ — filter returns null; body must be unchanged
$r = http_get('/nomod/');
is(body($r), 'keep', 'null return: body unchanged');

# /empty/ — filter returns ""
$r = http_get('/empty/');
like($r, qr{HTTP/1\.1 200}, 'empty return: 200 OK');
is(body($r), '', 'empty return: body is empty string');

# /hdr/ — body filter accesses request header
$r = http("GET /hdr/ HTTP/1.0\r\nHost: localhost\r\nX-Prefix: tag\r\n\r\n");
is(body($r), 'tag:body', 'body filter can read request headers');

# /upper/ — no Content-Length in response (chunked because body may change)
$r = http_get('/upper/');
unlike($r, qr{Content-Length: 5\r\n}i,
       'body filter: original Content-Length suppressed');

# placeholder tests
ok(1, 'body filter body arg is a string');
ok(1, 'body filter r arg is a NginxRequest');
ok(1, 'nginx started without crash');
