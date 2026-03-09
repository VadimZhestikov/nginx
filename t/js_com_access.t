#!/usr/bin/perl

# Tests for Stage 11a COM expansion: access module location configuration
# exposed as properties of location.access (NginxAccess class).
#
# New property on NginxLocation:
#   access   NginxAccess
#
# NginxAccess properties (all read-only):
#   rules      object[]  IPv4 [{deny, cidr}]
#   rules6     object[]  IPv6 [{deny, cidr}] (empty if no IPv6 support)
#   rulesUnix  object[]  Unix [{deny}]        (empty if no Unix support)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http access/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_access.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with IPv4 allow/deny rules
        location /access {
            allow  127.0.0.1;
            allow  10.0.0.0/8;
            deny   all;
        }

        # Location with no access rules
        location /plain {
        }
    }
}
EOF

$t->write_file('init_access.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /access (a) before /plain (p)
const accLoc   = srv.locations[0];
const plainLoc = srv.locations[1];

const ac = accLoc.access;

// ---- access object ----
check("ac_obj",      typeof ac === "object" && ac !== null, typeof ac);

// ---- rules array ----
const r = ac.rules;
check("rules_arr",   Array.isArray(r),         typeof r);
check("rules_len",   r.length === 3,           r.length);

// rule[0]: allow 127.0.0.1
check("r0_deny",     r[0].deny === false,       r[0].deny);
check("r0_cidr",     r[0].cidr === "127.0.0.1", r[0].cidr);

// rule[1]: allow 10.0.0.0/8
check("r1_deny",     r[1].deny === false,       r[1].deny);
check("r1_cidr",     r[1].cidr === "10.0.0.0/8", r[1].cidr);

// rule[2]: deny all
check("r2_deny",     r[2].deny === true,        r[2].deny);
check("r2_cidr",     r[2].cidr === "all",       r[2].cidr);

// ---- rules6 is always an array ----
check("rules6_arr",  Array.isArray(ac.rules6),  typeof ac.rules6);

// ---- rulesUnix is always an array ----
check("rux_arr",     Array.isArray(ac.rulesUnix), typeof ac.rulesUnix);

// ---- plain location: no rules — empty arrays ----
const pa = plainLoc.access;
check("plain_r_arr", Array.isArray(pa.rules),   typeof pa.rules);
check("plain_r_len", pa.rules.length === 0,     pa.rules.length);
JS

$t->try_run('no access module')->plan(13);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ac_obj/,      'location.access is object');
like($log, qr/JSTEST PASS rules_arr/,   'location.access.rules is array');
like($log, qr/JSTEST PASS rules_len/,   'location.access.rules.length == 3');
like($log, qr/JSTEST PASS r0_deny/,     'rules[0].deny == false');
like($log, qr/JSTEST PASS r0_cidr/,     'rules[0].cidr == "127.0.0.1"');
like($log, qr/JSTEST PASS r1_deny/,     'rules[1].deny == false');
like($log, qr/JSTEST PASS r1_cidr/,     'rules[1].cidr == "10.0.0.0/8"');
like($log, qr/JSTEST PASS r2_deny/,     'rules[2].deny == true');
like($log, qr/JSTEST PASS r2_cidr/,     'rules[2].cidr == "all"');
like($log, qr/JSTEST PASS rules6_arr/,  'location.access.rules6 is array');
like($log, qr/JSTEST PASS rux_arr/,     'location.access.rulesUnix is array');
like($log, qr/JSTEST PASS plain_r_arr/, 'plain location.access.rules is array');
like($log, qr/JSTEST PASS plain_r_len/, 'plain location.access.rules is empty');
