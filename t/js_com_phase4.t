#!/usr/bin/perl

# Tests for Phase 4 — request-phase JS content handlers:
#   location.handler = "fn"   (config-phase, installs ngx_js_content_handler)
#   req.respond(status, headers, body)
#   req.method / req.uri / req.args / req.remoteAddr / req.headers

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /hello/   { }
        location /echo/    { }
        location /headers/ { }
        location /status/  { }
        location /empty/   { }
        location /addr/        { }
        location /missing/     { }
        location /async/       { }
        location /async_throw/ { }
        location /async_timer/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Config phase — install JS handlers on each location.
// This script runs once in the master process (init_conf).  Workers
// inherit the fully-evaluated JSContext via fork() copy-on-write, so
// the handler functions are already available with no re-evaluation.
//
// location.handler now accepts a function value directly (not a name).

(function installHandlers() {
    const locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    // Named functions passed by reference
    set('/hello/',   helloHandler);
    set('/echo/',    echoHandler);
    set('/headers/', headersHandler);
    set('/status/',  statusHandler);
    set('/empty/',   emptyHandler);
    set('/addr/',    addrHandler);

    // Anonymous arrow function assigned inline
    set('/missing/', req => { throw new Error('deliberate error'); });

    // Async handlers
    set('/async/', async req => {
        const msg = await Promise.resolve('AsyncOK');
        req.respond(200, {'content-type': 'text/plain'}, msg);
    });

    set('/async_throw/', async req => {
        await Promise.resolve();
        throw new Error('async deliberate error');
    });

    set('/async_timer/', async req => {
        await nginx.setTimeout(10);
        req.respond(200, {'content-type': 'text/plain'}, 'TimerOK');
    });
})();

// --- Request-phase handler functions ---

function helloHandler(req) {
    req.respond(200, {'content-type': 'text/plain'}, 'Hello World');
}

function echoHandler(req) {
    const body = req.method + ' ' + req.uri
                 + (req.args ? '?' + req.args : '');
    req.respond(200, {'content-type': 'text/plain'}, body);
}

function headersHandler(req) {
    const ua = req.headers['user-agent'] || '';
    req.respond(200, {'content-type': 'text/plain'}, ua);
}

function statusHandler(req) {
    req.respond(201, {'content-type': 'text/plain'}, 'Created');
}

function emptyHandler(req) {
    req.respond(200, {'content-type': 'text/plain'}, '');
}

function addrHandler(req) {
    req.respond(200, {'content-type': 'text/plain'}, req.remoteAddr);
}
JS

$t->try_run('no js module')->plan(15);

# --- HTTP assertions ---

# basic respond: status + body
like(http_get('/hello/'), qr|200 OK|,     'handler responds 200 OK');
like(http_get('/hello/'), qr/Hello World/,'handler body is correct');

# req.method + req.uri + req.args
like(http_get('/echo/?x=1'), qr|GET /echo/|, 'req.method and req.uri in response');
like(http_get('/echo/?x=1'), qr/x=1/,         'req.args in response');

# content-type from respond() headers object
like(http_get('/hello/'), qr|Content-Type: text/plain|i, 'content-type header set');

# custom status code
like(http_get('/status/'), qr/201/, 'respond() with status 201');

# req.headers — incoming request header echoed back
like(
    http("GET /headers/ HTTP/1.0\r\nHost: localhost\r\nUser-Agent: TestBrowser/42\r\n\r\n"),
    qr/TestBrowser\/42/,
    'req.headers[user-agent] echoed in response'
);

# req.remoteAddr
like(http_get('/addr/'), qr/127\.0\.0\.1/, 'req.remoteAddr is loopback IP');

# empty body is accepted
like(http_get('/empty/'), qr|200 OK|, 'handler with empty body responds 200');

# handler that throws → 500 Internal Server Error
like(http_get('/missing/'), qr/500/, 'handler that throws returns 500');

# async handler — awaits a resolved Promise, then responds
like(http_get('/async/'), qr/200 OK/,  'async handler responds 200');
like(http_get('/async/'), qr/AsyncOK/, 'async handler body correct');

# async handler that throws → rejected Promise → 500
like(http_get('/async_throw/'), qr/500/, 'async handler rejection returns 500');

# async handler using a real NGINX timer — request is suspended and resumed
like(http_get('/async_timer/'), qr/200 OK/,  'async timer handler responds 200');
like(http_get('/async_timer/'), qr/TimerOK/, 'async timer handler body correct');
