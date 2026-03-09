#!/usr/bin/perl

# Tests for Stage 18 COM expansion: r.variable(name) and r.setVariable(name, value).
#
# r.variable(name)         — reads any nginx variable by name (no leading $)
# r.setVariable(name, val) — writes a settable nginx variable

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

js_source %%TESTDIR%%/init_request_var.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # location for reading built-in variables
        location /read {
        }

        # location for testing $arg_* variable
        location /args {
        }

        # location for testing r.setVariable with a set-directive variable
        location /write {
            set $myvar "original";
        }

        # location for testing unknown variable (should throw)
        location /unknown {
        }
    }
}
EOF

$t->write_file('init_request_var.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    set('/read',    readHandler);
    set('/args',    argsHandler);
    set('/write',   writeHandler);
    set('/unknown', unknownHandler);
})();

function readHandler(r) {
    const uri    = r.variable("uri");
    const method = r.variable("request_method");
    const addr   = r.variable("remote_addr");
    const undef  = r.variable("__no_such_variable__");

    const ok = uri === "/read"
            && method === "GET"
            && addr   === "127.0.0.1"
            && undef  === null;

    r.respond(200, {"content-type": "text/plain"}, ok ? "PASS" : "FAIL");
}

function argsHandler(r) {
    const foo = r.variable("arg_foo");
    r.respond(200, {"content-type": "text/plain"}, foo !== null ? foo : "null");
}

function writeHandler(r) {
    // read initial value set by `set $myvar "original"`
    const before = r.variable("myvar");

    // overwrite it
    r.setVariable("myvar", "changed");
    const after = r.variable("myvar");

    const ok = before === "original" && after === "changed";
    r.respond(200, {"content-type": "text/plain"}, ok ? "PASS" : "FAIL:" + before + "," + after);
}

function unknownHandler(r) {
    let threw = false;
    try {
        r.setVariable("__nosuchvar__", "x");
    } catch (e) {
        threw = true;
    }
    r.respond(200, {"content-type": "text/plain"}, threw ? "THREW" : "NOTHROW");
}
JS

$t->try_run('no js module')->plan(7);

# r.variable: uri, request_method, remote_addr, missing
like(http_get('/read'), qr/PASS/, 'r.variable reads built-in variables');

# r.variable: $arg_foo with value
like(http_get('/args?foo=bar'), qr/bar/, 'r.variable reads $arg_foo == bar');

# r.variable: $arg_foo absent → null
like(http_get('/args'), qr/null/, 'r.variable returns null for absent $arg_foo');

# r.setVariable: set a rewrite-module variable
like(http_get('/write'), qr/PASS/, 'r.setVariable changes variable value');

# r.setVariable: unknown variable throws TypeError
like(http_get('/unknown'), qr/THREW/, 'r.setVariable throws for unknown variable');

# r.variable: $uri for /read request
my $res = http_get('/read');
like($res, qr/200/, '/read returns 200');

# r.variable: $request_method
like(http_get('/read'), qr/200/, 'second /read request returns 200');
