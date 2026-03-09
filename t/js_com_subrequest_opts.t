#!/usr/bin/perl

# Tests for Stage 34 COM: r.subrequest(uri, opts)
#
# Enhanced subrequest API:
#   opts.method   — HTTP method for the subrequest ("GET", "POST", …)
#   opts.args     — query-string string passed to the internal location
#   opts.headers  — plain object of request headers added to the subrequest
#
# Result now includes:
#   headers   — plain object of response headers (lowercase keys)
#   status    — HTTP status (unchanged)
#   body      — response body (unchanged)

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

js_source %%TESTDIR%%/subreq_opts.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # main locations — JS handlers that issue subrequests
        location /test_args    { }
        location /test_method  { }
        location /test_headers { }
        location /test_rsp_headers { }

        # internal backend locations
        location /backend_args {
            # echo back the query string
        }
        location /backend_method {
            # report the request method
        }
        location /backend_hdr {
            # report a request header
        }
    }
}
EOF

$t->write_file('subreq_opts.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/test_args',        testArgs);
    set('/test_method',      testMethod);
    set('/test_headers',     testHeaders);
    set('/test_rsp_headers', testRspHeaders);

    /* backends */
    set('/backend_args',   backendArgs);
    set('/backend_method', backendMethod);
    set('/backend_hdr',    backendHdr);
})();

/* backend: echo back the query string (r.args) */
function backendArgs(r) {
    r.respond(200, {'content-type': 'text/plain', 'x-args': r.args}, r.args);
}

/* backend: echo back the HTTP method */
function backendMethod(r) {
    r.respond(200, {'content-type': 'text/plain'}, r.method);
}

/* backend: echo back the x-custom request header */
function backendHdr(r) {
    const val = r.headers['x-custom'] || 'none';
    r.respond(200, {'content-type': 'text/plain'}, val);
}

/* test: pass opts.args to internal location */
async function testArgs(r) {
    const sr = await r.subrequest('/backend_args', { args: 'foo=bar' });
    r.respond(200, {'content-type': 'text/plain'}, sr.body);
}

/* test: pass opts.method to internal location */
async function testMethod(r) {
    const sr = await r.subrequest('/backend_method', { method: 'POST' });
    r.respond(200, {'content-type': 'text/plain'}, sr.body);
}

/* test: pass opts.headers to internal location */
async function testHeaders(r) {
    const sr = await r.subrequest('/backend_hdr',
                                  { headers: { 'x-custom': 'myvalue' } });
    r.respond(200, {'content-type': 'text/plain'}, sr.body);
}

/* test: result.headers contains response headers */
async function testRspHeaders(r) {
    const sr = await r.subrequest('/backend_args', { args: 'check=1' });
    const ct = sr.headers['content-type'] || 'none';
    r.respond(200, {'content-type': 'text/plain'}, 'ct=' + ct);
}
JS

$t->try_run('no js module')->plan(8);

# opts.args — backend receives correct query string
like(http_get('/test_args'),   qr/foo=bar/, 'subrequest opts.args: backend receives args');

# opts.method — backend sees POST
like(http_get('/test_method'),  qr/POST/,   'subrequest opts.method: backend sees POST');

# opts.headers — backend echoes x-custom header
like(http_get('/test_headers'), qr/myvalue/, 'subrequest opts.headers: backend sees header');

# result.headers — headers object present and has content-type
like(http_get('/test_rsp_headers'), qr/ct=text\/plain/,
     'subrequest result.headers: content-type present');

# 200 OK on all
like(http_get('/test_args'),         qr/200 OK/, 'subrequest opts: 200 OK (args)');
like(http_get('/test_method'),       qr/200 OK/, 'subrequest opts: 200 OK (method)');
like(http_get('/test_headers'),      qr/200 OK/, 'subrequest opts: 200 OK (headers)');
like(http_get('/test_rsp_headers'),  qr/200 OK/, 'subrequest opts: 200 OK (rsp_headers)');
