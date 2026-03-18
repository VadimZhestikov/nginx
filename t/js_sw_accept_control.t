#!/usr/bin/perl

# Tests for the suspend-all → SharedWorker broadcast → per-worker resume pattern.
#
# Flow:
#   1. /pre_connect/  — each worker connects to the coordinator SW
#      synchronously and installs a persistent sw.onmessage handler that
#      calls nginx.resumeAcceptance() when it receives {cmd:'resume'}.
#      This handler survives after the request ends (JS_DupValue keeps it
#      alive in ws->on_message until overwritten or exit_process).
#
#      Must be non-async: the static SW only fires onconnect on the very
#      first activation per worker; subsequent calls reuse the existing
#      channel silently and would leave an async Promise hanging.
#
#   2. /sw_suspend_resume/  — the requesting worker:
#      a. calls nginx.suspendAllWorkers()   (coordinated suspend via manager)
#      b. sets sw.onmessage to resolve-on-resume (overwrites persistent handler
#         for this worker; still calls nginx.resumeAcceptance() itself)
#      c. sends {cmd:'broadcast_resume'} to the coordinator SW
#      d. the SW sends {cmd:'resume'} to all connected workers
#      e. requesting worker's onmessage fires → resumeAcceptance + resolve
#      f. other worker's persistent onmessage fires → resumeAcceptance
#
#   3. /check/  — verifies the server is still accepting connections.
#
# worker_processes 2: both workers pre-connect via /pre_connect/ so both
# are in the SW's ports[] and both receive the broadcast resume.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/sw_ac_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /pre_connect/       { }
        location /sw_suspend_resume/ { }
        location /check/             { }
    }
}
EOF

# Coordinator SharedWorker: keeps a list of all connected worker ports.
# On {cmd:'broadcast_resume'} it sends {cmd:'resume'} to every port.
$t->write_file('coord_sw.js', <<'JS');
var ports = [];

onconnect = function(e) {
    var port = e.ports[0];
    ports.push(port);

    port.onmessage = function(msg) {
        if (msg.data.cmd === 'broadcast_resume') {
            for (var i = 0; i < ports.length; i++) {
                ports[i].postMessage({cmd: 'resume'});
            }
        }
    };
};
JS

$t->write_file('sw_ac_init.js', <<'JS');
(function() {
    var prefix = nginx.cycle.prefix;
    var swUrl  = prefix + 'coord_sw.js';
    var locs   = nginx.http.servers[0].locations;

    function set(path, fn) {
        var loc = locs.find(function(l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /*
     * Synchronous pre-connect: activates this worker's SW channel and
     * installs a persistent per-worker resume handler.
     *
     * sw.onmessage is stored in ws->on_message via JS_DupValue and
     * survives after the request ends, so the SW can trigger it later
     * without an active request on this worker.
     *
     * Must be non-async: the static SW fires onconnect only on the very
     * first activation per worker; subsequent calls reuse the channel
     * silently and would leave an async Promise hanging forever.
     */
    set('/pre_connect/', function(req) {
        var sw = new SharedWorker(swUrl);

        sw.onmessage = function(e) {
            if (e.data.cmd === 'resume') {
                nginx.resumeAcceptance();
            }
        };

        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    /*
     * Main test route:
     *  1. suspend all workers (coordinated via manager + bcast protocol)
     *  2. ask the SW to broadcast {cmd:'resume'} to every connected worker
     *  3. this worker's onmessage receives 'resume', calls resumeAcceptance()
     *  4. every other worker's persistent onmessage also fires independently
     *  5. respond once acceptance is restored on this worker
     */
    set('/sw_suspend_resume/', async function(req) {
        await nginx.suspendAllWorkers();

        var sw = new SharedWorker(swUrl);

        await new Promise(function(resolve) {
            sw.onmessage = function(e) {
                if (e.data.cmd === 'resume') {
                    nginx.resumeAcceptance();
                    resolve();
                }
            };
            sw.postMessage({cmd: 'broadcast_resume'});
        });

        req.respond(200, {'content-type': 'text/plain'}, 'sw_resumed');
    });

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'check ok');
    });
})();
JS

$t->run();

# Pre-connect both workers to the coordinator SW (synchronous, non-blocking).
# Four requests ensure each worker connects at least twice; idempotent since
# subsequent SharedWorker() calls reuse the same channel silently.
http_get('/pre_connect/');
http_get('/pre_connect/');
http_get('/pre_connect/');
http_get('/pre_connect/');

# Main scenario: suspend all → SW broadcast → each worker resumes via onmessage
like(http_get('/sw_suspend_resume/'), qr/200 OK/,    'sw-coord resume: 200 OK');
like(http_get('/sw_suspend_resume/'), qr/sw_resumed/, 'sw-coord resume: body ok');

# Server still accepts after the coordinated suspend/SW-broadcast/resume cycle
like(http_get('/check/'),             qr/200 OK/,    'server accepts after sw-coord resume');
like(http_get('/check/'),             qr/check ok/,  'check body ok');

# Second cycle: ensure the mechanism is repeatable
like(http_get('/sw_suspend_resume/'), qr/200 OK/,    'second cycle: 200 OK');
like(http_get('/sw_suspend_resume/'), qr/sw_resumed/, 'second cycle: body ok');
like(http_get('/check/'),             qr/200 OK/,    'second cycle: server accepts');
like(http_get('/check/'),             qr/check ok/,  'second cycle: check body ok');
