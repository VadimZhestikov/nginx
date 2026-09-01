#!/usr/bin/perl

# COMCON increment A (A3.0): a confined tenant serving REAL requests.
#
# The tenant's persistent runtime is created in the master (init_conf, before
# fork), COW-inherited by workers, and serves js_tenant_handler locations via
# the function registered through its granted onRequest(fn). The response is
# the handler's RETURN VALUE (data out — no request capability granted), and
# the response body itself carries the confinement proof: deny-by-default
# holds on the request path (nginx undefined) and the A1 reach gate isolates
# during a live request (a granted host socket's .listener stays null).

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

js_source %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;

        location /t    { js_tenant_handler; }
        location /host { return 200 "host-ok\n"; }
    }
}
EOF

# Host: attach + activate a listener on a real socket, grant the socket.
$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);
JS

# Tenant: registers a request handler through its granted onRequest().
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    var iso = (typeof granted === "object" && granted.listener === null)
              ? "isolated" : "LEAK";
    return { status: 200,
             body: "tenant " + req.method + " " + req.uri +
                   " nginx=" + (typeof nginx) + " " + iso + "\n" };
});
report("A3 handler registered");
JS

$t->try_run('no js module')->plan(6);

my $r1 = http_get('/t');
like($r1, qr/ 200 /, 'tenant handler serves a real request (200)');
like($r1, qr/tenant GET \/t nginx=undefined/,
     'deny-by-default holds ON THE REQUEST PATH: tenant cannot name nginx');
like($r1, qr/isolated/,
     'A1 gate isolates during a live request: granted socket .listener null');

my $r2 = http_get('/t');
like($r2, qr/tenant GET \/t nginx=undefined isolated/,
     'second request served (handler JSValue lifecycle stable across calls)');

like(http_get('/host'), qr/host-ok/,
     'host locations are unaffected by the tenant path');

like($t->read_file('error.log'), qr/js tenant: A3 handler registered/,
     'tenant compartment booted and registered its handler');
