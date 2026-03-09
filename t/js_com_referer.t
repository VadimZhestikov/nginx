#!/usr/bin/perl

# Tests for Stage 13g COM expansion: referer location configuration
# exposed as properties of location.referer (NginxReferer class).
#
# New property on NginxLocation:
#   referer   NginxReferer
#
# NginxReferer properties (all read-only):
#   noReferer        boolean  — "none" keyword in valid_referers
#   blockedReferer   boolean  — "blocked" keyword in valid_referers
#   serverNames      boolean  — "server_names" keyword in valid_referers
#   hashMaxSize      number   — referer_hash_max_size
#   hashBucketSize   number   — referer_hash_bucket_size

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http referer/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_referer.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # no valid_referers — all flags stay false
        location /default {
        }

        # none + blocked + a domain
        location /none {
            valid_referers none blocked example.com;
        }

        # server_names only (server_name is "localhost" so it's valid alone)
        location /srv {
            valid_referers server_names;
        }
    }
}
EOF

$t->write_file('init_referer.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /none (n) < /srv (s)
const defLoc  = srv.locations[0];
const noneLoc = srv.locations[1];
const srvLoc  = srv.locations[2];

// ---- none + blocked ----
const rn = noneLoc.referer;
check("rn_obj",       typeof rn === "object" && rn !== null,  typeof rn);
check("rn_none",      rn.noReferer === true,                  rn.noReferer);
check("rn_blocked",   rn.blockedReferer === true,             rn.blockedReferer);
check("rn_srv",       rn.serverNames === false,               rn.serverNames);

// ---- server_names only ----
const rs = srvLoc.referer;
check("rs_none",      rs.noReferer === false,                 rs.noReferer);
check("rs_srv",       rs.serverNames === true,                rs.serverNames);

// ---- default: no valid_referers ----
const rd = defLoc.referer;
check("rd_none",      rd.noReferer === false,                 rd.noReferer);
check("rd_blocked",   rd.blockedReferer === false,            rd.blockedReferer);
check("rd_srv",       rd.serverNames === false,               rd.serverNames);
JS

$t->try_run('no referer module')->plan(9);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS rn_obj/,     'location.referer is object');
like($log, qr/JSTEST PASS rn_none/,    'noReferer == true for "none"');
like($log, qr/JSTEST PASS rn_blocked/, 'blockedReferer == true for "blocked"');
like($log, qr/JSTEST PASS rn_srv/,     'serverNames == false when not set');
like($log, qr/JSTEST PASS rs_none/,    'noReferer == false for server_names only');
like($log, qr/JSTEST PASS rs_srv/,     'serverNames == true for "server_names"');
like($log, qr/JSTEST PASS rd_none/,    'default: noReferer == false');
like($log, qr/JSTEST PASS rd_blocked/, 'default: blockedReferer == false');
like($log, qr/JSTEST PASS rd_srv/,     'default: serverNames == false');
