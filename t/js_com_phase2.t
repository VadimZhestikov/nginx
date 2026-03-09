#!/usr/bin/perl

# Tests for Phase 2 — config-phase mutation:
#   nginx.cycle.workers = N     (writable)
#   server.root  = '/path'      (writable, affects static file serving)
#   location.root = '/path'     (writable, overrides server root)
#   peer.weight  = N            (writable config-phase setter)
#   peer.maxFails = N           (writable config-phase setter)
#   peer.down    = true/false   (writable config-phase setter)

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

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%% backup;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # root at server level; JS will change it
        root %%TESTDIR%%/srv_orig;

        # explicit location root; JS will change it
        location /loc/ {
            root %%TESTDIR%%/loc_orig;
        }

        # catch-all for server-root test
        location / { }
    }
}
EOF

# Create file trees that differ in content so we can detect which root is active
my $d = $t->testdir();

mkdir "$d/srv_orig";
mkdir "$d/srv_new";
mkdir "$d/loc_orig";
mkdir "$d/loc_orig/loc";
mkdir "$d/loc_new";
mkdir "$d/loc_new/loc";

$t->write_file('srv_orig/marker.txt', 'SRV-ORIG');
$t->write_file('srv_new/marker.txt',  'SRV-NEW');
$t->write_file('loc_orig/loc/marker.txt', 'LOC-ORIG');
$t->write_file('loc_new/loc/marker.txt',  'LOC-NEW');

$t->write_file('init.js', <<"JS");
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

// --- nginx.cycle.workers setter ---
const before = nginx.cycle.workers;
nginx.cycle.workers = 4;
check("workers_setter", nginx.cycle.workers === 4, nginx.cycle.workers);
nginx.cycle.workers = before;   // restore so nginx actually starts with 1 worker

// --- server.root mutation ---
const srv = nginx.http.servers[0];
srv.root = "$d/srv_new";
check("server_root_mutated", srv.root === "$d/srv_new", srv.root);

// --- location.root mutation ---
const loc = srv.locations.find(l => l.path === "/loc/");
loc.root = "$d/loc_new";
check("loc_root_mutated", loc.root === "$d/loc_new", loc.root);

// --- peer.weight / peer.maxFails / peer.down setters ---
const ups = nginx.http.upstreams[0];
const p = ups.peers.find(pp => !pp.backup);

const origWeight = p.weight;
p.weight = 7;
check("peer_weight_setter",   p.weight === 7, p.weight);
p.weight = origWeight;

const origMF = p.maxFails;
p.maxFails = 5;
check("peer_maxfails_setter", p.maxFails === 5, p.maxFails);
p.maxFails = origMF;

p.down = true;
check("peer_down_setter_true",  p.down === true, p.down);
p.down = false;
check("peer_down_setter_false", p.down === false, p.down);
JS

$t->try_run('no js module')->plan(9);

# --- Error-log assertions (config-phase setters) ---

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS workers_setter/,      'cycle.workers setter works');
like($log, qr/JSTEST PASS server_root_mutated/, 'server.root write-back readable');
like($log, qr/JSTEST PASS loc_root_mutated/,    'location.root write-back readable');
like($log, qr/JSTEST PASS peer_weight_setter/,  'peer.weight setter works');
like($log, qr/JSTEST PASS peer_maxfails_setter/,'peer.maxFails setter works');
like($log, qr/JSTEST PASS peer_down_setter_true/,  'peer.down = true');
like($log, qr/JSTEST PASS peer_down_setter_false/, 'peer.down = false');

# --- HTTP assertions: verify mutations affect static file serving ---

# JS changed server root → srv_new/marker.txt should be served
like(http_get('/marker.txt'), qr/SRV-NEW/, 'server.root mutation serves new root');

# JS changed location /loc/ root → loc_new/loc/marker.txt should be served
like(http_get('/loc/marker.txt'), qr/LOC-NEW/, 'location.root mutation serves new root');
