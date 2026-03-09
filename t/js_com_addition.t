#!/usr/bin/perl

# Tests for Stage 13k COM expansion: addition location configuration
# exposed as properties of location.addition (NginxAddition class).
#
# New property on NginxLocation:
#   addition   NginxAddition
#
# NginxAddition properties (all read-only):
#   addBeforeBody  string — add_before_body URI (or "" if unset)
#   addAfterBody   string — add_after_body  URI (or "" if unset)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http addition/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_addition.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no addition directives
        location /default {
        }

        # before + after both set
        location /both {
            add_before_body /header;
            add_after_body  /footer;
        }

        # only after
        location /after {
            add_after_body /footer;
        }
    }
}
EOF

$t->write_file('init_addition.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /after (a) < /both (b) < /default (d)
const afterLoc   = srv.locations[0];
const bothLoc    = srv.locations[1];
const defaultLoc = srv.locations[2];

// ---- default: no directives ----
const ad = defaultLoc.addition;
check("ad_obj",    typeof ad === "object" && ad !== null, typeof ad);
check("ad_before", ad.addBeforeBody === "",               ad.addBeforeBody);
check("ad_after",  ad.addAfterBody  === "",               ad.addAfterBody);

// ---- both: before + after ----
const ab = bothLoc.addition;
check("ab_obj",    typeof ab === "object" && ab !== null, typeof ab);
check("ab_before", ab.addBeforeBody === "/header",        ab.addBeforeBody);
check("ab_after",  ab.addAfterBody  === "/footer",        ab.addAfterBody);

// ---- after only ----
const aa = afterLoc.addition;
check("aa_before", aa.addBeforeBody === "",               aa.addBeforeBody);
check("aa_after",  aa.addAfterBody  === "/footer",        aa.addAfterBody);
JS

$t->try_run('no addition module')->plan(8);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ad_obj/,    'location.addition is object');
like($log, qr/JSTEST PASS ad_before/, 'default: addBeforeBody == ""');
like($log, qr/JSTEST PASS ad_after/,  'default: addAfterBody == ""');
like($log, qr/JSTEST PASS ab_obj/,    'both: location.addition is object');
like($log, qr/JSTEST PASS ab_before/, 'both: addBeforeBody == "/header"');
like($log, qr/JSTEST PASS ab_after/,  'both: addAfterBody == "/footer"');
like($log, qr/JSTEST PASS aa_before/, 'after-only: addBeforeBody == ""');
like($log, qr/JSTEST PASS aa_after/,  'after-only: addAfterBody == "/footer"');
