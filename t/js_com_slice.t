#!/usr/bin/perl

# Tests for Stage 13m COM expansion: slice location configuration
# exposed as properties of location.slice (NginxSlice class).
#
# New property on NginxLocation:
#   slice   NginxSlice
#
# NginxSlice properties (all read-only):
#   size   number — bytes per subrequest (0 = disabled)

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

js_include %%TESTDIR%%/init_slice.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — slice 0 (off)
        location /default {
        }

        # slice enabled with 1m chunks
        location /sliced {
            slice 1m;
        }
    }
}
EOF

$t->write_file('init_slice.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /sliced (s)
const defLoc    = srv.locations[0];
const slicedLoc = srv.locations[1];

// ---- default: slice 0 ----
const sd = defLoc.slice;
check("sd_obj",  typeof sd === "object" && sd !== null, typeof sd);
check("sd_size", sd.size === 0,                         sd.size);

// ---- sliced: 1m = 1048576 bytes ----
const ss = slicedLoc.slice;
check("ss_obj",  typeof ss === "object" && ss !== null, typeof ss);
check("ss_size", ss.size === 1048576,                   ss.size);
JS

$t->try_run('no slice module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS sd_obj/,  'location.slice is object');
like($log, qr/JSTEST PASS sd_size/, 'default: size == 0');
like($log, qr/JSTEST PASS ss_obj/,  'sliced: location.slice is object');
like($log, qr/JSTEST PASS ss_size/, 'sliced: size == 1048576');
