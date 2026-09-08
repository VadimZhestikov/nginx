#!/usr/bin/perl

# Stage 50: srv.findLocation(pattern) — fast lookup by pattern string
#
# Verifies:
#   1. Plain prefix lookup          "/prefix"
#   2. Exact match lookup           "= /exact"
#   3. Preferential prefix lookup   "^~ /pref"
#   4. Regex lookup                 "~ /rx\d+"
#   5. Case-insensitive regex       "~* /ci"
#   6. Named location lookup        "@named"
#   7. Unknown pattern → null
#   8. After addLocation, newly added location is findable
#   9. After removeLocation, removed location returns null

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http rewrite/)->plan(22);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/find_location.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name alpha.local;

        location /prefix        { }
        location = /exact       { }
        location ^~ /pref       { }
        location ~ /rx\d+       { }
        location ~* /ci         { }
        location @named         { return 200 "named-body"; }

        location /check_prefix  { }
        location /check_exact   { }
        location /check_pref    { }
        location /check_regex   { }
        location /check_ci      { }
        location /check_named   { }
        location /check_miss    { }
        location /check_wide    { }
        location /check_wide2   { }
        location /check_dyn_add { }
        location /check_dyn_rm  { }
    }
}
EOF

$t->write_file('find_location.js', <<'JS');
(function() {
    var http  = nginx.http;
    var alpha = http.servers.find(function(s) { return s.name === 'alpha.local'; });

    function findLoc(srv, path) {
        return srv.locations.find(function(l) { return l.path === path; });
    }

    function probe(checkPath, pattern) {
        findLoc(alpha, checkPath).handler = function(r) {
            var loc = alpha.findLocation(pattern);
            r.respond(200, {}, loc ? loc.pattern : 'null');
        };
    }

    probe('/check_prefix', '/prefix');
    probe('/check_exact',  '= /exact');
    probe('/check_pref',   '^~ /pref');
    probe('/check_regex',  '~ /rx\\d+');
    probe('/check_ci',     '~* /ci');
    probe('/check_named',  '@named');
    probe('/check_miss',   '/no-such-location');

    /* A WIDE (non-Latin-1) pattern. This is the case where the old
     * JS_FreeCString-before-compare was a real use-after-free rather than a
     * latent one: for a Latin-1 argument JS_ToCString returns a pointer the
     * caller's own JSValue still references, but for a wide string it
     * allocates a fresh UTF-8 buffer that the free releases outright, so
     * every ngx_memcmp afterwards read freed memory. */
    probe('/check_wide',   '/\u00e4\u00f6\u00fc-no-such-location');
    probe('/check_wide2',  '= /\u4e2d\u6587');

    /* Dynamic add then find */
    findLoc(alpha, '/check_dyn_add').handler = function(r) {
        alpha.addLocation('/dynloc').handler = function(r2) {
            r2.respond(200, {}, 'dynloc-ok');
        };
        var loc = alpha.findLocation('/dynloc');
        r.respond(200, {}, loc ? loc.pattern : 'null');
    };

    /* Dynamic remove then find */
    findLoc(alpha, '/check_dyn_rm').handler = function(r) {
        alpha.addLocation('/rmme');
        alpha.removeLocation('/rmme');
        var loc = alpha.findLocation('/rmme');
        r.respond(200, {}, loc ? loc.pattern : 'null');
    };
})();
JS

$t->run();

sub vhost {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# 1. Plain prefix
like(vhost('alpha.local', '/check_prefix'), qr/200 OK/, 'prefix: 200');
like(vhost('alpha.local', '/check_prefix'), qr{/prefix},  'prefix: found');

# 2. Exact match
like(vhost('alpha.local', '/check_exact'), qr/200 OK/, 'exact: 200');
like(vhost('alpha.local', '/check_exact'), qr/= \/exact/, 'exact: found');

# 3. Preferential prefix
like(vhost('alpha.local', '/check_pref'), qr/200 OK/, 'pref: 200');
like(vhost('alpha.local', '/check_pref'), qr/\^\~ \/pref/, 'pref: found');

# 4. Case-sensitive regex
like(vhost('alpha.local', '/check_regex'), qr/200 OK/, 'regex: 200');
like(vhost('alpha.local', '/check_regex'), qr/~ /, 'regex: found');

# 5. Case-insensitive regex
like(vhost('alpha.local', '/check_ci'), qr/200 OK/, 'ci: 200');
like(vhost('alpha.local', '/check_ci'), qr/~\* \/ci/, 'ci: found');

# 6. Named location
like(vhost('alpha.local', '/check_named'), qr/200 OK/, 'named: 200');
like(vhost('alpha.local', '/check_named'), qr/\@named/, 'named: found');

# 7. Unknown pattern → null
like(vhost('alpha.local', '/check_miss'), qr/200 OK/, 'miss: 200');
like(vhost('alpha.local', '/check_miss'), qr/null/, 'miss: null');

# 8. Dynamically added location is findable
like(vhost('alpha.local', '/check_dyn_add'), qr/200 OK/, 'dyn_add: 200');
like(vhost('alpha.local', '/check_dyn_add'), qr{/dynloc}, 'dyn_add: found');

# 9. Removed location returns null
like(vhost('alpha.local', '/check_dyn_rm'), qr/200 OK/, 'dyn_rm: 200');
like(vhost('alpha.local', '/check_dyn_rm'), qr/null/, 'dyn_rm: null');

# 10. Wide-string patterns: must not fault and must miss cleanly.
#     Native "no crash" does NOT prove the UAF is gone -- freed pool/heap
#     memory usually stays mapped -- so this pins behaviour and exercises the
#     path; ASAN is what actually proves it.
like(vhost('alpha.local', '/check_wide'),  qr/200 OK/,  'wide pattern: 200');
like(vhost('alpha.local', '/check_wide'),  qr/null/,    'wide pattern: null');
like(vhost('alpha.local', '/check_wide2'), qr/200 OK/,  'wide exact pattern: 200');
like(vhost('alpha.local', '/check_wide2'), qr/null/,    'wide exact pattern: null');

$t->stop();
