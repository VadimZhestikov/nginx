#!/usr/bin/perl

# COMCON CONVERGE P3: comcon.include's contract.deps — pinned pure-library
# dependencies loaded onto the include primitive (retires js_tenant_dependency
# onto include). Each dep is read, verified against its SHA-256, evaluated as a
# bare-global pure script, and bound as a per-fragment CLOSURE PARAM (not on a
# shared global). A hijacked update (hash mismatch) refuses the include.

use warnings;
use strict;

use Test::More;
use Digest::SHA qw/sha256_hex/;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $lib = "function greet(n){ return \"hi \" + n; }\n({ greet: greet })\n";
my $hash = sha256_hex($lib);

$t->write_file('greet.js', $lib);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /d { }
    }
}
EOF

# double-quoted heredoc: $hash interpolates; %%TESTDIR%% left for write_file_expand
$t->write_file_expand('root.js', <<"JS");
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/d") {
        // 'lib' is a pinned dep -> bound as a closure param (not a free name)
        var h = comcon.include(
            "function(req){ return { status:200, body:'greet=' + lib.greet(req.method) }; }",
            { deps: [{ name:'lib', path:'%%TESTDIR%%/greet.js', sha256:'$hash' }] });

        locs[i].handler = function(req) {
            var r = {};
            var o = h({ method: req.method });
            r.body = o.body;
            // refusal: a wrong pin refuses the include
            try {
                comcon.include("function(req){ return lib.greet('x'); }",
                    { deps: [{ name:'lib', path:'%%TESTDIR%%/greet.js',
                               sha256:'00${\ substr($hash,2) }' }] });
                r.badPin = 'loaded';
            } catch (e) { r.badPin = ''+e; }
            req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
        };
    }
}
JS

$t->try_run('no js module')->plan(2);

my $body = http_get('/d');

like($body, qr/"body":"greet=hi GET"/,
     'a pinned pure-lib dep is bound as a closure param and usable in the fragment');
like($body, qr/"badPin":"[^"]*hash mismatch/,
     'a hijacked dep (hash mismatch) refuses the include');
