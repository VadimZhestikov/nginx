#!/usr/bin/perl

# Tests for Stage 7 COM expansion: gzip location configuration
# exposed as properties of location.gzip (NginxGzip class).
#
# New property on NginxLocation:
#   gzip   NginxGzip | null (null if gzip module not compiled in)
#
# NginxGzip properties (all read-only):
#   enable       bool     gzip on/off
#   level        number   compression level (1-9)
#   minLength    number   gzip_min_length (bytes)
#   buffers      object   { num, size }
#   vary         bool     gzip_vary on/off
#   httpVersion  string   "1.0" | "1.1"
#   proxied      string[] active gzip_proxied flags

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

js_source %%TESTDIR%%/init_gzip.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /gzip {
            gzip                on;
            gzip_comp_level     6;
            gzip_min_length     256;
            gzip_buffers        8 4k;
            gzip_vary           on;
            gzip_http_version   1.0;
            gzip_proxied        expired no-cache any;
        }

        location /plain {
            gzip  off;
        }
    }
}
EOF

$t->write_file('init_gzip.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv    = nginx.http.servers[0];
const gzLoc  = srv.locations[0];   // /gzip
const plLoc  = srv.locations[1];   // /plain

const g = gzLoc.gzip;

// ---- gzip object ----
check("gzip_obj",     typeof g === "object" && g !== null, typeof g);

// ---- enable ----
check("enable_on",    g.enable === true,  g.enable);

// ---- level ----
check("level_6",      g.level === 6,      g.level);

// ---- minLength ----
check("min_len_256",  g.minLength === 256, g.minLength);

// ---- buffers ----
const b = g.buffers;
check("bufs_obj",     typeof b === "object" && b !== null, typeof b);
check("bufs_num",     b.num === 8,         b.num);
check("bufs_size",    b.size === 4096,     b.size);

// ---- vary ----
check("vary_on",      g.vary === true,    g.vary);

// ---- httpVersion ----
check("http_ver_10",  g.httpVersion === "1.0", g.httpVersion);

// ---- proxied ----
const p = g.proxied;
check("prox_arr",     Array.isArray(p),               typeof p);
check("prox_exp",     p.indexOf("expired") !== -1,    p);
check("prox_nc",      p.indexOf("no-cache") !== -1,   p);
check("prox_any",     p.indexOf("any") !== -1,        p);
check("prox_no_ns",   p.indexOf("no-store") === -1,   p);

// ---- plain location: gzip off but object still present ----
const pg = plLoc.gzip;
check("plain_gzip_obj",  typeof pg === "object" && pg !== null, typeof pg);
check("plain_enable_off", pg.enable === false, pg.enable);
JS

$t->try_run('no js module')->plan(16);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS gzip_obj/,        'location.gzip is object');
like($log, qr/JSTEST PASS enable_on/,       'location.gzip.enable == true');
like($log, qr/JSTEST PASS level_6/,         'location.gzip.level == 6');
like($log, qr/JSTEST PASS min_len_256/,     'location.gzip.minLength == 256');
like($log, qr/JSTEST PASS bufs_obj/,        'location.gzip.buffers is object');
like($log, qr/JSTEST PASS bufs_num/,        'location.gzip.buffers.num == 8');
like($log, qr/JSTEST PASS bufs_size/,       'location.gzip.buffers.size == 4096');
like($log, qr/JSTEST PASS vary_on/,         'location.gzip.vary == true');
like($log, qr/JSTEST PASS http_ver_10/,     'location.gzip.httpVersion == "1.0"');
like($log, qr/JSTEST PASS prox_arr/,        'location.gzip.proxied is array');
like($log, qr/JSTEST PASS prox_exp/,        'location.gzip.proxied has "expired"');
like($log, qr/JSTEST PASS prox_nc/,         'location.gzip.proxied has "no-cache"');
like($log, qr/JSTEST PASS prox_any/,        'location.gzip.proxied has "any"');
like($log, qr/JSTEST PASS prox_no_ns/,      'location.gzip.proxied excludes "no-store"');
like($log, qr/JSTEST PASS plain_gzip_obj/,  'plain location.gzip is object');
like($log, qr/JSTEST PASS plain_enable_off/,'plain location.gzip.enable == false');
