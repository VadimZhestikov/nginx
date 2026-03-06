#!/usr/bin/perl

# Tests for SharedArrayBuffer created inside a SharedWorker thread and
# sent back to an nginx worker process.
#
# The SW thread is post-fork master; a plain MAP_SHARED|MAP_ANONYMOUS
# allocation there would NOT be visible to the worker.  ngx_js_sw_thread_active
# routes SW-thread SAB allocations through memfd so the fd is passed via
# SCM_RIGHTS and the worker maps the same physical pages.
#
# Routes:
#   /sw_creates_sab/   — SW creates SAB{42}, sends to worker; worker echoes
#   /sw_creates_sab2/  — same flow, confirms independent request works

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
# On each connection: when the worker sends a trigger message (any value),
# the SW creates a new SAB, writes a known value into it, and posts it back.

$t->write_file('sw_make_sab.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        /* Create a SAB in the SW thread and send it to the worker. */
        var sab  = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0] = msg.data;   /* echo the requested value */
        port.postMessage(sab);
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_sw_sab_to_worker.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /sw_creates_sab/  { }
        location /sw_creates_sab2/ { }
    }
}
EOF

# ---- init_sw_sab_to_worker.js ----

$t->write_file('init_sw_sab_to_worker.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * Worker sends a value (42 / 99) to the SW; the SW allocates a fresh
     * SAB, stores the value in buf[0], and posts the SAB back.
     * Worker reads buf[0] from the received SAB and responds.
     */
    function makeHandler(val) {
        return async function(req) {
            var sw = new SharedWorker('$dir/sw_make_sab.js');

            var result = await new Promise(function(resolve) {
                sw.onmessage = function(e) {
                    var view = new Int32Array(e.data);
                    resolve(view[0]);
                };
                sw.postMessage(val);   /* trigger: SW will create SAB */
            });

            req.respond(200, {'content-type': 'text/plain'}, String(result));
        };
    }

    set('/sw_creates_sab/',  makeHandler(42));
    set('/sw_creates_sab2/', makeHandler(99));
})();
JS

$t->try_run('no js module')->plan(6);

# ---- HTTP assertions ----

like(http_get('/sw_creates_sab/'),  qr/200 OK/, 'SW-created SAB: 200');
like(http_get('/sw_creates_sab/'),  qr/42/,     'SW-created SAB: worker reads 42');
like(http_get('/sw_creates_sab/'),  qr/42/,     'SW-created SAB: repeated request');

like(http_get('/sw_creates_sab2/'), qr/200 OK/, 'SW-created SAB (99): 200');
like(http_get('/sw_creates_sab2/'), qr/99/,     'SW-created SAB (99): worker reads 99');
like(http_get('/sw_creates_sab2/'), qr/99/,     'SW-created SAB (99): repeated request');
