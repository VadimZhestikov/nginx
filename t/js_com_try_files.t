#!/usr/bin/perl

# Tests for Stage 13x COM expansion: try_files arguments exposed as
# location.tryFiles (string array, no sub-object).
#
# New property on NginxLocation:
#   tryFiles  string[]  — try_files arguments as configured

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

js_include %%TESTDIR%%/init_try_files.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        root %%TESTDIR%%;

        # no try_files
        location /default {
        }

        # static paths only
        location /static {
            try_files $uri $uri/ /index.html;
        }

        # last arg is =404
        location /code {
            try_files $uri $uri/ =404;
        }

        # named location fallback
        location /named {
            try_files $uri @fallback;
        }

        location @fallback { return 200; }
    }
}
EOF

$t->write_file('init_try_files.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
let defLoc, staticLoc, codeLoc, namedLoc;
for (const loc of srv.locations) {
    if      (loc.path === "/default") defLoc    = loc;
    else if (loc.path === "/static")  staticLoc = loc;
    else if (loc.path === "/code")    codeLoc   = loc;
    else if (loc.path === "/named")   namedLoc  = loc;
}

// ---- default: no try_files ----
check("def_arr",    Array.isArray(defLoc.tryFiles),           defLoc.tryFiles);
check("def_empty",  defLoc.tryFiles.length === 0,             defLoc.tryFiles.length);

// ---- static: $uri $uri/ /index.html ----
const st = staticLoc.tryFiles;
check("st_arr",     Array.isArray(st),                        st);
check("st_len",     st.length === 3,                          st.length);
check("st_0",       st[0] === "$uri",                         st[0]);
check("st_1",       st[1] === "$uri/",                        st[1]);
check("st_2",       st[2] === "/index.html",                  st[2]);

// ---- code: $uri $uri/ =404 ----
const co = codeLoc.tryFiles;
check("co_arr",     Array.isArray(co),                        co);
check("co_len",     co.length === 3,                          co.length);
check("co_last",    co[2] === "=404",                         co[2]);

// ---- named: $uri @fallback ----
const na = namedLoc.tryFiles;
check("na_arr",     Array.isArray(na),                        na);
check("na_len",     na.length === 2,                          na.length);
check("na_0",       na[0] === "$uri",                         na[0]);
check("na_1",       na[1] === "@fallback",                    na[1]);
JS

$t->try_run('no try_files support')->plan(13);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS def_arr/,   'default: tryFiles is array');
like($log, qr/JSTEST PASS def_empty/, 'default: tryFiles is empty');
like($log, qr/JSTEST PASS st_arr/,    'static: tryFiles is array');
like($log, qr/JSTEST PASS st_len/,    'static: length == 3');
like($log, qr/JSTEST PASS st_0/,      'static: [0] == "$uri"');
like($log, qr/JSTEST PASS st_1/,      'static: [1] == "$uri/"');
like($log, qr/JSTEST PASS st_2/,      'static: [2] == "/index.html"');
like($log, qr/JSTEST PASS co_arr/,    'code: tryFiles is array');
like($log, qr/JSTEST PASS co_len/,    'code: length == 3');
like($log, qr/JSTEST PASS co_last/,   'code: last == "=404"');
like($log, qr/JSTEST PASS na_arr/,    'named: tryFiles is array');
like($log, qr/JSTEST PASS na_len/,    'named: length == 2');
like($log, qr/JSTEST PASS na_1/,      'named: [1] == "@fallback"');
