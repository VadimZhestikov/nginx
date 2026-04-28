#!/usr/bin/perl

# COM steady-state leak test: server.ssl wrapper + array getters.
#
# Accesses server.ssl.protocols[] and server.ssl.ciphers on every
# iteration.  protocols[] builds a new JS string array per call.
# Requires openssl to generate a self-signed test certificate.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use lib '../t/lib';
use Test::Nginx;
use ComStress;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl/)->has_daemon('openssl')->plan(3);

my $d = $t->testdir();

system('openssl req -x509 -new -days 1 -nodes '
    . "-keyout $d/server.key -out $d/server.crt "
    . '-subj "/CN=localhost" '
    . "2>$d/openssl.out") == 0
    or die "openssl req failed:\n" . `cat $d/openssl.out`;

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8443 ssl;
        server_name localhost;

        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;
        ssl_protocols       TLSv1.2 TLSv1.3;

        location /stress/ { }
        location /check/  { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name plain;

        location /stress/ { }
        location /check/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    /* SSL server is servers[0] (8443); plain server is servers[1] (8080). */
    var ssl_srv  = nginx.http.servers.find(function(s) {
        return s.ssl !== null;
    });
    var plain_srv = nginx.http.servers.find(function(s) {
        return s.ssl === null;
    });

    var locs = plain_srv.locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* Also install on SSL server locations for completeness */
    var ssl_locs = ssl_srv.locations;
    function set_ssl(path, fn) {
        var l = ssl_locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    function check_handler(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    }

    set('/check/',  check_handler);
    set_ssl('/check/', check_handler);

    set('/stress/', function(req) {
        var n      = parseInt(req.args.n) || 1000;
        var before = nginx.jsMemUsage();

        for (var i = 0; i < n; i++) {
            var ssl = ssl_srv.ssl;
            void ssl.ciphers;
            /* protocols[] builds a new string array each call */
            void ssl.protocols.length;
        }

        nginx.gc();
        var after = nginx.jsMemUsage();

        req.respond(200, {'content-type': 'text/plain'},
            'before=' + before.mallocSize +
            ' after='  + after.mallocSize  +
            ' delta='  + (after.mallocSize - before.mallocSize));
    });

    set_ssl('/stress/', function(req) {
        req.respond(200, {}, 'ssl stress not used directly');
    });
})();
JS

$t->run();

like(http_get('/check/'), qr/200 OK/, 'worker alive');

my $r = run_stress($t, '/stress/', 10000);
assert_flat($r, 10000, 'server.ssl wrapper + protocols[] getter');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
