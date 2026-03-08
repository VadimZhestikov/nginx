#!/usr/bin/perl

# Tests for Stage 13d COM expansion: charset location configuration
# exposed as properties of location.charset (NginxCharset class).
#
# New property on NginxLocation:
#   charset   NginxCharset
#
# NginxCharset properties (all read-only):
#   charset          string | null   target charset, "off", or null if unset
#   sourceCharset    string | null   source_charset, "off", or null if unset
#   overrideCharset  boolean         override_charset on/off

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http charset/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_charset.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # charset off
        location /off {
            charset off;
        }

        # no directives — all null
        location /plain {
        }

        # source_charset only (no output charset, no map needed)
        location /src {
            source_charset   windows-1251;
            override_charset on;
        }

        # charset only (output charset, no conversion)
        location /utf8 {
            charset UTF-8;
        }
    }
}
EOF

$t->write_file('init_charset.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /off (o) < /plain (p) < /src (s) < /utf8 (u)
const offLoc   = srv.locations[0];
const plainLoc = srv.locations[1];
const srcLoc   = srv.locations[2];
const utf8Loc  = srv.locations[3];

// ---- charset UTF-8 location ----
const cu = utf8Loc.charset;
check("cu_obj",      typeof cu === "object" && cu !== null,   typeof cu);
check("cu_str",      typeof cu.charset === "string",          typeof cu.charset);
check("cu_val",      cu.charset === "UTF-8",                  cu.charset);
// sourceCharset not set → merged to NGX_HTTP_CHARSET_OFF → "off"
check("cu_src_off",  cu.sourceCharset === "off",              cu.sourceCharset);

// ---- source_charset only ----
const cs = srcLoc.charset;
check("cs_src_str",  typeof cs.sourceCharset === "string",    typeof cs.sourceCharset);
check("cs_src_val",  cs.sourceCharset === "windows-1251",     cs.sourceCharset);
check("cs_override", cs.overrideCharset === true,             cs.overrideCharset);
// charset not set → merged to NGX_HTTP_CHARSET_OFF → "off"
check("cs_cs_off",   cs.charset === "off",                    cs.charset);

// ---- charset off ----
const co = offLoc.charset;
check("off_val",     co.charset === "off",                    co.charset);

// ---- plain: no directives — both merged to "off" ----
const cp = plainLoc.charset;
check("plain_cs",    cp.charset === "off",                    cp.charset);
check("plain_src",   cp.sourceCharset === "off",              cp.sourceCharset);
JS

$t->try_run('no charset module')->plan(11);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS cu_obj/,      'location.charset is object');
like($log, qr/JSTEST PASS cu_str/,      'charset.charset is string');
like($log, qr/JSTEST PASS cu_val/,      'charset.charset == "UTF-8"');
like($log, qr/JSTEST PASS cu_src_off/,  'charset.sourceCharset == "off" when unset');
like($log, qr/JSTEST PASS cs_src_str/,  'charset.sourceCharset is string');
like($log, qr/JSTEST PASS cs_src_val/,  'charset.sourceCharset == "windows-1251"');
like($log, qr/JSTEST PASS cs_override/, 'charset.overrideCharset == true');
like($log, qr/JSTEST PASS cs_cs_off/,   'charset.charset == "off" when only source set');
like($log, qr/JSTEST PASS off_val/,     'charset off: charset.charset == "off"');
like($log, qr/JSTEST PASS plain_cs/,    'plain: charset.charset == null');
like($log, qr/JSTEST PASS plain_src/,   'plain: charset.sourceCharset == null');
