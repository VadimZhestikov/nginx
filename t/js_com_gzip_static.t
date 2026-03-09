#!/usr/bin/perl

# Tests for Stage 13t COM expansion: gzip_static location configuration
# exposed as properties of location.gzipStatic (NginxGzipStatic class).
#
# New property on NginxLocation:
#   gzipStatic   NginxGzipStatic
#
# NginxGzipStatic properties (all read-only):
#   enable  string  — "off" | "on" | "always"

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

js_source %%TESTDIR%%/init_gzip_static.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — gzip_static not set (off)
        location /default {
        }

        # gzip_static on
        location /on {
            gzip_static on;
        }

        # gzip_static always
        location /always {
            gzip_static always;
        }
    }
}
EOF

$t->write_file('init_gzip_static.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /always (a) < /default (d) < /on (o)
const alwaysLoc  = srv.locations[0];
const defLoc     = srv.locations[1];
const onLoc      = srv.locations[2];

// ---- default: gzip_static off ----
const gd = defLoc.gzipStatic;
check("gd_obj",    typeof gd === "object" && gd !== null, typeof gd);
check("gd_enable", gd.enable === "off",                   gd.enable);

// ---- on ----
const go = onLoc.gzipStatic;
check("go_obj",    typeof go === "object" && go !== null, typeof go);
check("go_enable", go.enable === "on",                    go.enable);

// ---- always ----
const ga = alwaysLoc.gzipStatic;
check("ga_obj",    typeof ga === "object" && ga !== null, typeof ga);
check("ga_enable", ga.enable === "always",                ga.enable);
JS

$t->try_run('no gzip_static module')->plan(6);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS gd_obj/,    'default: gzipStatic is object');
like($log, qr/JSTEST PASS gd_enable/, 'default: enable == "off"');
like($log, qr/JSTEST PASS go_obj/,    'on: gzipStatic is object');
like($log, qr/JSTEST PASS go_enable/, 'on: enable == "on"');
like($log, qr/JSTEST PASS ga_obj/,    'always: gzipStatic is object');
like($log, qr/JSTEST PASS ga_enable/, 'always: enable == "always"');
