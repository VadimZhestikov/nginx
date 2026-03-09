#!/usr/bin/perl

# Tests for Stage 13z COM expansion: miscellaneous core location properties
# (plain getters on NginxLocation, no new classes).
#
# New properties:
#   lingering              string   — "off" | "on" | "always"
#   lingeringTimeout       number   — ms
#   lingeringTime          number   — ms
#   resolverTimeout        number   — ms
#   chunkedTransferEncoding boolean
#   msieRefresh            boolean
#   logNotFound            boolean
#   logSubrequest          boolean
#   recursiveErrorPages    boolean

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

js_include %%TESTDIR%%/init_misc.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default values
        location /default {
        }

        # custom values
        location /custom {
            lingering_close           always;
            lingering_timeout         3s;
            lingering_time            40s;
            chunked_transfer_encoding off;
            msie_refresh              on;
            log_not_found             off;
            log_subrequest            on;
            recursive_error_pages     on;
        }
    }
}
EOF

$t->write_file('init_misc.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
let defLoc, custLoc;
for (const loc of srv.locations) {
    if      (loc.path === "/default") defLoc  = loc;
    else if (loc.path === "/custom")  custLoc = loc;
}

// ---- default ----
check("d_lingering",   defLoc.lingering === "on",                    defLoc.lingering);
check("d_lt_num",      typeof defLoc.lingeringTimeout === "number",  defLoc.lingeringTimeout);
check("d_ltime_num",   typeof defLoc.lingeringTime    === "number",  defLoc.lingeringTime);
check("d_rt_num",      typeof defLoc.resolverTimeout  === "number",  defLoc.resolverTimeout);
check("d_cte_bool",    typeof defLoc.chunkedTransferEncoding === "boolean",
                       defLoc.chunkedTransferEncoding);
check("d_msie_bool",   typeof defLoc.msieRefresh       === "boolean", defLoc.msieRefresh);
check("d_lnf_bool",    typeof defLoc.logNotFound       === "boolean", defLoc.logNotFound);
check("d_lsub_bool",   typeof defLoc.logSubrequest     === "boolean", defLoc.logSubrequest);
check("d_rep_bool",    typeof defLoc.recursiveErrorPages === "boolean",
                       defLoc.recursiveErrorPages);

// ---- custom ----
check("c_lingering",   custLoc.lingering === "always",               custLoc.lingering);
check("c_lt",          custLoc.lingeringTimeout === 3000,            custLoc.lingeringTimeout);
check("c_ltime",       custLoc.lingeringTime    === 40000,           custLoc.lingeringTime);
check("c_cte",         custLoc.chunkedTransferEncoding === false,    custLoc.chunkedTransferEncoding);
check("c_msie",        custLoc.msieRefresh       === true,           custLoc.msieRefresh);
check("c_lnf",         custLoc.logNotFound       === false,          custLoc.logNotFound);
check("c_lsub",        custLoc.logSubrequest     === true,           custLoc.logSubrequest);
check("c_rep",         custLoc.recursiveErrorPages === true,         custLoc.recursiveErrorPages);
JS

$t->try_run('no http module')->plan(17);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS d_lingering/,  'default: lingering == "on"');
like($log, qr/JSTEST PASS d_lt_num/,     'default: lingeringTimeout is number');
like($log, qr/JSTEST PASS d_ltime_num/,  'default: lingeringTime is number');
like($log, qr/JSTEST PASS d_rt_num/,     'default: resolverTimeout is number');
like($log, qr/JSTEST PASS d_cte_bool/,   'default: chunkedTransferEncoding is boolean');
like($log, qr/JSTEST PASS d_msie_bool/,  'default: msieRefresh is boolean');
like($log, qr/JSTEST PASS d_lnf_bool/,   'default: logNotFound is boolean');
like($log, qr/JSTEST PASS d_lsub_bool/,  'default: logSubrequest is boolean');
like($log, qr/JSTEST PASS d_rep_bool/,   'default: recursiveErrorPages is boolean');
like($log, qr/JSTEST PASS c_lingering/,  'custom: lingering == "always"');
like($log, qr/JSTEST PASS c_lt/,         'custom: lingeringTimeout == 3000');
like($log, qr/JSTEST PASS c_ltime/,      'custom: lingeringTime == 40000');
like($log, qr/JSTEST PASS c_cte/,        'custom: chunkedTransferEncoding == false');
like($log, qr/JSTEST PASS c_msie/,       'custom: msieRefresh == true');
like($log, qr/JSTEST PASS c_lnf/,        'custom: logNotFound == false');
like($log, qr/JSTEST PASS c_lsub/,       'custom: logSubrequest == true');
like($log, qr/JSTEST PASS c_rep/,        'custom: recursiveErrorPages == true');
