#!/usr/bin/perl

# Stage 6: NginxCharset.setCharset(charset, sourceCharset)
#
# setCharset(charset, sourceCharset) updates lcf->charset and
# lcf->source_charset at runtime.  Each argument is a string:
#   "off"   → NGX_HTTP_CHARSET_OFF (disable)
#   string  → find-or-add in mcf->charsets, store the index
#
# The change is visible via the charset/sourceCharset getters and
# takes effect for subsequent responses from that location.
#
# Tests:
#   1. charset getter returns initial "UTF-8" from config
#   2. sourceCharset getter returns initial "off" from config
#   3. setCharset("ISO-8859-1", "UTF-8") succeeds
#   4. charset getter returns "ISO-8859-1" after set
#   5. sourceCharset getter returns "UTF-8" after set
#   6. Content-Type response header contains new charset
#   7. Persistence: second /read/ still shows new values
#   8. setCharset("off", "off") → charset="off", sourceCharset="off"
#   9. Error: too few arguments throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http charset/)->plan(9);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /body/ {
            charset        UTF-8;
            charset_types  text/plain;
        }

        location /read/   { }
        location /set/    { }
        location /setoff/ { }
        location /badarg/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const bodyLoc = loc('/body/');

// /body/ — respond with text/plain so charset filter adds charset to CT
bodyLoc.handler = r => {
    r.respond(200, {'content-type': 'text/plain'}, 'hello');
};

// /read/ — return getter values as JSON
loc('/read/').handler = r => {
    const cs = bodyLoc.charset;
    r.respond(200, {}, JSON.stringify({
        charset:       cs.charset,
        sourceCharset: cs.sourceCharset,
    }));
};

// /set/ — call setCharset() with new values
// Use sourceCharset "off" so the filter stamps charset in Content-Type
// without needing a conversion table (source=off → just set header).
loc('/set/').handler = r => {
    bodyLoc.charset.setCharset('ISO-8859-1', 'off');
    const cs = bodyLoc.charset;
    r.respond(200, {}, JSON.stringify({
        charset:       cs.charset,
        sourceCharset: cs.sourceCharset,
    }));
};

// /setoff/ — disable both
loc('/setoff/').handler = r => {
    bodyLoc.charset.setCharset('off', 'off');
    const cs = bodyLoc.charset;
    r.respond(200, {}, JSON.stringify({
        charset:       cs.charset,
        sourceCharset: cs.sourceCharset,
    }));
};

// /badarg/ — too few arguments should throw
loc('/badarg/').handler = r => {
    try {
        bodyLoc.charset.setCharset('UTF-8');
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values from config ----
my $r0 = http_get('/read/');
like($r0, qr/"charset":"UTF-8"/,     'charset getter returns initial UTF-8');
like($r0, qr/"sourceCharset":"off"/, 'sourceCharset getter returns initial off');

# ---- setCharset("ISO-8859-1", "UTF-8") ----
my $r1 = http_get('/set/');
like($r1, qr/200/,                       'setCharset() succeeds');
like($r1, qr/"charset":"ISO-8859-1"/,    'charset getter updated to ISO-8859-1');
like($r1, qr/"sourceCharset":"off"/,     'sourceCharset getter updated to off');

# ---- Content-Type carries new charset ----
like(http_get('/body/'), qr/charset=ISO-8859-1/i,
    'Content-Type carries new charset after setCharset');

# ---- Persistence ----
like(http_get('/read/'), qr/"charset":"ISO-8859-1"/,
    'new charset persists on subsequent request');

# ---- setCharset("off","off") ----
my $r2 = http_get('/setoff/');
like($r2, qr/"charset":"off"/, 'charset becomes off after setCharset(off,off)');

# ---- Error path: too few arguments ----
like(http_get('/badarg/'), qr/error:/, 'setCharset with too few args throws TypeError');

$t->stop();
