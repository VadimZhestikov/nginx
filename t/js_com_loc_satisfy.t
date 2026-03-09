#!/usr/bin/perl

# Tests for Stage 13y COM expansion: satisfy and limitExcept properties
# on NginxLocation (plain getters, no sub-object).
#
# New properties on NginxLocation:
#   satisfy      string    — "all" | "any"
#   limitExcept  string[]  — allowed method names (empty if no limit_except)

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

js_include %%TESTDIR%%/init_satisfy.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — satisfy all, no limit_except
        location /default {
        }

        # satisfy any
        location /any {
            satisfy any;
            deny all;
        }

        # limit_except restricts to GET+HEAD (allows all others to bypass)
        location /readonly {
            limit_except GET HEAD {
                deny all;
            }
        }

        # limit_except with POST and PUT allowed
        location /write {
            limit_except POST PUT {
                deny all;
            }
        }
    }
}
EOF

$t->write_file('init_satisfy.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
let defLoc, anyLoc, roLoc, writeLoc;
for (const loc of srv.locations) {
    if      (loc.path === "/default")  defLoc   = loc;
    else if (loc.path === "/any")      anyLoc   = loc;
    else if (loc.path === "/readonly") roLoc    = loc;
    else if (loc.path === "/write")    writeLoc = loc;
}

// ---- default: satisfy all, no limit_except ----
check("def_satisfy",   defLoc.satisfy  === "all",                          defLoc.satisfy);
check("def_le_arr",    Array.isArray(defLoc.limitExcept),                  defLoc.limitExcept);
check("def_le_empty",  defLoc.limitExcept.length === 0,                    defLoc.limitExcept.length);

// ---- any: satisfy any ----
check("any_satisfy",   anyLoc.satisfy === "any",                           anyLoc.satisfy);

// ---- readonly: limit_except GET HEAD ----
// limit_except stores the RESTRICTED methods (complement of the listed ones)
// so GET/HEAD are excepted → bitmask has POST, PUT, DELETE, etc.
const ro = roLoc.limitExcept;
check("ro_le_arr",     Array.isArray(ro),                                  ro);
check("ro_le_no_get",  !ro.includes("GET"),                                ro);
check("ro_le_no_hd",   !ro.includes("HEAD"),                               ro);
check("ro_le_has_post", ro.includes("POST"),                               ro);

// ---- write: limit_except POST PUT ----
// POST/PUT are excepted → bitmask has GET, HEAD, DELETE, etc.
const wr = writeLoc.limitExcept;
check("wr_le_arr",     Array.isArray(wr),                                  wr);
check("wr_le_no_po",   !wr.includes("POST"),                               wr);
check("wr_le_no_put",  !wr.includes("PUT"),                                wr);
check("wr_le_has_get", wr.includes("GET"),                                 wr);
JS

$t->try_run('no http module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS def_satisfy/,   'default: satisfy == "all"');
like($log, qr/JSTEST PASS def_le_arr/,    'default: limitExcept is array');
like($log, qr/JSTEST PASS def_le_empty/,  'default: limitExcept is empty');
like($log, qr/JSTEST PASS any_satisfy/,   'any: satisfy == "any"');
like($log, qr/JSTEST PASS ro_le_arr/,      'readonly: limitExcept is array');
like($log, qr/JSTEST PASS ro_le_no_get/,   'readonly: excludes GET (excepted)');
like($log, qr/JSTEST PASS ro_le_no_hd/,    'readonly: excludes HEAD (excepted)');
like($log, qr/JSTEST PASS ro_le_has_post/, 'readonly: includes POST (restricted)');
like($log, qr/JSTEST PASS wr_le_arr/,      'write: limitExcept is array');
like($log, qr/JSTEST PASS wr_le_no_po/,    'write: excludes POST (excepted)');
like($log, qr/JSTEST PASS wr_le_no_put/,   'write: excludes PUT (excepted)');
like($log, qr/JSTEST PASS wr_le_has_get/,  'write: includes GET (restricted)');
