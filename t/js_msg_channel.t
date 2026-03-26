#!/usr/bin/perl

# Tests for bidirectional JS messaging between master and workers:
#   Phase 2: nginx.sendToWorker() / nginx.broadcastToWorkers() +
#            nginx.on('message', fn)  — master → worker
#   Phase 3: nginx.sendToMaster(data) + nginx.on('workerMessage', fn(slot,data))
#            — worker → master
#
# Phase 2 test strategy:
#   1. nginx starts → workerSpawned fires → master sends {slot:N, msg:'hello'}
#   2. first HTTP request activates the channel (already active via ngx_channel_handler)
#      → message handler sets globalMsg
#   3. HTTP GET /get returns the stored payload as JSON
#
# Phase 3 test strategy:
#   1. HTTP GET /send?msg=ping → worker calls nginx.sendToMaster({slot,msg:'ping'})
#   2. Master's workerMessage handler appends to worker_msgs.log
#   3. Test reads log and verifies content

use warnings;
use strict;
use Test::More;
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(9);
my $dir = $t->testdir();

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

        location /get  { }
        location /send { }
    }
}
EOF

$t->write_file_expand('msg_channel.js', <<'JS');
import * as std from 'std';

// Phase 2: worker → store received master message
var globalMsg = null;

nginx.on('message', function(data) {
    globalMsg = data;
});

// Phase 2: master sends greeting to each worker on spawn
nginx.on('workerSpawned', function(pid, slot) {
    nginx.sendToWorker(slot, { slot: slot, msg: 'hello' });
});

// Phase 3: master → append to log when a worker message arrives
var workerMsgLog = '%%TESTDIR%%/worker_msgs.log';

nginx.on('workerMessage', function(slot, data) {
    var f = std.open(workerMsgLog, 'a');
    if (f) {
        f.puts('slot:' + slot + ' msg:' + data.msg + '\n');
        f.close();
    }
});

// Install HTTP handlers
(function() {
    var locs = nginx.http.servers[0].locations;

    // /get — return the last message received from master
    var getLoc = locs.find(function(l) { return l.path === '/get'; });
    if (getLoc) {
        getLoc.handler = function(req) {
            if (globalMsg === null) {
                req.respond(503, {'content-type': 'text/plain'}, 'no message yet');
            } else {
                req.respond(200, {'content-type': 'application/json'},
                            JSON.stringify(globalMsg) + '\n');
            }
        };
    }

    // /send — worker sends a message to the master
    var sendLoc = locs.find(function(l) { return l.path === '/send'; });
    if (sendLoc) {
        sendLoc.handler = function(req) {
            nginx.sendToMaster({ slot: nginx.workerIdx, msg: 'ping' });
            req.respond(200, {'content-type': 'text/plain'}, 'sent\n');
        };
    }
})();
JS

$t->run();

# ---- Phase 2: master → worker ----

# First request activates the channel and drains the queued hello message.
my $resp;
for my $attempt (1..20) {
    $resp = http_get('/get');
    last if $resp =~ /200/;
    select undef, undef, undef, 0.1;
}

like($resp, qr/200/, 'Phase2: message delivered: HTTP 200');

my ($body) = $resp =~ /\r\n\r\n(.*)/s;
$body //= '';
$body =~ s/\s+$//;

my $obj = eval { JSON::PP->new->decode($body) };
ok(!$@, "Phase2: response is valid JSON (body=$body)");
is(ref($obj), 'HASH', 'Phase2: payload is an object');
like($body, qr/"msg"\s*:\s*"hello"/, 'Phase2: payload contains msg:"hello"');
ok(exists $obj->{slot} && $obj->{slot} =~ /^\d+$/, 'Phase2: payload has numeric slot');

like(http_get('/get'), qr/200/, 'Phase2: second request returns stored message');

# ---- Phase 3: worker → master ----

# Send message from worker to master
like(http_get('/send'), qr/200/, 'Phase3: /send returned 200');

# Give master time to process the SIGIO and write to the log
select undef, undef, undef, 0.3;

sub wlog_read {
    my $path = "$dir/worker_msgs.log";
    return '' unless -f $path;
    open my $fh, '<', $path or return '';
    local $/;
    return <$fh> // '';
}

my $wlog = wlog_read();
like($wlog, qr/msg:ping/, 'Phase3: workerMessage received by master');
like($wlog, qr/slot:\d+/, 'Phase3: slot field present in master log');

$t->stop();
