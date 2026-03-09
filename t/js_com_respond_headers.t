#!/usr/bin/perl

# Tests for Stage 24 COM expansion: r.respond() arbitrary response headers.
#
# r.respond(status, headers, body) routes every key in the headers object
# to r->headers_out.  "content-type" sets headers_out.content_type directly;
# all other keys go via ngx_list_push into headers_out.headers so that
# nginx's header filter sends them verbatim.

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

js_include %%TESTDIR%%/init_respond_headers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /cache   { }
        location /multi   { }
        location /redir   { }
        location /ct      { }
    }
}
EOF

$t->write_file('init_respond_headers.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/cache', cacheHandler);
    set('/multi', multiHandler);
    set('/redir', redirHandler);
    set('/ct',    ctHandler);
})();

function cacheHandler(r) {
    r.respond(200,
        { 'content-type': 'text/plain', 'cache-control': 'no-store' },
        'cache');
}

function multiHandler(r) {
    r.respond(200,
        {
            'content-type':  'text/plain',
            'x-foo':         'foo-value',
            'x-bar':         'bar-value',
            'x-baz':         'baz-value'
        },
        'multi');
}

function redirHandler(r) {
    r.respond(302,
        { 'location': 'https://example.com/target' },
        '');
}

function ctHandler(r) {
    r.respond(200,
        { 'content-type': 'application/json; charset=utf-8' },
        '{"ok":true}');
}
JS

$t->try_run('no js module')->plan(10);

# Cache-Control header
my $r1 = http_get('/cache');
like($r1, qr/200 OK/,                     'cache: status 200');
like($r1, qr/Cache-Control: no-store/i,   'cache: Cache-Control: no-store');

# Multiple custom X- headers in one respond() call
my $r2 = http_get('/multi');
like($r2, qr/200 OK/,          'multi: status 200');
like($r2, qr/X-Foo: foo-value/i, 'multi: X-Foo header');
like($r2, qr/X-Bar: bar-value/i, 'multi: X-Bar header');
like($r2, qr/X-Baz: baz-value/i, 'multi: X-Baz header');

# Location header in a redirect
my $r3 = http_get('/redir');
like($r3, qr/302/,                               'redir: status 302');
like($r3, qr{Location: https://example\.com/target}i, 'redir: Location header');

# Content-Type value is preserved (special path)
my $r4 = http_get('/ct');
like($r4, qr/200 OK/,                                   'ct: status 200');
like($r4, qr{Content-Type: application/json; charset=utf-8}i, 'ct: content-type preserved');
