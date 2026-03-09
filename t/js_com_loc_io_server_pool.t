#!/usr/bin/perl

# Tests for Stage 15 COM expansion: remaining ngx_http_core_loc_conf_t
# getters on NginxLocation plus two new ngx_http_core_srv_conf_t getters
# on NginxServer.
#
# New properties on NginxLocation:
#   keepaliveDisable     string[]  — ["msie6"] | ["safari"] | ["msie6","safari"] | []
#   keepaliveMinTimeout  number    — ms
#   sendfileMaxChunk     number    — bytes
#   readAhead            number    — bytes
#   directio             number | "off"
#   directioAlignment    number    — bytes
#
# New properties on NginxServer:
#   connectionPoolSize   number    — bytes
#   requestPoolSize      number    — bytes

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

js_source %%TESTDIR%%/init_loc_io_server_pool.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        connection_pool_size  512;
        request_pool_size     8k;

        # default location — defaults for I/O fields
        location /default {
        }

        # custom location — explicit I/O settings
        location /custom {
            keepalive_disable    safari;
            sendfile_max_chunk   1m;
            read_ahead           512k;
            directio             4m;
            directio_alignment   4k;
        }
    }
}
EOF

$t->write_file('init_loc_io_server_pool.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
let defLoc, custLoc;
for (const loc of srv.locations) {
    if      (loc.path === "/default") defLoc  = loc;
    else if (loc.path === "/custom")  custLoc = loc;
}

// ---- default: type checks ----
check("d_kd_arr",   Array.isArray(defLoc.keepaliveDisable),          defLoc.keepaliveDisable);
check("d_kmt_num",  typeof defLoc.keepaliveMinTimeout === "number",  defLoc.keepaliveMinTimeout);
check("d_smc_num",  typeof defLoc.sendfileMaxChunk    === "number",  defLoc.sendfileMaxChunk);
check("d_ra_num",   typeof defLoc.readAhead           === "number",  defLoc.readAhead);
check("d_dio_off",  defLoc.directio === "off",                       defLoc.directio);
check("d_dioal_num",typeof defLoc.directioAlignment   === "number",  defLoc.directioAlignment);

// ---- custom: exact values ----
const kd = custLoc.keepaliveDisable;
check("c_kd_arr",      Array.isArray(kd),             kd);
check("c_kd_no_msie6", !kd.includes("msie6"),         kd);
check("c_kd_safari",   kd.includes("safari"),          kd);

check("c_smc",  custLoc.sendfileMaxChunk === 1048576,   custLoc.sendfileMaxChunk);
check("c_ra",   custLoc.readAhead        === 524288,    custLoc.readAhead);
check("c_dio",  custLoc.directio         === 4194304,   custLoc.directio);
check("c_dioal",custLoc.directioAlignment=== 4096,      custLoc.directioAlignment);

// ---- server pool sizes ----
check("srv_cp",  srv.connectionPoolSize === 512,   srv.connectionPoolSize);
check("srv_rp",  srv.requestPoolSize    === 8192,  srv.requestPoolSize);
JS

$t->try_run('no http module')->plan(15);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS d_kd_arr/,    'default: keepaliveDisable is array');
like($log, qr/JSTEST PASS d_kmt_num/,   'default: keepaliveMinTimeout is number');
like($log, qr/JSTEST PASS d_smc_num/,   'default: sendfileMaxChunk is number');
like($log, qr/JSTEST PASS d_ra_num/,    'default: readAhead is number');
like($log, qr/JSTEST PASS d_dio_off/,   'default: directio == "off"');
like($log, qr/JSTEST PASS d_dioal_num/, 'default: directioAlignment is number');
like($log, qr/JSTEST PASS c_kd_arr/,    'custom: keepaliveDisable is array');
like($log, qr/JSTEST PASS c_kd_no_msie6/, 'custom: keepaliveDisable excludes msie6');
like($log, qr/JSTEST PASS c_kd_safari/, 'custom: keepaliveDisable includes safari');
like($log, qr/JSTEST PASS c_smc/,       'custom: sendfileMaxChunk == 1048576');
like($log, qr/JSTEST PASS c_ra/,        'custom: readAhead == 524288');
like($log, qr/JSTEST PASS c_dio/,       'custom: directio == 4194304');
like($log, qr/JSTEST PASS c_dioal/,     'custom: directioAlignment == 4096');
like($log, qr/JSTEST PASS srv_cp/,      'server: connectionPoolSize == 512');
like($log, qr/JSTEST PASS srv_rp/,      'server: requestPoolSize == 8192');
