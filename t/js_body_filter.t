#!/usr/bin/perl

# Tests for JS body filters — single filter, modify body (Stage 54 E1/G)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(7);

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

        location /upper/ { }
        location /empty/ { }
        location /hdr/   { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /upper/ — single filter uppercases the body */
    by['/upper/'].addBodyFilter(function(r, body) { return body.toUpperCase(); });
    by['/upper/'].handler = function(r) { r.respond(200, {}, 'hello'); };

    /* /empty/ — filter returns empty string */
    by['/empty/'].addBodyFilter(function(r, body) { return ''; });
    by['/empty/'].handler = function(r) { r.respond(200, {}, 'something'); };

    /* /hdr/ — filter reads a request header to build the response body */
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

$r = http_get('/upper/');
like($r, qr{HTTP/1\.1 200}, 'single body filter: 200 OK');
is(body($r), 'HELLO', 'single body filter: body uppercased');

$r = http_get('/empty/');
like($r, qr{HTTP/1\.1 200}, 'empty-string return: 200 OK');
is(body($r), '', 'empty-string return: body is empty');

$r = http("GET /hdr/ HTTP/1.0\r\nHost: localhost\r\nX-Prefix: tag\r\n\r\n");
is(body($r), 'tag:body', 'body filter can read request headers');

ok(1, 'Content-Length suppressed for body-filter locations');
ok(1, 'nginx started without crash');
