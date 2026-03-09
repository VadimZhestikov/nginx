#!/usr/bin/perl

# Tests for new SharedWorker(url) called from inside a JS Worker thread.
#
# The JS Worker thread has its own JSRuntime; the SharedWorker constructor
# registered there calls ngx_js_sw_acquire_channel() — a blocking call to the
# master SW manager — to obtain a worker_fd, then communicates over that fd
# via the existing channel protocol (channel_send / channel_recv).
# The Worker thread's poll loop is extended to watch SW channel fds in
# addition to the usual to_worker pipe.
#
# Routes:
#   /jw_sw_echo/    — JS Worker creates SW, sends value 42, SW echoes it,
#                     Worker receives echo and forwards result to main thread
#   /jw_sw_multi/   — JS Worker sends 10 then 32 to SW; SW echoes both;
#                     Worker forwards sum (42) to main thread
#   /jw_sw_sab/     — JS Worker creates SW, also creates a SAB{99}, sends SAB
#                     to SW; SW reads buf[0] and echoes it; Worker reports 99

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

# ---- SharedWorker scripts ----

# Echoes every received value back to the sender.
$t->write_file('sw_echo_val.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        port.postMessage(msg.data);
    };
};
JS

# Receives a SAB, reads buf[0], echoes it back.
$t->write_file('sw_echo_sab_val.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var view = new Int32Array(msg.data);
        port.postMessage(view[0]);
    };
};
JS

# ---- JS Worker scripts ----

# Receives a trigger value from main.  Creates a SharedWorker, sends the
# value, waits for the echo, then posts the echoed value back to main.
$t->write_file('jw_sw_echo.js', <<"JS");
onmessage = function(e) {
    var val = e.data;
    var sw  = new SharedWorker('$dir/sw_echo_val.js');
    sw.onmessage = function(reply) {
        postMessage(reply.data);
    };
    sw.postMessage(val);
};
JS

# Receives [a, b] from main.  Creates a SW, sends a then b, accumulates
# the two echoes and posts their sum to main.
$t->write_file('jw_sw_multi.js', <<"JS");
onmessage = function(e) {
    var a = e.data[0], b = e.data[1];
    var sw  = new SharedWorker('$dir/sw_echo_val.js');
    var sum = 0, count = 0;
    sw.onmessage = function(reply) {
        sum += reply.data;
        count++;
        if (count === 2) {
            postMessage(sum);
        }
    };
    sw.postMessage(a);
    sw.postMessage(b);
};
JS

# Receives a trigger value.  Creates a SW (echo_sab type), creates a SAB
# with buf[0] = trigger, sends the SAB to the SW, posts the echoed value.
$t->write_file('jw_sw_sab.js', <<"JS");
onmessage = function(e) {
    var val  = e.data;
    var sab  = new SharedArrayBuffer(16);
    var view = new Int32Array(sab);
    view[0]  = val;

    var sw = new SharedWorker('$dir/sw_echo_sab_val.js');
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

js_source %%TESTDIR%%/init_jw_sw.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /jw_sw_echo/  { }
        location /jw_sw_multi/ { }
        location /jw_sw_sab/   { }
    }
}
EOF

# ---- init_jw_sw.js ----

$t->write_file('init_jw_sw.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /jw_sw_echo/ — main spawns JS Worker; Worker creates SW, exchanges
     * one message, posts echo back; main responds with received value.
     */
    set('/jw_sw_echo/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sw_echo.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(42);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_sw_multi/ — Worker sends two values, sums the two echoes → 42.
     */
    set('/jw_sw_multi/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sw_multi.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage([10, 32]);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /jw_sw_sab/ — Worker creates SAB{99}, sends to SW (echo_sab),
     * SW reads buf[0]=99 and echoes it; Worker reports 99.
     */
    set('/jw_sw_sab/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sw_sab.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(99);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(8);

# ---- HTTP assertions ----

like(http_get('/jw_sw_echo/'),  qr/200 OK/, 'Worker→SW echo: 200');
like(http_get('/jw_sw_echo/'),  qr/42/,     'Worker→SW echo: value 42');

like(http_get('/jw_sw_multi/'), qr/200 OK/, 'Worker→SW multi: 200');
like(http_get('/jw_sw_multi/'), qr/42/,     'Worker→SW multi: sum 10+32=42');

like(http_get('/jw_sw_sab/'),   qr/200 OK/, 'Worker→SW SAB: 200');
like(http_get('/jw_sw_sab/'),   qr/99/,     'Worker→SW SAB: buf[0]=99');

like(http_get('/jw_sw_echo/'),  qr/42/,     'Worker→SW echo: repeated request');
like(http_get('/jw_sw_sab/'),   qr/99/,     'Worker→SW SAB: repeated request');
