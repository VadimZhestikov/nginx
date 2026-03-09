#!/usr/bin/perl

# Tests for a JS Worker creating a SharedArrayBuffer and sending it to the
# nginx worker main thread (worker → main direction).
#
# The existing js_worker_sab.t covers main → worker SAB passing.  Here the
# Worker thread is the allocator: it calls new SharedArrayBuffer(), which
# invokes ngx_js_sab_alloc in a post-fork worker process (memfd path).
# QuickJS serializes the SAB via JS_WriteObject / JS_ReadObject over the
# intra-process worker pipe; ngx_js_sab_dup is called on the receiving side.
#
# Routes:
#   /worker_creates_sab/       — Worker creates SAB{buf[0]=42}; main reads it
#   /worker_creates_sab_pair/  — Worker creates SAB{buf[0]=6, buf[1]=7};
#                                main reads buf[0]*buf[1] = 42
#   /worker_creates_writable/  — Worker creates SAB{buf[0]=21}; main receives
#                                it and doubles buf[0] in-place → 42; confirms
#                                the mapping is truly writable by main

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

# ---- Worker scripts ----

# Receives a trigger value; creates a SAB, stores the value in buf[0],
# sends the SAB back via postMessage.
$t->write_file('sab_creator.js', <<'JS');
onmessage = function(e) {
    var val = e.data;
    var sab  = new SharedArrayBuffer(16);
    var view = new Int32Array(sab);
    view[0] = val;
    postMessage(sab);
};
JS

# Receives a trigger; creates SAB with two Int32 values [a, b] where
# a = e.data[0], b = e.data[1]; sends SAB back.
$t->write_file('sab_pair_creator.js', <<'JS');
onmessage = function(e) {
    var a = e.data[0], b = e.data[1];
    var sab  = new SharedArrayBuffer(8);
    var view = new Int32Array(sab);
    view[0] = a;
    view[1] = b;
    postMessage(sab);
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_worker_creates_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /worker_creates_sab/      { }
        location /worker_creates_sab_pair/ { }
        location /worker_creates_writable/ { }
    }
}
EOF

# ---- init_worker_creates_sab.js ----

$t->write_file('init_worker_creates_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /worker_creates_sab/
     *
     * Main tells the worker "make a SAB with value 42".
     * Worker allocates SharedArrayBuffer, fills buf[0]=42, posts it back.
     * Main reads buf[0] from the received SAB.
     */
    set('/worker_creates_sab/', async function(req) {
        var sab = await new Promise(function(resolve) {
            var w = new Worker('$dir/sab_creator.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(42);
        });

        var view = new Int32Array(sab);
        req.respond(200, {'content-type': 'text/plain'}, String(view[0]));
    });

    /*
     * /worker_creates_sab_pair/
     *
     * Worker creates SAB with buf[0]=6, buf[1]=7.
     * Main receives and computes buf[0] * buf[1] = 42.
     */
    set('/worker_creates_sab_pair/', async function(req) {
        var sab = await new Promise(function(resolve) {
            var w = new Worker('$dir/sab_pair_creator.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage([6, 7]);
        });

        var view = new Int32Array(sab);
        req.respond(200, {'content-type': 'text/plain'},
                    String(view[0] * view[1]));
    });

    /*
     * /worker_creates_writable/
     *
     * Worker creates SAB with buf[0]=21; main receives it, multiplies
     * buf[0] by 2 in-place, then reads it back — confirms the SAB is
     * truly writable (not a read-only copy) after crossing the thread
     * boundary within the same process.
     */
    set('/worker_creates_writable/', async function(req) {
        var sab = await new Promise(function(resolve) {
            var w = new Worker('$dir/sab_creator.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(21);
        });

        var view = new Int32Array(sab);
        view[0] *= 2;   /* 21 → 42 */
        req.respond(200, {'content-type': 'text/plain'}, String(view[0]));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/worker_creates_sab/'),      qr/200 OK/, 'worker creates SAB: 200');
like(http_get('/worker_creates_sab/'),      qr/42/,     'worker creates SAB: main reads 42');

like(http_get('/worker_creates_sab_pair/'), qr/200 OK/, 'worker creates SAB pair: 200');
like(http_get('/worker_creates_sab_pair/'), qr/42/,     'worker creates SAB pair: main reads 6*7=42');

like(http_get('/worker_creates_writable/'), qr/200 OK/, 'worker creates writable SAB: 200');
like(http_get('/worker_creates_writable/'), qr/42/,     'worker creates writable SAB: main writes 21*2=42');
