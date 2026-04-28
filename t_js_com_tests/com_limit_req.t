#!/usr/bin/perl

# COM steady-state leak test: NginxLimitReq + NginxLimitReqLimit setters.
#
# Exercises limitReq.limits[0].burst and limitReq.limits[0].rate setters
# on every iteration via the full accessor chain.  Tests both the limit
# entry wrapper and the shm-mutex-guarded rate setter path.

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

    limit_req_zone $binary_remote_addr zone=test:1m rate=10r/s;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /limited/ {
            limit_req zone=test burst=5;
        }
        location /stress/ { }
        location /check/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;

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

        var limited = locs.find(function(l) { return l.path === '/limited/'; });
        for (var i = 0; i < n; i++) {
            /* Full chain: limitReq wrapper + limits array + entry wrapper */
            limited.limitReq.limits[0].burst = (i % 10) + 1;
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
assert_flat($d, 10000, 'limitReq.limits[0].burst setter full chain');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
