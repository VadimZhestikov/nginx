#!/usr/bin/perl

# Stage 42 DYN: location.pattern — full pattern including modifier prefix
#
# Verifies that location.pattern returns the complete pattern string accepted
# by addLocation() / removeLocation():
#
#   prefix              →  "/path"
#   exact match         →  "= /path"
#   preferential-prefix →  "^~ /path"
#   case-sensitive regex→  "~ /regex"
#   case-insensitive    →  "~* /regex"
#   named               →  "@name"   (@ already in clcf->name)
#
# Also verifies the round-trip property:
#   addLocation(loc.pattern)    returns the same location (idempotent)
#   removeLocation(loc.pattern) removes the correct location

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(20);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/loc_pattern.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Static locations of each type
        location /prefix/        { }
        location = /exact        { }
        location ^~ /noregex/    { }
        location ~ \.php$        { }
        location ~* \.html$      { }
        location @fallback       { }

        # Probe: reports named-location pattern + round-trip ops
        location /probe { }
    }
}
EOF

$t->write_file('loc_pattern.js', <<'JS');
(function() {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function find(path) {
        return locs.find(function(l) { return l.path === path; });
    }

    /* Assign handlers that return loc.pattern so we can verify the value */
    find('/prefix/').handler = function(r) {
        r.respond(200, {}, r.location.pattern);
    };
    find('/exact').handler = function(r) {
        r.respond(200, {}, r.location.pattern);
    };
    find('/noregex/').handler = function(r) {
        r.respond(200, {}, r.location.pattern);
    };
    /* regex locations found by bare path (without modifier) */
    locs.find(function(l) { return l.path === '\\.php$'; }).handler =
        function(r) { r.respond(200, {}, r.location.pattern); };
    locs.find(function(l) { return l.path === '\\.html$'; }).handler =
        function(r) { r.respond(200, {}, r.location.pattern); };

    /* named location: capture pattern at init time */
    var named = locs.find(function(l) { return l.matchType === 'named'; });
    var namedPattern = named ? named.pattern : 'none';

    /* /probe: reports named pattern + exercises round-trip add/remove */
    find('/probe').handler = function(r) {
        /* Add a dynamic regex location */
        var loc = srv.addLocation('~ /dyn/[0-9]+');
        loc.handler = function(r2) { r2.respond(200, {}, 'dyn-ok'); };

        /* Idempotent: addLocation(loc.pattern) returns same entry */
        var loc2 = srv.addLocation(loc.pattern);
        var same = (loc2.pattern === loc.pattern) ? 'same' : 'diff';

        /* Remove via pattern */
        var removed = srv.removeLocation(loc.pattern) ? 'removed' : 'not-removed';

        r.respond(200, {}, namedPattern + '|' + same + '|' + removed);
    };
})();
JS

$t->run();

# ---- prefix location.pattern ----
like(http_get('/prefix/'), qr/200 OK/,       'prefix: 200');
like(http_get('/prefix/'), qr{^/prefix/$}m,  'prefix: pattern == "/prefix/"');

# ---- exact-match location.pattern ----
like(http_get('/exact'),   qr/200 OK/,       'exact: 200');
like(http_get('/exact'),   qr{^= /exact$}m,  'exact: pattern == "= /exact"');

# ---- preferential-prefix location.pattern ----
like(http_get('/noregex/path'), qr/200 OK/,           'noregex: 200');
like(http_get('/noregex/path'), qr{^\^~ /noregex/$}m, 'noregex: pattern == "^~ /noregex/"');

# ---- case-sensitive regex location.pattern ----
like(http_get('/page.php'), qr/200 OK/,         'regex: 200');
like(http_get('/page.php'), qr{^~ \\\.php\$$}m, 'regex: pattern == "~ \\.php$"');

# ---- case-insensitive regex location.pattern ----
like(http_get('/page.html'), qr/200 OK/,            'regexi: 200');
like(http_get('/page.html'), qr{^~\* \\\.html\$$}m, 'regexi: pattern == "~* \\.html$"');

# ---- probe: named pattern + round-trip ----
my $probe = http_get('/probe');
like($probe,  qr/200 OK/,      'probe: 200');
like($probe,  qr/\@fallback/,  'named: pattern == "@fallback"');
like($probe,  qr/same/,        'round-trip: addLocation(pattern) idempotent');
like($probe,  qr/removed/,     'round-trip: removeLocation(pattern) succeeded');

# ---- dynamic location is gone after round-trip removal ----
like(http_get('/dyn/42'), qr/404/, 'dyn location gone after removal');

# ---- all static locations still alive ----
like(http_get('/prefix/'),      qr{/prefix/},      'prefix still live');
like(http_get('/exact'),        qr{= /exact},       'exact still live');
like(http_get('/noregex/x'),    qr{\^~ /noregex/},  'noregex still live');
like(http_get('/page.php'),     qr{~ \\\.php},      'regex still live');
like(http_get('/page.html'),    qr{~\* \\\.html},   'regexi still live');

$t->stop();
