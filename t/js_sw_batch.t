#!/usr/bin/perl

# Tests for the SharedWorker atomic batch-update pattern.
#
# A JS handler may need to update several related config fields (e.g.,
# rate + burst + nodelay) at once.  Sending them as three separate
# postMessage calls exposes a window in which another worker can read an
# inconsistent intermediate state.
#
# The batch pattern bundles all ops into a single postMessage:
#
#   sw.postMessage({ type: 'batch', ops: [
#       { key: 'rate',  value: 2 },
#       { key: 'burst', value: 2 },
#   ]});
#
# The C channel delivers each postMessage as one atomic sendmsg/recvmsg,
# so the SW thread applies all ops before any other message can arrive.
#
# Routes:
#
#   /batch/read   — returns current SW state as "rate:burst"
#   /batch/set    — sends a batch of ops and returns new "rate:burst"
#   /batch/reset  — resets to initial values via batch and returns "rate:burst"

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
# Maintains a small state object {rate, burst}.
# Supports two message types:
#   { type: 'batch', ops: [{key, value}, ...] }
#       Apply all ops atomically, then reply with the new state.
#   { type: 'read' }
#       Reply with the current state without changing it.

$t->write_file('batch_sw.js', <<'JS');
var state = { rate: 20, burst: 10 };
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        var msg = ev.data;
        if (msg && msg.type === 'batch') {
            msg.ops.forEach(function(op) { state[op.key] = op.value; });
            port.postMessage({ rate: state.rate, burst: state.burst });
        } else if (msg && msg.type === 'read') {
            port.postMessage({ rate: state.rate, burst: state.burst });
        }
    };
};
JS

# ---- nginx.conf ----

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_sw_batch.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /batch/read  { }
        location /batch/set   { }
        location /batch/reset { }
    }
}
EOF

# ---- init_sw_batch.js ----

$t->write_file('init_sw_batch.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;
    var sw = new SharedWorker('$dir/batch_sw.js');

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /* /batch/read — query SW state without modifying it */
    set('/batch/read', async function(req) {
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({ type: 'read' });
        });
        req.respond(200, {'content-type': 'text/plain'},
                    result.rate + ':' + result.burst);
    });

    /* /batch/set — apply {rate:2, burst:2} in one atomic message */
    set('/batch/set', async function(req) {
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({
                type: 'batch',
                ops: [
                    { key: 'rate',  value: 2 },
                    { key: 'burst', value: 2 }
                ]
            });
        });
        req.respond(200, {'content-type': 'text/plain'},
                    result.rate + ':' + result.burst);
    });

    /* /batch/reset — restore initial values in one atomic message */
    set('/batch/reset', async function(req) {
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage({
                type: 'batch',
                ops: [
                    { key: 'rate',  value: 20 },
                    { key: 'burst', value: 10 }
                ]
            });
        });
        req.respond(200, {'content-type': 'text/plain'},
                    result.rate + ':' + result.burst);
    });
})();
JS

$t->try_run('no js module')->plan(8);

# ---- HTTP assertions ----

# Initial state
like(http_get('/batch/read'),  qr/200 OK/, 'batch: read initial state responds 200');
like(http_get('/batch/read'),  qr/20:10/,  'batch: initial state is rate=20 burst=10');

# Apply batch — both fields change together
like(http_get('/batch/set'),   qr/200 OK/, 'batch: set responds 200');
like(http_get('/batch/set'),   qr/2:2/,    'batch: rate and burst both changed to 2');

# State persists across requests
like(http_get('/batch/read'),  qr/2:2/,    'batch: state persists after batch update');

# Reset via batch — both fields restored together
like(http_get('/batch/reset'), qr/200 OK/, 'batch: reset responds 200');
like(http_get('/batch/reset'), qr/20:10/,  'batch: rate and burst both restored to 20:10');

# State persists after reset
like(http_get('/batch/read'),  qr/20:10/,  'batch: state persists after batch reset');
