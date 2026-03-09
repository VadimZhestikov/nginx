#!/usr/bin/perl

# Tests for Stage 13s COM expansion: auth_request location configuration
# exposed as properties of location.authRequest (NginxAuthRequest class).
#
# New property on NginxLocation:
#   authRequest   NginxAuthRequest
#
# NginxAuthRequest properties (all read-only):
#   uri   string  — auth_request subrequest URI (empty if not configured)

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

js_include %%TESTDIR%%/init_auth_request.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no auth_request directive
        location /default {
        }

        # auth_request configured
        location /protected {
            auth_request /auth;
        }

        # auth backend (never called in tests)
        location /auth {
            return 200;
        }
    }
}
EOF

$t->write_file('init_auth_request.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /auth (a) < /default (d) < /protected (p)
const authLoc      = srv.locations[0];
const defLoc       = srv.locations[1];
const protectedLoc = srv.locations[2];

// ---- default: no auth_request ----
const ad = defLoc.authRequest;
check("ad_obj", typeof ad === "object" && ad !== null, typeof ad);
check("ad_uri", ad.uri === "",                         ad.uri);

// ---- protected: auth_request /auth ----
const ap = protectedLoc.authRequest;
check("ap_obj", typeof ap === "object" && ap !== null, typeof ap);
check("ap_uri", ap.uri === "/auth",                    ap.uri);
JS

$t->try_run('no auth_request module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ad_obj/, 'default: authRequest is object');
like($log, qr/JSTEST PASS ad_uri/, 'default: uri == ""');
like($log, qr/JSTEST PASS ap_obj/, 'protected: authRequest is object');
like($log, qr/JSTEST PASS ap_uri/, 'protected: uri == "/auth"');
