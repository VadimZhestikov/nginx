#!/usr/bin/perl

# Tests for SharedWorker class:
#   new SharedWorker(url)    — creates (master) or connects (worker) to a
#                              long-lived JS thread in the master process.
#   sw.postMessage(data)     — worker → SharedWorker thread
#   sw.onmessage = fn        — receive messages from SharedWorker thread
#   onconnect = fn           — SharedWorker thread receives new connection
#   port.postMessage(data)   — SharedWorker thread → worker
#   port.onmessage = fn      — SharedWorker thread receives messages
#
# Two SharedWorkers are created:
#   echo_sw.js  — echoes back whatever it receives
#   add_sw.js   — returns sum of {a, b} sent to it

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

        location /sw_echo/ { }
        location /sw_add/  { }
    }
}
EOF

# SharedWorker script: echo back whatever message it receives on the port.
$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage(ev.data);
    };
};
JS

# SharedWorker script: add two numbers sent as {a, b} and return the sum.
$t->write_file('add_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage(ev.data.a + ev.data.b);
    };
};
JS

# Main init script.  Runs once in the master process (init_conf).
# Creates both SharedWorkers and installs async request handlers that
# communicate with them.
$t->write_file('init.js', <<'JS');
(function() {
    var prefix = nginx.cycle.prefix;
    var echoSW = new SharedWorker(prefix + 'echo_sw.js');
    var addSW  = new SharedWorker(prefix + 'add_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(path) {
        return locs.find(function(l) { return l.path === path; });
    }

    findLoc('/sw_echo/').handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('EchoOK');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };

    findLoc('/sw_add/').handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'add_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({a: 20, b: 22});
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(6);

# --- assertions ---

like(http_get('/sw_echo/'), qr/200 OK/,  'shared worker echo 200');
like(http_get('/sw_echo/'), qr/EchoOK/,  'shared worker echo body');

like(http_get('/sw_add/'),  qr/200 OK/,  'shared worker add 200');
like(http_get('/sw_add/'),  qr/\b42\b/,  'shared worker add result is 42');

# Second requests reuse the existing connection (no second CONNECT).
like(http_get('/sw_echo/'), qr/EchoOK/,  'shared worker echo second request');
like(http_get('/sw_add/'),  qr/\b42\b/,  'shared worker add second request');
