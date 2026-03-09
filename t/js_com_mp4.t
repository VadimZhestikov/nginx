#!/usr/bin/perl

# Tests for Stage 13q COM expansion: mp4 location configuration
# exposed as properties of location.mp4 (NginxMp4 class).
#
# New property on NginxLocation:
#   mp4   NginxMp4
#
# NginxMp4 properties (all read-only):
#   bufferSize     number   — mp4_buffer_size (bytes)
#   maxBufferSize  number   — mp4_max_buffer_size (bytes)
#   startKeyFrame  boolean  — mp4_start_key_frame on/off

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

js_source %%TESTDIR%%/init_mp4.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no mp4 directives (default values apply)
        location /default {
        }

        # custom mp4 settings
        location /video {
            mp4;
            mp4_buffer_size     2m;
            mp4_max_buffer_size 16m;
            mp4_start_key_frame on;
        }
    }
}
EOF

$t->write_file('init_mp4.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /video (v)
const defLoc   = srv.locations[0];
const videoLoc = srv.locations[1];

// ---- default: no mp4 directives ----
const md = defLoc.mp4;
check("md_obj",   typeof md === "object" && md !== null, typeof md);
check("md_buf",   md.bufferSize    > 0,                  md.bufferSize);
check("md_maxbuf",md.maxBufferSize > 0,                  md.maxBufferSize);
check("md_skf_bool", typeof md.startKeyFrame === "boolean", typeof md.startKeyFrame);

// ---- video: custom mp4 settings ----
const mv = videoLoc.mp4;
check("mv_obj",   typeof mv === "object" && mv !== null, typeof mv);
check("mv_buf",   mv.bufferSize    === 2 * 1024 * 1024,  mv.bufferSize);
check("mv_maxbuf",mv.maxBufferSize === 16 * 1024 * 1024, mv.maxBufferSize);
check("mv_skf",   mv.startKeyFrame === true,             mv.startKeyFrame);
JS

$t->try_run('no mp4 module')->plan(8);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS md_obj/,     'default: mp4 is object');
like($log, qr/JSTEST PASS md_buf/,     'default: bufferSize > 0');
like($log, qr/JSTEST PASS md_maxbuf/,  'default: maxBufferSize > 0');
like($log, qr/JSTEST PASS md_skf_bool/,'default: startKeyFrame is boolean');
like($log, qr/JSTEST PASS mv_obj/,     'video: mp4 is object');
like($log, qr/JSTEST PASS mv_buf/,     'video: bufferSize == 2m');
like($log, qr/JSTEST PASS mv_maxbuf/,  'video: maxBufferSize == 16m');
like($log, qr/JSTEST PASS mv_skf/,     'video: startKeyFrame == true');
