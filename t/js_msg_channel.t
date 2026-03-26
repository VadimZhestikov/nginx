#!/usr/bin/perl

# Tests for nginx.sendToWorker() / nginx.broadcastToWorkers() +
# nginx.on('message', fn) master→worker message channel.
#
# Test strategy:
#   1. nginx starts → workerSpawned fires → master sends {slot:N, msg:'hello'}
#   2. first HTTP request activates the message channel (lazy activation)
#      → worker drains queued messages → message handler sets globalMsg
#   3. HTTP GET /get returns the stored payload as JSON

use warnings;
use strict;
use Test::More;
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/msg_channel.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /get { }
    }
}
EOF

$t->write_file('msg_channel.js', <<'JS');
// Master→worker messaging test script.

var globalMsg = null;

// nginx.on('message') is registered in master init_conf context and
// stored in master_handlers (jcf).  Workers inherit it via COW fork.
nginx.on('message', function(data) {
    globalMsg = data;
});

// When a worker spawns, master sends it a greeting with the slot index.
nginx.on('workerSpawned', function(pid, slot) {
    nginx.sendToWorker(slot, { slot: slot, msg: 'hello' });
});

// Install the /get HTTP handler on the location defined in nginx.conf.
(function() {
    var locs = nginx.http.servers[0].locations;
    var loc = locs.find(function(l) { return l.path === '/get'; });
    if (loc) {
        loc.handler = function(req) {
            if (globalMsg === null) {
                req.respond(503, {'content-type': 'text/plain'}, 'no message yet');
            } else {
                req.respond(200, {'content-type': 'application/json'},
                            JSON.stringify(globalMsg) + '\n');
            }
        };
    }
})();
JS

$t->run();

# First request activates the message channel.  The message was queued in the
# kernel socket buffer during workerSpawned, so it should be delivered on the
# first (or a subsequent) event loop iteration.  Poll briefly.
my $resp;
for my $attempt (1..20) {
    $resp = http_get('/get');
    last if $resp =~ /200/;
    select undef, undef, undef, 0.1;
}

like($resp, qr/200/, 'message delivered: HTTP 200');

my ($body) = $resp =~ /\r\n\r\n(.*)/s;
$body //= '';
$body =~ s/\s+$//;

my $obj = eval { JSON::PP->new->decode($body) };
ok(!$@, "response is valid JSON (body=$body)");
is(ref($obj), 'HASH', 'payload is an object');
like($body, qr/"msg"\s*:\s*"hello"/, 'payload contains msg:"hello"');
ok(exists $obj->{slot} && $obj->{slot} =~ /^\d+$/, 'payload has numeric slot');

like(http_get('/get'), qr/200/, 'second request returns stored message');

$t->stop();
