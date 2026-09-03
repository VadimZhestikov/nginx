#!/usr/bin/perl

# COMCON increment D2 — the POM selector language. node.query(sel) is a
# value-level target DSL over the NodeView subtree (POM.md §6 Q2):
#   selector := term ('within' term)*  ;  term := factor+ (AND)
#   factor   := 'module' | 'function' | '*' | 'name(' glob ')'
#   glob     := exact | pre* | *suf | *mid* | *
# `A within B` selects nodes matching A that have an ANCESTOR matching B — the
# intensional-composition pattern that hardening needs ("functions within
# module X"). Selection recomputes live on every call (born-bound, R9). Results
# are NodeViews (the same reflective values D1 returns), so a query result is
# directly quote()-able / navigable.

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
        location /q { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/q") {
        locs[i].handler = function(req) {
            var out = {};

            var frag = function(a) {
                function twice(x) { return x + x; }
                function outer(y) {
                    function innerTwice(z) { return z + z; }
                    return innerTwice(y);
                }
                return twice(a) + outer(a);
            };

            var root = comcon.pom(frag);

            function names(arr) {
                return arr.map(function(n){ return n.name; }).sort().join(",");
            }

            // kind selectors
            out.allFns   = root.query("function").length;     // twice,outer,innerTwice
            out.fnNames  = names(root.query("function"));
            out.modules  = root.query("module").length;       // just the root
            out.star     = root.query("*").length;            // module + 3 fns = 4

            // name selectors: exact, suffix glob
            out.exact    = names(root.query("name(twice)"));   // "twice"
            out.suffix   = names(root.query("name(*Twice)"));  // "innerTwice"
            out.prefix   = names(root.query("name(inner*)"));  // "innerTwice"

            // AND of factors
            out.fnAnd    = names(root.query("function name(outer)")); // "outer"

            // `within`: intensional composition (functions nested under `outer`)
            out.within   = names(root.query("function within name(outer)")); // innerTwice

            // a query result is a NodeView (quote()-able)
            var hit = root.query("name(twice)")[0];
            out.hitQuote = /x \+ x/.test(hit.quote().source);

            // live: re-running recomputes (fresh array, same content)
            var a1 = root.query("function"), a2 = root.query("function");
            out.freshArray = (a1 !== a2) && (a1.length === a2.length);

            // a bad selector factor throws
            try { root.query("bogus"); out.badThrew = false; }
            catch (e) { out.badThrew = /bad selector factor/.test(e.message); }

            // an empty selector throws
            try { root.query("  "); out.emptyThrew = false; }
            catch (e) { out.emptyThrew = /empty selector/.test(e.message); }

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(13);

my $body = http_get('/q');

like($body, qr/"allFns":3/,                    'kind selector: all functions');
like($body, qr/"fnNames":"innerTwice,outer,twice"/, 'function names, subtree-wide');
like($body, qr/"modules":1/,                   'kind selector: module (root only)');
like($body, qr/"star":4/,                      '* selects the whole subtree');
like($body, qr/"exact":"twice"/,               'name(exact) selects one node');
like($body, qr/"suffix":"innerTwice"/,         'name(*glob) suffix match');
like($body, qr/"prefix":"innerTwice"/,         'name(glob*) prefix match');
like($body, qr/"fnAnd":"outer"/,               'AND of factors (function name(outer))');
like($body, qr/"within":"innerTwice"/,         'A within B: intensional ancestor composition');
like($body, qr/"hitQuote":true/,               'a query result is a quote()-able NodeView');
like($body, qr/"freshArray":true/,             'born-bound: recomputed live each call');
like($body, qr/"badThrew":true/,               'a bad selector factor throws');
like($body, qr/"emptyThrew":true/,             'an empty selector throws');
