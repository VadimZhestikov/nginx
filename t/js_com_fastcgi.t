#!/usr/bin/perl

# Tests for Stage 13a COM expansion: fastcgi location configuration
# exposed as properties of location.fastcgi (NginxFastCGI class).
#
# New property on NginxLocation:
#   fastcgi   NginxFastCGI | null   (null when no fastcgi_pass)
#
# NginxFastCGI properties (all read-only):
#   pass             string|null
#   index            string|null
#   keepConn         boolean
#   connectTimeout   number (ms)
#   sendTimeout      number (ms)
#   readTimeout      number (ms)
#   buffering        boolean
#   requestBuffering boolean
#   interceptErrors  boolean
#   params           object[]  [{key, value}]
#   catchStderr      string[]

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http fastcgi/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_fastcgi.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # fastcgi location with custom params and settings
        location /fcgi {
            fastcgi_pass             127.0.0.1:9000;
            fastcgi_index            index.php;
            fastcgi_keep_conn        on;
            fastcgi_connect_timeout  5s;
            fastcgi_send_timeout     10s;
            fastcgi_read_timeout     30s;
            fastcgi_buffering        off;
            fastcgi_request_buffering off;
            fastcgi_intercept_errors  on;
            fastcgi_param            SCRIPT_FILENAME /var/www$fastcgi_script_name;
            fastcgi_param            SERVER_NAME     myhost;
            fastcgi_catch_stderr     "PHP Fatal error";
        }

        # location without fastcgi_pass
        location /plain {
        }
    }
}
EOF

$t->write_file('init_fastcgi.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /fcgi (f) < /plain (p)
const fcgiLoc  = srv.locations[0];
const plainLoc = srv.locations[1];

const fc = fcgiLoc.fastcgi;

// ---- object ----
check("fc_obj",      typeof fc === "object" && fc !== null, typeof fc);

// ---- pass ----
check("pass_str",    typeof fc.pass === "string",           typeof fc.pass);
check("pass_val",    fc.pass === "127.0.0.1:9000",          fc.pass);

// ---- index ----
check("index_str",   typeof fc.index === "string",          typeof fc.index);
check("index_val",   fc.index === "index.php",              fc.index);

// ---- keepConn ----
check("keep_conn",   fc.keepConn === true,                  fc.keepConn);

// ---- timeouts ----
check("connect_to",  fc.connectTimeout === 5000,            fc.connectTimeout);
check("send_to",     fc.sendTimeout === 10000,              fc.sendTimeout);
check("read_to",     fc.readTimeout === 30000,              fc.readTimeout);

// ---- flags ----
check("buffering",   fc.buffering === false,                fc.buffering);
check("req_buf",     fc.requestBuffering === false,         fc.requestBuffering);
check("intercept",   fc.interceptErrors === true,           fc.interceptErrors);

// ---- params ----
const p = fc.params;
check("params_arr",  Array.isArray(p),                      typeof p);
check("params_len",  p.length === 2,                        p.length);
check("p0_key",      p[0].key === "SCRIPT_FILENAME",        p[0].key);
check("p1_key",      p[1].key === "SERVER_NAME",            p[1].key);
check("p1_val",      p[1].value === "myhost",               p[1].value);

// ---- catchStderr ----
const cs = fc.catchStderr;
check("cs_arr",      Array.isArray(cs),                     typeof cs);
check("cs_len",      cs.length === 1,                       cs.length);
check("cs_val",      cs[0] === "PHP Fatal error",           cs[0]);

// ---- plain location: fastcgi is null ----
check("plain_null",  plainLoc.fastcgi === null,             plainLoc.fastcgi);
JS

$t->try_run('no fastcgi module')->plan(21);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS fc_obj/,      'location.fastcgi is object');
like($log, qr/JSTEST PASS pass_str/,    'fastcgi.pass is string');
like($log, qr/JSTEST PASS pass_val/,    'fastcgi.pass == "127.0.0.1:9000"');
like($log, qr/JSTEST PASS index_str/,   'fastcgi.index is string');
like($log, qr/JSTEST PASS index_val/,   'fastcgi.index == "index.php"');
like($log, qr/JSTEST PASS keep_conn/,   'fastcgi.keepConn == true');
like($log, qr/JSTEST PASS connect_to/,  'fastcgi.connectTimeout == 5000');
like($log, qr/JSTEST PASS send_to/,     'fastcgi.sendTimeout == 10000');
like($log, qr/JSTEST PASS read_to/,     'fastcgi.readTimeout == 30000');
like($log, qr/JSTEST PASS buffering/,   'fastcgi.buffering == false');
like($log, qr/JSTEST PASS req_buf/,     'fastcgi.requestBuffering == false');
like($log, qr/JSTEST PASS intercept/,   'fastcgi.interceptErrors == true');
like($log, qr/JSTEST PASS params_arr/,  'fastcgi.params is array');
like($log, qr/JSTEST PASS params_len/,  'fastcgi.params.length == 2');
like($log, qr/JSTEST PASS p0_key/,      'params[0].key == "SCRIPT_FILENAME"');
like($log, qr/JSTEST PASS p1_key/,      'params[1].key == "SERVER_NAME"');
like($log, qr/JSTEST PASS p1_val/,      'params[1].value == "myhost"');
like($log, qr/JSTEST PASS cs_arr/,      'fastcgi.catchStderr is array');
like($log, qr/JSTEST PASS cs_len/,      'fastcgi.catchStderr.length == 1');
like($log, qr/JSTEST PASS cs_val/,      'fastcgi.catchStderr[0] == "PHP Fatal error"');
like($log, qr/JSTEST PASS plain_null/,  'plain location.fastcgi == null');
