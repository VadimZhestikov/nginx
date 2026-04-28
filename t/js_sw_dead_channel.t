#!/usr/bin/perl

# Tests for SharedWorker channel error-handling code paths.
#
# Directly simulating a dead SW thread is hard to orchestrate in a portable
# test, so instead we verify:
#   1. Normal echo/ping works (exercises the refactored channel_send path).
#   2. The server stays alive across repeated requests (no crash from the
#      error-handling additions).
#
# The channel_send refactoring (void → ngx_int_t, EPIPE/EAGAIN logging) is
# exercised indirectly: every successful postMessage goes through the new
# code path.

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

js_source %%TESTDIR%%/dc_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /ping/ { }
    }
}
EOF

# A simple echo SharedWorker.
$t->write_file('dc_echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage('pong:' + ev.data);
    };
};
JS

# Main init script.
$t->write_file('dc_init.js', <<'JS');
(function() {
    var prefix = nginx.cycle.prefix;
    var echoSW = new SharedWorker(prefix + 'dc_echo_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(path) {
        return locs.find(function(l) { return l.path === path; });
    }

    findLoc('/ping/').handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'dc_echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('hello');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(4);

# --- assertions ---

# Basic functionality works (exercises channel_send NGX_OK path).
like(http_get('/ping/'), qr/200 OK/,      'channel_send ok: status 200');
like(http_get('/ping/'), qr/pong:hello/,  'channel_send ok: correct body');

# Server stays alive across repeated calls (no crash from error-handling code).
like(http_get('/ping/'), qr/200 OK/,      'server alive after second request');
like(http_get('/ping/'), qr/pong:hello/,  'server alive after third request');
