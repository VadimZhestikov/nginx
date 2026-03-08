#!/usr/bin/perl

# Tests for Stage 5 COM expansion: extended peer fields on NginxPeer
# (config-phase) and NginxUpstream.zone.
#
# New read-write properties on NginxPeer (config-phase):
#   failTimeout   number (seconds)   ngx_http_upstream_server_t.fail_timeout
#   maxConns      number             ngx_http_upstream_server_t.max_conns
#
# New read-write properties on NginxRRPeer (runtime — same names):
#   failTimeout   number (seconds)
#   maxConns      number
#
# New read-only properties on NginxRRPeer:
#   server        string  (configured "server" address string)
#   fails         number  (runtime failure counter)
#
# New read-only property on NginxUpstream:
#   zone          string | null  (shm_zone name, or null if no zone{} used)

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

js_include %%TESTDIR%%/init_upstream_ext.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:9091 weight=3 max_fails=2 fail_timeout=20s max_conns=10;
        server 127.0.0.1:9092 backup   fail_timeout=30s max_conns=5;
    }

    server {
        listen      127.0.0.1:8080;
        server_name localhost;
        location /  { }
    }
}
EOF

$t->write_file('init_upstream_ext.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const ups = nginx.http.upstreams[0];

// ---- NginxUpstream.zone (no zone{} directive → null) ----
check("zone_null", ups.zone === null, ups.zone);

// ---- NginxPeer: read failTimeout and maxConns ----
const p0 = ups.peers[0];   /* 127.0.0.1:9091 — primary */
const p1 = ups.peers[1];   /* 127.0.0.1:9092 — backup  */

check("p0_ft_read", p0.failTimeout === 20, p0.failTimeout);
check("p0_mc_read", p0.maxConns    === 10, p0.maxConns);
check("p1_ft_read", p1.failTimeout === 30, p1.failTimeout);
check("p1_mc_read", p1.maxConns    === 5,  p1.maxConns);

// ---- NginxPeer: write failTimeout and maxConns, then read back ----
p0.failTimeout = 45;
p0.maxConns    = 20;
check("p0_ft_write", p0.failTimeout === 45, p0.failTimeout);
check("p0_mc_write", p0.maxConns    === 20, p0.maxConns);

p1.failTimeout = 60;
p1.maxConns    = 0;
check("p1_ft_write", p1.failTimeout === 60, p1.failTimeout);
check("p1_mc_write", p1.maxConns    === 0,  p1.maxConns);

// ---- NginxPeer: verify the other existing properties still work ----
check("p0_weight",   p0.weight   === 3,    p0.weight);
check("p0_maxfails", p0.maxFails === 2,    p0.maxFails);
check("p0_backup",   p0.backup   === false, p0.backup);
check("p1_backup",   p1.backup   === true,  p1.backup);
JS

$t->try_run('no js module')->plan(13);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS zone_null/,   'upstream.zone == null (no zone directive)');
like($log, qr/JSTEST PASS p0_ft_read/,  'peer[0].failTimeout == 20');
like($log, qr/JSTEST PASS p0_mc_read/,  'peer[0].maxConns == 10');
like($log, qr/JSTEST PASS p1_ft_read/,  'peer[1].failTimeout == 30');
like($log, qr/JSTEST PASS p1_mc_read/,  'peer[1].maxConns == 5');
like($log, qr/JSTEST PASS p0_ft_write/, 'peer[0].failTimeout write → 45');
like($log, qr/JSTEST PASS p0_mc_write/, 'peer[0].maxConns write → 20');
like($log, qr/JSTEST PASS p1_ft_write/, 'peer[1].failTimeout write → 60');
like($log, qr/JSTEST PASS p1_mc_write/, 'peer[1].maxConns write → 0');
like($log, qr/JSTEST PASS p0_weight/,   'peer[0].weight still == 3');
like($log, qr/JSTEST PASS p0_maxfails/, 'peer[0].maxFails still == 2');
like($log, qr/JSTEST PASS p0_backup/,   'peer[0].backup == false');
like($log, qr/JSTEST PASS p1_backup/,   'peer[1].backup == true');
