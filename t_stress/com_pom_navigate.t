#!/usr/bin/perl

# COMCON D1 steady-state leak test: POM NodeView navigation.
#
# Each iteration builds a fresh comcon.pom() NodeView over a fragment and walks
# it (children, parent, text()/quote() quotations, describe()). The C accessor
# (__pomNodeAt) holds NO pointers — it returns a fresh plain object per call and
# the root fragment is kept alive by the JS closure — so navigation must be flat:
# nothing accumulates C-side, and the per-iteration JS objects are GC-eligible.

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

    var frag = function(a) {
        function twice(x) { return x + x; }
        function inc(x) { return x + 1; }
        return twice(a) + inc(a);
    };

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    set('/stress/', function(req) {
        var n      = parseInt(req.args.n) || 1000;
        var before = nginx.jsMemUsage();
        var acc    = 0;

        for (var i = 0; i < n; i++) {
            var root = comcon.pom(frag);
            var kids = root.children;                 /* lazy getter */
            acc += kids.length;
            acc += kids[0].parent.id;                 /* parent navigation */
            acc += kids[0].text().source.length;      /* quotation read */
            acc += kids[1].quote().source.length;
            acc += root.describe().ops.length;
        }

        nginx.gc();
        var after = nginx.jsMemUsage();

        req.respond(200, {'content-type': 'text/plain'},
            'acc=' + acc +
            ' before=' + before.mallocSize +
            ' after='  + after.mallocSize  +
            ' delta='  + (after.mallocSize - before.mallocSize));
    });
})();
JS

$t->run();

like(http_get('/check/'), qr/200 OK/, 'worker alive');

my $d = run_stress($t, '/stress/', 5000);
assert_flat($d, 5000, 'pom navigation');

like(http_get('/check/'), qr/200 OK/, 'worker alive after stress');
