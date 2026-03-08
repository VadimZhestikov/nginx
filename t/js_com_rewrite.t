#!/usr/bin/perl

# Tests for Stage 10 COM expansion: rewrite location configuration
# exposed as properties of location.rewrite (NginxRewrite class).
#
# New property on NginxLocation:
#   rewrite   NginxRewrite
#
# NginxRewrite properties (all read-only):
#   log                       bool    rewrite_log on/off
#   uninitializedVariableWarn bool    uninitialized_variable_warn
#   stackSize                 number  script stack size
#   hasRules                  bool    any rewrite/return/set/if present

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_rewrite.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with rewrite rules and log enabled
        location /active {
            rewrite_log  on;
            rewrite      ^/active/(.*)$  /$1  last;
            set          $myvar  "hello";
        }

        # Location with uninitialized_variable_warn off, no rules
        location /flags {
            uninitialized_variable_warn  off;
        }

        # Plain location — defaults only, no rules
        location /plain {
        }
    }
}
EOF

$t->write_file('init_rewrite.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical order: /active (a), /flags (f), /plain (p)
const activeLoc = srv.locations[0];
const flagsLoc  = srv.locations[1];
const plainLoc  = srv.locations[2];

const ra = activeLoc.rewrite;
const rf = flagsLoc.rewrite;
const rp = plainLoc.rewrite;

// ---- rewrite object ----
check("rw_obj",     typeof ra === "object" && ra !== null, typeof ra);

// ---- log: on in /active ----
check("log_on",     ra.log === true,   ra.log);

// ---- hasRules: true when rewrite/set rules are present ----
check("has_rules",  ra.hasRules === true,  ra.hasRules);

// ---- stackSize: default is 10 ----
check("stack_sz",   ra.stackSize === 10,   ra.stackSize);

// ---- uninitializedVariableWarn defaults to true ----
check("uv_warn_def", ra.uninitializedVariableWarn === true, ra.uninitializedVariableWarn);

// ---- /flags: uninitialized_variable_warn off, no rules ----
check("uv_warn_off", rf.uninitializedVariableWarn === false, rf.uninitializedVariableWarn);
check("flags_no_rules", rf.hasRules === false, rf.hasRules);

// ---- /plain: defaults ----
check("plain_log_off",  rp.log === false,  rp.log);
check("plain_no_rules", rp.hasRules === false, rp.hasRules);
check("plain_uv_warn",  rp.uninitializedVariableWarn === true, rp.uninitializedVariableWarn);
JS

$t->try_run('no rewrite module')->plan(10);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS rw_obj/,       'location.rewrite is object');
like($log, qr/JSTEST PASS log_on/,       'location.rewrite.log == true');
like($log, qr/JSTEST PASS has_rules/,    'location.rewrite.hasRules == true');
like($log, qr/JSTEST PASS stack_sz/,     'location.rewrite.stackSize == 10');
like($log, qr/JSTEST PASS uv_warn_def/,  'location.rewrite.uninitializedVariableWarn default true');
like($log, qr/JSTEST PASS uv_warn_off/,  'uninitializedVariableWarn off');
like($log, qr/JSTEST PASS flags_no_rules/, '/flags hasRules == false');
like($log, qr/JSTEST PASS plain_log_off/,  '/plain log == false');
like($log, qr/JSTEST PASS plain_no_rules/, '/plain hasRules == false');
like($log, qr/JSTEST PASS plain_uv_warn/,  '/plain uninitializedVariableWarn == true');
