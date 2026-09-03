#!/usr/bin/perl

# COMCON CONVERGE P4: the SR-1 regression probes of comcon_sr1_regression.t,
# re-expressed on include + location.handler.
#
#  HIGH-1  a reach attempt hidden in a RETURN-VALUE GETTER must be gated. In the
#          include path the result is JSON-materialized in C AFTER the fragment
#          returns; that materialization must run UNDER the tenant compartment,
#          else the getter fires as HOST_ROOT and bypasses the A1 gate. (Fixed:
#          compartment_leave now happens after JS_JSONStringify.)
#  MEDIUM-4 mutating a socket it does not own (granted.close()) must throw.
#  MEDIUM-2 a tenant-set content-length must not smuggle framing.

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
        location /probe { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var l = nginx.http.attach(sock);
l.addServer(nginx.http.servers[0]);

var h = comcon.include(
    "function(req){" +
    "  if (req.uri === '/probe/escape') {" +
    "    return { get status(){ return (granted.listener === null) ? 200 : 599; }," +
    "             body: 'escape-probe' }; }" +
    "  if (req.uri === '/probe/close') {" +
    "    try { granted.close(); return { status:200, body:'CLOSED' }; }" +
    "    catch (e) { return { status:200, body:'denied' }; } }" +
    "  if (req.uri === '/probe/framing') {" +
    "    return { status:200, headers:{ 'content-length':'999', 'x-ok':'1' }," +
    "             body:'hi' }; }" +
    "  return { status:200, body:'ok' }; }",
    { grants: { granted: sock } });

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/probe") {
        locs[i].handler = function(req) {
            var o = h({ uri: req.uri });
            req.respond(o.status, o.headers || {}, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(6);

# HIGH-1: reach from a return-value getter is gated (materialized under TENANT)
like(http_get('/probe/escape'), qr/ 200 /,
     'HIGH-1: reach from a return-value getter is denied (materialized under TENANT)');
unlike(http_get('/probe/escape'), qr/ 599 /,
     'HIGH-1: the getter did NOT obtain the listener (no HOST_ROOT leak on marshal)');

# MEDIUM-4: close() on a socket it does not own is denied; socket survives
like(http_get('/probe/close'), qr/denied/,
     'MEDIUM-4: granted socket close() denied (not owner)');
like(http_get('/probe/other'), qr/ 200 /,
     'the host socket survived the close attempt (still serving)');

# MEDIUM-2: a valid header passes; a tenant-set content-length does not smuggle
my $f = http_get('/probe/framing');
like($f, qr/x-ok: 1/i, 'MEDIUM-2: a valid fragment-set header still passes');
unlike($f, qr/Content-Length: 999/i,
     'MEDIUM-2: the fragment-set content-length is not smuggled into framing');
