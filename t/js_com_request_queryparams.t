#!/usr/bin/perl

# Tests for Stage 22 COM expansion: r.queryParams
#
# r.queryParams returns a plain JS object with %XX-decoded key/value pairs
# parsed from the query string (r->args).

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

js_include %%TESTDIR%%/init_queryparams.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /qp { }
    }
}
EOF

$t->write_file('init_queryparams.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    const loc = locs.find(l => l.path === '/qp');
    if (loc) { loc.handler = qpHandler; }
})();

function qpHandler(r) {
    const q = r.queryParams;
    const test = r.args ? r.variable('arg_test') : '';

    let result = 'FAIL';

    if (test === 'basic') {
        result = (q.foo === 'bar' && q.count === '3') ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'empty') {
        result = (Object.keys(q).length === 0) ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'encoded') {
        /* ?key=hello%20world&path=%2Fsome%2Fpath */
        result = (q.key === 'hello world' && q.path === '/some/path')
            ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'novalue') {
        /* ?flag — key with no '=' */
        result = (q.flag === '') ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'multi') {
        /* ?a=1&b=2&c=3 */
        result = (q.a === '1' && q.b === '2' && q.c === '3') ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'overwrite') {
        /* ?x=first&x=second — last value wins */
        result = (q.x === 'second') ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === 'enckey') {
        /* ?hel%6Co=world — encoded key */
        result = (q['hello'] === 'world') ? 'PASS' : 'FAIL:' + JSON.stringify(q);

    } else if (test === '') {
        /* no args at all — queryParams must be {} */
        result = (Object.keys(q).length === 0) ? 'PASS' : 'FAIL:' + JSON.stringify(q);
    }

    r.respond(200, {'content-type': 'text/plain'}, result);
}
JS

$t->try_run('no js module')->plan(7);

like(http_get('/qp?test=basic&foo=bar&count=3'),
     qr/PASS/, 'queryParams: basic key/value');

like(http_get('/qp'),
     qr/PASS/, 'queryParams: empty args → {}');

like(http_get('/qp?test=encoded&key=hello%20world&path=%2Fsome%2Fpath'),
     qr/PASS/, 'queryParams: %XX-decoded values');

like(http_get('/qp?test=novalue&flag'),
     qr/PASS/, 'queryParams: key without value → ""');

like(http_get('/qp?test=multi&a=1&b=2&c=3'),
     qr/PASS/, 'queryParams: multiple params');

like(http_get('/qp?test=overwrite&x=first&x=second'),
     qr/PASS/, 'queryParams: duplicate key — last wins');

like(http_get('/qp?test=enckey&hel%6Co=world'),
     qr/PASS/, 'queryParams: %XX-decoded key');
