#!/usr/bin/perl

# Tests for sending worker-created SharedArrayBuffers to SharedWorkers.
#
# A worker creates a SAB post-fork (backed by memfd), fills a value into it,
# sends it to a SharedWorker.  The SW reads the value and echoes it back.
# The worker then modifies the SAB and the SW sees the updated value (true
# shared memory between worker and master).
#
# Routes:
#   /sw_sab_echo/   — worker creates SAB{42}, sends to SW, SW echoes buf[0]
#   /sw_sab_share/  — same SAB; worker writes 99 after send; SW reads buf[0]

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $dir = $t->testdir();

# ---- SharedWorker script ----

$t->write_file('sw_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var sab = msg.data;
        var view = new Int32Array(sab);
        /* Echo back whatever is in buf[0] */
        port.postMessage(view[0]);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_sw_worker_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_sab_echo/  { }
        location /sw_sab_share/ { }
    }
}
EOF

# ---- init_sw_worker_sab.js ----

$t->write_file('init_sw_worker_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    set('/sw_sab_echo/', async function(req) {
        var sw = new SharedWorker('$dir/sw_sab.js');
        var sab = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0] = 42;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    set('/sw_sab_share/', async function(req) {
        var sw = new SharedWorker('$dir/sw_sab.js');
        var sab = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0] = 10;

        /* Send SAB; SW will read buf[0].  We write 99 *before* the SW
           processes the message — the SW must see 99, not 10. */
        sw.postMessage(sab);
        view[0] = 99;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            /* send a second message so SW echoes the updated value */
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/sw_sab_echo/'),  qr/200 OK/, 'worker SAB to SW: 200');
like(http_get('/sw_sab_echo/'),  qr/42/,     'worker SAB to SW: echoes 42');

like(http_get('/sw_sab_echo/'),  qr/42/,     'worker SAB to SW: second request');

like(http_get('/sw_sab_share/'), qr/200 OK/, 'shared SAB mutation: 200');
like(http_get('/sw_sab_share/'), qr/99/,     'shared SAB mutation: SW sees 99');

like(http_get('/sw_sab_echo/'),  qr/42/,     'worker SAB to SW: still works');
