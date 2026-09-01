#!/usr/bin/perl

# COMCON increment A (A4): audit mode — the observe-then-enforce loop.
# With `js_tenant_mode audit;` every reach gate LOGS the denial event and
# ALLOWS the operation: the tenant actually receives the listener object
# (wrapped in its own context), the event is recorded with mode=audit, and
# the counters count. An operator runs audit, reads nginx.tenantDenials()
# and the log, then flips to enforce (comcon_denial_log.t is the enforce
# half of the same loop).

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

js_tenant_mode audit;
js_source %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;

        location /probe   { js_tenant_handler; }
        location /denials { }
    }
}
EOF

$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/denials") {
        locs[i].handler = function(req) {
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(nginx.tenantDenials()));
        };
    }
}
JS

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return "listener=" + (granted.listener === null ? "null" : "obj");
});
JS

$t->try_run('no js module')->plan(4);

like(http_get('/probe'), qr/listener=obj/,
     'audit mode: the gate allows — tenant receives the listener object');

my $rep = http_get('/denials');
like($rep, qr/"mode":"audit"/, 'report: audit mode');
like($rep, qr/"sock\.listener":1/, 'report: the audited event was counted');

like($t->read_file('error.log'),
     qr/js denial: comp=1 op=sock\.listener .* mode=audit n=1/,
     'the would-be denial was logged with mode=audit');
