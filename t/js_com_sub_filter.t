#!/usr/bin/perl

# Tests for Stage 13e COM expansion: sub_filter location configuration
# exposed as properties of location.subFilter (NginxSubFilter class).
#
# New property on NginxLocation:
#   subFilter   NginxSubFilter
#
# NginxSubFilter properties (all read-only):
#   pairs[]        Array of {match, replacement} — static strings or null
#   once           boolean  substitute only once per response
#   lastModified   boolean  preserve Last-Modified header

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http sub/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_sub_filter.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # dynamic pair (variable in replacement)
        location /dynamic {
            sub_filter foo $host;
            sub_filter_once off;
        }

        # sub_filter off (no pairs, default flags)
        location /off {
        }

        # plain: no sub_filter
        location /plain {
        }

        # static pair
        location /sub {
            sub_filter    foo  bar;
            sub_filter_once     on;
            sub_filter_last_modified on;
        }
    }
}
EOF

$t->write_file('init_sub_filter.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /dynamic (d) < /off (o) < /plain (p) < /sub (s)
const dynLoc   = srv.locations[0];
const offLoc   = srv.locations[1];
const plainLoc = srv.locations[2];
const subLoc   = srv.locations[3];

// ---- static pair location ----
const sf = subLoc.subFilter;
check("sf_obj",          typeof sf === "object" && sf !== null,   typeof sf);
check("sf_pairs_arr",    Array.isArray(sf.pairs),                 typeof sf.pairs);
check("sf_pairs_len",    sf.pairs.length === 1,                   sf.pairs.length);
check("sf_match",        sf.pairs[0].match === "foo",             sf.pairs[0].match);
check("sf_replacement",  sf.pairs[0].replacement === "bar",       sf.pairs[0].replacement);
check("sf_once",         sf.once === true,                        sf.once);
check("sf_last_mod",     sf.lastModified === true,                sf.lastModified);

// ---- dynamic pair location ----
const df = dynLoc.subFilter;
check("df_pairs_len",    df.pairs.length === 1,                   df.pairs.length);
check("df_match",        df.pairs[0].match === "foo",             df.pairs[0].match);
check("df_repl_null",    df.pairs[0].replacement === null,        df.pairs[0].replacement);
check("df_once",         df.once === false,                       df.once);

// ---- no sub_filter (no pairs) ----
const pf = plainLoc.subFilter;
check("pf_pairs_empty",  Array.isArray(pf.pairs) && pf.pairs.length === 0,
      pf.pairs.length);
JS

$t->try_run('no sub_filter module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS sf_obj/,         'location.subFilter is object');
like($log, qr/JSTEST PASS sf_pairs_arr/,   'subFilter.pairs is array');
like($log, qr/JSTEST PASS sf_pairs_len/,   'subFilter.pairs has 1 entry');
like($log, qr/JSTEST PASS sf_match/,       'pairs[0].match == "foo"');
like($log, qr/JSTEST PASS sf_replacement/, 'pairs[0].replacement == "bar"');
like($log, qr/JSTEST PASS sf_once/,        'subFilter.once == true');
like($log, qr/JSTEST PASS sf_last_mod/,    'subFilter.lastModified == true');
like($log, qr/JSTEST PASS df_pairs_len/,   'dynamic location has 1 pair');
like($log, qr/JSTEST PASS df_match/,       'dynamic match == "foo"');
like($log, qr/JSTEST PASS df_repl_null/,   'dynamic replacement == null');
like($log, qr/JSTEST PASS df_once/,        'dynamic once == false');
like($log, qr/JSTEST PASS pf_pairs_empty/, 'no sub_filter: pairs is empty array');
