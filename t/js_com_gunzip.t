#!/usr/bin/perl

# Tests for Stage 13l COM expansion: gunzip location configuration
# exposed as properties of location.gunzip (NginxGunzip class).
#
# New property on NginxLocation:
#   gunzip   NginxGunzip
#
# NginxGunzip properties (all read-only):
#   enable   boolean — gunzip on/off
#   buffers  object  — { num: N, size: S } from gunzip_buffers

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

js_source %%TESTDIR%%/init_gunzip.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — gunzip off
        location /default {
        }

        # gunzip on with explicit buffers
        location /on {
            gunzip on;
            gunzip_buffers 4 8k;
        }
    }
}
EOF

$t->write_file('init_gunzip.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /on (o)
const defLoc = srv.locations[0];
const onLoc  = srv.locations[1];

// ---- default: gunzip off ----
const gd = defLoc.gunzip;
check("gd_obj",    typeof gd === "object" && gd !== null, typeof gd);
check("gd_enable", gd.enable === false,                   gd.enable);

// ---- on: gunzip on with buffers ----
const go = onLoc.gunzip;
check("go_obj",      typeof go === "object" && go !== null,    typeof go);
check("go_enable",   go.enable === true,                       go.enable);
check("go_buf_obj",  typeof go.buffers === "object",           typeof go.buffers);
check("go_buf_num",  go.buffers.num === 4,                     go.buffers.num);
check("go_buf_size", go.buffers.size === 8192,                 go.buffers.size);
JS

$t->try_run('no gunzip module')->plan(7);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS gd_obj/,      'location.gunzip is object');
like($log, qr/JSTEST PASS gd_enable/,   'default: enable == false');
like($log, qr/JSTEST PASS go_obj/,      'on: location.gunzip is object');
like($log, qr/JSTEST PASS go_enable/,   'on: enable == true');
like($log, qr/JSTEST PASS go_buf_obj/,  'on: buffers is object');
like($log, qr/JSTEST PASS go_buf_num/,  'on: buffers.num == 4');
like($log, qr/JSTEST PASS go_buf_size/, 'on: buffers.size == 8192');
