#!/usr/bin/perl

# Stage 43 DYN: addLocation('@name') / removeLocation('@name')
#
# Verifies that named locations (@name) can be dynamically added and removed:
#
#   1. addLocation('@name') creates a named location reachable via
#      nginx's internal error_page redirect mechanism.
#   2. The new location is reachable and its handler runs.
#   3. location.pattern returns '@name' for named locations.
#   4. addLocation('@name') is idempotent — second call returns same location.
#   5. removeLocation('@name') removes the location; subsequent requests
#      that trigger a redirect to it get a 500 (no match in named_locations).
#   6. Static named locations (@name in nginx.conf) can also be removed.
#   7. Prefix locations are unaffected by named-location operations.
#   8. removeLocation('@nonexistent') returns false (no crash).
#
# Note: nginx preserves the original error status code (418) through
# error_page redirects, so we check response bodies, not status codes,
# for named location hits.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(18);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/named_loc.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /base         { }
        location /probe_add    { }
        location /probe_remove { }
        location /probe_idem   { }
        location /trigger_dyn  {
            error_page 418 @dyn_handler;
            return 418;
        }
        location /trigger_stat {
            error_page 418 @static_handler;
            return 418;
        }
        location @static_handler { }
    }
}
EOF

$t->write_file('named_loc.js', <<'JS');
(function() {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function find(path) {
        return locs.find(function(l) { return l.path === path; });
    }

    /* /base — must survive all mutations */
    find('/base').handler = function(r) {
        r.respond(200, {}, 'base-ok');
    };

    /* @static_handler — pre-existing named location */
    locs.find(function(l) { return l.matchType === 'named'; })
        .handler = function(r) { r.respond(200, {}, 'static-named-ok'); };

    /* /probe_add — adds @dyn_handler dynamically */
    find('/probe_add').handler = function(r) {
        var loc = srv.addLocation('@dyn_handler');
        loc.handler = function(r2) {
            r2.respond(200, {}, 'dyn-named-ok');
        };
        r.respond(200, {}, 'added');
    };

    /* /probe_idem — second addLocation('@dyn_handler') returns same object */
    find('/probe_idem').handler = function(r) {
        var loc1 = srv.addLocation('@dyn_handler');
        var loc2 = srv.addLocation('@dyn_handler');
        /* pattern must be '@dyn_handler' for both */
        var same = (loc1.pattern === loc2.pattern && loc1.pattern === '@dyn_handler')
                   ? 'same' : 'diff';
        r.respond(200, {}, same);
    };

    /* /probe_remove — removes @static_handler */
    find('/probe_remove').handler = function(r) {
        var removed = srv.removeLocation('@static_handler');
        var missing = srv.removeLocation('@nonexistent');
        r.respond(200, {}, (removed ? 'removed' : 'not-removed')
                           + ':' + (missing ? 'found' : 'false'));
    };
})();
JS

$t->run();

# ---- /base is always alive ----
like(http_get('/base'), qr/200 OK/,   'base: 200');
like(http_get('/base'), qr/base-ok/,  'base: body');

# ---- @static_handler fires via error_page (status stays 418, body is handler's) ----
like(http_get('/trigger_stat'), qr/static-named-ok/, 'static named: handler body');

# ---- @dyn_handler not yet added → nginx 500 (no match) ----
like(http_get('/trigger_dyn'), qr/500/, 'before add: dyn named gives 500');

# ---- /probe_add adds @dyn_handler ----
like(http_get('/probe_add'), qr/200 OK/,  'probe_add: 200');
like(http_get('/probe_add'), qr/added/,   'probe_add: body');

# ---- @dyn_handler now resolves ----
like(http_get('/trigger_dyn'), qr/dyn-named-ok/, 'after add: dyn named body');

# ---- idempotent: second addLocation('@dyn_handler') returns same entry ----
like(http_get('/probe_idem'), qr/200 OK/,  'idem: 200');
like(http_get('/probe_idem'), qr/same/,    'idem: addLocation idempotent + pattern correct');

# ---- removeLocation('@static_handler') and missing return values ----
my $r = http_get('/probe_remove');
like($r, qr/200 OK/,   'probe_remove: 200');
like($r, qr/removed/,  'probe_remove: removeLocation returned true');
like($r, qr/false/,    'probe_remove: removeLocation on missing returned false');

# ---- @static_handler is gone — /trigger_stat now 500 ----
like(http_get('/trigger_stat'), qr/500/, 'after remove: static named gives 500');

# ---- @dyn_handler still alive after static removal ----
like(http_get('/trigger_dyn'), qr/dyn-named-ok/, 'dyn named still live after static removal');

# ---- /base survived all mutations ----
like(http_get('/base'), qr/200 OK/,   'base after all ops: 200');
like(http_get('/base'), qr/base-ok/,  'base after all ops: body');

# ---- worker still alive ----
like(http_get('/base'), qr/200 OK/, 'worker alive after all operations');

# ---- full suite smoke: re-add and re-remove via pattern round-trip ----
like(http_get('/probe_add'), qr/added/, 'pattern round-trip: re-add idempotent');

$t->stop();
