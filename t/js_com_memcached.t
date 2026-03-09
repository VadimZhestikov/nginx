#!/usr/bin/perl

# Tests for Stage 13u COM expansion: memcached location configuration
# exposed as properties of location.memcached (NginxMemcached class).
#
# New property on NginxLocation:
#   memcached   NginxMemcached
#
# NginxMemcached properties (all read-only):
#   connectTimeout  number  — memcached_connect_timeout (ms)
#   sendTimeout     number  — memcached_send_timeout (ms)
#   readTimeout     number  — memcached_read_timeout (ms)
#   bufferSize      number  — memcached_buffer_size (bytes)
#   gzipFlag        number  — memcached_gzip_flag value (0 = not set)

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

js_include %%TESTDIR%%/init_memcached.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no memcached directives (defaults apply)
        location /default {
        }

        # custom memcached settings
        location /cache {
            set $memcached_key "$uri";
            memcached_pass     127.0.0.1:11211;
            memcached_connect_timeout  2s;
            memcached_send_timeout     3s;
            memcached_read_timeout     4s;
            memcached_buffer_size      16k;
            memcached_gzip_flag        16;
        }
    }
}
EOF

$t->write_file('init_memcached.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /cache (c) < /default (d)
const cacheLoc = srv.locations[0];
const defLoc   = srv.locations[1];

// ---- default: inherited defaults ----
const md = defLoc.memcached;
check("md_obj",     typeof md === "object" && md !== null, typeof md);
check("md_conn",    md.connectTimeout > 0,                 md.connectTimeout);
check("md_send",    md.sendTimeout    > 0,                 md.sendTimeout);
check("md_read",    md.readTimeout    > 0,                 md.readTimeout);
check("md_buf",     md.bufferSize     > 0,                 md.bufferSize);
check("md_gzip",    md.gzipFlag       === 0,               md.gzipFlag);

// ---- cache: custom settings ----
const mc = cacheLoc.memcached;
check("mc_obj",     typeof mc === "object" && mc !== null, typeof mc);
check("mc_conn",    mc.connectTimeout === 2000,            mc.connectTimeout);
check("mc_send",    mc.sendTimeout    === 3000,            mc.sendTimeout);
check("mc_read",    mc.readTimeout    === 4000,            mc.readTimeout);
check("mc_buf",     mc.bufferSize     === 16 * 1024,       mc.bufferSize);
check("mc_gzip",    mc.gzipFlag       === 16,              mc.gzipFlag);
JS

$t->try_run('no memcached module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS md_obj/,  'default: memcached is object');
like($log, qr/JSTEST PASS md_conn/, 'default: connectTimeout > 0');
like($log, qr/JSTEST PASS md_send/, 'default: sendTimeout > 0');
like($log, qr/JSTEST PASS md_read/, 'default: readTimeout > 0');
like($log, qr/JSTEST PASS md_buf/,  'default: bufferSize > 0');
like($log, qr/JSTEST PASS md_gzip/, 'default: gzipFlag == 0');
like($log, qr/JSTEST PASS mc_obj/,  'cache: memcached is object');
like($log, qr/JSTEST PASS mc_conn/, 'cache: connectTimeout == 2000');
like($log, qr/JSTEST PASS mc_send/, 'cache: sendTimeout == 3000');
like($log, qr/JSTEST PASS mc_read/, 'cache: readTimeout == 4000');
like($log, qr/JSTEST PASS mc_buf/,  'cache: bufferSize == 16k');
like($log, qr/JSTEST PASS mc_gzip/, 'cache: gzipFlag == 16');
