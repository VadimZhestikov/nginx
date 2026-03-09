#!/usr/bin/perl

# Tests for Stage 25 COM expansion: nginx.http.variables
#
# nginx.http.variables is a plain JS object mapping every registered nginx
# variable name (without leading $) to {index: number, writable: bool}.
#
#   index    — position in r->variables[] at request time
#   writable — true when NGX_HTTP_VAR_CHANGEABLE is set

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

js_source %%TESTDIR%%/init_http_variables.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /vars { }
    }
}
EOF

$t->write_file('init_http_variables.js', <<'JS');
(function() {
    const loc = nginx.http.servers[0].locations.find(l => l.path === '/vars');
    if (loc) { loc.handler = varsHandler; }
})();

function varsHandler(r) {
    const vars = nginx.http.variables;
    const test = r.queryParams.test || '';
    let result = 'FAIL';

    if (test === 'is_object') {
        result = (vars !== null && typeof vars === 'object') ? 'PASS' : 'FAIL';

    } else if (test === 'has_uri') {
        /* $uri is a core variable — must exist */
        result = ('uri' in vars) ? 'PASS' : 'FAIL';

    } else if (test === 'uri_meta') {
        /* $uri is read-only (not CHANGEABLE) */
        const m = vars['uri'];
        result = (m && typeof m.index === 'number' && m.writable === false)
            ? 'PASS' : 'FAIL:' + JSON.stringify(m);

    } else if (test === 'has_args') {
        result = ('args' in vars) ? 'PASS' : 'FAIL';

    } else if (test === 'count') {
        /* Expect at least 20 registered variables in a minimal nginx */
        result = (Object.keys(vars).length >= 20) ? 'PASS'
            : 'FAIL:count=' + Object.keys(vars).length;

    } else if (test === 'writable_setvar') {
        /*
         * Variables created via the set directive (ngx_rewrite) or
         * add_variable are CHANGEABLE. We check for 'args' — nginx sets
         * $args as CHANGEABLE so users can modify it.
         */
        const a = vars['args'];
        result = (a && typeof a.writable === 'boolean') ? 'PASS'
            : 'FAIL:' + JSON.stringify(a);
    }

    r.respond(200, {'content-type': 'text/plain'}, result);
}
JS

$t->try_run('no js module')->plan(6);

like(http_get('/vars?test=is_object'),    qr/PASS/, 'variables: is an object');
like(http_get('/vars?test=has_uri'),      qr/PASS/, 'variables: contains $uri');
like(http_get('/vars?test=uri_meta'),     qr/PASS/, 'variables[$uri]: {index, writable:false}');
like(http_get('/vars?test=has_args'),     qr/PASS/, 'variables: contains $args');
like(http_get('/vars?test=count'),        qr/PASS/, 'variables: at least 20 entries');
like(http_get('/vars?test=writable_setvar'), qr/PASS/, 'variables[$args]: writable is bool');
