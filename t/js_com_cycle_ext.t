#!/usr/bin/perl

# Tests for Stage 17 COM expansion: additional nginx.cycle properties.
#
# New read-only properties on the NginxCycle object:
#   confFile      string  — full path to nginx.conf
#   errorLog      string  — error log path
#   installPrefix string  — installation prefix (cycle->prefix)
#   pid           string  — pid file path
#   connectionN   number  — max connections per worker
#   daemon        boolean — daemon directive value

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

js_include %%TESTDIR%%/init_cycle_ext.js;

events {
    worker_connections 512;
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;
        location / { }
    }
}
EOF

$t->write_file('init_cycle_ext.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const c = nginx.cycle;

// confFile — must be a non-empty string
check("cf_str",   typeof c.confFile === "string" && c.confFile.length > 0,
                  c.confFile);

// confFile must end with nginx.conf
check("cf_name",  c.confFile.endsWith("nginx.conf"), c.confFile);

// errorLog — non-empty string
check("el_str",   typeof c.errorLog === "string" && c.errorLog.length > 0,
                  c.errorLog);

// installPrefix — non-empty string ending with /
check("ip_str",   typeof c.installPrefix === "string" && c.installPrefix.length > 0,
                  c.installPrefix);
check("ip_slash", c.installPrefix.endsWith("/"), c.installPrefix);

// pid — non-empty string
check("pid_str",  typeof c.pid === "string" && c.pid.length > 0, c.pid);

// connectionN — positive number (worker_connections 512)
check("cn_num",   typeof c.connectionN === "number", c.connectionN);
check("cn_512",   c.connectionN === 512, c.connectionN);

// daemon — false (daemon off in this test)
check("daemon_bool", typeof c.daemon === "boolean", c.daemon);
check("daemon_off",  c.daemon === false, c.daemon);
JS

$t->try_run('no js module')->plan(10);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS cf_str/,      'confFile is non-empty string');
like($log, qr/JSTEST PASS cf_name/,     'confFile ends with nginx.conf');
like($log, qr/JSTEST PASS el_str/,      'errorLog is non-empty string');
like($log, qr/JSTEST PASS ip_str/,      'installPrefix is non-empty string');
like($log, qr/JSTEST PASS ip_slash/,    'installPrefix ends with /');
like($log, qr/JSTEST PASS pid_str/,     'pid is non-empty string');
like($log, qr/JSTEST PASS cn_num/,      'connectionN is number');
like($log, qr/JSTEST PASS cn_512/,      'connectionN == 512');
like($log, qr/JSTEST PASS daemon_bool/, 'daemon is boolean');
like($log, qr/JSTEST PASS daemon_off/,  'daemon == false (daemon off)');
