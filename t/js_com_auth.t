#!/usr/bin/perl

# Tests for Stage 11b COM expansion: auth_basic location configuration
# exposed as properties of location.auth (NginxAuth class).
#
# New property on NginxLocation:
#   auth   NginxAuth
#
# NginxAuth properties (all read-only):
#   realm      string | null   auth_basic realm (literal) or null
#   userFile   string | null   auth_basic_user_file path (literal) or null

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http auth_basic/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_auth.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # auth_basic enabled with literal realm and file
        location /auth_on {
            auth_basic           "My Realm";
            auth_basic_user_file /tmp/test.htpasswd;
        }

        # auth_basic off
        location /auth_off {
            auth_basic  off;
        }

        # no auth directives — realm and userFile should be null
        location /plain {
        }
    }
}
EOF

$t->write_file('init_auth.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /auth_off (auth_o-f) < /auth_on (auth_o-n) < /plain (p)
const offLoc   = srv.locations[0];
const onLoc    = srv.locations[1];
const plainLoc = srv.locations[2];

const ao = onLoc.auth;
const af = offLoc.auth;
const ap = plainLoc.auth;

// ---- auth object ----
check("auth_obj",      typeof ao === "object" && ao !== null, typeof ao);

// ---- auth on: realm and userFile ----
check("realm_str",     typeof ao.realm === "string",          typeof ao.realm);
check("realm_val",     ao.realm === "My Realm",               ao.realm);
check("file_str",      typeof ao.userFile === "string",       typeof ao.userFile);
check("file_val",      ao.userFile === "/tmp/test.htpasswd",  ao.userFile);

// ---- auth off: realm is "off" ----
check("realm_off",     af.realm === "off",                    af.realm);

// ---- plain: no auth_basic directives → null ----
check("plain_realm",   ap.realm === null,                     ap.realm);
check("plain_file",    ap.userFile === null,                  ap.userFile);
JS

$t->try_run('no auth_basic module')->plan(8);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS auth_obj/,    'location.auth is object');
like($log, qr/JSTEST PASS realm_str/,   'location.auth.realm is string');
like($log, qr/JSTEST PASS realm_val/,   'location.auth.realm == "My Realm"');
like($log, qr/JSTEST PASS file_str/,    'location.auth.userFile is string');
like($log, qr/JSTEST PASS file_val/,    'location.auth.userFile == "/tmp/test.htpasswd"');
like($log, qr/JSTEST PASS realm_off/,   'auth off: realm == "off"');
like($log, qr/JSTEST PASS plain_realm/, 'plain: realm == null');
like($log, qr/JSTEST PASS plain_file/,  'plain: userFile == null');
