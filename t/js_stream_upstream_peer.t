#!/usr/bin/perl

# Tests for nginx.stream.upstreams[i].addPeer() / removePeer() —
# Stream upstream peer management (Stage A).
#
# All tests run at config-phase (js_source evaluated by init_conf).
# Assertions logged to error.log as "JSTEST PASS/FAIL <name>".

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location / { }
    }
}

stream {
    upstream sback {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%% backup;
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

const ups = nginx.stream.upstreams[0];

// --- initial state ---
check("ups_name",     ups.name === "sback", ups.name);

const peers0 = ups.peers;
check("initial_count",  peers0.length === 2, peers0.length);
check("has_primary",    peers0.some(p => !p.backup));
check("has_backup",     peers0.some(p =>  p.backup));

// --- addPeer: primary, default options ---
ups.addPeer("127.0.0.3:9000");
const after_add = ups.peers;
check("add_primary_count",
      after_add.length === 3, after_add.length);
check("added_primary_present",
      after_add.some(p => !p.backup && p.address.startsWith("127.0.0.3")),
      JSON.stringify(after_add.map(p => p.address)));

// --- addPeer: primary with weight ---
ups.addPeer("127.0.0.5:9000", { weight: 3, maxFails: 2, failTimeout: 20 });
const after_opts = ups.peers;
const p5 = after_opts.find(p => p.address.startsWith("127.0.0.5"));
check("add_with_opts_present", p5 !== undefined);
check("add_weight_set",        p5 && p5.weight === 3, p5 && p5.weight);
check("add_maxfails_set",      p5 && p5.maxFails === 2, p5 && p5.maxFails);

// --- addPeer: backup ---
ups.addPeer("127.0.0.4:9000", { backup: true });
const after_backup = ups.peers;
check("add_backup_count",
      after_backup.length === 5, after_backup.length);
check("added_backup_present",
      after_backup.some(p => p.backup && p.address.startsWith("127.0.0.4")),
      JSON.stringify(after_backup.map(p => p.address)));

// --- addPeer: down flag ---
ups.addPeer("127.0.0.6:9000", { down: true });
const p6 = ups.peers.find(p => p.address.startsWith("127.0.0.6"));
check("add_down_flag", p6 && p6.down === true, p6 && p6.down);

// --- removePeer: existing primary ---
ups.removePeer("127.0.0.3:9000");
const after_remove = ups.peers;
check("remove_count",
      after_remove.length === 5, after_remove.length);
check("removed_absent",
      !after_remove.some(p => p.address.startsWith("127.0.0.3")),
      JSON.stringify(after_remove.map(p => p.address)));

// --- removePeer: existing backup ---
ups.removePeer("127.0.0.4:9000");
const after_remove_bk = ups.peers;
check("remove_backup_count",
      after_remove_bk.length === 4, after_remove_bk.length);
check("removed_backup_absent",
      !after_remove_bk.some(p => p.address.startsWith("127.0.0.4")),
      JSON.stringify(after_remove_bk.map(p => p.address)));

// --- removePeer: non-existent throws ---
let threw = false;
try {
    ups.removePeer("127.0.0.99:9000");
} catch (e) {
    threw = true;
}
check("remove_not_found_throws", threw);

// --- addPeer: invalid address throws ---
let threwInvalid = false;
try {
    ups.addPeer("not-an-address");
} catch (e) {
    threwInvalid = true;
}
check("add_invalid_addr_throws", threwInvalid);
JS

$t->try_run('no js module or stream module')->plan(18);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ups_name/,               'upstream name is sback');
like($log, qr/JSTEST PASS initial_count/,          'initial peer count is 2');
like($log, qr/JSTEST PASS has_primary/,            'initial list has a primary peer');
like($log, qr/JSTEST PASS has_backup/,             'initial list has a backup peer');
like($log, qr/JSTEST PASS add_primary_count/,      'addPeer: primary count becomes 3');
like($log, qr/JSTEST PASS added_primary_present/,  'added primary peer visible in peers[]');
like($log, qr/JSTEST PASS add_with_opts_present/,  'addPeer with opts: peer present');
like($log, qr/JSTEST PASS add_weight_set/,         'addPeer with opts: weight=3 stored');
like($log, qr/JSTEST PASS add_maxfails_set/,       'addPeer with opts: maxFails=2 stored');
like($log, qr/JSTEST PASS add_backup_count/,       'addPeer backup: total becomes 5');
like($log, qr/JSTEST PASS added_backup_present/,   'added backup peer has backup=true');
like($log, qr/JSTEST PASS add_down_flag/,          'addPeer with down:true stored');
like($log, qr/JSTEST PASS remove_count/,           'removePeer primary: count becomes 5');
like($log, qr/JSTEST PASS removed_absent/,         'removed primary peer absent');
like($log, qr/JSTEST PASS remove_backup_count/,    'removePeer backup: count becomes 4');
like($log, qr/JSTEST PASS removed_backup_absent/,  'removed backup peer absent');
like($log, qr/JSTEST PASS remove_not_found_throws/,'removePeer throws for nonexistent peer');
like($log, qr/JSTEST PASS add_invalid_addr_throws/, 'addPeer throws for invalid address');
