#!/usr/bin/perl

# Tests for admin.js core: base-state capture, state(), applySnapshot().
#
# Config-phase checks (via error log):
#   ups_found          — upstream "backend" is visible via COM
#   base_peers_count   — _captureBaseState sees 2 peers
#   base_weight_0/1    — captured weights are 1 (from nginx.conf)
#   state_not_null     — state() returns an object
#   state_peers_empty  — state().peers is empty at startup (no mutations yet)
#   state_delta_found  — state() reports a delta after weight change + reset
#
# Request-phase checks (via HTTP):
#   /apply_snap        — POST: admin.applySnapshot('test-snap') sets weight to 5
#   /get_weight        — GET:  peer[0].weight returns 5 after apply

use warnings;
use strict;
use Test::More;
use File::Spec;
use Cwd qw(abs_path);
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# Path to admin.js (sibling conf/ directory)
my $admin_js = abs_path(
    File::Spec->catfile($FindBin::Bin, '..', 'conf', 'admin.js')
);

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_source $admin_js;
js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /apply_snap { }
        location /get_weight { }
    }
}
EOF

# Create snapshots/ directory and a pre-seeded snapshot for request-phase test
mkdir $t->testdir() . '/snapshots';

# Resolve the actual port that %%PORT_8091%% was allocated to.
# Test::Nginx may assign a different port in the same 8000-8499 range.
my $peer_port = port(8091);

my $snap_id = 'test-snap';
$t->write_file('snapshots/test-snap.json', encode_json({
    id       => $snap_id,
    ts       => 0,
    peers    => [
        { upstream => 'backend', address => "127.0.0.1:$peer_port",
          weight => 5, down => 0 }
    ],
    handlers => [],
}));

$t->write_file('init.js', <<'JS');
function pass(n) { nginx.log(6, 'JSTEST PASS ' + n); }
function fail(n, got) { nginx.log(6, 'JSTEST FAIL ' + n + ': ' + got); }
function check(n, ok, got) { ok ? pass(n) : fail(n, String(got)); }

/* ---- config-phase: base state ---- */
var ups = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });
check('ups_found', !!ups);

var peers = ups ? ups.peers : [];
check('base_peers_count', peers.length === 2, peers.length);
check('base_weight_0', peers.length > 0 && peers[0].weight === 1,
      peers.length > 0 ? peers[0].weight : 'no peer');
check('base_weight_1', peers.length > 1 && peers[1].weight === 1,
      peers.length > 1 ? peers[1].weight : 'no peer');

var st = nginx.admin.state();
check('state_not_null', st !== null && st !== undefined);
check('state_peers_empty', Array.isArray(st.peers) && st.peers.length === 0,
      JSON.stringify(st ? st.peers : null));

/* mutate a peer, check delta, restore */
if (peers.length > 0) {
    peers[0].weight = 7;
    var delta = nginx.admin.state();
    check('state_delta_found', delta.peers.length === 1, delta.peers.length);
    peers[0].weight = 1;   /* restore for request-phase tests */
}

/* ---- request-phase handlers ---- */
var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === '/apply_snap'; }).handler =
    function (req) {
        try {
            nginx.admin.applySnapshot('test-snap');
            req.respond(200, {'Content-Type': 'text/plain'}, 'ok');
        } catch (e) {
            req.respond(500, {}, String(e));
        }
    };

locs.find(function (l) { return l.path === '/get_weight'; }).handler =
    function (req) {
        var u   = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });
        var w   = u ? u.peers[0].weight : -1;
        req.respond(200, {'Content-Type': 'text/plain'}, String(w));
    };
JS

$t->try_run('no js module')->plan(9);

# --- config-phase assertions (error log) ---
my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ups_found/,         'upstream backend found');
like($log, qr/JSTEST PASS base_peers_count/,  'base state has 2 peers');
like($log, qr/JSTEST PASS base_weight_0/,     'base weight[0] == 1');
like($log, qr/JSTEST PASS base_weight_1/,     'base weight[1] == 1');
like($log, qr/JSTEST PASS state_not_null/,    'state() returns an object');
like($log, qr/JSTEST PASS state_peers_empty/, 'state().peers empty at startup');
like($log, qr/JSTEST PASS state_delta_found/, 'state() detects weight delta');

# --- request-phase: applySnapshot changes peer weight ---
my $r_apply = http("POST /apply_snap HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_apply, qr/200/, 'applySnapshot returns 200');

my $r_weight = http_get('/get_weight');
like($r_weight, qr/\b5\b/, 'peer weight is 5 after applySnapshot');

$t->stop();
