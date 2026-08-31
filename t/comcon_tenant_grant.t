#!/usr/bin/perl

# COMCON increment A (A2.1): a granted capability + the A1 reach gate isolating
# cross-compartment. The host creates a socket, attaches an HTTP listener, and
# grants the socket into the tenant. The tenant legitimately HOLDS the socket
# (an ungated scalar read works), but sock.listener — the entry to the
# reach cycle — returns null for the tenant while the host sees the listener.
# Proves the A1 registry gate isolates by compartment, not just by holding.

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
        location / { }
    }
}
EOF

# Host: create a socket, attach + activate a listener, grant the socket.
# write_file_expand so %%PORT_8091%% is substituted.
$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);
nginx.log(6, "JSTEST host_listener=" + (sock.listener !== null ? "obj" : "null"));
JS

# Tenant: holds the granted socket, but the reach edge is gated.
$t->write_file('tenant.js', <<'JS');
report("granted_type=" + typeof granted);
report("granted_addr=" + (granted ? granted.address : "MISSING"));
report("granted_listener=" + (granted.listener === null ? "null" : "obj"));
JS

$t->try_run('no js module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST host_listener=obj/,
     'control: the host (HOST_ROOT) reaches the attached listener');
like($log, qr/js tenant: granted_type=object/,
     'grant delivered: the tenant holds the granted socket object');
like($log, qr/js tenant: granted_addr=127\.0\.0\.1:/,
     'grant is usable: an ungated scalar read (address) works for the tenant');
like($log, qr/js tenant: granted_listener=null/,
     'A1 gate isolates: sock.listener is null cross-compartment (host saw it)');
