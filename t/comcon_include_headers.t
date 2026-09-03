#!/usr/bin/perl

# COMCON CONVERGE P4: the data-in/data-out header scenario of
# comcon_tenant_headers.t, re-expressed on include + location.handler. A confined
# fragment reads request headers (data-in) and returns response headers
# (data-out); a header value carrying CR/LF is dropped by req.respond (the CRLF
# guard now lives in the general response path, not just js_tenant_handler).

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /m { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var h = comcon.include(
    "function(req){ return {" +
    "  status: 200," +
    "  headers: {" +
    "    'X-Seen-UA': (req.headers && req.headers['user-agent']) || '-'," +
    "    'X-Seen-Custom': (req.headers && req.headers['x-probe']) || '-'," +
    "    'content-type': 'text/x-mirror'," +
    "    'X-Try-Inject': 'ok\\r\\nX-Evil: pwned'" +
    "  }," +
    "  body: 'ok\\n' }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/m") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method, uri: req.uri, headers: req.headers });
            req.respond(o.status, o.headers, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(6);

my $r = http(<<'REQ');
GET /m HTTP/1.0
Host: localhost
User-Agent: probe-agent/9
X-Probe: hello

REQ

like($r, qr!Content-Type: text/x-mirror!,
     'data-out: the fragment set content-type via its return value');
like($r, qr/X-Seen-UA: probe-agent\/9/,
     'data-in: request User-Agent reached the fragment as data');
like($r, qr/X-Seen-Custom: hello/,
     'data-in: an arbitrary request header reached the fragment');
unlike($r, qr/X-Evil: pwned/,
     'CRLF injection dropped by req.respond: no smuggled header');
unlike($r, qr/X-Try-Inject:/,
     'the offending header (CR/LF in value) is dropped whole');
like($r, qr/ 200 /, 'request served 200');
