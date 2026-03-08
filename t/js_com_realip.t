#!/usr/bin/perl

# Tests for Stage 13c COM expansion: realip location configuration
# exposed as properties of location.realip (NginxRealIP class).
# Requires --with-http_realip_module.
#
# New property on NginxLocation:
#   realip   NginxRealIP
#
# NginxRealIP properties (all read-only):
#   header     string    effective header name for real IP lookup
#   recursive  boolean   real_ip_recursive on/off
#   from       string[]  trusted proxy CIDRs

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http realip/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_realip.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # location with full realip configuration
        location /realip {
            set_real_ip_from  127.0.0.1;
            set_real_ip_from  10.0.0.0/8;
            real_ip_header    X-Forwarded-For;
            real_ip_recursive on;
        }

        # location with no realip directives — gets default header, empty from
        location /plain {
        }
    }
}
EOF

$t->write_file('init_realip.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /plain (p) < /realip (r)
const plainLoc  = srv.locations[0];
const realipLoc = srv.locations[1];

const ri = realipLoc.realip;

// ---- object ----
check("ri_obj",       typeof ri === "object" && ri !== null, typeof ri);

// ---- header ----
check("hdr_str",      typeof ri.header === "string",         typeof ri.header);
check("hdr_val",      ri.header === "X-Forwarded-For",       ri.header);

// ---- recursive ----
check("recursive",    ri.recursive === true,                  ri.recursive);

// ---- from array ----
const from = ri.from;
check("from_arr",     Array.isArray(from),                    typeof from);
check("from_len",     from.length === 2,                      from.length);
check("from0",        from[0] === "127.0.0.1",               from[0]);
check("from1",        from[1] === "10.0.0.0/8",              from[1]);

// ---- plain location: default header, empty from ----
const rp = plainLoc.realip;
check("plain_obj",    typeof rp === "object" && rp !== null, typeof rp);
check("plain_hdr",    rp.header === "X-Real-IP",             rp.header);
check("plain_from",   Array.isArray(rp.from),                typeof rp.from);
check("plain_flen",   rp.from.length === 0,                  rp.from.length);
JS

$t->try_run('no realip module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ri_obj/,      'location.realip is object');
like($log, qr/JSTEST PASS hdr_str/,     'realip.header is string');
like($log, qr/JSTEST PASS hdr_val/,     'realip.header == "X-Forwarded-For"');
like($log, qr/JSTEST PASS recursive/,   'realip.recursive == true');
like($log, qr/JSTEST PASS from_arr/,    'realip.from is array');
like($log, qr/JSTEST PASS from_len/,    'realip.from.length == 2');
like($log, qr/JSTEST PASS from0/,       'realip.from[0] == "127.0.0.1"');
like($log, qr/JSTEST PASS from1/,       'realip.from[1] == "10.0.0.0/8"');
like($log, qr/JSTEST PASS plain_obj/,   'plain location.realip is object');
like($log, qr/JSTEST PASS plain_hdr/,   'plain realip.header == "X-Real-IP"');
like($log, qr/JSTEST PASS plain_from/,  'plain realip.from is array');
like($log, qr/JSTEST PASS plain_flen/,  'plain realip.from is empty');
