#!/usr/bin/perl

# Tests for Stage 12a COM expansion: limit_req location configuration
# exposed as properties of location.limitReq (NginxLimitReq class).
#
# NginxLimitReq properties:
#   limits         NginxLimitReqLimit[]  live proxy objects
#   logLevel       string    "info"|"notice"|"warn"|"error"  (r/w)
#   delayLogLevel  string    "info"|"notice"|"warn"|"error"  (r/w)
#   statusCode     number    HTTP status on limit exceed (r/w)
#   dryRun         boolean   (r/w)
#
# NginxLimitReqLimit properties (all r/w except zone):
#   zone     string   shared memory zone name (read-only)
#   burst    number   burst queue size (stored *1000 internally)
#   nodelay  boolean  true when delay == NGX_MAX_UINT32_VALUE
#   delay    number   delay threshold
#   rate     number   zone rate in r/s (stored *1000; writes under shm mutex)

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

        # Location with limit_req -- uses burst and delay threshold
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

// -- object --
check("lr_obj",      typeof lr === "object" && lr !== null, typeof lr);

// -- limits array --
const lims = lr.limits;
check("lims_arr",    Array.isArray(lims),           typeof lims);
check("lims_len",    lims.length === 1,             lims.length);

// -- NginxLimitReqLimit: initial read --
const e = lims[0];
check("zone_str",    typeof e.zone === "string",    typeof e.zone);
check("zone_val",    e.zone === "req_zone",         e.zone);
check("burst_val",   e.burst === 5,                 e.burst);
check("nodelay_val", e.nodelay === false,            e.nodelay);
check("delay_val",   e.delay === 2,                 e.delay);
check("rate_val",    e.rate === 10,                 e.rate);

// -- NginxLimitReqLimit: live proxy setters --
// Two successive limits[] calls return different wrapper objects but both
// proxy the same underlying C struct, so writes through one are visible via
// the next call to limits[].
e.burst = 20;
check("burst_set",   lr.limits[0].burst === 20,     lr.limits[0].burst);

e.delay = 7;
check("delay_set",   lr.limits[0].delay === 7,      lr.limits[0].delay);

e.nodelay = true;
check("nodelay_set", lr.limits[0].nodelay === true,  lr.limits[0].nodelay);
// after setting nodelay, delay getter returns 0
check("nodelay_delay0", lr.limits[0].delay === 0,   lr.limits[0].delay);

e.nodelay = false;
check("nodelay_clr", lr.limits[0].nodelay === false, lr.limits[0].nodelay);

e.rate = 50;
check("rate_set",    lr.limits[0].rate === 50,      lr.limits[0].rate);

// -- zone is read-only --
var threw = false;
try { e.zone = "other"; } catch (_) { threw = true; }
check("zone_ro",     threw,                          "no throw");

// -- scalar properties: initial read --
check("log_level",   lr.logLevel === "warn",        lr.logLevel);
check("status",      lr.statusCode === 429,         lr.statusCode);
check("dry_run",     lr.dryRun === true,            lr.dryRun);

// -- scalar properties: setters --
lr.logLevel  = "info";
check("log_set",     lr.logLevel === "info",        lr.logLevel);

lr.statusCode = 503;
check("status_set",  lr.statusCode === 503,         lr.statusCode);

lr.dryRun = false;
check("dry_set",     lr.dryRun === false,           lr.dryRun);

// -- plain location: limits is empty array --
const plr = plainLoc.limitReq;
check("plain_arr",   Array.isArray(plr.limits),     typeof plr.limits);
check("plain_len",   plr.limits.length === 0,       plr.limits.length);
JS

$t->try_run('no limit_req module')->plan(26);

my $log = $t->read_file('error.log');

# initial reads
like($log, qr/JSTEST PASS lr_obj/,        'location.limitReq is object');
like($log, qr/JSTEST PASS lims_arr/,      'location.limitReq.limits is array');
like($log, qr/JSTEST PASS lims_len/,      'location.limitReq.limits.length == 1');
like($log, qr/JSTEST PASS zone_str/,      'limits[0].zone is string');
like($log, qr/JSTEST PASS zone_val/,      'limits[0].zone == "req_zone"');
like($log, qr/JSTEST PASS burst_val/,     'limits[0].burst == 5');
like($log, qr/JSTEST PASS nodelay_val/,   'limits[0].nodelay == false');
like($log, qr/JSTEST PASS delay_val/,     'limits[0].delay == 2');
like($log, qr/JSTEST PASS rate_val/,      'limits[0].rate == 10');

# setters
like($log, qr/JSTEST PASS burst_set/,     'limits[0].burst setter');
like($log, qr/JSTEST PASS delay_set/,     'limits[0].delay setter');
like($log, qr/JSTEST PASS nodelay_set/,   'limits[0].nodelay setter (true)');
like($log, qr/JSTEST PASS nodelay_delay0/,'limits[0].delay == 0 after nodelay=true');
like($log, qr/JSTEST PASS nodelay_clr/,   'limits[0].nodelay setter (false)');
like($log, qr/JSTEST PASS rate_set/,      'limits[0].rate setter');
like($log, qr/JSTEST PASS zone_ro/,       'limits[0].zone is read-only');

# scalar r/w
like($log, qr/JSTEST PASS log_level/,     'limitReq.logLevel == "warn"');
like($log, qr/JSTEST PASS status/,        'limitReq.statusCode == 429');
like($log, qr/JSTEST PASS dry_run/,       'limitReq.dryRun == true');
like($log, qr/JSTEST PASS log_set/,       'limitReq.logLevel setter');
like($log, qr/JSTEST PASS status_set/,    'limitReq.statusCode setter');
like($log, qr/JSTEST PASS dry_set/,       'limitReq.dryRun setter');

# plain location
like($log, qr/JSTEST PASS plain_arr/,     'plain limitReq.limits is array');
like($log, qr/JSTEST PASS plain_len/,     'plain limitReq.limits is empty');

like($log, qr/JSTEST PASS/, 'at least one JSTEST PASS line present');
unlike($log, qr/JSTEST FAIL/, 'no JSTEST FAIL lines');
