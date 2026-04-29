#!/usr/bin/perl

# COM steady-state leak test: nginx.http.upstreams[] full array read.
#
# Accesses the upstreams array and enumerates .name and .zone on each
# entry every iteration.  Tests whether the NginxUpstream wrapper array
# is rebuilt per call without releasing the previous one.

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

    upstream alpha   { server 127.0.0.1:%%PORT_8091%%; }
    upstream beta    { server 127.0.0.2:%%PORT_8091%%; server 127.0.0.3:%%PORT_8091%%; }
    upstream gamma   { server 127.0.0.4:%%PORT_8091%%; server 127.0.0.5:%%PORT_8091%% backup; }

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

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

        for (var i = 0; i < n; i++) {
            var ups = nginx.http.upstreams;
            for (var j = 0; j < ups.length; j++) {
                void ups[j].name;
                void ups[j].zone;
            }
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
assert_flat($d, 10000, 'upstreams[] array enumeration');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
