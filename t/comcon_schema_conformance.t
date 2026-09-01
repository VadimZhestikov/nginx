#!/usr/bin/perl

# COMCON increment C (C2): the tenant-environment TYPED SCHEMA is grounded —
# it matches the running surface. A tenant probes each schema entry (env
# names, the Request shape, the granted Socket, the numeric discipline) and
# reports conformance; the response itself exercises the Response type. If the
# real surface ever drifts from js_comcon/schema/tenant-env.schema.json, this
# fails (the describe-superset-of-reality discipline, applied to the schema).

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $schema = "../js_comcon/schema/tenant-env.schema.json";

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source        %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /probe { js_tenant_handler; }
    }
}
EOF

$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var l = nginx.http.attach(sock);
l.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);
JS

# Probe every schema entry against the actual surface.
$t->write_file('tenant.js', <<'JS');
report("onRequest:" + typeof onRequest);   // env.onRequest = function
onRequest(function(req) {
    function ck(n, ok) { return n + "=" + (ok ? "OK" : "FAIL"); }
    var r = [];
    // env
    r.push(ck("report_fn",        typeof report === "function"));
    // Request type
    r.push(ck("req_method_str",   typeof req.method === "string"));
    r.push(ck("req_uri_str",      typeof req.uri === "string"));
    r.push(ck("req_args_str",     typeof req.args === "string"));
    r.push(ck("req_headers_obj",  req.headers !== null && typeof req.headers === "object"));
    r.push(ck("req_hdr_val_str",  typeof (req.headers["host"] || "") === "string"));
    // Socket grant type + numeric discipline (int = safe integer)
    r.push(ck("socket_obj",       typeof granted === "object"));
    r.push(ck("socket_addr_str",  typeof granted.address === "string"));
    r.push(ck("socket_port_int",  Number.isSafeInteger(granted.port)));
    r.push(ck("socket_fd_int",    Number.isSafeInteger(granted.fd)));
    r.push(ck("socket_listener_gated", granted.listener === null));
    // Response = {status:int, body:string} — exercised by returning it
    return { status: 200, body: r.join("\n") + "\n" };
});
JS

$t->try_run('no js module')->plan(14);

ok(-f $schema, "schema file present (js_comcon/schema/tenant-env.schema.json)");

my $r = http_get('/probe');
like($r, qr/ 200 /,                       'Response {status:int, body:string} works');
like($r, qr/report_fn=OK/,                'env.report is a function');
like($r, qr/req_method_str=OK/,           'Request.method : string');
like($r, qr/req_uri_str=OK/,              'Request.uri : string');
like($r, qr/req_args_str=OK/,             'Request.args : string');
like($r, qr/req_headers_obj=OK/,          'Request.headers : record');
like($r, qr/req_hdr_val_str=OK/,          'Request.headers value : string');
like($r, qr/socket_obj=OK/,               'Socket grant : object');
like($r, qr/socket_addr_str=OK/,          'Socket.address : string');
like($r, qr/socket_port_int=OK/,          'Socket.port : int (safe integer — V1 numeric rule)');
like($r, qr/socket_fd_int=OK/,            'Socket.fd : int');
like($r, qr/socket_listener_gated=OK/,    'Socket.listener : gated null cross-compartment');
like($t->read_file('error.log'), qr/js tenant: onRequest:function/,
     'env.onRequest is a function');
