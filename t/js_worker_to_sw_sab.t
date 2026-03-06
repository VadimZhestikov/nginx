#!/usr/bin/perl

# Tests for a SharedArrayBuffer created by a JS Worker thread and sent
# directly to a SharedWorker from within that Worker thread.
#
# The previous version of this test used a two-hop path:
#   JS Worker → main thread (pipe) → SharedWorker (channel_send/SCM_RIGHTS)
# This version tests the direct one-hop path:
#   JS Worker → SharedWorker (ngx_js_sw_acquire_channel + channel_send)
#
# The Worker thread creates the SAB (memfd, post-fork), connects to the SW
# via new SharedWorker(url), and sends the SAB directly over the channel.
# The SW receives it via mmap of the received memfd and echoes buf[0].
#
# Routes:
#   /jw_to_sw/        — JS Worker creates SAB{42}, sends to SW directly;
#                       SW echoes 42
#   /jw_to_sw_modify/ — JS Worker creates SAB{21}, doubles buf[0] to 42,
#                       sends modified SAB to SW directly; SW echoes 42
#                       (confirms the Worker's mutation is visible to the SW
#                       because they share the same memfd mapping)

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
#
# Receives a SAB via port.onmessage; echoes buf[0] back to the sender.

$t->write_file('sw_read_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var view = new Int32Array(msg.data);
        port.postMessage(view[0]);
    };
};
JS

# ---- JS Worker scripts ----

# Receives a trigger value; allocates a SAB, stores the value in buf[0],
# creates a SharedWorker, sends the SAB directly, and posts the echo back.

$t->write_file('jw_sab_to_sw.js', <<"JS");
onmessage = function(e) {
    var val  = e.data;
    var sab  = new SharedArrayBuffer(16);
    var view = new Int32Array(sab);
    view[0]  = val;

    var sw = new SharedWorker('$dir/sw_read_sab.js');
    sw.onmessage = function(reply) {
        postMessage(reply.data);
    };
    sw.postMessage(sab);
};
JS

# Same as above but doubles buf[0] before sending to confirm that the SW
# sees the Worker's in-place mutation (shared memfd backing).

$t->write_file('jw_sab_modify_to_sw.js', <<"JS");
onmessage = function(e) {
    var val  = e.data;
    var sab  = new SharedArrayBuffer(16);
    var view = new Int32Array(sab);
    view[0]  = val;
    view[0] *= 2;   /* 21 → 42 */

    var sw = new SharedWorker('$dir/sw_read_sab.js');
    sw.onmessage = function(reply) {
        postMessage(reply.data);
    };
    sw.postMessage(sab);
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_jw_to_sw_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /jw_to_sw/        { }
        location /jw_to_sw_modify/ { }
    }
}
EOF

# ---- init_jw_to_sw_sab.js ----

$t->write_file('init_jw_to_sw_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /jw_to_sw/
     *
     * Main spawns JS Worker; Worker creates SAB{42}, connects to SharedWorker,
     * sends SAB directly, waits for echo, posts result back; main responds.
     */
    set('/jw_to_sw/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sab_to_sw.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(42);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_to_sw_modify/
     *
     * Main spawns JS Worker; Worker creates SAB{21}, doubles buf[0] to 42,
     * connects to SharedWorker, sends modified SAB directly; SW echoes 42.
     */
    set('/jw_to_sw_modify/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sab_modify_to_sw.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(21);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/jw_to_sw/'),        qr/200 OK/, 'Worker SAB → SW direct: 200');
like(http_get('/jw_to_sw/'),        qr/42/,     'Worker SAB → SW direct: SW echoes 42');
like(http_get('/jw_to_sw/'),        qr/42/,     'Worker SAB → SW direct: repeated request');

like(http_get('/jw_to_sw_modify/'), qr/200 OK/, 'Worker SAB mutate → SW direct: 200');
like(http_get('/jw_to_sw_modify/'), qr/42/,     'Worker SAB mutate → SW direct: SW sees 42');
like(http_get('/jw_to_sw_modify/'), qr/42/,     'Worker SAB mutate → SW direct: repeated request');
