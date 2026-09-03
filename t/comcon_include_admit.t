#!/usr/bin/perl

# COMCON CONVERGE P1: comcon.include composes the C3 admission gate + an optional
# identity pin, so an include fragment gets tenant-grade admission — the first
# step of folding the tenant subsystem onto the one include primitive.
#
# Admission is OPT-IN via the contract (imports/identity/checkRequest present);
# without it, include stays un-admitted (backward compatible). With it:
#   - an ungranted free name (nginx) is refused unless listed in imports
#   - dynamic code (eval/with) is refused
#   - a wrong identity pin H(H(source)‖schema) is refused

use warnings;
use strict;

use Test::More;
use Digest::SHA qw/sha256 sha256_hex/;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $src  = 'function(q){ return q.a; }';
my $good = sha256_hex(sha256($src) . 'c2-tenant-env-1');
my $bad  = '00' . substr($good, 2);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /a { }
    }
}
EOF

# Host handler exercises comcon.include's admission on a battery of fragments.
$t->write_file('host.js', <<"JS");
var SRC = '$src', GOOD = '$good', BAD = '$bad';
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/a") {
        locs[i].handler = function(req) {
            var r = {};
            function tri(fn){ try { fn(); return "ok"; } catch(e){ return ""+e; } }
            r.clean    = tri(function(){ comcon.include('function(q){ return q.a; }', {imports:[]}); });
            r.nginxNo  = tri(function(){ comcon.include('function(q){ return nginx.version; }', {imports:[]}); });
            r.nginxYes = tri(function(){ comcon.include('function(q){ return typeof nginx; }', {imports:['nginx']}); });
            r.evalBad  = tri(function(){ comcon.include('function(q){ return eval("1"); }', {imports:[]}); });
            // front-end soundness (was comcon_frontend_audit.t): the restricted
            // profile refuses reflective globals + the Function constructor even
            // if listed in imports, since they defeat the free-name manifest.
            r.fnBad    = tri(function(){ comcon.include('function(q){ return Function("return 1"); }', {imports:['Function']}); });
            r.globalTh = tri(function(){ comcon.include('function(q){ return globalThis; }', {imports:['globalThis']}); });
            r.selfBad  = tri(function(){ comcon.include('function(q){ return self; }', {imports:['self']}); });
            r.idOk     = tri(function(){ comcon.include(SRC, {identity: GOOD}); });
            r.idBad    = tri(function(){ comcon.include(SRC, {identity: BAD}); });
            req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
        };
    }
}
JS

$t->try_run('no js module')->plan(9);

my $body = http_get('/a');

like($body, qr/"clean":"ok"/,
     'admission: a fragment with no free names + imports:[] is admitted');
like($body, qr/"nginxNo":"[^"]*admission refused[^"]*free name not granted: nginx/,
     'admission: an ungranted free name (nginx) is refused');
like($body, qr/"nginxYes":"ok"/,
     'admission: the same name listed in imports is admitted');
like($body, qr/"evalBad":"[^"]*admission refused[^"]*dynamic-code/,
     'admission: dynamic code (eval) is refused');
like($body, qr/"idOk":"ok"/,
     'identity: the correct H(H(source)\N{U+2016}schema) pin is admitted');
like($body, qr/"idBad":"[^"]*artifact identity mismatch/,
     'identity: a wrong pin is refused');
like($body, qr/"fnBad":"[^"]*admission refused[^"]*free name not granted: Function/,
     'restricted: the Function constructor is refused (deny-list) even if in imports');
like($body, qr/"globalTh":"[^"]*admission refused[^"]*free name not granted: globalThis/,
     'restricted: the reflective global globalThis is refused');
like($body, qr/"selfBad":"[^"]*admission refused[^"]*free name not granted: self/,
     'restricted: the reflective global self is refused');
