#!/usr/bin/perl

# Tests for Stage 28 COM expansion: r.variables
#
# r.variables is a live read/write exotic object backed by nginx's
# variables hash and r->variables[].  Property names are nginx variable
# names (without leading $).
#
#   r.variables.uri          → string value or null if not_found
#   r.variables.my_var = "x" → sets an indexed CHANGEABLE variable
#   "uri" in r.variables     → true
#   Object.keys(r.variables) → array of all variable names

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_req_vars.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        set $my_var "";

        location /vars { }
    }
}
EOF

$t->write_file('init_req_vars.js', <<'JS');
(function() {
    const loc = nginx.http.servers[0].locations.find(l => l.path === '/vars');
    if (loc) { loc.handler = varsHandler; }
})();

function varsHandler(r) {
    const v   = r.variables;
    const test = r.queryParams.test || '';
    let result = 'FAIL';

    if (test === 'is_object') {
        result = (v !== null && typeof v === 'object') ? 'PASS' : 'FAIL';

    } else if (test === 'has_uri') {
        /* $uri is a core variable — must exist */
        result = ('uri' in v) ? 'PASS' : 'FAIL';

    } else if (test === 'uri_value') {
        /* $uri for /vars should be "/vars" */
        result = (typeof v.uri === 'string' && v.uri.length > 0)
            ? 'PASS' : 'FAIL:' + JSON.stringify(v.uri);

    } else if (test === 'unknown_undef') {
        /* A nonexistent variable returns undefined (not in hash) */
        result = (v.__no_such_nginx_var_xyzzy === undefined)
            ? 'PASS' : 'FAIL:' + JSON.stringify(v.__no_such_nginx_var_xyzzy);

    } else if (test === 'set_read') {
        /* set $my_var is a CHANGEABLE indexed variable */
        v.my_var = 'hello';
        result = (v.my_var === 'hello') ? 'PASS' : 'FAIL:' + JSON.stringify(v.my_var);

    } else if (test === 'keys_non_empty') {
        const k = Object.keys(v);
        result = (Array.isArray(k) && k.length >= 20)
            ? 'PASS' : 'FAIL:length=' + k.length;

    } else if (test === 'keys_has_uri') {
        result = (Object.keys(v).indexOf('uri') >= 0)
            ? 'PASS' : 'FAIL';
    }

    r.respond(200, {'content-type': 'text/plain'}, result);
}
JS

$t->try_run('no js module')->plan(7);

like(http_get('/vars?test=is_object'),     qr/PASS/, 'r.variables: is an object');
like(http_get('/vars?test=has_uri'),       qr/PASS/, 'r.variables: "uri" in r.variables');
like(http_get('/vars?test=uri_value'),     qr/PASS/, 'r.variables.uri is a non-empty string');
like(http_get('/vars?test=unknown_undef'), qr/PASS/, 'r.variables: unknown name → undefined');
like(http_get('/vars?test=set_read'),      qr/PASS/, 'r.variables: set and read back my_var');
like(http_get('/vars?test=keys_non_empty'),qr/PASS/, 'r.variables: Object.keys has ≥20 entries');
like(http_get('/vars?test=keys_has_uri'),  qr/PASS/, 'r.variables: Object.keys includes "uri"');
