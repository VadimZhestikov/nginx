#!/usr/bin/perl

# Tests for Stage 13f COM expansion: autoindex location configuration
# exposed as properties of location.autoindex (NginxAutoindex class).
#
# New property on NginxLocation:
#   autoindex   NginxAutoindex
#
# NginxAutoindex properties (all read-only):
#   enable      boolean  — autoindex on/off
#   format      string   — "html" | "json" | "jsonp" | "xml"
#   localtime   boolean  — autoindex_localtime on/off
#   exactSize   boolean  — autoindex_exact_size on/off

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http autoindex/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_autoindex.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # autoindex on, html (default format), non-default flags
        location /autoindex {
            autoindex            on;
            autoindex_localtime  on;
            autoindex_exact_size off;
        }

        # default (no autoindex directive): enable=off after merge
        location /default {
        }

        # autoindex on, json format
        location /json {
            autoindex        on;
            autoindex_format json;
        }

        # explicit off
        location /off {
            autoindex off;
        }
    }
}
EOF

$t->write_file('init_autoindex.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /autoindex (a) < /default (d) < /json (j) < /off (o)
const aiLoc  = srv.locations[0];
const defLoc = srv.locations[1];
const jsonLoc = srv.locations[2];
const offLoc = srv.locations[3];

// ---- autoindex on, html format ----
const ai = aiLoc.autoindex;
check("ai_obj",       typeof ai === "object" && ai !== null,  typeof ai);
check("ai_enable",    ai.enable === true,                     ai.enable);
check("ai_format",    ai.format === "html",                   ai.format);
check("ai_localtime", ai.localtime === true,                  ai.localtime);
check("ai_exact",     ai.exactSize === false,                 ai.exactSize);

// ---- json format ----
const aj = jsonLoc.autoindex;
check("aj_format",    aj.format === "json",                   aj.format);

// ---- explicit off ----
const ao = offLoc.autoindex;
check("ao_enable",    ao.enable === false,                    ao.enable);

// ---- default: enable defaults to false ----
const ad = defLoc.autoindex;
check("ad_enable",    ad.enable === false,                    ad.enable);
// exact_size default is on (true)
check("ad_exact",     ad.exactSize === true,                  ad.exactSize);
JS

$t->try_run('no autoindex module')->plan(9);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ai_obj/,       'location.autoindex is object');
like($log, qr/JSTEST PASS ai_enable/,    'autoindex.enable == true');
like($log, qr/JSTEST PASS ai_format/,    'autoindex.format == "html"');
like($log, qr/JSTEST PASS ai_localtime/, 'autoindex.localtime == true');
like($log, qr/JSTEST PASS ai_exact/,     'autoindex.exactSize == false');
like($log, qr/JSTEST PASS aj_format/,    'json format location');
like($log, qr/JSTEST PASS ao_enable/,    'explicit off: enable == false');
like($log, qr/JSTEST PASS ad_enable/,    'default: enable == false');
like($log, qr/JSTEST PASS ad_exact/,     'default: exactSize == true');
