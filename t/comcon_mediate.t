#!/usr/bin/perl

# COMCON M-CFG: mediate(cap, interceptor) — attenuation-only membrane over a
# granted capability. For a NginxSocket the fields are address/port/fd/listener;
# the membrane is realized as a C-side field mask on the re-wrapped cap, so a
# redacted field reads as undefined inside the fragment (A(cap') ⊆ A(cap)).
#
#   allow(['port'])     -> only port readable; address hidden
#   redact(['address']) -> address hidden; port still readable
#   revoke()            -> the grant is withheld entirely (name undefined)

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
        location /med { }
    }
}
EOF

$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/med") {
        // allow: only 'port' passes the membrane
        var onlyPort = comcon.include(
            "function(){ return { port: (s.port|0), addrType: typeof s.address }; }",
            { grants: { s: comcon.mediate(sock, comcon.allow(['port'])) } });

        // redact: 'address' hidden, 'port' still visible
        var noAddr = comcon.include(
            "function(){ return { addrType: typeof s.address, port: (s.port|0) }; }",
            { grants: { s: comcon.mediate(sock, comcon.redact(['address'])) } });

        // revoke: the grant is withheld entirely
        var revoked = comcon.include(
            "function(){ return { hasS: (typeof s) }; }",
            { grants: { s: comcon.mediate(sock, comcon.revoke()) } });

        locs[i].handler = function(req) {
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify({ onlyPort: onlyPort({}),
                                         noAddr: noAddr({}),
                                         revoked: revoked({}) }));
        };
    }
}
JS

$t->try_run('no js module')->plan(5);

my $body = http_get('/med');

like($body, qr/"onlyPort":\{[^}]*"port":8091/,
     'allow: the allowed field (port) passes the membrane');
like($body, qr/"onlyPort":\{[^}]*"addrType":"undefined"/,
     'allow: a non-allowed field (address) is hidden (undefined)');
like($body, qr/"noAddr":\{[^}]*"addrType":"undefined"/,
     'redact: the redacted field (address) reads undefined');
like($body, qr/"noAddr":\{[^}]*"port":8091/,
     'redact: a non-redacted field (port) still passes');
like($body, qr/"revoked":\{"hasS":"undefined"\}/,
     'revoke: the grant is withheld entirely (name undefined in the fragment)');
