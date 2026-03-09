#!/usr/bin/perl

# Tests for Stage 13v COM expansion: uwsgi location configuration
# exposed as properties of location.uwsgi (NginxUwsgi class).
#
# New property on NginxLocation:
#   uwsgi   NginxUwsgi
#
# NginxUwsgi properties (all read-only):
#   connectTimeout  number  — uwsgi_connect_timeout (ms)
#   sendTimeout     number  — uwsgi_send_timeout (ms)
#   readTimeout     number  — uwsgi_read_timeout (ms)
#   bufferSize      number  — uwsgi_buffer_size (bytes)
#   modifier1       number  — uwsgi_modifier1 (default 0)
#   modifier2       number  — uwsgi_modifier2 (default 0)

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

js_include %%TESTDIR%%/init_uwsgi.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no uwsgi directives (defaults apply)
        location /default {
        }

        # custom uwsgi settings
        location /app {
            uwsgi_pass            127.0.0.1:9000;
            uwsgi_connect_timeout 2s;
            uwsgi_send_timeout    3s;
            uwsgi_read_timeout    4s;
            uwsgi_buffers         4 16k;
            uwsgi_buffer_size     16k;
            uwsgi_modifier1       1;
            uwsgi_modifier2       2;
        }
    }
}
EOF

$t->write_file('init_uwsgi.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /app (a) < /default (d)
const appLoc = srv.locations[0];
const defLoc = srv.locations[1];

// ---- default: inherited defaults ----
const ud = defLoc.uwsgi;
check("ud_obj",   typeof ud === "object" && ud !== null, typeof ud);
check("ud_conn",  ud.connectTimeout > 0,                 ud.connectTimeout);
check("ud_send",  ud.sendTimeout    > 0,                 ud.sendTimeout);
check("ud_read",  ud.readTimeout    > 0,                 ud.readTimeout);
check("ud_buf",   ud.bufferSize     > 0,                 ud.bufferSize);
check("ud_mod1",  ud.modifier1      === 0,               ud.modifier1);
check("ud_mod2",  ud.modifier2      === 0,               ud.modifier2);

// ---- app: custom uwsgi settings ----
const ua = appLoc.uwsgi;
check("ua_obj",   typeof ua === "object" && ua !== null, typeof ua);
check("ua_conn",  ua.connectTimeout === 2000,            ua.connectTimeout);
check("ua_send",  ua.sendTimeout    === 3000,            ua.sendTimeout);
check("ua_read",  ua.readTimeout    === 4000,            ua.readTimeout);
check("ua_buf",   ua.bufferSize     === 16 * 1024,       ua.bufferSize);
check("ua_mod1",  ua.modifier1      === 1,               ua.modifier1);
check("ua_mod2",  ua.modifier2      === 2,               ua.modifier2);
JS

$t->try_run('no uwsgi module')->plan(14);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ud_obj/,  'default: uwsgi is object');
like($log, qr/JSTEST PASS ud_conn/, 'default: connectTimeout > 0');
like($log, qr/JSTEST PASS ud_send/, 'default: sendTimeout > 0');
like($log, qr/JSTEST PASS ud_read/, 'default: readTimeout > 0');
like($log, qr/JSTEST PASS ud_buf/,  'default: bufferSize > 0');
like($log, qr/JSTEST PASS ud_mod1/, 'default: modifier1 == 0');
like($log, qr/JSTEST PASS ud_mod2/, 'default: modifier2 == 0');
like($log, qr/JSTEST PASS ua_obj/,  'app: uwsgi is object');
like($log, qr/JSTEST PASS ua_conn/, 'app: connectTimeout == 2000');
like($log, qr/JSTEST PASS ua_send/, 'app: sendTimeout == 3000');
like($log, qr/JSTEST PASS ua_read/, 'app: readTimeout == 4000');
like($log, qr/JSTEST PASS ua_buf/,  'app: bufferSize == 16k');
like($log, qr/JSTEST PASS ua_mod1/, 'app: modifier1 == 1');
like($log, qr/JSTEST PASS ua_mod2/, 'app: modifier2 == 2');
