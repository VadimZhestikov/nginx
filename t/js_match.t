#!/usr/bin/perl

# Stage 49: nginx.http.match(uri [, serverName]) — simulate nginx routing
#
# Verifies:
#   1. Exact match (= /exact) wins over prefix.
#   2. Preferential-prefix (^~ /pref) beats a longer regex.
#   3. Regex match (~ /rx\d+) wins over plain prefix.
#   4. Plain-prefix fallback when nothing else matches.
#   5. Returns null for an unknown serverName.
#   6. Returns null when no location matches.
#   7. Omitting serverName uses the first server.
#   8. Named locations are not returned by match().

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

js_source %%TESTDIR%%/match.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name alpha.local;

        location = /exact        { }
        location ^~ /pref        { }
        location ~ /rx\d+        { }
        location /prefix         { }
        location @named          { return 200 "named"; }

        location /check_exact    { }
        location /check_noregex  { }
        location /check_regex    { }
        location /check_prefix   { }
        location /check_no_srv   { }
        location /check_no_match { }
        location /check_no_name  { }
        location /check_named    { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name beta.local;

        location /beta { }
    }
}
EOF

$t->write_file('match.js', <<'JS');
(function() {
    var http = nginx.http;

    function findSrv(name) {
        return http.servers.find(function(s) { return s.name === name; });
    }

    function findLoc(srv, path) {
        return srv.locations.find(function(l) { return l.path === path; });
    }

    var alpha = findSrv('alpha.local');

    function handler(uri, srv) {
        return function(r) {
            var loc = srv ? http.match(uri, srv) : http.match(uri);
            r.respond(200, {}, loc ? loc.pattern : 'null');
        };
    }

    /* 1. Exact match */
    findLoc(alpha, '/check_exact').handler =
        handler('/exact', 'alpha.local');

    /* 2. Preferential prefix (^~ /pref) beats regex /rx42 */
    findLoc(alpha, '/check_noregex').handler =
        handler('/pref/rx42', 'alpha.local');

    /* 3. Regex wins over plain prefix */
    findLoc(alpha, '/check_regex').handler =
        handler('/rx99', 'alpha.local');

    /* 4. Plain prefix fallback */
    findLoc(alpha, '/check_prefix').handler =
        handler('/prefix/sub', 'alpha.local');

    /* 5. Unknown server → null */
    findLoc(alpha, '/check_no_srv').handler =
        handler('/beta', 'nosuchserver.local');

    /* 6. No matching location → null */
    findLoc(alpha, '/check_no_match').handler =
        handler('/zzz', 'alpha.local');

    /* 7. No serverName → use first server (alpha.local) */
    findLoc(alpha, '/check_no_name').handler =
        handler('/exact');

    /* 8. Named locations not returned */
    findLoc(alpha, '/check_named').handler =
        handler('/@named', 'alpha.local');
})();
JS

$t->run();

sub vhost {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# 1. Exact match
like(vhost('alpha.local', '/check_exact'), qr/200 OK/, 'exact: 200');
like(vhost('alpha.local', '/check_exact'), qr/= \/exact/, 'exact: pattern');

# 2. Preferential prefix beats regex
like(vhost('alpha.local', '/check_noregex'), qr/200 OK/, 'noregex: 200');
like(vhost('alpha.local', '/check_noregex'), qr/\^\~ \/pref/, 'noregex: pattern');

# 3. Regex wins over plain prefix
like(vhost('alpha.local', '/check_regex'), qr/200 OK/, 'regex: 200');
like(vhost('alpha.local', '/check_regex'), qr/~ /, 'regex: pattern');

# 4. Plain prefix fallback
like(vhost('alpha.local', '/check_prefix'), qr/200 OK/, 'prefix: 200');
like(vhost('alpha.local', '/check_prefix'), qr/\/prefix/, 'prefix: pattern');

# 5. Unknown server → null
like(vhost('alpha.local', '/check_no_srv'), qr/200 OK/, 'no_srv: 200');
like(vhost('alpha.local', '/check_no_srv'), qr/null/, 'no_srv: null');

# 6. No matching location → null
like(vhost('alpha.local', '/check_no_match'), qr/200 OK/, 'no_match: 200');
like(vhost('alpha.local', '/check_no_match'), qr/null/, 'no_match: null');

# 7. No serverName → first server
like(vhost('alpha.local', '/check_no_name'), qr/200 OK/, 'no_name: 200');
like(vhost('alpha.local', '/check_no_name'), qr/= \/exact/, 'no_name: pattern');

# 8. Named location not returned
like(vhost('alpha.local', '/check_named'), qr/200 OK/, 'named: 200');
like(vhost('alpha.local', '/check_named'), qr/null/, 'named: null');

$t->stop();
