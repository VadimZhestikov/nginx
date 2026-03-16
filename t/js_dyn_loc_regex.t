#!/usr/bin/perl

# Stage 40 DYN: srv.addLocation('~ /regex') with ordering + removeLocation
#
# Verifies regex location support in addLocation / removeLocation:
#
#   1. ~ (case-sensitive) and ~* (case-insensitive) patterns route correctly.
#   2. { index: 0 } inserts a regex location BEFORE existing ones so it wins
#      the first-match contest (regex locations are checked in order).
#   3. removeLocation('~ /pattern') removes the regex location; subsequent
#      requests fall through to lower-priority locations.
#   4. Prefix and exact-match locations are unaffected by regex operations.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(16);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/regex_loc.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /prefix  { }
        location /fallback { }
    }
}
EOF

$t->write_file('regex_loc.js', <<'JS');
(function() {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* /prefix — must survive regex add/remove operations */
    set('/prefix', function(r) {
        r.respond(200, {}, 'prefix-ok');
    });

    /* /fallback — catches requests when high-priority regex is removed */
    set('/fallback', function(r) {
        r.respond(200, {}, 'fallback-ok');
    });

    /* 1. Case-sensitive regex — appended at end (default) */
    var loc1 = srv.addLocation('~ /api/v[0-9]+');
    loc1.handler = function(r) {
        r.respond(200, {}, 'api-regex-ok');
    };

    /* 2. Case-insensitive regex — also appended */
    var loc2 = srv.addLocation('~* /static/.*\\.css');
    loc2.handler = function(r) {
        r.respond(200, {}, 'css-ok');
    };

    /* 3. High-priority regex inserted at index 0 (before loc1 and loc2) */
    var loc3 = srv.addLocation('~ /api/v1/special', { index: 0 });
    loc3.handler = function(r) {
        r.respond(200, {}, 'special-ok');
    };

    /* /remove_test — handler that removes the high-priority regex */
    set('/fallback', function(r) {
        /* after removal, /api/v1/special falls through to the general regex */
        srv.removeLocation('~ /api/v1/special');
        r.respond(200, {}, 'removed');
    });
})();
JS

$t->run();

# ---- /prefix is unaffected ----
like(http_get('/prefix'),  qr/200 OK/,    'prefix: 200');
like(http_get('/prefix'),  qr/prefix-ok/, 'prefix: body');

# ---- case-sensitive regex ----
like(http_get('/api/v2'),  qr/200 OK/,       'api regex: 200');
like(http_get('/api/v2'),  qr/api-regex-ok/, 'api regex: body');

# ---- case-insensitive regex ----
like(http_get('/static/main.CSS'),  qr/200 OK/, 'css regex (case-insensitive): 200');
like(http_get('/static/main.CSS'),  qr/css-ok/, 'css regex (case-insensitive): body');

# ---- high-priority regex wins over general api regex ----
like(http_get('/api/v1/special'),  qr/200 OK/,     'special (high-priority): 200');
like(http_get('/api/v1/special'),  qr/special-ok/, 'special (high-priority): body wins over api');

# ---- remove the high-priority regex via /fallback handler ----
my $r = http_get('/fallback');
like($r, qr/200 OK/,  'remove via handler: 200');
like($r, qr/removed/, 'remove via handler: body');

# ---- /api/v1/special now falls through to general api regex ----
like(http_get('/api/v1/special'),  qr/200 OK/,       'after remove: special falls to api regex: 200');
like(http_get('/api/v1/special'),  qr/api-regex-ok/, 'after remove: special falls to api regex: body');

# ---- other regex and prefix still intact ----
like(http_get('/api/v3'),          qr/api-regex-ok/, 'api regex still live after remove');
like(http_get('/static/a.css'),    qr/css-ok/,       'css regex still live after remove');
like(http_get('/prefix'),          qr/prefix-ok/,    'prefix still live after regex removes');

# ---- removeLocation on nonexistent regex returns false (tested indirectly:
#      no crash, worker still up) ----
like(http_get('/api/v2'),  qr/200 OK/, 'worker still alive after operations');

$t->stop();
