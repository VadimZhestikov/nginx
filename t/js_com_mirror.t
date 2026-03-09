#!/usr/bin/perl

# Tests for Stage 13w COM expansion: mirror location configuration
# exposed as properties of location.mirror (NginxMirror class).
#
# New property on NginxLocation:
#   mirror   NginxMirror
#
# NginxMirror properties (all read-only):
#   uris         string[]  — mirror target URIs (empty array if none)
#   requestBody  boolean   — mirror_request_body on/off

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

js_include %%TESTDIR%%/init_mirror.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no mirror directives
        location /default {
        }

        # one mirror target, request body disabled
        location /one {
            mirror              /backend;
            mirror_request_body off;
        }

        # two mirror targets, request body enabled
        location /two {
            mirror /backend1;
            mirror /backend2;
            mirror_request_body on;
        }

        location /backend  { return 200; }
        location /backend1 { return 200; }
        location /backend2 { return 200; }
    }
}
EOF

$t->write_file('init_mirror.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /backend(1/2) < /default < /one < /two
// Find by path
let defLoc, oneLoc, twoLoc;
for (const loc of srv.locations) {
    if (loc.path === "/default")  defLoc = loc;
    else if (loc.path === "/one") oneLoc = loc;
    else if (loc.path === "/two") twoLoc = loc;
}

// ---- default: no mirror ----
const md = defLoc.mirror;
check("md_obj",  typeof md === "object" && md !== null, typeof md);
check("md_uris", Array.isArray(md.uris) && md.uris.length === 0, md.uris);
check("md_rb",   typeof md.requestBody === "boolean",  typeof md.requestBody);

// ---- one mirror target ----
const mo = oneLoc.mirror;
check("mo_obj",   typeof mo === "object" && mo !== null, typeof mo);
check("mo_uris",  mo.uris.length === 1,                  mo.uris.length);
check("mo_uri0",  mo.uris[0] === "/backend",             mo.uris[0]);
check("mo_rb",    mo.requestBody === false,               mo.requestBody);

// ---- two mirror targets ----
const mt = twoLoc.mirror;
check("mt_obj",   typeof mt === "object" && mt !== null, typeof mt);
check("mt_uris",  mt.uris.length === 2,                  mt.uris.length);
check("mt_uri0",  mt.uris[0] === "/backend1",            mt.uris[0]);
check("mt_uri1",  mt.uris[1] === "/backend2",            mt.uris[1]);
check("mt_rb",    mt.requestBody === true,                mt.requestBody);
JS

$t->try_run('no mirror module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS md_obj/,  'default: mirror is object');
like($log, qr/JSTEST PASS md_uris/, 'default: uris is empty array');
like($log, qr/JSTEST PASS md_rb/,   'default: requestBody is boolean');
like($log, qr/JSTEST PASS mo_obj/,  'one: mirror is object');
like($log, qr/JSTEST PASS mo_uris/, 'one: uris.length == 1');
like($log, qr/JSTEST PASS mo_uri0/, 'one: uris[0] == "/backend"');
like($log, qr/JSTEST PASS mo_rb/,   'one: requestBody == false');
like($log, qr/JSTEST PASS mt_obj/,  'two: mirror is object');
like($log, qr/JSTEST PASS mt_uris/, 'two: uris.length == 2');
like($log, qr/JSTEST PASS mt_uri0/, 'two: uris[0] == "/backend1"');
like($log, qr/JSTEST PASS mt_uri1/, 'two: uris[1] == "/backend2"');
like($log, qr/JSTEST PASS mt_rb/,   'two: requestBody == true');
