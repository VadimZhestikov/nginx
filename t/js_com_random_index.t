#!/usr/bin/perl

# Tests for Stage 13r COM expansion: random_index location configuration
# exposed as properties of location.randomIndex (NginxRandomIndex class).
#
# New property on NginxLocation:
#   randomIndex   NginxRandomIndex
#
# NginxRandomIndex properties (all read-only):
#   enable  boolean  — random_index on/off

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

js_include %%TESTDIR%%/init_random_index.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — random_index not enabled
        location /default {
        }

        # random_index enabled
        location /gallery {
            random_index on;
        }
    }
}
EOF

$t->write_file('init_random_index.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /gallery (g)
const defLoc     = srv.locations[0];
const galleryLoc = srv.locations[1];

// ---- default: random_index not set ----
const rd = defLoc.randomIndex;
check("rd_obj",    typeof rd === "object" && rd !== null, typeof rd);
check("rd_enable", rd.enable === false,                   rd.enable);

// ---- gallery: random_index on ----
const rg = galleryLoc.randomIndex;
check("rg_obj",    typeof rg === "object" && rg !== null, typeof rg);
check("rg_enable", rg.enable === true,                    rg.enable);
JS

$t->try_run('no random_index module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS rd_obj/,    'default: randomIndex is object');
like($log, qr/JSTEST PASS rd_enable/, 'default: enable == false');
like($log, qr/JSTEST PASS rg_obj/,    'gallery: randomIndex is object');
like($log, qr/JSTEST PASS rg_enable/, 'gallery: enable == true');
