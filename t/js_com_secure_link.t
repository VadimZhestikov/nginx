#!/usr/bin/perl

# Tests for Stage 13p COM expansion: secure_link location configuration
# exposed as properties of location.secureLink (NginxSecureLink class).
#
# New property on NginxLocation:
#   secureLink   NginxSecureLink
#
# NginxSecureLink properties (all read-only):
#   secret    string  — secure_link_secret value (empty if not set)
#   variable  string  — static portion of secure_link expression (or "")
#   md5       string  — static portion of secure_link_md5 expression (or "")

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

js_source %%TESTDIR%%/init_secure_link.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no secure_link directives
        location /default {
        }

        # secret mode: secure_link_secret
        location /secret {
            secure_link_secret  mypassword;
        }

        # md5 mode: secure_link + secure_link_md5
        location /md5 {
            secure_link        $arg_md5,$arg_expires;
            secure_link_md5    "$secure_link_expires$uri secret";
        }
    }
}
EOF

$t->write_file('init_secure_link.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /md5 (m) < /secret (s)
const defLoc    = srv.locations[0];
const md5Loc    = srv.locations[1];
const secretLoc = srv.locations[2];

// ---- default: no secure_link directives ----
const sd = defLoc.secureLink;
check("sd_obj",      typeof sd === "object" && sd !== null, typeof sd);
check("sd_secret",   sd.secret   === "",                    sd.secret);
check("sd_variable", sd.variable === "",                    sd.variable);
check("sd_md5",      sd.md5      === "",                    sd.md5);

// ---- secret mode ----
const ss = secretLoc.secureLink;
check("ss_obj",      typeof ss === "object" && ss !== null, typeof ss);
check("ss_secret",   ss.secret === "mypassword",            ss.secret);
check("ss_variable", ss.variable === "",                    ss.variable);
check("ss_md5",      ss.md5      === "",                    ss.md5);

// ---- md5 mode ----
const sm = md5Loc.secureLink;
check("sm_obj",      typeof sm === "object" && sm !== null, typeof sm);
check("sm_secret",   sm.secret === "",                      sm.secret);
// secure_link has a variable ($arg_md5,...) so static portion may be empty
check("sm_variable_str", typeof sm.variable === "string",   typeof sm.variable);
check("sm_md5_str",      typeof sm.md5      === "string",   typeof sm.md5);
JS

$t->try_run('no secure_link module')->plan(12);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS sd_obj/,      'default: secureLink is object');
like($log, qr/JSTEST PASS sd_secret/,   'default: secret == ""');
like($log, qr/JSTEST PASS sd_variable/, 'default: variable == ""');
like($log, qr/JSTEST PASS sd_md5/,      'default: md5 == ""');
like($log, qr/JSTEST PASS ss_obj/,      'secret mode: secureLink is object');
like($log, qr/JSTEST PASS ss_secret/,   'secret mode: secret == "mypassword"');
like($log, qr/JSTEST PASS ss_variable/, 'secret mode: variable == ""');
like($log, qr/JSTEST PASS ss_md5/,      'secret mode: md5 == ""');
like($log, qr/JSTEST PASS sm_obj/,      'md5 mode: secureLink is object');
like($log, qr/JSTEST PASS sm_secret/,   'md5 mode: secret == ""');
like($log, qr/JSTEST PASS sm_variable_str/, 'md5 mode: variable is string');
like($log, qr/JSTEST PASS sm_md5_str/,  'md5 mode: md5 is string');
