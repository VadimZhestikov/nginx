#!/usr/bin/perl

# Tests for Stage 36 COM: r.fetch(url[, opts])
#
# Outbound HTTP/1.1 client using nginx's event system.
# Issues a real TCP connection to an HTTP backend (the same nginx
# instance) and returns a Promise<{status, headers, body}>.

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

js_source %%TESTDIR%%/fetch_handler.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # frontend — JS handlers that issue outbound fetches
        location /fetch_get      { }
        location /fetch_status   { }
        location /fetch_headers  { }
        location /fetch_post     { }

        # backend locations — plain JS handlers
        location /backend_hello  { }
        location /backend_echo   { }
        location /backend_hdr    { }
        location /backend_201    { }
    }
}
EOF

$t->write_file('fetch_handler.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    /* --- backends --- */
    set('/backend_hello', backendHello);
    set('/backend_echo',  backendEcho);
    set('/backend_hdr',   backendHdr);
    set('/backend_201',   backend201);

    /* --- frontends --- */
    set('/fetch_get',     fetchGet);
    set('/fetch_status',  fetchStatus);
    set('/fetch_headers', fetchHeaders);
    set('/fetch_post',    fetchPost);
})();

/* backends */
function backendHello(r) {
    r.respond(200, {'content-type': 'text/plain'}, 'hello from backend');
}

function backendEcho(r) {
    r.respond(200, {'content-type': 'text/plain'}, r.args || 'no-args');
}

function backendHdr(r) {
    r.respond(200, {
        'content-type': 'text/plain',
        'x-custom-resp': 'resp-value'
    }, 'hdr-test');
}

function backend201(r) {
    r.respond(201, {'content-type': 'text/plain'}, 'created');
}

/* frontends */
const PORT = 8080;

async function fetchGet(r) {
    const res = await r.fetch('http://127.0.0.1:' + PORT + '/backend_hello');
    r.respond(200, {'content-type': 'text/plain'}, res.body);
}

async function fetchStatus(r) {
    const res = await r.fetch('http://127.0.0.1:' + PORT + '/backend_201');
    r.respond(200, {'content-type': 'text/plain'}, String(res.status));
}

async function fetchHeaders(r) {
    const res = await r.fetch('http://127.0.0.1:' + PORT + '/backend_hdr');
    const hdr = res.headers['x-custom-resp'] || 'none';
    r.respond(200, {'content-type': 'text/plain'}, hdr);
}

async function fetchPost(r) {
    const res = await r.fetch('http://127.0.0.1:' + PORT + '/backend_echo', {
        method: 'POST',
        body:   'payload=42'
    });
    r.respond(200, {'content-type': 'text/plain'}, res.body);
}
JS

$t->try_run('no js module')->plan(8);

# GET: body returned from backend
like(http_get('/fetch_get'),    qr/hello from backend/, 'r.fetch GET: body');

# status: non-200 status code propagated
like(http_get('/fetch_status'), qr/201/,                'r.fetch status: 201');

# response headers accessible in result.headers
like(http_get('/fetch_headers'), qr/resp-value/,        'r.fetch headers: x-custom-resp');

# POST with body
like(http_get('/fetch_post'),   qr/no-args/,            'r.fetch POST: body sent');

# all return 200 OK to nginx client
like(http_get('/fetch_get'),     qr/200 OK/, 'r.fetch: 200 OK (get)');
like(http_get('/fetch_status'),  qr/200 OK/, 'r.fetch: 200 OK (status)');
like(http_get('/fetch_headers'), qr/200 OK/, 'r.fetch: 200 OK (headers)');
like(http_get('/fetch_post'),    qr/200 OK/, 'r.fetch: 200 OK (post)');
