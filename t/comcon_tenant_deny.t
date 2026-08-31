#!/usr/bin/perl

# COMCON increment A (A2.0): a js_tenant_source runs in a reduced,
# deny-by-default compartment — it cannot reach the `nginx` host authority
# surface, while a host js_source in the same config can. Proves the primary
# confinement control (the environment), not just the A1 reach gates.

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

js_source %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location / { }
    }
}
EOF

# Host script: full authority — nginx IS an object (the control).
$t->write_file('host.js', <<'JS');
if (typeof nginx === "object" && nginx !== null) {
    nginx.log(6, "JSTEST PASS host_sees_nginx");
} else {
    nginx.log(6, "JSTEST FAIL host_sees_nginx");
}
JS

# Tenant script: reduced compartment — only report() is granted; nginx must be
# unreachable, and touching a withheld capability must fail.
$t->write_file('tenant.js', <<'JS');
report("PASS tenant_ran");

if (typeof nginx === "undefined") {
    report("PASS tenant_no_nginx");
} else {
    report("FAIL tenant_no_nginx: " + typeof nginx);
}

try {
    nginx.createSocket("127.0.0.1:1");
    report("FAIL createSocket_reachable");
} catch (e) {
    report("PASS createSocket_denied");
}
JS

$t->try_run('no js module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS host_sees_nginx/,
     'control: host js_source sees the nginx global');
like($log, qr/js tenant: PASS tenant_ran/,
     'tenant compartment executed');
like($log, qr/js tenant: PASS tenant_no_nginx/,
     'deny-by-default: tenant cannot name the nginx global');
like($log, qr/js tenant: PASS createSocket_denied/,
     'deny-by-default: a withheld capability is unreachable');
