#!/usr/bin/perl

# Tests for Phase 3 — live RR peer objects and upstream mutation:
#   peers[] returns NginxRRPeer (has .conns) after postconfiguration
#   peer.weight / peer.maxFails / peer.down setters (with wlock)
#   addPeer(addr, opts)   — add primary or backup peer
#   removePeer(addr)      — remove peer, throws for non-existent addr

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%% backup;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location / { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

const ups = nginx.http.upstreams[0];

// --- Phase 3: peers[] returns NginxRRPeer objects (have .conns) ---

const peers = ups.peers;
check("peers_is_array",    Array.isArray(peers));
check("peers_two_initial", peers.length === 2, peers.length);

// NginxRRPeer has .conns (runtime stat); NginxPeer (fallback) does not
const p0 = peers.find(pp => !pp.backup);
const p1 = peers.find(pp =>  pp.backup);

check("p0_conns_zero",  p0.conns === 0,    p0.conns);
check("p1_backup_true", p1.backup === true, p1.backup);

// --- weight setter (wlock) ---
const origWeight = p0.weight;
p0.weight = 9;
check("rr_weight_set", p0.weight === 9, p0.weight);
p0.weight = origWeight;

// --- maxFails setter ---
const origMF = p0.maxFails;
p0.maxFails = 3;
check("rr_maxfails_set", p0.maxFails === 3, p0.maxFails);
p0.maxFails = origMF;

// --- down setter (also adjusts peers->tries) ---
p0.down = true;
check("rr_down_true",  p0.down === true,  p0.down);
p0.down = false;
check("rr_down_false", p0.down === false, p0.down);

// --- addPeer: primary ---
ups.addPeer("127.0.0.3:9000");
const afterAdd = ups.peers;
check("add_primary_count",  afterAdd.length === 3,
      afterAdd.length);
check("added_peer_present",
      afterAdd.some(pp => !pp.backup && pp.address.startsWith("127.0.0.3")),
      JSON.stringify(afterAdd.map(pp => pp.address)));

// --- addPeer: backup ---
ups.addPeer("127.0.0.4:9000", { backup: true });
const afterBackup = ups.peers;
check("add_backup_count", afterBackup.length === 4, afterBackup.length);
check("added_backup_present",
      afterBackup.some(pp => pp.backup && pp.address.startsWith("127.0.0.4")),
      JSON.stringify(afterBackup.map(pp => pp.address)));

// --- removePeer: existing ---
ups.removePeer("127.0.0.3:9000");
const afterRemove = ups.peers;
check("remove_count",   afterRemove.length === 3, afterRemove.length);
check("removed_absent",
      !afterRemove.some(pp => pp.address.startsWith("127.0.0.3")),
      JSON.stringify(afterRemove.map(pp => pp.address)));

// --- removePeer: non-existent throws ---
let threwForMissing = false;
try {
    ups.removePeer("127.0.0.99:9000");
} catch (e) {
    threwForMissing = true;
}
check("remove_not_found_throws", threwForMissing);
JS

$t->try_run('no js module')->plan(15);

# --- Error-log assertions (all config-phase) ---

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS peers_is_array/,           'peers is array');
like($log, qr/JSTEST PASS peers_two_initial/,        'initial peers count is 2');
like($log, qr/JSTEST PASS p0_conns_zero/,            'p0.conns === 0 (NginxRRPeer)');
like($log, qr/JSTEST PASS p1_backup_true/,           'p1.backup === true');
like($log, qr/JSTEST PASS rr_weight_set/,            'peer.weight setter works under wlock');
like($log, qr/JSTEST PASS rr_maxfails_set/,          'peer.maxFails setter works under wlock');
like($log, qr/JSTEST PASS rr_down_true/,             'peer.down = true');
like($log, qr/JSTEST PASS rr_down_false/,            'peer.down = false');
like($log, qr/JSTEST PASS add_primary_count/,        'addPeer increases primary count to 3');
like($log, qr/JSTEST PASS added_peer_present/,       'added primary peer visible in peers[]');
like($log, qr/JSTEST PASS add_backup_count/,         'addPeer backup increases total to 4');
like($log, qr/JSTEST PASS added_backup_present/,     'added backup peer has backup=true');
like($log, qr/JSTEST PASS remove_count/,             'removePeer decreases count to 3');
like($log, qr/JSTEST PASS removed_absent/,           'removed peer absent from peers[]');
like($log, qr/JSTEST PASS remove_not_found_throws/,  'removePeer throws for nonexistent peer');
