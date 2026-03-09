#!/usr/bin/perl

# Tests for Stage 16 COM expansion: additional NginxRequest getters.
#
# New properties on the request object (r):
#   host          string   — Host header without port
#   httpVersion   string   — "1.0" | "1.1" | ...
#   isInternal    boolean
#   keepalive     boolean
#   contentLength number   — -1 when absent
#   contentType   string   — "" when absent
#   startTime     number   — ms (nginx internal timer)
#   remotePort    number
#   scheme        string   — "http" | "https"
#   connection    object   — {id, requests, fd}

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

js_source %%TESTDIR%%/init_request_ext.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /check { }
    }
}
EOF

$t->write_file('init_request_ext.js', <<'JS');
(function() {
    const loc = nginx.http.servers[0].locations.find(l => l.path === "/check");
    if (loc) { loc.handler = checkRequest; }
})();

function checkRequest(r) {
    const pass = (n) => nginx.log(6, "JSTEST PASS " + n);
    const fail = (n, v) => nginx.log(6, "JSTEST FAIL " + n + ": " + String(v));
    const check = (n, ok, v) => ok ? pass(n) : fail(n, v);

    // host
    check("host_str",   typeof r.host === "string",          r.host);
    check("host_val",   r.host === "localhost",               r.host);

    // httpVersion — HTTP/1.0 request
    check("ver_str",    typeof r.httpVersion === "string",   r.httpVersion);
    check("ver_val",    r.httpVersion === "1.0",             r.httpVersion);

    // isInternal — regular requests are not internal
    check("int_bool",   typeof r.isInternal === "boolean",   r.isInternal);
    check("int_false",  r.isInternal === false,               r.isInternal);

    // keepalive — boolean
    check("ka_bool",    typeof r.keepalive === "boolean",    r.keepalive);

    // contentLength — no body → -1
    check("cl_num",     typeof r.contentLength === "number", r.contentLength);
    check("cl_neg1",    r.contentLength === -1,               r.contentLength);

    // contentType — no body → ""
    check("ct_str",     typeof r.contentType === "string",   r.contentType);
    check("ct_empty",   r.contentType === "",                 r.contentType);

    // startTime — positive number
    check("st_num",     typeof r.startTime === "number",     r.startTime);
    check("st_pos",     r.startTime > 0,                     r.startTime);

    // remotePort — positive integer
    check("rp_num",     typeof r.remotePort === "number",    r.remotePort);
    check("rp_pos",     r.remotePort > 0,                    r.remotePort);

    // scheme — plain HTTP in tests
    check("scheme_str", typeof r.scheme === "string",        r.scheme);
    check("scheme_val", r.scheme === "http",                 r.scheme);

    // connection object
    const c = r.connection;
    check("conn_obj",   typeof c === "object" && c !== null, c);
    check("conn_id",    typeof c.id === "number" && c.id > 0, c.id);
    check("conn_req",   typeof c.requests === "number" && c.requests >= 1,
                        c.requests);
    check("conn_fd",    typeof c.fd === "number" && c.fd > 0, c.fd);

    r.respond(200, {"content-type": "text/plain"}, "ok");
}
JS

$t->try_run('no js module')->plan(22);

# Use HTTP/1.0 so httpVersion == "1.0"
my $res = http("GET /check HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($res, qr/200 OK/, 'handler responds 200');

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS host_str/,   'host is string');
like($log, qr/JSTEST PASS host_val/,   'host == "localhost"');
like($log, qr/JSTEST PASS ver_str/,    'httpVersion is string');
like($log, qr/JSTEST PASS ver_val/,    'httpVersion == "1.0"');
like($log, qr/JSTEST PASS int_bool/,   'isInternal is boolean');
like($log, qr/JSTEST PASS int_false/,  'isInternal == false');
like($log, qr/JSTEST PASS ka_bool/,    'keepalive is boolean');
like($log, qr/JSTEST PASS cl_num/,     'contentLength is number');
like($log, qr/JSTEST PASS cl_neg1/,    'contentLength == -1 (no body)');
like($log, qr/JSTEST PASS ct_str/,     'contentType is string');
like($log, qr/JSTEST PASS ct_empty/,   'contentType == "" (no body)');
like($log, qr/JSTEST PASS st_num/,     'startTime is number');
like($log, qr/JSTEST PASS st_pos/,     'startTime > 0');
like($log, qr/JSTEST PASS rp_num/,     'remotePort is number');
like($log, qr/JSTEST PASS rp_pos/,     'remotePort > 0');
like($log, qr/JSTEST PASS scheme_str/, 'scheme is string');
like($log, qr/JSTEST PASS scheme_val/, 'scheme == "http"');
like($log, qr/JSTEST PASS conn_obj/,   'connection is object');
like($log, qr/JSTEST PASS conn_id/,    'connection.id > 0');
like($log, qr/JSTEST PASS conn_req/,   'connection.requests >= 1');
like($log, qr/JSTEST PASS conn_fd/,    'connection.fd > 0');
