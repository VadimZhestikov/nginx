#!/usr/bin/perl

# COMCON M-CFG: NginxComFacet mutation slice — gated addLocation/removeLocation.
#
# A facet granted mediate(server, routes(glob)) can MUTATE the live config, but
# only within its route: addLocation/removeLocation are gated on the glob and
# routed to the ONE canonical server op (no divergence). A path outside the glob
# throws; the raw NginxLocation result is discarded (no ungated authority leaks
# into the fragment).

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
        var edit = comcon.include(
            "function(){" +
            " var r={addOk:false,addDenied:false,rmOk:false,rmDenied:false};" +
            " try { acme.addLocation('/acme/new'); r.addOk=true; } catch(e){}" +
            " try { acme.addLocation('/evil'); } catch(e){ r.addDenied=true; }" +
            " try { acme.removeLocation('/acme/a'); r.rmOk=true; } catch(e){}" +
            " try { acme.removeLocation('/other'); } catch(e){ r.rmDenied=true; }" +
            " r.paths = acme.paths();" +
            " return r; }",
            { grants: { acme: comcon.mediate(srv, comcon.routes('/acme/*')) } });

        locs[i].handler = function(req) {
            var r = edit({});
            // Host-side view AFTER the confined fragment mutated: proves the
            // facet routed to the ONE canonical op (no divergent copy).
            var hostPaths = [];
            var hl = nginx.http.servers[0].locations;
            for (var j = 0; j < hl.length; j++) { hostPaths.push(hl[j].path); }
            r.hostPaths = hostPaths;
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(r));
        };
    }
}
JS

$t->try_run('no js module')->plan(8);

my $body = http_get('/probe');

like($body, qr/"addOk":true/,
     'gated addLocation inside the route succeeds');
like($body, qr{"paths":\[[^\]]*"/acme/new"},
     'the added in-route location is now visible');
like($body, qr/"addDenied":true/,
     'addLocation outside the route is denied (throws)');
unlike($body, qr{"/evil"},
     'the out-of-route location was NOT added');
like($body, qr/"rmOk":true/,
     'gated removeLocation inside the route succeeds');
like($body, qr/"rmDenied":true/,
     'removeLocation outside the route is denied (cannot remove /other)');

# canonical-op proof: the host sees the fragment's mutations (one op, no divergence)
like($body, qr{"hostPaths":\[[^\]]*"/acme/new"},
     'canonical op: the HOST sees the added location (routed to the one op)');
like($body, qr{"hostPaths":\[(?:(?!/acme/a").)*\]},
     'canonical op: the HOST no longer sees the removed /acme/a');
