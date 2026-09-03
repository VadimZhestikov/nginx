#!/usr/bin/perl

# COMCON step-4 (directive retirement): configure the tenant compartment from
# the single js_source root script via host-JS operators — NO js_tenant_*
# directives in nginx.conf (the fundament: never add directives).
#
#   comcon.mode('audit')     replaces  js_tenant_mode audit;
#   comcon.tenant('t.js')    replaces  js_tenant_source t.js;
#
# Proof: the tenant runs CONFINED (typeof nginx === "undefined") — so
# comcon.tenant() loaded it into the reduced compartment — and the granted
# socket's reach edge is ALLOWED (listener != null) — so comcon.mode('audit')
# took effect (default enforce would gate it to null).

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# NOTE: only js_source — no js_tenant_source / js_tenant_mode directives.
$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location / { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);

comcon.mode('audit');                       // was: js_tenant_mode audit;
comcon.tenant('%%TESTDIR%%/tenant.js');     // was: js_tenant_source tenant.js;

nginx.log(6, "JSTEST host_listener=" + (sock.listener !== null ? "obj" : "null"));
JS

$t->write_file('tenant.js', <<'JS');
report("nginx_type=" + typeof nginx);
report("listener=" + (granted.listener === null ? "null" : "obj"));
JS

$t->try_run('no js module')->plan(3);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST host_listener=obj/,
     'control: the host (HOST_ROOT) reaches the attached listener');
like($log, qr/js tenant: nginx_type=undefined/,
     'comcon.tenant(): the tenant runs CONFINED (host authority unreachable)');
like($log, qr/js tenant: listener=obj/,
     'comcon.mode("audit"): the reach edge is log-and-allowed (not enforce-gated)');
