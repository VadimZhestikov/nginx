#!/usr/bin/perl

# Tests for Stage 13j COM expansion: userid location configuration
# exposed as properties of location.userid (NginxUserid class).
#
# New property on NginxLocation:
#   userid   NginxUserid
#
# NginxUserid properties (all read-only):
#   enable     string  — "off" / "log" / "v1" / "on"
#   name       string  — userid_name (cookie name)
#   domain     string  — userid_domain
#   path       string  — userid_path
#   p3p        string  — userid_p3p header value
#   expires    number  — userid_expires in seconds
#   mark       string  — userid_mark character

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http userid/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_userid.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — userid off
        location /default {
        }

        # userid on with all options set
        location /full {
            userid        on;
            userid_name   uid;
            userid_domain example.com;
            userid_path   /app;
            userid_expires 86400;
            userid_p3p    "policyref=\"/w3c/p3p.xml\"";
            userid_mark   z;
        }

        # userid log only
        location /log {
            userid log;
        }
    }
}
EOF

$t->write_file('init_userid.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /full (f) < /log (l)
const defLoc  = srv.locations[0];
const fullLoc = srv.locations[1];
const logLoc  = srv.locations[2];

// ---- default: userid off ----
const ud = defLoc.userid;
check("ud_obj",    typeof ud === "object" && ud !== null, typeof ud);
check("ud_enable", ud.enable === "off",                   ud.enable);

// ---- full: all options set ----
const uf = fullLoc.userid;
check("uf_obj",     typeof uf === "object" && uf !== null, typeof uf);
check("uf_enable",  uf.enable === "on",                    uf.enable);
check("uf_name",    uf.name === "uid",                     uf.name);
check("uf_domain",  uf.domain === "example.com",           uf.domain);
check("uf_path",    uf.path === "/app",                    uf.path);
check("uf_expires", uf.expires === 86400,                  uf.expires);
check("uf_mark",    uf.mark === "z",                       uf.mark);
check("uf_p3p",     uf.p3p.length > 0,                    uf.p3p);

// ---- log: userid log ----
const ul = logLoc.userid;
check("ul_enable", ul.enable === "log",                    ul.enable);
JS

$t->try_run('no userid module')->plan(11);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ud_obj/,     'location.userid is object');
like($log, qr/JSTEST PASS ud_enable/,  'default: enable == "off"');
like($log, qr/JSTEST PASS uf_obj/,     'full: location.userid is object');
like($log, qr/JSTEST PASS uf_enable/,  'full: enable == "on"');
like($log, qr/JSTEST PASS uf_name/,    'full: name == "uid"');
like($log, qr/JSTEST PASS uf_domain/,  'full: domain == "example.com"');
like($log, qr/JSTEST PASS uf_path/,    'full: path == "/app"');
like($log, qr/JSTEST PASS uf_expires/, 'full: expires == 86400');
like($log, qr/JSTEST PASS uf_mark/,    'full: mark == "z"');
like($log, qr/JSTEST PASS uf_p3p/,     'full: p3p is non-empty');
like($log, qr/JSTEST PASS ul_enable/,  'log: enable == "log"');
