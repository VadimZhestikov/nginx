#!/usr/bin/perl

# COMCON increment A (A3.1): the confined request path carries headers as
# DATA — request headers in (a copy on req.headers), response headers out via
# the return value's .headers — while staying zero-capability. The data-out
# side is the classic header-injection surface (showcase 4), so CRLF in a
# tenant-chosen header value is dropped, not smuggled. This is the shape the
# dogfood mirror policy needs (js_com_demos/COMCON_dogfood).

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

js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /m { js_tenant_handler; }
    }
}
EOF

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return {
        status: 200,
        headers: {
            "X-Seen-UA": (req.headers && req.headers["user-agent"]) || "-",
            "X-Seen-Custom": (req.headers && req.headers["x-probe"]) || "-",
            "content-type": "text/x-mirror",
            "X-Try-Inject": "ok\r\nX-Evil: pwned"
        },
        body: "ok\n"
    };
});
JS

$t->try_run('no js module')->plan(6);

my $r = http(<<'REQ');
GET /m HTTP/1.0
Host: localhost
User-Agent: probe-agent/9
X-Probe: hello

REQ

like($r, qr!Content-Type: text/x-mirror!,
     'A3.1 data-out: tenant set the content-type via return value');
like($r, qr/X-Seen-UA: probe-agent\/9/,
     'A3.1 data-in: request User-Agent reached the tenant as data');
like($r, qr/X-Seen-Custom: hello/,
     'A3.1 data-in: an arbitrary request header reached the tenant');
unlike($r, qr/X-Evil: pwned/,
     'CRLF injection dropped: no smuggled header in the response');
unlike($r, qr/X-Try-Inject:/,
     'the offending header (CRLF in value) is dropped whole, not kept');
like($r, qr/ 200 /, 'request served 200');
