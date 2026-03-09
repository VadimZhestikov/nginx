#!/usr/bin/perl

# Tests for Stage 13h COM expansion: WebDAV location configuration
# exposed as properties of location.dav (NginxDav class).
#
# New property on NginxLocation:
#   dav   NginxDav
#
# NginxDav properties (all read-only):
#   methods[]          Array of strings — enabled DAV methods
#   access             number  — dav_access octal permission bits
#   minDeleteDepth     number  — min_delete_depth
#   createFullPutPath  boolean — create_full_put_path on/off

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

js_source %%TESTDIR%%/init_dav.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no dav_methods set
        location /default {
        }

        # all methods enabled, custom access, full put path
        location /full {
            dav_methods PUT DELETE MKCOL COPY MOVE;
            dav_access  user:rw group:rw all:r;
            create_full_put_path on;
        }

        # subset of methods, min_delete_depth
        location /sub {
            dav_methods PUT DELETE;
            min_delete_depth 2;
        }
    }
}
EOF

$t->write_file('init_dav.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /full (f) < /sub (s)
const defLoc  = srv.locations[0];
const fullLoc = srv.locations[1];
const subLoc  = srv.locations[2];

// ---- default: no dav_methods ----
const dd = defLoc.dav;
check("dd_obj",    typeof dd === "object" && dd !== null, typeof dd);
check("dd_empty",  Array.isArray(dd.methods) && dd.methods.length === 0, dd.methods);

// ---- full: all methods ----
const df = fullLoc.dav;
check("df_obj",    typeof df === "object" && df !== null, typeof df);
const fm = df.methods;
check("df_mlen",   fm.length === 5,                       fm.length);
check("df_put",    fm.indexOf("PUT")    >= 0,             fm);
check("df_delete", fm.indexOf("DELETE") >= 0,             fm);
check("df_mkcol",  fm.indexOf("MKCOL")  >= 0,             fm);
check("df_copy",   fm.indexOf("COPY")   >= 0,             fm);
check("df_move",   fm.indexOf("MOVE")   >= 0,             fm);
check("df_cfpp",   df.createFullPutPath === true,          df.createFullPutPath);

// ---- sub: PUT + DELETE only, depth 2 ----
const ds = subLoc.dav;
const sm = ds.methods;
check("ds_mlen",   sm.length === 2,                        sm.length);
check("ds_put",    sm.indexOf("PUT")    >= 0,              sm);
check("ds_delete", sm.indexOf("DELETE") >= 0,              sm);
check("ds_depth",  ds.minDeleteDepth === 2,                ds.minDeleteDepth);
JS

$t->try_run('no dav module')->plan(14);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS dd_obj/,    'location.dav is object');
like($log, qr/JSTEST PASS dd_empty/,  'default: methods[] is empty array');
like($log, qr/JSTEST PASS df_obj/,    'full: location.dav is object');
like($log, qr/JSTEST PASS df_mlen/,   'full: methods[] has 5 entries');
like($log, qr/JSTEST PASS df_put/,    'full: methods includes PUT');
like($log, qr/JSTEST PASS df_delete/, 'full: methods includes DELETE');
like($log, qr/JSTEST PASS df_mkcol/,  'full: methods includes MKCOL');
like($log, qr/JSTEST PASS df_copy/,   'full: methods includes COPY');
like($log, qr/JSTEST PASS df_move/,   'full: methods includes MOVE');
like($log, qr/JSTEST PASS df_cfpp/,   'full: createFullPutPath == true');
like($log, qr/JSTEST PASS ds_mlen/,   'sub: methods[] has 2 entries');
like($log, qr/JSTEST PASS ds_put/,    'sub: methods includes PUT');
like($log, qr/JSTEST PASS ds_delete/, 'sub: methods includes DELETE');
like($log, qr/JSTEST PASS ds_depth/,  'sub: minDeleteDepth == 2');
