#!/usr/bin/perl

# Tests for Stage 13o COM expansion: xslt location configuration
# exposed as properties of location.xslt (NginxXslt class).
#
# New property on NginxLocation:
#   xslt   NginxXslt
#
# NginxXslt properties (all read-only):
#   lastModified  boolean  — xslt_last_modified on/off
#   sheetsCount   number   — number of xslt_stylesheet entries
#   params        string[] — xslt_param / xslt_string_param names

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# create a minimal valid XSL stylesheet the module can parse
my $xsl = <<'XSL';
<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:template match="/">
    <root/>
  </xsl:template>
</xsl:stylesheet>
XSL

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_xslt.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — no xslt_stylesheet
        location /default {
        }

        # one stylesheet + params + last_modified
        location /styled {
            xslt_stylesheet  %%TESTDIR%%/style.xsl;
            xslt_param       lang "'en'";
            xslt_string_param charset "utf-8";
            xslt_last_modified on;
        }
    }
}
EOF

$t->write_file('style.xsl', $xsl);

$t->write_file('init_xslt.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /styled (s)
const defLoc    = srv.locations[0];
const styledLoc = srv.locations[1];

// ---- default: no xslt directives ----
const xd = defLoc.xslt;
check("xd_obj",    typeof xd === "object" && xd !== null, typeof xd);
check("xd_lm",     xd.lastModified === false,             xd.lastModified);
check("xd_sheets", xd.sheetsCount === 0,                  xd.sheetsCount);

// ---- styled: one sheet + two params ----
const xs = styledLoc.xslt;
check("xs_obj",    typeof xs === "object" && xs !== null, typeof xs);
check("xs_lm",     xs.lastModified === true,              xs.lastModified);
check("xs_sheets", xs.sheetsCount === 1,                  xs.sheetsCount);
check("xs_plen",   xs.params.length === 2,                xs.params.length);
check("xs_p0",     xs.params[0] === "lang",               xs.params[0]);
check("xs_p1",     xs.params[1] === "charset",            xs.params[1]);
JS

$t->try_run('no xslt module')->plan(9);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS xd_obj/,    'location.xslt is object');
like($log, qr/JSTEST PASS xd_lm/,     'default: lastModified == false');
like($log, qr/JSTEST PASS xd_sheets/, 'default: sheetsCount == 0');
like($log, qr/JSTEST PASS xs_obj/,    'styled: location.xslt is object');
like($log, qr/JSTEST PASS xs_lm/,     'styled: lastModified == true');
like($log, qr/JSTEST PASS xs_sheets/, 'styled: sheetsCount == 1');
like($log, qr/JSTEST PASS xs_plen/,   'styled: params.length == 2');
like($log, qr/JSTEST PASS xs_p0/,     'styled: params[0] == "lang"');
like($log, qr/JSTEST PASS xs_p1/,     'styled: params[1] == "charset"');
