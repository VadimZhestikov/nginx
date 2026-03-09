#!/usr/bin/perl

# Tests for Stage 8 COM expansion: headers filter location configuration
# exposed as properties of location.headers (NginxHeaders class).
#
# New property on NginxLocation:
#   headers   NginxHeaders
#
# NginxHeaders properties (all read-only):
#   expires          string   expires mode
#   expiresTime      number   seconds (for access/modified/daily modes)
#   addHeaders       object[] [{key, value, always}]
#   addTrailers      object[] [{key, value, always}]
#   headersInherit   string   "off"|"on"|"merge"
#   trailersInherit  string   "off"|"on"|"merge"

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

js_source %%TESTDIR%%/init_headers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with full headers config
        location /full {
            expires           30m;

            add_header        X-Foo  "bar"        always;
            add_header        X-Baz  "qux";

            add_trailer       X-Trail "trail-val";

            add_header_inherit   on;
            add_trailer_inherit  merge;
        }

        # Location with epoch expires and no extra headers
        location /epoch {
            expires  epoch;
        }

        # Location with no explicit headers directives (defaults)
        location /plain {
        }
    }
}
EOF

$t->write_file('init_headers.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv   = nginx.http.servers[0];
// Locations are returned in alphabetical order:
//   index 0 => /epoch, index 1 => /full, index 2 => /plain
const epoch = srv.locations[0];
const full  = srv.locations[1];

const h = full.headers;

// ---- headers object ----
check("headers_obj",     typeof h === "object" && h !== null, typeof h);

// ---- expires ----
check("expires_access",  h.expires === "access",  h.expires);
check("expires_time",    h.expiresTime === 1800,  h.expiresTime);

// ---- addHeaders ----
const ah = h.addHeaders;
check("ah_arr",     Array.isArray(ah),           typeof ah);
check("ah_len",     ah.length === 2,             ah.length);
check("ah0_key",    ah[0].key === "X-Foo",       ah[0].key);
check("ah0_val",    ah[0].value === "bar",        ah[0].value);
check("ah0_always", ah[0].always === true,        ah[0].always);
check("ah1_key",    ah[1].key === "X-Baz",       ah[1].key);
check("ah1_val",    ah[1].value === "qux",        ah[1].value);
check("ah1_always", ah[1].always === false,       ah[1].always);

// ---- addTrailers ----
const at = h.addTrailers;
check("at_arr",     Array.isArray(at),           typeof at);
check("at_len",     at.length === 1,             at.length);
check("at0_key",    at[0].key === "X-Trail",     at[0].key);
check("at0_val",    at[0].value === "trail-val", at[0].value);

// ---- inherit flags ----
check("h_inherit",   h.headersInherit  === "on",    h.headersInherit);
check("t_inherit",   h.trailersInherit === "merge",  h.trailersInherit);

// ---- epoch location ----
const eh = epoch.headers;
check("epoch_exp",   eh.expires === "epoch",   eh.expires);
check("epoch_ah",    eh.addHeaders.length === 0, eh.addHeaders.length);
JS

$t->try_run('no js module')->plan(19);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS headers_obj/,    'location.headers is object');
like($log, qr/JSTEST PASS expires_access/, 'location.headers.expires == "access"');
like($log, qr/JSTEST PASS expires_time/,   'location.headers.expiresTime == 1800');
like($log, qr/JSTEST PASS ah_arr/,         'location.headers.addHeaders is array');
like($log, qr/JSTEST PASS ah_len/,         'location.headers.addHeaders.length == 2');
like($log, qr/JSTEST PASS ah0_key/,        'addHeaders[0].key == "X-Foo"');
like($log, qr/JSTEST PASS ah0_val/,        'addHeaders[0].value == "bar"');
like($log, qr/JSTEST PASS ah0_always/,     'addHeaders[0].always == true');
like($log, qr/JSTEST PASS ah1_key/,        'addHeaders[1].key == "X-Baz"');
like($log, qr/JSTEST PASS ah1_val/,        'addHeaders[1].value == "qux"');
like($log, qr/JSTEST PASS ah1_always/,     'addHeaders[1].always == false');
like($log, qr/JSTEST PASS at_arr/,         'location.headers.addTrailers is array');
like($log, qr/JSTEST PASS at_len/,         'location.headers.addTrailers.length == 1');
like($log, qr/JSTEST PASS at0_key/,        'addTrailers[0].key == "X-Trail"');
like($log, qr/JSTEST PASS at0_val/,        'addTrailers[0].value == "trail-val"');
like($log, qr/JSTEST PASS h_inherit/,      'location.headers.headersInherit == "on"');
like($log, qr/JSTEST PASS t_inherit/,      'location.headers.trailersInherit == "merge"');
like($log, qr/JSTEST PASS epoch_exp/,      'epoch location.headers.expires == "epoch"');
like($log, qr/JSTEST PASS epoch_ah/,       'epoch location.headers.addHeaders.length == 0');
