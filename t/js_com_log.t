#!/usr/bin/perl

# Tests for Stage 13b COM expansion: access log location configuration
# exposed as properties of location.log (NginxLog class).
#
# New property on NginxLocation:
#   log   NginxLog
#
# NginxLog properties (all read-only):
#   off    boolean    true when "access_log off"
#   logs   object[]   [{path, format}] per access_log directive
#            path     string | null    literal file path; null for syslog
#                                      or variable-based paths
#            format   string | null    log format name

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

js_source %%TESTDIR%%/init_log.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    log_format myformat '$remote_addr - $request';

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # access_log off
        location /off {
            access_log off;
        }

        # explicit access_log with custom format
        location /withlog {
            access_log %%TESTDIR%%/access.log myformat;
        }
    }
}
EOF

$t->write_file('init_log.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /off (o) < /withlog (w)
const offLoc = srv.locations[0];
const wlLoc  = srv.locations[1];

// ---- off location ----
const lo = offLoc.log;
check("off_obj",    typeof lo === "object" && lo !== null, typeof lo);
check("off_flag",   lo.off === true,                       lo.off);
check("off_logs",   Array.isArray(lo.logs),                typeof lo.logs);

// ---- withlog location ----
const lw = wlLoc.log;
check("wl_obj",     typeof lw === "object" && lw !== null, typeof lw);
check("wl_off",     lw.off === false,                      lw.off);

const logs = lw.logs;
check("logs_arr",   Array.isArray(logs),                   typeof logs);
check("logs_len",   logs.length === 1,                     logs.length);

// path ends with /access.log
check("path_str",   typeof logs[0].path === "string",      typeof logs[0].path);
check("path_end",   logs[0].path.endsWith("/access.log"),  logs[0].path);

// format name
check("fmt_str",    typeof logs[0].format === "string",    typeof logs[0].format);
check("fmt_val",    logs[0].format === "myformat",         logs[0].format);
JS

$t->try_run('no log module')->plan(11);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS off_obj/,   'location.log is object (off location)');
like($log, qr/JSTEST PASS off_flag/,  'location.log.off == true');
like($log, qr/JSTEST PASS off_logs/,  'location.log.logs is array when off');
like($log, qr/JSTEST PASS wl_obj/,    'location.log is object (withlog location)');
like($log, qr/JSTEST PASS wl_off/,    'location.log.off == false');
like($log, qr/JSTEST PASS logs_arr/,  'location.log.logs is array');
like($log, qr/JSTEST PASS logs_len/,  'location.log.logs.length == 1');
like($log, qr/JSTEST PASS path_str/,  'logs[0].path is string');
like($log, qr/JSTEST PASS path_end/,  'logs[0].path ends with /access.log');
like($log, qr/JSTEST PASS fmt_str/,   'logs[0].format is string');
like($log, qr/JSTEST PASS fmt_val/,   'logs[0].format == "myformat"');
