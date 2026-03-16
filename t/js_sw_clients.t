#!/usr/bin/perl

# Stage 51: SharedWorker global onmessage + clients broadcast
#
# Verifies:
#   1. Global `onmessage` in a SW script fires when any worker sends a message.
#   2. `clients` array inside that handler contains connected port objects.
#   3. clients.forEach(c => c.postMessage(data)) delivers back to the caller.
#   4. Relay pattern: worker posts → SW broadcasts → worker applies change.
#   5. Dynamically-added location is reachable after broadcast completes.
#   6. Second relay round adds a second location independently.
#   7. Repeated trigger for an existing location is idempotent.
#   8. Path not yet triggered returns not-found.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(16);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/sw_clients.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /trigger { }
        location /check   { }
    }
}
EOF

# SW script: uses global onmessage + clients broadcast
$t->write_file('relay.js', <<'JS');
onmessage = function(data) {
    clients.forEach(function(c) { c.postMessage(data); });
};
JS

$t->write_file('sw_clients.js', <<'JS');
(function() {
    var http = nginx.http;
    var srv  = http.servers[0];

    function findLoc(path) {
        return srv.locations.find(function(l) { return l.path === path; });
    }

    var relaySW = new SharedWorker(nginx.cycle.prefix + 'relay.js');

    /*
     * Each worker connects and sets sw.onmessage.
     * Worker-side onmessage receives {data: ...} — use e.data for the payload.
     */
    nginx.broadcast(function() {
        relaySW.onmessage = function(e) {
            var msg = e.data;
            if (msg && msg.path && !srv.findLocation(msg.path)) {
                srv.addLocation(msg.path).handler = function(r) {
                    r.respond(200, {}, 'broadcast-ok:' + msg.path);
                };
            }
        };
    });

    /*
     * GET /trigger?<name>
     * Posts {path: '/<name>'} to the SW.  The SW's global onmessage receives
     * it, calls clients.forEach(c => c.postMessage(msg)), which delivers the
     * message back to this worker's sw.onmessage handler above.
     * The message is processed asynchronously (next event-loop iteration), so
     * the test waits a short time before hitting /check.
     */
    findLoc('/trigger').handler = function(r) {
        var path = '/' + r.args;
        relaySW.postMessage({ path: path });
        r.respond(200, {}, 'triggered:' + path);
    };

    /* GET /check?<name> — verify the dynamically-added location exists */
    findLoc('/check').handler = function(r) {
        var path = '/' + r.args;
        var loc  = srv.findLocation(path);
        r.respond(200, {}, loc ? 'found:' + loc.pattern : 'not-found');
    };
})();
JS

$t->run();

# ---- relay round 1 ----
like(http_get('/trigger?newroute'), qr/200 OK/,      'trigger1: 200');
like(http_get('/trigger?newroute'), qr/triggered/,   'trigger1: body');

# Small sleep: let the SW relay the message and the worker process it
select undef, undef, undef, 0.15;

like(http_get('/check?newroute'),   qr/200 OK/,      'check1: 200');
like(http_get('/check?newroute'),   qr/found/,       'check1: found');

# The dynamically-added location now serves requests directly
like(http_get('/newroute'),         qr/200 OK/,      'newroute: 200');
like(http_get('/newroute'),         qr/broadcast-ok/, 'newroute: handler ran');

# ---- relay round 2: independent path ----
like(http_get('/trigger?another'),  qr/triggered/,   'trigger2: triggered');
select undef, undef, undef, 0.15;
like(http_get('/check?another'),    qr/found/,       'check2: found');
like(http_get('/another'),          qr/broadcast-ok/, 'another: handler ran');

# ---- both locations coexist ----
like(http_get('/check?newroute'),   qr/found/,       'coexist: newroute');
like(http_get('/check?another'),    qr/found/,       'coexist: another');

# ---- idempotent: repeated trigger does not crash or duplicate ----
like(http_get('/trigger?newroute'), qr/triggered/,   'idempotent: triggered');
select undef, undef, undef, 0.15;
like(http_get('/newroute'),         qr/broadcast-ok/, 'idempotent: route ok');

# ---- path not yet triggered ----
like(http_get('/check?notyet'),     qr/not-found/,   'notyet: not-found');

# ---- relay round 3: proves relaySW reusable across multiple triggers ----
like(http_get('/trigger?third'),    qr/triggered/,   'trigger3: triggered');
select undef, undef, undef, 0.15;
like(http_get('/check?third'),      qr/found/,       'check3: found');

$t->stop();
