#!/usr/bin/perl

# Tests for Stage 12b COM expansion: limit_conn location configuration
# exposed as properties of location.limitConn (NginxLimitConn class).
#
# New property on NginxLocation:
#   limitConn   NginxLimitConn
#
# NginxLimitConn properties (all read-only):
#   limits       object[]  [{zone, conn}]
#   logLevel     string    "info"|"notice"|"warn"|"error"
#   statusCode   number    HTTP status on limit exceed
#   dryRun       boolean

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http limit_conn/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_limit_conn.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    limit_conn_zone $binary_remote_addr zone=conn_zone:1m;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with limit_conn
        location /limited {
            limit_conn           conn_zone 10;
            limit_conn_log_level notice;
            limit_conn_status    503;
            limit_conn_dry_run   on;
        }

        # Location with no limit_conn directives
        location /plain {
        }
    }
}
EOF

$t->write_file('init_limit_conn.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /limited (l) < /plain (p)
const limLoc   = srv.locations[0];
const plainLoc = srv.locations[1];

const lc = limLoc.limitConn;

// ---- object ----
check("lc_obj",      typeof lc === "object" && lc !== null, typeof lc);

// ---- limits array ----
const lims = lc.limits;
check("lims_arr",    Array.isArray(lims),           typeof lims);
check("lims_len",    lims.length === 1,             lims.length);

// ---- first limit entry ----
check("zone_str",    typeof lims[0].zone === "string",     typeof lims[0].zone);
check("zone_val",    lims[0].zone === "conn_zone",         lims[0].zone);
check("conn_val",    lims[0].conn === 10,                  lims[0].conn);

// ---- scalar properties ----
check("log_level",   lc.logLevel === "notice",             lc.logLevel);
check("status",      lc.statusCode === 503,                lc.statusCode);
check("dry_run",     lc.dryRun === true,                   lc.dryRun);

// ---- plain location: limits is empty array ----
const plc = plainLoc.limitConn;
check("plain_arr",   Array.isArray(plc.limits),            typeof plc.limits);
check("plain_len",   plc.limits.length === 0,              plc.limits.length);
JS

$t->try_run('no limit_conn module')->plan(11);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS lc_obj/,      'location.limitConn is object');
like($log, qr/JSTEST PASS lims_arr/,    'location.limitConn.limits is array');
like($log, qr/JSTEST PASS lims_len/,    'location.limitConn.limits.length == 1');
like($log, qr/JSTEST PASS zone_str/,    'limits[0].zone is string');
like($log, qr/JSTEST PASS zone_val/,    'limits[0].zone == "conn_zone"');
like($log, qr/JSTEST PASS conn_val/,    'limits[0].conn == 10');
like($log, qr/JSTEST PASS log_level/,   'limitConn.logLevel == "notice"');
like($log, qr/JSTEST PASS status/,      'limitConn.statusCode == 503');
like($log, qr/JSTEST PASS dry_run/,     'limitConn.dryRun == true');
like($log, qr/JSTEST PASS plain_arr/,   'plain limitConn.limits is array');
like($log, qr/JSTEST PASS plain_len/,   'plain limitConn.limits is empty');
