#!/usr/bin/perl

# COMCON CONVERGE P6: audit mode on the include primitive (the observe-then-
# enforce loop of comcon_audit_mode.t). With comcon.mode('audit'), every reach
# gate LOGS the denial event and ALLOWS the operation: a confined fragment
# holding a granted socket actually receives the listener object (not null), the
# event is recorded with mode=audit, and enforcement is off. Configured entirely
# via operators (comcon.mode + a live-cap grant) — no js_tenant_* directives.

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
        location /a { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
comcon.mode('audit');

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var l = nginx.http.attach(sock);
l.addServer(nginx.http.servers[0]);

var h = comcon.include(
    "function(req){ return { status:200," +
    "  body: 'listener=' + (granted.listener === null ? 'null' : 'obj') }; }",
    { grants: { granted: sock } });

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/a") {
        locs[i].handler = function(req) {
            var o = h({});
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(2);

like(http_get('/a'), qr/listener=obj/,
     'audit mode: the reach gate log-and-ALLOWS (granted.listener is the object, not null)');
like($t->read_file('error.log'), qr/js denial: comp=1 op=sock\.listener .*mode=audit/,
     'audit mode: the reach event is recorded with mode=audit');
