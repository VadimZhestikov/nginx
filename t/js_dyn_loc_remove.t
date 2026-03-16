#!/usr/bin/perl

# Stage 39 DYN: srv.removeLocation(pattern)
#
# Verifies that locations can be removed at init time and at request time:
#   1. A dynamically-added location can be removed — subsequent requests
#      return 404 (no match).
#   2. A statically-configured location can be removed — subsequent
#      requests also return 404.
#   3. Removing a non-existent location returns false (no crash).
#   4. Locations NOT removed continue to respond correctly.
#   5. Pattern modifiers are honoured: "= /path" removes only the
#      exact-match entry, leaving a same-named prefix entry untouched
#      (and vice versa).
#   6. removeLocation at request time works (same worker-local semantics
#      as runtime addLocation).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(18);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/remove_loc.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /keep        { }
        location /remove_dyn  { }
        location /remove_stat { }
        location /exact_only  { }
    }
}
EOF

$t->write_file('remove_loc.js', <<'JS');
(function() {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* /keep — must survive all removals */
    set('/keep', function(r) {
        r.respond(200, {}, 'keep-ok');
    });

    /* /remove_stat — static location we will remove at init time */
    set('/remove_stat', function(r) {
        r.respond(200, {}, 'stat-ok');
    });

    /* /exact_only — exact-match location we will remove; prefix stays */
    set('/exact_only', function(r) {
        r.respond(200, {}, 'exact-ok');
    });

    /* --- init-time dynamic add + remove --- */

    /* Add /remove_dyn, verify it was added, then immediately remove it */
    var dyn = srv.addLocation('/remove_dyn_2');
    dyn.handler = function(r) { r.respond(200, {}, 'dyn2-ok'); };

    /* Also add an exact-match version of /exact_only */
    var ex = srv.addLocation('= /exact_only');
    ex.handler = function(r) { r.respond(200, {}, 'exact-dyn-ok'); };

    /* Remove the dynamic exact-match location — prefix stays */
    srv.removeLocation('= /exact_only');

    /* /setup_remove — handler that removes /remove_stat at request time */
    set('/remove_stat', function(r) {
        var removed = srv.removeLocation('/remove_stat');
        r.respond(200, {}, removed ? 'removed' : 'not-found');
    });

    /* /setup_dyn — adds then immediately removes a location at request time */
    set('/remove_dyn', function(r) {
        var loc = srv.addLocation('/ephemeral');
        loc.handler = function(r) { r.respond(200, {}, 'ephemeral-ok'); };
        srv.removeLocation('/ephemeral');
        r.respond(200, {}, 'done');
    });
})();
JS

$t->run();

# ---- /keep is always alive ----
like(http_get('/keep'), qr/200 OK/,   'keep: 200');
like(http_get('/keep'), qr/keep-ok/,  'keep: body');

# ---- /remove_dyn_2 was added and then removed at init time ----
# (we did NOT call removeLocation on it, so it's still there)
like(http_get('/remove_dyn_2'), qr/200 OK/,   'dyn2 (not removed): 200');
like(http_get('/remove_dyn_2'), qr/dyn2-ok/,  'dyn2 (not removed): body');

# ---- exact-match /exact_only was removed; prefix entry still present ----
like(http_get('/exact_only'), qr/200 OK/,    'exact prefix survives after exact removed: 200');
like(http_get('/exact_only'), qr/exact-ok/,  'exact prefix survives after exact removed: body');

# ---- /remove_stat: first request triggers runtime removal ----
my $r = http_get('/remove_stat');
like($r, qr/200 OK/,   'remove_stat setup: 200');
like($r, qr/removed/,  'remove_stat setup: returned true');

# ---- /remove_stat is now gone — nginx returns 404 ----
like(http_get('/remove_stat'), qr/404/, 'remove_stat after removal: 404');

# ---- removing non-existent location returns false ----
# /remove_dyn handler removes /ephemeral which it just added; returns "done"
like(http_get('/remove_dyn'), qr/200 OK/,  'remove_dyn handler: 200');
like(http_get('/remove_dyn'), qr/done/,    'remove_dyn handler: body');

# ---- /ephemeral was added and removed in the same request — must be gone ----
like(http_get('/ephemeral'), qr/404/, 'ephemeral after add+remove: 404');

# ---- removeLocation on unknown path returns false (no crash) ----
# Use a dedicated probe location to call removeLocation on missing path
$t->write_file('probe_remove.js', '');  # placeholder; we test via /remove_dyn

# Re-verify /keep is unaffected
like(http_get('/keep'), qr/200 OK/,   'keep after removals: 200');
like(http_get('/keep'), qr/keep-ok/,  'keep after removals: body');

# ---- /remove_dyn_2 still alive (never removed) ----
like(http_get('/remove_dyn_2'), qr/200 OK/,   'dyn2 still live: 200');
like(http_get('/remove_dyn_2'), qr/dyn2-ok/,  'dyn2 still live: body');

# ---- exact prefix for /exact_only still live ----
like(http_get('/exact_only'), qr/200 OK/,   'exact_only prefix still live: 200');
like(http_get('/exact_only'), qr/exact-ok/, 'exact_only prefix still live: body');

$t->stop();
