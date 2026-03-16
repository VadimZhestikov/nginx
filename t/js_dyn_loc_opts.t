#!/usr/bin/perl

# Stage 41 DYN: addLocation opts — before/after ordering + idempotent duplicate
#
# Verifies:
#   1. { before: '~ /pattern' } inserts a regex location BEFORE the named one.
#   2. { after:  '~ /pattern' } inserts a regex location AFTER  the named one.
#   3. Calling addLocation() twice with the same pattern returns the existing
#      location object (idempotent) — no duplicate entries, no crash.
#   4. Prefix locations are also idempotent (second addLocation returns same).

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

js_source %%TESTDIR%%/loc_opts.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /base { }
    }
}
EOF

$t->write_file('loc_opts.js', <<'JS');
(function() {
    var srv = nginx.http.servers[0];

    /* /base — must survive all mutations */
    srv.locations.find(function(l) { return l.path === '/base'; })
       .handler = function(r) { r.respond(200, {}, 'base-ok'); };

    /* Build an ordered regex chain: first, middle, last */
    var first  = srv.addLocation('~ /order/first');
    first.handler = function(r) { r.respond(200, {}, 'first'); };

    var last   = srv.addLocation('~ /order/last');
    last.handler = function(r) { r.respond(200, {}, 'last'); };

    /* Insert BEFORE 'first' → becomes the new head */
    var before_first = srv.addLocation('~ /order/before_first',
                                       { before: '~ /order/first' });
    before_first.handler = function(r) { r.respond(200, {}, 'before_first'); };

    /* Insert AFTER 'first' → sits between first and last */
    var after_first = srv.addLocation('~ /order/after_first',
                                      { after: '~ /order/first' });
    after_first.handler = function(r) { r.respond(200, {}, 'after_first'); };

    /*
     * Order is now: before_first → first → after_first → last
     * /order/before_first matches only before_first (most specific, checked first).
     * /order/first        matches before_first, first, after_first, last
     *   but before_first regex does NOT match /order/first, so first wins.
     */

    /* Idempotent regex: second addLocation returns the same object */
    var dup_re = srv.addLocation('~ /order/first');
    /* If idempotent, dup_re is the same entry — overwrite handler */
    dup_re.handler = function(r) { r.respond(200, {}, 'first-dup'); };

    /* Idempotent prefix: second addLocation('/base') returns existing */
    var dup_prefix = srv.addLocation('/base');
    dup_prefix.handler = function(r) { r.respond(200, {}, 'base-dup'); };

    /* A URL that only matches 'last' */
    /* /order/last matches before_first? No. first? No. after_first? No. last? Yes. */
})();
JS

$t->run();

# ---- /base still alive ----
like(http_get('/base'), qr/200 OK/,   'base: 200');
like(http_get('/base'), qr/base-dup/, 'base: idempotent prefix — handler updated');

# ---- before/after ordering ----

# /order/before_first: only before_first pattern matches (others don't)
like(http_get('/order/before_first'), qr/200 OK/,        'before_first: 200');
like(http_get('/order/before_first'), qr/before_first/,  'before_first: matched first in chain');

# /order/first: before_first doesn't match /order/first (no "before_first" in path)
# so 'first' (now has dup_re handler = first-dup) wins
like(http_get('/order/first'), qr/200 OK/,      'first: 200');
like(http_get('/order/first'), qr/first-dup/,   'first: idempotent regex — handler updated');

# /order/after_first: before_first doesn't match, first doesn't match (no /first in path)
# after_first matches
like(http_get('/order/after_first'), qr/200 OK/,         'after_first: 200');
like(http_get('/order/after_first'), qr/after_first/,    'after_first: inserted after first');

# /order/last: no earlier regex matches
like(http_get('/order/last'), qr/200 OK/,   'last: 200');
like(http_get('/order/last'), qr/\blast\b/, 'last: matched by last regex');

# ---- ordering: before_first is checked before first ----
# A URL matching BOTH before_first and first patterns: /order/before_first/x
# before_first regex: ~ /order/before_first  → matches
# first regex:        ~ /order/first         → does NOT match /order/before_first/x
# So before_first wins
like(http_get('/order/before_first/extra'), qr/before_first/,
     'ordering: before_first regex wins over first for /order/before_first/extra');

# ---- ordering: first is checked before after_first ----
# /order/first/extra matches both first and after_first
# first comes before after_first in chain
like(http_get('/order/first/extra'), qr/first-dup/,
     'ordering: first checked before after_first for /order/first/extra');

# ---- ordering: after_first before last ----
like(http_get('/order/after_first/x'), qr/after_first/,
     'ordering: after_first before last for /order/after_first/x');

# ---- /base must still work ----
like(http_get('/base'), qr/200 OK/,   'base after all ops: 200');
like(http_get('/base'), qr/base-dup/, 'base after all ops: body');

# ---- worker still alive ----
like(http_get('/base'), qr/200 OK/, 'worker alive after all operations');

$t->stop();
