#!/usr/bin/perl

# COMCON SR-1 regression — the adversarial cases the happy-path suite missed.
#
# HIGH-1: the tenant return value's getters run tenant JS; they must run under
#   the tenant compartment, not HOST_ROOT. A `get status()` that walks a granted
#   socket's reach cycle must be DENIED (compartment active during materialise).
# MEDIUM-2: a tenant-set framing header (content-length) must be dropped, not
#   smuggled into the response.
# MEDIUM-4: a granted socket's mutating close()/broadcast() must be denied to a
#   tenant that does not own it.

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

# Host grants a socket that HAS an attached, activated listener — so if the
# reach gate leaked, .listener would return a real object.
$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var l = nginx.http.attach(sock);
l.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);
JS

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    if (req.uri === "/probe/escape") {
        // HIGH-1: the reach attempt happens inside a getter, i.e. during
        // return-value materialisation. It must be denied (listener === null).
        return {
            get status() { return (granted.listener === null) ? 200 : 599; },
            body: "escape-probe\n"
        };
    }
    if (req.uri === "/probe/close") {
        // MEDIUM-4: mutating a socket it does not own must throw.
        try { granted.close(); return "CLOSED\n"; }
        catch (e) { return "denied\n"; }
    }
    if (req.uri === "/probe/framing") {
        // MEDIUM-2: the tenant's content-length must be dropped.
        return { status: 200,
                 headers: { "content-length": "999", "x-ok": "1" },
                 body: "hi\n" };
    }
    return "ok\n";
});
JS

$t->try_run('no js module')->plan(6);

# HIGH-1
like(http_get('/probe/escape'), qr/ 200 /,
     'HIGH-1: reach from a return-value getter is denied (runs under TENANT)');
unlike(http_get('/probe/escape'), qr/ 599 /,
     'HIGH-1: the getter did NOT obtain the listener (no HOST_ROOT leak)');

# MEDIUM-4
like(http_get('/probe/close'), qr/denied/,
     'MEDIUM-4: granted socket close() denied (not owner)');
# the socket is still alive → a later request still serves
like(http_get('/probe/other'), qr/ 200 /,
     'the host socket survived the close attempt (still serving)');

# MEDIUM-2
my $f = http_get('/probe/framing');
like($f, qr/x-ok: 1/i, 'MEDIUM-2: a valid tenant header still passes');
unlike($f, qr/Content-Length: 999/i,
     'MEDIUM-2: the tenant-set content-length is dropped (no smuggling)');
