#!/usr/bin/perl

# Tests for nginx.stream.servers[i].access — NginxStreamAccess (Stage C).
#
# Covers:
#   srv.access.rules     — IPv4 allow/deny rules: getter and setter
#   srv.access.rules6    — IPv6 allow/deny rules: getter and setter
#   srv.access.rulesUnix — Unix-socket rules: getter and setter
#   Empty-rules reset (setting [] clears the array)
#   Invalid CIDR throws TypeError

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location / { }
    }
}

stream {
    server {
        listen      127.0.0.1:%%PORT_8092%%;
        proxy_pass  127.0.0.1:%%PORT_8091%%;

        allow  10.0.0.0/8;
        deny   all;
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

const srv    = nginx.stream.servers[0];
const access = srv.access;

/* ---- initial rules getter ----------------------------------------- */
const rules0 = access.rules;
check("rules_is_array",     Array.isArray(rules0), typeof rules0);
check("rules_initial_count", rules0.length === 2, rules0.length);

const r0 = rules0[0];   /* allow 10.0.0.0/8 */
const r1 = rules0[1];   /* deny  all        */

check("allow_entry_deny_false", r0.deny === false, r0.deny);
check("allow_entry_cidr",       r0.cidr === "10.0.0.0/8", r0.cidr);
check("deny_all_deny_true",     r1.deny === true, r1.deny);
check("deny_all_cidr",          r1.cidr === "all", r1.cidr);

/* ---- rules setter: replace list ------------------------------------ */
access.rules = [
    { deny: false, cidr: "192.168.1.0/24" },
    { deny: true,  cidr: "all" }
];

const rules1 = access.rules;
check("rules_set_count",       rules1.length === 2, rules1.length);
check("rules_set_first_allow", rules1[0].deny === false, rules1[0].deny);
check("rules_set_first_cidr",  rules1[0].cidr === "192.168.1.0/24",
      rules1[0].cidr);
check("rules_set_second_deny", rules1[1].deny === true, rules1[1].deny);
check("rules_set_second_all",  rules1[1].cidr === "all", rules1[1].cidr);

/* ---- rules setter: host address (no prefix) ----------------------- */
access.rules = [{ deny: false, cidr: "203.0.113.5" }];
const rules2 = access.rules;
check("host_cidr_roundtrip",   rules2[0].cidr === "203.0.113.5",
      rules2[0].cidr);

/* ---- rules setter: empty array resets rules ----------------------- */
access.rules = [];
const rules3 = access.rules;
check("empty_rules_cleared",   rules3.length === 0, rules3.length);

/* ---- invalid CIDR throws ------------------------------------------ */
let threwBadCidr = false;
try {
    access.rules = [{ deny: false, cidr: "not-an-ip" }];
} catch (e) {
    threwBadCidr = true;
}
check("bad_cidr_throws", threwBadCidr);

/* ---- rules6 getter (empty since config has no IPv6 rules) --------- */
const rules6 = access.rules6;
check("rules6_is_array",  Array.isArray(rules6), typeof rules6);

/* ---- rules6 setter ------------------------------------------------- */
access.rules6 = [
    { deny: false, cidr: "2001:db8::/32" },
    { deny: true,  cidr: "all" }
];

const rules6b = access.rules6;
check("rules6_set_count",  rules6b.length === 2, rules6b.length);
check("rules6_allow_cidr", rules6b[0].cidr === "2001:db8::/32",
      rules6b[0].cidr);
check("rules6_deny_all",   rules6b[1].cidr === "all", rules6b[1].cidr);

/* ---- rulesUnix getter --------------------------------------------- */
const rulesUnix = access.rulesUnix;
check("rulesUnix_is_array",  Array.isArray(rulesUnix), typeof rulesUnix);

/* ---- rulesUnix setter --------------------------------------------- */
access.rulesUnix = [
    { deny: false },
    { deny: true  }
];

const rulesUnix2 = access.rulesUnix;
check("rulesUnix_set_count",  rulesUnix2.length === 2, rulesUnix2.length);
check("rulesUnix_allow",      rulesUnix2[0].deny === false,
      rulesUnix2[0].deny);
check("rulesUnix_deny",       rulesUnix2[1].deny === true,
      rulesUnix2[1].deny);
JS

$t->try_run('no js module or stream module')->plan(22);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS rules_is_array/,         'rules is array');
like($log, qr/JSTEST PASS rules_initial_count/,    'initial rule count is 2');
like($log, qr/JSTEST PASS allow_entry_deny_false/,  'first rule: deny=false');
like($log, qr/JSTEST PASS allow_entry_cidr/,        'first rule: cidr=10.0.0.0/8');
like($log, qr/JSTEST PASS deny_all_deny_true/,      'second rule: deny=true');
like($log, qr/JSTEST PASS deny_all_cidr/,           'second rule: cidr=all');
like($log, qr/JSTEST PASS rules_set_count/,         'setter: count is 2');
like($log, qr/JSTEST PASS rules_set_first_allow/,   'setter: first deny=false');
like($log, qr/JSTEST PASS rules_set_first_cidr/,    'setter: first cidr=192.168.1.0/24');
like($log, qr/JSTEST PASS rules_set_second_deny/,   'setter: second deny=true');
like($log, qr/JSTEST PASS rules_set_second_all/,    'setter: second cidr=all');
like($log, qr/JSTEST PASS host_cidr_roundtrip/,     'host address roundtrips without prefix');
like($log, qr/JSTEST PASS empty_rules_cleared/,     'empty array clears rules');
like($log, qr/JSTEST PASS bad_cidr_throws/,         'invalid CIDR throws TypeError');
like($log, qr/JSTEST PASS rules6_is_array/,         'rules6 is array');
like($log, qr/JSTEST PASS rules6_set_count/,        'rules6 setter: count is 2');
like($log, qr/JSTEST PASS rules6_allow_cidr/,       'rules6 setter: prefix roundtrip');
like($log, qr/JSTEST PASS rules6_deny_all/,         'rules6 setter: all entry');
like($log, qr/JSTEST PASS rulesUnix_is_array/,      'rulesUnix is array');
like($log, qr/JSTEST PASS rulesUnix_set_count/,     'rulesUnix setter: count is 2');
like($log, qr/JSTEST PASS rulesUnix_allow/,         'rulesUnix first: deny=false');
like($log, qr/JSTEST PASS rulesUnix_deny/,          'rulesUnix second: deny=true');
