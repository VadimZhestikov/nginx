#!/usr/bin/perl

# Tests for Stage 13i COM expansion: SSI location configuration
# exposed as properties of location.ssi (NginxSsi class).
#
# New property on NginxLocation:
#   ssi   NginxSsi
#
# NginxSsi properties (all read-only):
#   enable                 boolean — ssi on/off
#   silentErrors           boolean — ssi_silent_errors on/off
#   ignoreRecycledBuffers  boolean — ssi_ignore_recycled_buffers on/off
#   lastModified           boolean — ssi_last_modified on/off
#   minFileChunk           number  — ssi_min_file_chunk (bytes)
#   valueLen               number  — ssi_value_len (bytes)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http ssi/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_ssi.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — ssi off (default)
        location /default {
        }

        # ssi on with all options set
        location /full {
            ssi on;
            ssi_silent_errors on;
            ssi_ignore_recycled_buffers on;
            ssi_last_modified on;
            ssi_min_file_chunk 4096;
            ssi_value_length 256;
        }
    }
}
EOF

$t->write_file('init_ssi.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /full (f)
const defLoc  = srv.locations[0];
const fullLoc = srv.locations[1];

// ---- default: ssi off ----
const sd = defLoc.ssi;
check("sd_obj",    typeof sd === "object" && sd !== null, typeof sd);
check("sd_enable", sd.enable === false,                   sd.enable);

// ---- full: all options on ----
const sf = fullLoc.ssi;
check("sf_obj",    typeof sf === "object" && sf !== null, typeof sf);
check("sf_enable", sf.enable === true,                    sf.enable);
check("sf_silent", sf.silentErrors === true,              sf.silentErrors);
check("sf_irb",    sf.ignoreRecycledBuffers === true,     sf.ignoreRecycledBuffers);
check("sf_lm",     sf.lastModified === true,              sf.lastModified);
check("sf_chunk",  sf.minFileChunk === 4096,              sf.minFileChunk);
check("sf_vlen",   sf.valueLen === 256,                   sf.valueLen);
JS

$t->try_run('no ssi module')->plan(9);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS sd_obj/,    'location.ssi is object');
like($log, qr/JSTEST PASS sd_enable/, 'default: enable == false');
like($log, qr/JSTEST PASS sf_obj/,    'full: location.ssi is object');
like($log, qr/JSTEST PASS sf_enable/, 'full: enable == true');
like($log, qr/JSTEST PASS sf_silent/, 'full: silentErrors == true');
like($log, qr/JSTEST PASS sf_irb/,    'full: ignoreRecycledBuffers == true');
like($log, qr/JSTEST PASS sf_lm/,     'full: lastModified == true');
like($log, qr/JSTEST PASS sf_chunk/,  'full: minFileChunk == 4096');
like($log, qr/JSTEST PASS sf_vlen/,   'full: valueLen == 256');
