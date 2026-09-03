#!/usr/bin/perl

# COMCON M-CFG: live-cap grants for comcon.include(source, contract).
#
# A fragment is data-in/data-out by default; contract.grants lets the host hand
# it a LIVE capability (a NginxSocket) that the fragment holds directly. The
# grant is re-wrapped compartment-native (a fresh wrapper around the same C
# handle) and injected as a closure binding of the grant name — no live object
# crosses the realm, mirroring the tenant grant mechanism. The A1 reach gate
# still isolates: the fragment HOLDS the socket and can read an ungated scalar
# (address), but the reach edge (sock.listener) is null cross-compartment, and
# host authority (nginx) remains structurally unreachable.

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

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /inc { }
    }
}
EOF

# Host: create + attach a socket, grant it live into an include fragment.
$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.log(6, "JSTEST host_listener=" + (sock.listener !== null ? "obj" : "null"));

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/inc") {
        var probe = comcon.include(
            "function(){" +
            " var planted = false;" +
            " try { Object.getPrototypeOf(granted).__evil = 1;" +
            "       planted = (granted.__evil === 1); } catch (e) {}" +
            " return {" +
            "  nginx: typeof nginx," +
            "  held: typeof granted," +
            "  addr: (granted ? granted.address : 'MISSING')," +
            "  reach: (granted.listener === null ? 'null' : 'obj')," +
            "  planted: planted" +
            " }; }",
            { grants: { granted: sock } });

        locs[i].handler = function(req) {
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(probe({})));
        };
    }
}
JS

$t->try_run('no js module')->plan(5);

my $body = http_get('/inc');

like($body, qr/"nginx":"undefined"/,
     'AUTHORITY: host authority (nginx) stays unreachable in the fragment');
like($body, qr/"held":"object"/,
     'grant delivered: the fragment holds the live granted socket');
like($body, qr/"addr":"127\.0\.0\.1:/,
     'grant usable: an ungated scalar read (address) works in the fragment');
like($body, qr/"reach":"null"/,
     'A1 gate isolates: sock.listener is null cross-compartment');
like($body, qr/"planted":false/,
     'M-SES-1b: the cap prototype is non-extensible (no cross-fragment pollution)');
