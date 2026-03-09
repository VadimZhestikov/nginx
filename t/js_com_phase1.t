#!/usr/bin/perl

# Tests for Phase 1 — read-only JavaScript COM:
#   nginx.version, nginx.cpu_count, nginx.log()
#   nginx.cycle.hostname / prefix / workers
#   nginx.http.servers[].name / names[] / root / locations[]
#   nginx.http.upstreams[].name / peers[]
#   peer.address / weight / maxFails / down / backup

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
        server_name  example.com www.example.com;
        root         %%TESTDIR%%/html;

        location / { }
        location /api/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
// Phase 1 — read-only COM assertions
// nginx.log level 6 = NGX_LOG_NOTICE, visible at "debug" error_log level

function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }

function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

// --- nginx.* top-level ---

check("version_set",        typeof nginx.version === "string" && nginx.version.length > 0, nginx.version);
check("version_1x",         /^1\./.test(nginx.version), nginx.version);
check("cpu_count_positive", Number.isInteger(nginx.cpu_count) && nginx.cpu_count > 0, nginx.cpu_count);

// --- nginx.cycle ---

check("hostname_set",       typeof nginx.cycle.hostname === "string" && nginx.cycle.hostname.length > 0);
check("prefix_ends_slash",  nginx.cycle.prefix.endsWith("/"), nginx.cycle.prefix);
check("workers_one",        nginx.cycle.workers === 1, nginx.cycle.workers);

// --- nginx.http.servers[] ---

const servers = nginx.http.servers;
check("servers_is_array",   Array.isArray(servers));
check("servers_not_empty",  servers.length >= 1, servers.length);

const srv = servers[0];
check("server_name_set",    srv.name === "example.com", srv.name);
check("server_root_set",    srv.root.length > 0, srv.root);
check("server_names_array", Array.isArray(srv.names));
check("server_names_count", srv.names.length >= 2, srv.names.length);
check("server_names_first", srv.names[0] === "example.com", srv.names[0]);

const locs = srv.locations;
check("locations_is_array", Array.isArray(locs));
check("locations_not_empty", locs.length >= 2, locs.length);

const root_loc = locs.find(l => l.path === "/");
check("loc_path_found",     root_loc !== undefined);
check("loc_root_set",       typeof root_loc.root === "string" && root_loc.root.length > 0, root_loc.root);

// --- nginx.http.upstreams[] ---

const ups = nginx.http.upstreams;
check("upstreams_is_array",  Array.isArray(ups));
check("upstreams_not_empty", ups.length >= 1, ups.length);

const up = ups[0];
check("upstream_name",       up.name === "backend", up.name);

const peers = up.peers;
check("peers_is_array",      Array.isArray(peers));
check("peers_two",           peers.length === 2, peers.length);

// primary peer
const p0 = peers[0];
check("peer0_address",       typeof p0.address === "string" && p0.address.includes(":"), p0.address);
check("peer0_weight_one",    p0.weight === 1, p0.weight);
check("peer0_down_false",    p0.down === false, p0.down);
check("peer0_backup_false",  p0.backup === false, p0.backup);

// backup peer
const p1 = peers[1];
check("peer1_backup_true",   p1.backup === true, p1.backup);
check("peer1_maxfails_int",  Number.isInteger(p1.maxFails), p1.maxFails);
JS

$t->try_run('no js module')->plan(27);

my $log = $t->read_file('error.log');

# nginx.* top-level
like($log, qr/JSTEST PASS version_set/,        'nginx.version is set');
like($log, qr/JSTEST PASS version_1x/,         'nginx.version starts with 1.');
like($log, qr/JSTEST PASS cpu_count_positive/, 'nginx.cpu_count > 0');

# nginx.cycle
like($log, qr/JSTEST PASS hostname_set/,      'nginx.cycle.hostname is set');
like($log, qr/JSTEST PASS prefix_ends_slash/, 'nginx.cycle.prefix ends with /');
like($log, qr/JSTEST PASS workers_one/,       'nginx.cycle.workers == 1');

# servers[]
like($log, qr/JSTEST PASS servers_is_array/,   'servers is array');
like($log, qr/JSTEST PASS servers_not_empty/,  'servers is not empty');
like($log, qr/JSTEST PASS server_name_set/,    'server.name == example.com');
like($log, qr/JSTEST PASS server_root_set/,    'server.root is set');
like($log, qr/JSTEST PASS server_names_array/, 'server.names is array');
like($log, qr/JSTEST PASS server_names_count/, 'server.names has 2+ entries');
like($log, qr/JSTEST PASS server_names_first/, 'server.names[0] == example.com');

# locations[]
like($log, qr/JSTEST PASS locations_is_array/,  'locations is array');
like($log, qr/JSTEST PASS locations_not_empty/, 'locations is not empty');
like($log, qr/JSTEST PASS loc_path_found/,      '/ location found');
like($log, qr/JSTEST PASS loc_root_set/,        'location.root is set');

# upstreams[] and peers[]
like($log, qr/JSTEST PASS upstreams_is_array/,  'upstreams is array');
like($log, qr/JSTEST PASS upstreams_not_empty/, 'upstreams is not empty');
like($log, qr/JSTEST PASS upstream_name/,       'upstream.name == backend');
like($log, qr/JSTEST PASS peers_is_array/,      'peers is array');
like($log, qr/JSTEST PASS peers_two/,           'peers has 2 entries (primary + backup)');
like($log, qr/JSTEST PASS peer0_address/,       'peer[0].address is host:port');
like($log, qr/JSTEST PASS peer0_weight_one/,    'peer[0].weight == 1');
like($log, qr/JSTEST PASS peer0_down_false/,    'peer[0].down == false');
like($log, qr/JSTEST PASS peer0_backup_false/,  'peer[0].backup == false');
like($log, qr/JSTEST PASS peer1_backup_true/,   'peer[1].backup == true');
