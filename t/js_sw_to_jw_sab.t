#!/usr/bin/perl

# Tests for SharedArrayBuffer created inside a SharedWorker thread and
# sent to a JS Worker thread (reverse of js_worker_to_sw_sab.t).
#
# The SW thread lives in the master process; the JS Worker thread lives in
# the nginx worker process.  SABs in this direction (SW→Worker) use the
# memfd path: the SW allocates via memfd_create, carries the fd via
# SCM_RIGHTS, and the Worker thread mmap's it to a local VA.
#
# Routes:
#   /sw_to_jw/        — JS Worker creates SW, sends 'create' trigger;
#                       SW creates SAB{42}, sends to Worker; Worker reads buf[0]
#   /sw_to_jw_mutate/ — Worker receives SAB, writes 77 to buf[0] via its own
#                       mapping, then sends 'read' to SW; SW reads buf[0]
#                       from its retained mapping → must see 77 (true shared mem)

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
# 'create' → allocate SAB{42} (memfd-backed in the SW/master thread),
#             store a reference, and send the SAB to the Worker.
# 'read'   → read buf[0] from the retained SAB and echo the value.
# This lets us verify that the Worker's mutation is visible via the SW's
# own mapping of the same physical pages.

$t->write_file('sw_jw_sab.js', <<'JS');
onconnect = function(e) {
    var port       = e.ports[0];
    var storedSab  = null;
    port.onmessage = function(msg) {
        if (msg.data === 'create') {
            storedSab    = new SharedArrayBuffer(16);
            var view     = new Int32Array(storedSab);
            view[0]      = 42;
            port.postMessage(storedSab);   /* send to Worker */
        } else if (msg.data === 'read') {
            var view = new Int32Array(storedSab);
            port.postMessage(view[0]);     /* echo current value */
        }
    };
};
JS

# ---- JS Worker scripts ----

# Sends 'create' to SW, receives SAB, reads buf[0], posts to main.
$t->write_file('jw_sw_to_jw.js', <<"JS");
onmessage = function(e) {
    var sw = new SharedWorker('$dir/sw_jw_sab.js');
    sw.onmessage = function(reply) {
        var view = new Int32Array(reply.data);
        postMessage(view[0]);
    };
    sw.postMessage('create');
};
JS

# Sends 'create', receives SAB, writes 77 to buf[0] via Worker's mapping,
# then sends 'read' to SW to confirm SW sees 77 (same physical pages).
$t->write_file('jw_sw_mutate.js', <<"JS");
onmessage = function(e) {
    var sw   = new SharedWorker('$dir/sw_jw_sab.js');
    var step = 0;
    sw.onmessage = function(reply) {
        if (step === 0) {
            /* First reply: received SAB from SW.  Mutate it. */
            step = 1;
            var view = new Int32Array(reply.data);
            view[0]  = 77;
            /* Ask SW to read buf[0] from its own retained mapping */
            sw.postMessage('read');
        } else {
            /* Second reply: SW echoed buf[0] via its own mapping */
            postMessage(reply.data);
        }
    };
    sw.postMessage('create');
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_to_jw_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_to_jw/        { }
        location /sw_to_jw_mutate/ { }
    }
}
EOF

# ---- init_sw_to_jw_sab.js ----

$t->write_file('init_sw_to_jw_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /sw_to_jw/
     *
     * Main spawns a JS Worker; Worker connects to the SW and sends 'create'.
     * SW allocates a new SAB{42} (memfd-backed, since SW lives in master)
     * and sends it to the Worker.  Worker reads buf[0] and posts the value.
     * Confirms the SW→Worker SAB direction works.
     */
    set('/sw_to_jw/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sw_to_jw.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(null);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });

    /*
     * /sw_to_jw_mutate/
     *
     * Main spawns a JS Worker.  Worker receives SAB from SW, then writes
     * 77 to buf[0] via its Worker-process mmap.  Worker sends 'read' to
     * SW; SW reads buf[0] from its own retained mmap of the same memfd.
     * SW must report 77 — confirming true shared memory between master
     * (SW thread) and worker process (JS Worker thread).
     */
    set('/sw_to_jw_mutate/', async function(req) {
        var result = await new Promise(function(resolve) {
            var w = new Worker('$dir/jw_sw_mutate.js');
            w.onmessage = function(e) { w.terminate(); resolve(e.data); };
            w.postMessage(null);
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/sw_to_jw/'),        qr/200 OK/, 'SW→JsWorker SAB: 200');
like(http_get('/sw_to_jw/'),        qr/42/,     'SW→JsWorker SAB: Worker reads 42');
like(http_get('/sw_to_jw/'),        qr/42/,     'SW→JsWorker SAB: repeated request');

like(http_get('/sw_to_jw_mutate/'), qr/200 OK/, 'SW→JsWorker mutate: 200');
like(http_get('/sw_to_jw_mutate/'), qr/77/,     'SW→JsWorker mutate: SW sees 77 after Worker write');
like(http_get('/sw_to_jw_mutate/'), qr/77/,     'SW→JsWorker mutate: repeated request');
