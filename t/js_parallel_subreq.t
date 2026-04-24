#!/usr/bin/perl

# Tests for parallel r.subrequest() via Promise.all
#
# Verifies that multiple in-flight subrequests can be initiated simultaneously
# with Promise.all([r.subrequest(A), r.subrequest(B), r.subrequest(C)]).
# The parent request must receive all three results and merge them correctly.
#
# Also tests: sequential groups after parallel groups, and that sequential
# await still works after this refactoring.

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

js_source %%TESTDIR%%/parallel_subreq.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /parallel2    { }
        location /parallel3    { }
        location /sequential   { }
        location /two_groups   { }

        location /svc/price    { }
        location /svc/stock    { }
        location /svc/rating   { }
        location /svc/shipping { }
    }
}
EOF

$t->write_file('parallel_subreq.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    /* Internal micro-services */
    set('/svc/price',    function(r) { r.respond(200, {}, 'price=10'); });
    set('/svc/stock',    function(r) { r.respond(200, {}, 'stock=50'); });
    set('/svc/rating',   function(r) { r.respond(200, {}, 'rating=4.5'); });
    set('/svc/shipping', function(r) { r.respond(200, {}, 'shipping=free'); });

    /* 2 parallel subrequests */
    set('/parallel2', async function(r) {
        const [a, b] = await Promise.all([
            r.subrequest('/svc/price'),
            r.subrequest('/svc/stock'),
        ]);
        r.respond(200, {}, a.body + ',' + b.body);
    });

    /* 3 parallel subrequests */
    set('/parallel3', async function(r) {
        const [a, b, c] = await Promise.all([
            r.subrequest('/svc/price'),
            r.subrequest('/svc/stock'),
            r.subrequest('/svc/rating'),
        ]);
        r.respond(200, {}, a.body + ',' + b.body + ',' + c.body);
    });

    /* sequential still works */
    set('/sequential', async function(r) {
        const a = await r.subrequest('/svc/price');
        const b = await r.subrequest('/svc/stock');
        r.respond(200, {}, a.body + ',' + b.body);
    });

    /* two sequential Promise.all groups */
    set('/two_groups', async function(r) {
        const [a, b] = await Promise.all([
            r.subrequest('/svc/price'),
            r.subrequest('/svc/stock'),
        ]);
        const [c, d] = await Promise.all([
            r.subrequest('/svc/rating'),
            r.subrequest('/svc/shipping'),
        ]);
        r.respond(200, {}, a.body + ',' + b.body + ',' + c.body + ',' + d.body);
    });
})();
JS

$t->try_run('no js module')->plan(14);

# 2 parallel subrequests
my $r2 = http_get('/parallel2');
like($r2, qr/200 OK/,      'parallel2: 200 OK');
like($r2, qr/price=10/,    'parallel2: price present');
like($r2, qr/stock=50/,    'parallel2: stock present');

# 3 parallel subrequests (the fan-out/fan-in case)
my $r3 = http_get('/parallel3');
like($r3, qr/200 OK/,      'parallel3: 200 OK');
like($r3, qr/price=10/,    'parallel3: price present');
like($r3, qr/stock=50/,    'parallel3: stock present');
like($r3, qr/rating=4\.5/, 'parallel3: rating present');

# sequential (regression test)
my $rs = http_get('/sequential');
like($rs, qr/200 OK/,   'sequential: 200 OK');
like($rs, qr/price=10/, 'sequential: price present');
like($rs, qr/stock=50/, 'sequential: stock present');

# two sequential Promise.all groups on same request
my $rg = http_get('/two_groups');
like($rg, qr/200 OK/,       'two_groups: 200 OK');
like($rg, qr/price=10/,     'two_groups: price present');
like($rg, qr/rating=4\.5/,  'two_groups: rating present');
like($rg, qr/shipping=free/, 'two_groups: shipping present');
