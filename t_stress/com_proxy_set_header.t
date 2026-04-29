#!/usr/bin/perl

# COM steady-state leak test: proxy.setHeader[] array getter.
#
# ngx_js_proxy_get_set_header() allocates a new JS Array + one plain
# object per header on every call.  Verifies all temporaries are freed
# when JS discards the result.

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
            proxy_pass          http://backend;
            proxy_set_header    X-Real-IP   $remote_addr;
            proxy_set_header    X-Host      $host;
            proxy_set_header    X-Request   $request_uri;
            proxy_set_header    Accept-Encoding "";
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
            /* Each call allocates array + 4 header objects */
            var hdrs = proxied.proxy.setHeader;
            void hdrs.length;
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
assert_flat($d, 10000, 'proxy.setHeader[] array getter');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
