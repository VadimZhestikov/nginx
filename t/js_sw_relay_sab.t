#!/usr/bin/perl

# Tests for cross-process SAB relay via a SharedWorker.
#
# A SharedWorker acts as a shared-memory hub: one nginx worker sends a SAB
# to the SW which stores it; another request (potentially a different worker)
# retrieves it.  Because the SAB is memfd-backed, physical pages are the
# same in both worker processes and in the master SW thread — mutations in
# one process are immediately visible to the others.
#
# Routes:
#   /relay_store/    — worker creates SAB{42}, sends to SW (cmd='store'),
#                      waits for acknowledgment
#   /relay_fetch/    — worker asks SW to return the stored SAB (cmd='fetch'),
#                      reads buf[0]; expects 42
#   /relay_read/     — worker asks SW to report buf[0] of the stored SAB
#                      without sending it back (cmd='read')
#   /relay_mutate/   — worker stores SAB, writes 99 to it *after* sending,
#                      then asks SW to report buf[0] → must be 99 (shared mem)

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
# Global 'relay' holds the last stored SAB across all connections.
# Commands:
#   {cmd:'store', sab:<sab>}  → stores sab globally, replies 'stored'
#   {cmd:'fetch'}             → sends the stored SAB back to caller
#   {cmd:'read'}              → reads buf[0] from stored SAB, replies value

$t->write_file('sw_relay.js', <<'JS');
var relay = null;

onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var cmd = msg.data.cmd;
        if (cmd === 'store') {
            relay = msg.data.sab;
            port.postMessage('stored');
        } else if (cmd === 'fetch') {
            port.postMessage(relay);
        } else if (cmd === 'read') {
            var view = new Int32Array(relay);
            port.postMessage(view[0]);
        }
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_relay_sab.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /relay_store/   { }
        location /relay_fetch/   { }
        location /relay_read/    { }
        location /relay_mutate/  { }
    }
}
EOF

# ---- init_sw_relay_sab.js ----

$t->write_file('init_sw_relay_sab.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * /relay_store/
     *
     * Worker creates SAB{42} (post-fork, memfd-backed), sends it to the SW
     * with cmd='store'.  SW stores it globally and acks with 'stored'.
     * Worker responds with the ack string.
     */
    set('/relay_store/', async function(req) {
        var sw   = new SharedWorker('$dir/sw_relay.js');
        var sab  = new SharedArrayBuffer(8);
        var view = new Int32Array(sab);
        view[0]  = 42;

        var ack = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({cmd: 'store', sab: sab});
        });

        req.respond(200, {'content-type': 'text/plain'}, String(ack));
    });

    /*
     * /relay_fetch/
     *
     * Worker asks SW to send back the stored SAB (cmd='fetch').
     * Worker receives the SAB — different VA but same physical pages as the
     * one stored by /relay_store/ — and reads buf[0].
     * Expects 42 (the value stored by a previous /relay_store/ request).
     */
    set('/relay_fetch/', async function(req) {
        var sw = new SharedWorker('$dir/sw_relay.js');

        var sab = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({cmd: 'fetch'});
        });

        var view = new Int32Array(sab);
        req.respond(200, {'content-type': 'text/plain'}, String(view[0]));
    });

    /*
     * /relay_read/
     *
     * Worker asks SW to read buf[0] from its own stored-SAB mapping and
     * report the value directly (no SAB transfer back).
     */
    set('/relay_read/', async function(req) {
        var sw = new SharedWorker('$dir/sw_relay.js');

        var val = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({cmd: 'read'});
        });

        req.respond(200, {'content-type': 'text/plain'}, String(val));
    });

    /*
     * /relay_mutate/
     *
     * Worker creates SAB{42}, sends to SW (store), then writes 99 to the
     * SAB *after* the send.  Since the SAB is backed by the same memfd
     * pages in both worker and master, the SW's stored reference reflects
     * the mutation immediately.  Worker then asks SW to read buf[0] and
     * expects 99.
     */
    set('/relay_mutate/', async function(req) {
        var sw   = new SharedWorker('$dir/sw_relay.js');
        var sab  = new SharedArrayBuffer(8);
        var view = new Int32Array(sab);
        view[0]  = 42;

        /* Store the SAB in the SW; wait for ack before mutating */
        await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({cmd: 'store', sab: sab});
        });

        /* Mutate via worker-process mapping */
        view[0] = 99;

        /* SW reads its own mapping — must see 99 */
        var val = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({cmd: 'read'});
        });

        req.respond(200, {'content-type': 'text/plain'}, String(val));
    });
})();
JS

$t->try_run('no js module')->plan(10);

# ---- HTTP assertions ----

# Seed the relay with a known SAB.
like(http_get('/relay_store/'),   qr/200 OK/, 'relay store: 200');
like(http_get('/relay_store/'),   qr/stored/,  'relay store: SW acks');

# Fetch the stored SAB and read from it — different request, same physical pages.
like(http_get('/relay_fetch/'),   qr/200 OK/, 'relay fetch: 200');
like(http_get('/relay_fetch/'),   qr/42/,     'relay fetch: worker reads 42 from SW SAB');

# SW reads via its own mapping (no SAB transfer).
like(http_get('/relay_read/'),    qr/200 OK/, 'relay read: 200');
like(http_get('/relay_read/'),    qr/42/,     'relay read: SW reports 42');

# Mutation test: worker writes *after* sending to SW; SW must see the new value.
like(http_get('/relay_mutate/'),  qr/200 OK/, 'relay mutate: 200');
like(http_get('/relay_mutate/'),  qr/99/,     'relay mutate: SW sees post-send mutation (99)');

# Confirm relay_fetch still returns the last stored SAB (from relay_mutate above).
like(http_get('/relay_read/'),    qr/99/,     'relay read after mutate: SW sees 99');
like(http_get('/relay_fetch/'),   qr/99/,     'relay fetch after mutate: worker reads 99');
