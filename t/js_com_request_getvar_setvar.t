#!/usr/bin/perl

# Tests for Stage 35 COM: r.getVar(name) and r.setVar(name, value)
#
# Convenience aliases for the existing r.variable(name) and
# r.setVariable(name, value) methods.  Registered via nginx.http.addVariable
# so custom JS variables can be read back and written at request time.

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

js_source %%TESTDIR%%/handler_getvar.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /getvar_builtin  { }
        location /setvar_custom   { }
        location /getvar_missing  { }
        location /roundtrip       { }
    }

    js_init_http %%TESTDIR%%/init_getvar.js;
}
EOF

$t->write_file('init_getvar.js', <<'JS');
/* register a writable JS variable for the roundtrip test */
nginx.http.addVariable('js_roundtrip');
JS

$t->write_file('handler_getvar.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/getvar_builtin',  getVarBuiltin);
    set('/setvar_custom',   setVarCustom);
    set('/getvar_missing',  getVarMissing);
    set('/roundtrip',       roundtrip);
})();

/* r.getVar on a built-in nginx variable ($host) */
function getVarBuiltin(r) {
    const host = r.getVar('host');
    r.respond(200, {'content-type': 'text/plain'}, 'host=' + host);
}

/* r.setVar on a custom JS variable, then read it back via r.getVar */
function setVarCustom(r) {
    r.setVar('js_roundtrip', 'hello');
    const got = r.getVar('js_roundtrip');
    r.respond(200, {'content-type': 'text/plain'}, got);
}

/* r.getVar on an unknown variable — should return null */
function getVarMissing(r) {
    const val = r.getVar('does_not_exist_ever');
    r.respond(200, {'content-type': 'text/plain'},
              val === null ? 'null' : String(val));
}

/* write via setVar, read via r.variables[] exotic */
function roundtrip(r) {
    r.setVar('js_roundtrip', 'roundtrip_val');
    const via_exotic = r.variables['js_roundtrip'];
    r.respond(200, {'content-type': 'text/plain'}, via_exotic || 'none');
}
JS

$t->try_run('no js module')->plan(7);

# r.getVar returns built-in $host
like(http_get('/getvar_builtin'),  qr/host=localhost/,   'r.getVar: built-in host variable');

# r.setVar + r.getVar roundtrip on a custom variable
like(http_get('/setvar_custom'),   qr/hello/,            'r.setVar/getVar: custom var roundtrip');

# r.getVar on unknown variable returns null
like(http_get('/getvar_missing'),  qr/null/,             'r.getVar: unknown var returns null');

# value set via r.setVar is readable via r.variables[] exotic
like(http_get('/roundtrip'),       qr/roundtrip_val/,    'r.setVar: value visible in r.variables[]');

# all return 200 OK
like(http_get('/getvar_builtin'),  qr/200 OK/,           'r.getVar: 200 status');
like(http_get('/setvar_custom'),   qr/200 OK/,           'r.setVar: 200 status');
like(http_get('/roundtrip'),       qr/200 OK/,           'r.setVar: roundtrip 200');
