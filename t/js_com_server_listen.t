#!/usr/bin/perl

# Tests for Stage 27 COM expansion: nginx.http.servers[i].listen
#
# server.listen is an array of objects, one per listen directive where
# this server is the default:
#   { addr: string, port: number, ssl: bool, http2: bool, default: bool }

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

js_include %%TESTDIR%%/init_server_listen.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /listen { }
    }
}
EOF

$t->write_file('init_server_listen.js', <<'JS');
(function() {
    const loc = nginx.http.servers[0].locations.find(l => l.path === '/listen');
    if (loc) { loc.handler = listenHandler; }
})();

function listenHandler(r) {
    const srv = nginx.http.servers[0];
    const la  = srv.listen;
    const test = r.queryParams.test || '';
    let result = 'FAIL';

    if (test === 'is_array') {
        result = Array.isArray(la) ? 'PASS' : 'FAIL';

    } else if (test === 'non_empty') {
        result = (la.length >= 1) ? 'PASS' : 'FAIL:length=' + la.length;

    } else if (test === 'has_port') {
        const e = la[0];
        result = (e && typeof e.port === 'number' && e.port > 0)
            ? 'PASS' : 'FAIL:' + JSON.stringify(e);

    } else if (test === 'has_addr') {
        const e = la[0];
        result = (e && typeof e.addr === 'string' && e.addr.length > 0)
            ? 'PASS' : 'FAIL:' + JSON.stringify(e);

    } else if (test === 'ssl_false') {
        const e = la[0];
        result = (e && e.ssl === false) ? 'PASS' : 'FAIL:' + JSON.stringify(e);

    } else if (test === 'http2_bool') {
        const e = la[0];
        result = (e && typeof e.http2 === 'boolean')
            ? 'PASS' : 'FAIL:' + JSON.stringify(e);

    } else if (test === 'default_true') {
        const e = la[0];
        result = (e && e.default === true) ? 'PASS' : 'FAIL:' + JSON.stringify(e);
    }

    r.respond(200, {'content-type': 'text/plain'}, result);
}
JS

$t->try_run('no js module')->plan(7);

like(http_get('/listen?test=is_array'),    qr/PASS/, 'server.listen: is an array');
like(http_get('/listen?test=non_empty'),   qr/PASS/, 'server.listen: has at least one entry');
like(http_get('/listen?test=has_port'),    qr/PASS/, 'server.listen[0].port is a number > 0');
like(http_get('/listen?test=has_addr'),    qr/PASS/, 'server.listen[0].addr is a string');
like(http_get('/listen?test=ssl_false'),   qr/PASS/, 'server.listen[0].ssl === false');
like(http_get('/listen?test=http2_bool'),  qr/PASS/, 'server.listen[0].http2 is boolean');
like(http_get('/listen?test=default_true'),qr/PASS/, 'server.listen[0].default === true');
