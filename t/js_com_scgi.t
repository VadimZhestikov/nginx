#!/usr/bin/perl

# Tests for Stage 13v COM expansion: scgi location configuration
# exposed as properties of location.scgi (NginxScgi class).
#
# New property on NginxLocation:
#   scgi   NginxScgi
#
# NginxScgi properties (all read-only):
#   connectTimeout  number  — scgi_connect_timeout (ms)
#   sendTimeout     number  — scgi_send_timeout (ms)
#   readTimeout     number  — scgi_read_timeout (ms)
#   bufferSize      number  — scgi_buffer_size (bytes)

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

js_source %%TESTDIR%%/init_scgi.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no scgi directives (defaults apply)
        location /default {
        }

        # custom scgi settings
        location /app {
            scgi_pass            127.0.0.1:9000;
            scgi_connect_timeout 2s;
            scgi_send_timeout    3s;
            scgi_read_timeout    4s;
            scgi_buffers         4 16k;
            scgi_buffer_size     16k;
        }
    }
}
EOF

$t->write_file('init_scgi.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /app (a) < /default (d)
const appLoc = srv.locations[0];
const defLoc = srv.locations[1];

// ---- default: inherited defaults ----
const sd = defLoc.scgi;
check("sd_obj",  typeof sd === "object" && sd !== null, typeof sd);
check("sd_conn", sd.connectTimeout > 0,                 sd.connectTimeout);
check("sd_send", sd.sendTimeout    > 0,                 sd.sendTimeout);
check("sd_read", sd.readTimeout    > 0,                 sd.readTimeout);
check("sd_buf",  sd.bufferSize     > 0,                 sd.bufferSize);

// ---- app: custom scgi settings ----
const sa = appLoc.scgi;
check("sa_obj",  typeof sa === "object" && sa !== null, typeof sa);
check("sa_conn", sa.connectTimeout === 2000,            sa.connectTimeout);
check("sa_send", sa.sendTimeout    === 3000,            sa.sendTimeout);
check("sa_read", sa.readTimeout    === 4000,            sa.readTimeout);
check("sa_buf",  sa.bufferSize     === 16 * 1024,       sa.bufferSize);
JS

$t->try_run('no scgi module')->plan(10);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS sd_obj/,  'default: scgi is object');
like($log, qr/JSTEST PASS sd_conn/, 'default: connectTimeout > 0');
like($log, qr/JSTEST PASS sd_send/, 'default: sendTimeout > 0');
like($log, qr/JSTEST PASS sd_read/, 'default: readTimeout > 0');
like($log, qr/JSTEST PASS sd_buf/,  'default: bufferSize > 0');
like($log, qr/JSTEST PASS sa_obj/,  'app: scgi is object');
like($log, qr/JSTEST PASS sa_conn/, 'app: connectTimeout == 2000');
like($log, qr/JSTEST PASS sa_send/, 'app: sendTimeout == 3000');
like($log, qr/JSTEST PASS sa_read/, 'app: readTimeout == 4000');
like($log, qr/JSTEST PASS sa_buf/,  'app: bufferSize == 16k');
