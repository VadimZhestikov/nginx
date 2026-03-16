#!/usr/bin/perl

# Stage 37 DYN: srv.addLocation(pattern [, {template: '/path'}])
#
# Verifies that new prefix, exact-match, and preferential-prefix locations
# can be injected at init time (from js_source) and that:
#   1. The new location responds correctly.
#   2. Pre-existing locations are unaffected.
#   3. The optional {template: '/path'} option copies settings (root) from
#      an existing location.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(10);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/dyn_loc_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Pre-existing locations
        location /existing  { }
        location /tmpl      { root /tmpl-root; }
    }
}
EOF

$t->write_file('dyn_loc_init.js', <<'JS');
(function() {
    var srv = nginx.http.servers[0];

    /* Pre-existing location handler */
    srv.locations.find(function(l) {
        return l.path === '/existing';
    }).handler = function(r) {
        r.respond(200, {}, 'existing-ok');
    };

    /* 1. Plain prefix location */
    var loc1 = srv.addLocation('/dynprefix');
    loc1.handler = function(r) {
        r.respond(200, {}, 'prefix-ok');
    };

    /* 2. Exact-match location */
    var loc2 = srv.addLocation('= /dynexact');
    loc2.handler = function(r) {
        r.respond(200, {}, 'exact-ok');
    };

    /* 3. Preferential-prefix location */
    var loc3 = srv.addLocation('^~ /dynnoregex');
    loc3.handler = function(r) {
        r.respond(200, {}, 'noregex-ok');
    };

    /* 4. Location with template — inherits root from /tmpl */
    var loc4 = srv.addLocation('/dyncopy', { template: '/tmpl' });
    loc4.handler = function(r) {
        r.respond(200, {}, r.location.root);
    };
})();
JS

$t->run();

# Existing location is unaffected
like(http_get('/existing'),   qr/200 OK/,      'existing location: 200');
like(http_get('/existing'),   qr/existing-ok/, 'existing location: body');

# New prefix location
like(http_get('/dynprefix'),  qr/200 OK/,      'addLocation prefix: 200');
like(http_get('/dynprefix'),  qr/prefix-ok/,   'addLocation prefix: body');

# New exact-match location
like(http_get('/dynexact'),   qr/200 OK/,      'addLocation exact: 200');
like(http_get('/dynexact'),   qr/exact-ok/,    'addLocation exact: body');

# New preferential-prefix location
like(http_get('/dynnoregex'), qr/200 OK/,      'addLocation ^~: 200');
like(http_get('/dynnoregex'), qr/noregex-ok/,  'addLocation ^~: body');

# Template location — inherits root /tmpl-root from /tmpl
like(http_get('/dyncopy'),    qr/200 OK/,      'addLocation template: 200');
like(http_get('/dyncopy'),    qr{/tmpl-root},  'addLocation template: root inherited');

$t->stop();
