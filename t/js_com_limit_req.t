#!/usr/bin/perl

# Tests for Stage 12a COM expansion: limit_req location configuration
# exposed as properties of location.limitReq (NginxLimitReq class).
#
# New property on NginxLocation:
#   limitReq   NginxLimitReq
#
# NginxLimitReq properties (all read-only):
#   limits         object[]  [{zone, burst, nodelay, delay}]
#   logLevel       string    "info"|"notice"|"warn"|"error"
#   delayLogLevel  string    "info"|"notice"|"warn"|"error"
#   statusCode     number    HTTP status on limit exceed
#   dryRun         boolean

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http limit_req/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_limit_req.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    limit_req_zone $binary_remote_addr zone=req_zone:1m rate=10r/s;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with limit_req — uses burst and delay threshold
        location /limited {
            limit_req           zone=req_zone burst=5 delay=2;
            limit_req_log_level warn;
            limit_req_status    429;
            limit_req_dry_run   on;
        }

        # Location with no limit_req directives
        location /plain {
        }
    }
}
EOF

$t->write_file('init_limit_req.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /limited (l) < /plain (p)
const limLoc   = srv.locations[0];
const plainLoc = srv.locations[1];

const lr = limLoc.limitReq;

// ---- object ----
check("lr_obj",      typeof lr === "object" && lr !== null, typeof lr);

// ---- limits array ----
const lims = lr.limits;
check("lims_arr",    Array.isArray(lims),           typeof lims);
check("lims_len",    lims.length === 1,             lims.length);

// ---- first limit entry ----
check("zone_str",    typeof lims[0].zone === "string",     typeof lims[0].zone);
check("zone_val",    lims[0].zone === "req_zone",          lims[0].zone);
check("burst_val",   lims[0].burst === 5,                  lims[0].burst);
check("nodelay_val", lims[0].nodelay === false,             lims[0].nodelay);
check("delay_val",   lims[0].delay === 2,                  lims[0].delay);

// ---- scalar properties ----
check("log_level",   lr.logLevel === "warn",               lr.logLevel);
check("status",      lr.statusCode === 429,                lr.statusCode);
check("dry_run",     lr.dryRun === true,                   lr.dryRun);

// ---- plain location: limits is empty array ----
const plr = plainLoc.limitReq;
check("plain_arr",   Array.isArray(plr.limits),            typeof plr.limits);
check("plain_len",   plr.limits.length === 0,              plr.limits.length);
JS

$t->try_run('no limit_req module')->plan(13);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS lr_obj/,      'location.limitReq is object');
like($log, qr/JSTEST PASS lims_arr/,    'location.limitReq.limits is array');
like($log, qr/JSTEST PASS lims_len/,    'location.limitReq.limits.length == 1');
like($log, qr/JSTEST PASS zone_str/,    'limits[0].zone is string');
like($log, qr/JSTEST PASS zone_val/,    'limits[0].zone == "req_zone"');
like($log, qr/JSTEST PASS burst_val/,   'limits[0].burst == 5');
like($log, qr/JSTEST PASS nodelay_val/, 'limits[0].nodelay == false');
like($log, qr/JSTEST PASS delay_val/,   'limits[0].delay == 2');
like($log, qr/JSTEST PASS log_level/,   'limitReq.logLevel == "warn"');
like($log, qr/JSTEST PASS status/,      'limitReq.statusCode == 429');
like($log, qr/JSTEST PASS dry_run/,     'limitReq.dryRun == true');
like($log, qr/JSTEST PASS plain_arr/,   'plain limitReq.limits is array');
like($log, qr/JSTEST PASS plain_len/,   'plain limitReq.limits is empty');
