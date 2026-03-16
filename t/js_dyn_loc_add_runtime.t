#!/usr/bin/perl

# Stage 38 DYN: srv.addLocation() called at request time
#
# Verifies that addLocation() works when called from inside a JS request
# handler (not just during init-time script evaluation).
#
# After a request to /setup adds a new location at runtime, subsequent
# requests to that location are served by the dynamically-installed handler.
# Each worker maintains its own location set (COW isolation); within one
# worker's event loop everything is serialised so no locks are needed.
#
# Test layout:
#   /setup      — handler calls addLocation('/runtime_loc') then responds
#   /runtime_loc — location does not exist until /setup is requested first
#   /base       — pre-existing location; must survive runtime addLocation
#   /add_exact  — adds an exact-match location at runtime
#   /runtime_exact — added by /add_exact

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/runtime_add.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /setup        { }
        location /base         { }
        location /add_exact    { }
    }
}
EOF

$t->write_file('runtime_add.js', <<'JS');
(function() {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* /base — always present, must survive runtime addLocation calls */
    set('/base', function(r) {
        r.respond(200, {}, 'base-ok');
    });

    /* /setup — adds /runtime_loc dynamically then responds */
    set('/setup', function(r) {
        var loc = srv.addLocation('/runtime_loc');
        loc.handler = function(r) {
            r.respond(200, {}, 'runtime-ok');
        };
        r.respond(200, {}, 'setup-ok');
    });

    /* /add_exact — adds an exact-match location dynamically */
    set('/add_exact', function(r) {
        var loc = srv.addLocation('= /runtime_exact');
        loc.handler = function(r) {
            r.respond(200, {}, 'exact-runtime-ok');
        };
        r.respond(200, {}, 'added-exact');
    });
})();
JS

$t->run();

# ---- /base is alive before any runtime mutation ----
like(http_get('/base'), qr/200 OK/,   'base before runtime add: 200');
like(http_get('/base'), qr/base-ok/,  'base before runtime add: body');

# ---- /setup adds /runtime_loc ----
like(http_get('/setup'), qr/200 OK/,  'setup: 200');
like(http_get('/setup'), qr/setup-ok/,'setup: body');

# ---- /runtime_loc must now be live ----
like(http_get('/runtime_loc'), qr/200 OK/,     'runtime_loc: 200');
like(http_get('/runtime_loc'), qr/runtime-ok/, 'runtime_loc: body');

# ---- /add_exact adds = /runtime_exact ----
like(http_get('/add_exact'), qr/200 OK/,       'add_exact: 200');
like(http_get('/add_exact'), qr/added-exact/,  'add_exact: body');

# ---- exact match location must now be live ----
like(http_get('/runtime_exact'), qr/200 OK/,           'runtime_exact: 200');
like(http_get('/runtime_exact'), qr/exact-runtime-ok/, 'runtime_exact: body');

# ---- /base must survive all the mutations ----
like(http_get('/base'), qr/200 OK/,   'base after runtime adds: 200');
like(http_get('/base'), qr/base-ok/,  'base after runtime adds: body');

$t->stop();
