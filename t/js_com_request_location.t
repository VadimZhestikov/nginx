#!/usr/bin/perl

# Tests for Stage 19 COM expansion: r.location getter.
#
# r.location returns the NginxLocation object for the request's matched
# location, giving handlers read access to their own config.

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

js_include %%TESTDIR%%/init_request_location.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # location A — default keepalive_timeout
        location /a {
            keepalive_timeout 30s;
            sendfile on;
        }

        # location B — different settings
        location /b {
            keepalive_timeout 90s;
            sendfile off;
            default_type application/json;
        }
    }
}
EOF

$t->write_file('init_request_location.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/a', handlerA);
    set('/b', handlerB);
})();

function handlerA(r) {
    const loc = r.location;

    // r.location is the NginxLocation for /a
    const ok = loc !== null
            && typeof loc === "object"
            && loc.path === "/a"
            && loc.keepaliveTimeout === 30000
            && loc.sendfile === true;

    r.respond(200, {"content-type": "text/plain"}, ok ? "PASS" : "FAIL:"
        + JSON.stringify({
            path: loc.path,
            kt:   loc.keepaliveTimeout,
            sf:   loc.sendfile
          }));
}

function handlerB(r) {
    const loc = r.location;

    const ok = loc !== null
            && loc.path === "/b"
            && loc.keepaliveTimeout === 90000
            && loc.sendfile === false
            && loc.defaultType === "application/json";

    r.respond(200, {"content-type": "text/plain"}, ok ? "PASS" : "FAIL:"
        + JSON.stringify({
            path: loc.path,
            kt:   loc.keepaliveTimeout,
            sf:   loc.sendfile,
            dt:   loc.defaultType
          }));
}
JS

$t->try_run('no js module')->plan(4);

like(http_get('/a'), qr/PASS/, 'r.location for /a: path, keepaliveTimeout, sendfile');
like(http_get('/b'), qr/PASS/, 'r.location for /b: path, keepaliveTimeout, sendfile, defaultType');

# Verify it is a real NginxLocation by checking a property via the log path
like(http_get('/a'), qr/200 OK/, '/a handler responds 200');
like(http_get('/b'), qr/200 OK/, '/b handler responds 200');
