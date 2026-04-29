#!/usr/bin/perl

# COM steady-state leak test: location.proxy sub-wrapper access.
#
# ngx_js_wrap_proxy() creates a new NginxProxy object (with fresh proto)
# on every call.  This test verifies the wrapper is freed immediately
# when JS discards the reference, with no per-iteration heap growth.
# Exercises .pass, .httpVersion, .connectTimeout, and .readTimeout.

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

my $t = Test::Nginx->new()->has(qw/http/)->plan(3);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
    }

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /proxied/ {
            proxy_pass         http://backend;
            proxy_http_version 1.1;
            proxy_connect_timeout 5s;
            proxy_read_timeout    10s;
        }

        location /stress/ { }
        location /check/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs    = nginx.http.servers[0].locations;
    var proxied = locs.find(function(l) { return l.path === '/proxied/'; });

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    set('/stress/', function(req) {
        var n      = parseInt(req.args.n) || 1000;
        var before = nginx.jsMemUsage();

        for (var i = 0; i < n; i++) {
            /* Full chain: location → proxy wrapper → scalar getters */
            void proxied.proxy.pass;
            void proxied.proxy.httpVersion;
            void proxied.proxy.connectTimeout;
            void proxied.proxy.readTimeout;
        }

        nginx.gc();
        var after = nginx.jsMemUsage();

        req.respond(200, {'content-type': 'text/plain'},
            'before=' + before.mallocSize +
            ' after='  + after.mallocSize  +
            ' delta='  + (after.mallocSize - before.mallocSize));
    });
})();
JS

$t->run();

like(http_get('/check/'), qr/200 OK/, 'worker alive');

my $d = run_stress($t, '/stress/', 10000);
assert_flat($d, 10000, 'proxy wrapper + scalar getters');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
