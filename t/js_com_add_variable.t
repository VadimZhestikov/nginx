#!/usr/bin/perl

# Tests for Stage 33 COM: nginx.http.addVariable(name)
#
# Registers a custom nginx variable from js_init_http, then reads and
# writes it from a js_source request handler via r.variables.

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

js_source %%TESTDIR%%/handler_vars.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /write  { }
        location /read   { }
        location /scope  { }
    }

    js_init_http %%TESTDIR%%/init_vars.js;
}
EOF

# js_init_http: register two custom variables
$t->write_file('init_vars.js', <<'JS');
var idx1 = nginx.http.addVariable('js_custom');
var idx2 = nginx.http.addVariable('js_other');

/* Calling addVariable outside js_init_http should throw at request time,
   but here we just verify both return numeric indexes. */
if (typeof idx1 !== 'number' || idx1 < 0) {
    throw new Error('addVariable js_custom returned bad index: ' + idx1);
}
if (typeof idx2 !== 'number' || idx2 < 0) {
    throw new Error('addVariable js_other returned bad index: ' + idx2);
}
JS

# js_source: request handlers that read/write r.variables.js_custom
$t->write_file('handler_vars.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/write',  writeHandler);
    set('/read',   readHandler);
    set('/scope',  scopeHandler);
})();

/* write then read back the custom variable */
function writeHandler(r) {
    const val = r.args || 'default';
    r.variables['js_custom'] = val;
    const got = r.variables['js_custom'];
    r.respond(200, {'content-type': 'text/plain'}, got);
}

/* read without prior write — should return empty string or null */
function readHandler(r) {
    const got = r.variables['js_custom'];
    /* unset indexed var returns null from our exotic getter */
    r.respond(200, {'content-type': 'text/plain'},
              got === null ? 'null' : String(got));
}

/* write js_other, leave js_custom unset, read both */
function scopeHandler(r) {
    r.variables['js_other'] = 'other_val';
    const custom = r.variables['js_custom'];
    const other  = r.variables['js_other'];
    r.respond(200, {'content-type': 'text/plain'},
              'custom=' + (custom === null ? 'null' : custom)
              + ' other=' + other);
}
JS

$t->try_run('no js module')->plan(6);

# write and read back a custom value
like(http_get('/write?hello'), qr/hello/, 'addVariable: write+read custom var');

# write default value
like(http_get('/write'), qr/default/, 'addVariable: default value write');

# unset variable returns null-like empty
like(http_get('/read'), qr/null|^$/, 'addVariable: unset var returns null');

# independent variables don't collide
like(http_get('/scope'), qr/other=other_val/, 'addVariable: js_other written');
like(http_get('/scope'), qr/custom=null/,     'addVariable: js_custom unset in scope');

# 200 OK on all
like(http_get('/write?test'), qr/200 OK/, 'addVariable: 200 OK status');
