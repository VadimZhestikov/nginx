#!/usr/bin/perl

# COMCON M-CFG: mediate() for a COM node -> a NginxComFacet (attenuated COM cap).
#
# A server is a STATEFUL COM node (per-wrapper dynamic-location pools), so it
# can't be re-wrapped into the confined compartment's separate runtime without
# diverging / UAF. mediate(server, routes(glob)) instead grants a facet: a thin,
# stateless C cap that borrows the ONE canonical server opaque and routes reads
# through a glob membrane. The fragment sees only in-glob locations.
#
#   mediate(nginx.http.servers[0], routes('/acme/*'))
#     facet.paths()          -> only /acme/* location paths
#     facet.allowed('/acme/x') -> true ; facet.allowed('/other') -> false

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /acme/a { return 200 "a"; }
        location /acme/b { return 200 "b"; }
        location /other  { return 200 "o"; }
        location /probe  { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var srv = nginx.http.servers[0];
var locs = srv.locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/probe") {
        var view = comcon.include(
            "function(){ return {" +
            "  paths: acme.paths()," +
            "  route: acme.route," +
            "  inAcme: acme.allowed('/acme/x')," +
            "  inOther: acme.allowed('/other')" +
            " }; }",
            { grants: { acme: comcon.mediate(srv, comcon.routes('/acme/*')) } });

        locs[i].handler = function(req) {
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(view({})));
        };
    }
}
JS

$t->try_run('no js module')->plan(5);

my $body = http_get('/probe');

like($body, qr{"route":"/acme/\*"},
     'the facet carries its route glob');
like($body, qr{"paths":\[[^\]]*"/acme/a"},
     'facet.paths(): an in-glob location (/acme/a) is visible');
like($body, qr{"paths":\[[^\]]*"/acme/b"},
     'facet.paths(): an in-glob location (/acme/b) is visible');
unlike($body, qr{"/other"},
     'facet.paths(): an out-of-glob location (/other) is NOT visible');
like($body, qr{"inAcme":true,"inOther":false},
     'facet.allowed(): the glob membrane admits /acme/* and denies /other');
