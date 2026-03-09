#!/usr/bin/perl

# Tests for Stage 23 COM expansion: r.cookies
#
# r.cookies parses the Cookie request header(s) into a plain JS object.
# Pairs are split on ';', names/values trimmed of surrounding whitespace.
# Duplicate cookie names retain the last value seen.

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

js_source %%TESTDIR%%/init_cookies.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /ck { }
    }
}
EOF

$t->write_file('init_cookies.js', <<'JS');
(function() {
    const loc = nginx.http.servers[0].locations.find(l => l.path === '/ck');
    if (loc) { loc.handler = ckHandler; }
})();

function ckHandler(r) {
    const c = r.cookies;
    r.respond(200, {'content-type': 'application/json'}, JSON.stringify(c));
}
JS

$t->try_run('no js module')->plan(8);

# No Cookie header — empty object
my $res = http("GET /ck HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($res, qr/200 OK/,  'no cookie header: 200');
like($res, qr/\{\}/,    'no cookie header: empty object');

# Single cookie
my $r2 = http("GET /ck HTTP/1.0\r\nHost: localhost\r\nCookie: session=abc123\r\n\r\n");
like($r2, qr/"session":"abc123"/, 'single cookie: name=value');

# Multiple cookies in one header
my $r3 = http("GET /ck HTTP/1.0\r\nHost: localhost\r\nCookie: a=1; b=2; c=3\r\n\r\n");
like($r3, qr/"a":"1"/, 'multi-cookie: a=1');
like($r3, qr/"b":"2"/, 'multi-cookie: b=2');
like($r3, qr/"c":"3"/, 'multi-cookie: c=3');

# Cookie value containing '='  (e.g. base64)
my $r4 = http("GET /ck HTTP/1.0\r\nHost: localhost\r\nCookie: tok=aGVs\r\n\r\n");
like($r4, qr/"tok":"aGVs"/, 'cookie with base64-like value');

# Whitespace around name and value is trimmed
my $r5 = http("GET /ck HTTP/1.0\r\nHost: localhost\r\nCookie:  name = val \r\n\r\n");
like($r5, qr/"name":"val"/, 'cookie: leading/trailing spaces trimmed');
