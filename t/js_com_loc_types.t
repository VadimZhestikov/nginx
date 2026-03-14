#!/usr/bin/perl

# Tests for location.matchType property across all five nginx location
# modifier types (prefix, exact, preferentialPrefix, regex,
# regexCaseInsensitive) and named locations (@name).
#
# Also verifies that server.locations includes named (@) locations.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http pcre/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_loc_types.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # Standard prefix (no modifier)
        location /prefix/ { }

        # Exact match
        location = /exact { }

        # Preferential prefix (^~)
        location ^~ /prefer/ { }

        # Case-sensitive regex (~)
        location ~ \.php$ { }

        # Case-insensitive regex (~*)
        location ~* \.html$ { }

        # Named location
        location @fallback { internal; }

        location / { }
    }
}
EOF

$t->write_file('init_loc_types.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const locs = nginx.http.servers[0].locations;

function find(path) {
    return locs.find(function(l) { return l.path === path; });
}

const prefix  = find("/prefix/");
const exact   = find("/exact");
const prefer  = find("/prefer/");
const regex   = find("\\.php$");
const regexi  = find("\\.html$");
const named   = find("@fallback");

// matchType
check("prefix_matchType",     prefix  && prefix.matchType  === "prefix",               prefix  && prefix.matchType);
check("exact_matchType",      exact   && exact.matchType   === "exact",                exact   && exact.matchType);
check("prefer_matchType",     prefer  && prefer.matchType  === "preferentialPrefix",   prefer  && prefer.matchType);
check("regex_matchType",      regex   && regex.matchType   === "regex",                regex   && regex.matchType);
check("regexi_matchType",     regexi  && regexi.matchType  === "regexCaseInsensitive", regexi  && regexi.matchType);
check("named_matchType",      named   && named.matchType   === "named",                named   && named.matchType);

// path round-trip
check("prefix_path",  prefix  && prefix.path  === "/prefix/",  prefix  && prefix.path);
check("exact_path",   exact   && exact.path   === "/exact",    exact   && exact.path);
check("prefer_path",  prefer  && prefer.path  === "/prefer/",  prefer  && prefer.path);
check("regex_path",   regex   && regex.path   === "\\.php$",   regex   && regex.path);
check("regexi_path",  regexi  && regexi.path  === "\\.html$",  regexi  && regexi.path);
check("named_path",   named   && named.path   === "@fallback", named   && named.path);

// named location appears in the locations array
check("named_in_locs", named !== undefined, named);
JS

$t->try_run('no js module')->plan(13);

my $log = $t->read_file('error.log');

# matchType
like($log, qr/JSTEST PASS prefix_matchType/,   'prefix location.matchType == "prefix"');
like($log, qr/JSTEST PASS exact_matchType/,    'exact location.matchType == "exact"');
like($log, qr/JSTEST PASS prefer_matchType/,   'preferentialPrefix location.matchType == "preferentialPrefix"');
like($log, qr/JSTEST PASS regex_matchType/,    'regex location.matchType == "regex"');
like($log, qr/JSTEST PASS regexi_matchType/,   'case-insensitive regex location.matchType == "regexCaseInsensitive"');
like($log, qr/JSTEST PASS named_matchType/,    'named location.matchType == "named"');

# path
like($log, qr/JSTEST PASS prefix_path/,   'prefix location.path == "/prefix/"');
like($log, qr/JSTEST PASS exact_path/,    'exact location.path == "/exact"');
like($log, qr/JSTEST PASS prefer_path/,   'preferentialPrefix location.path == "/prefer/"');
like($log, qr/JSTEST PASS regex_path/,    'regex location.path == "\\.php$"');
like($log, qr/JSTEST PASS regexi_path/,   'case-insensitive regex location.path == "\\.html$"');
like($log, qr/JSTEST PASS named_path/,    'named location.path == "@fallback"');

# named in locations array
like($log, qr/JSTEST PASS named_in_locs/, 'named location appears in server.locations');
