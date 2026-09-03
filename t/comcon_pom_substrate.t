#!/usr/bin/perl

# COMCON increment D0 — the POM substrate. comcon.__pomInspect(fn) reflects a
# compiled fragment as a POM node tree at MODULE/FUNCTION granularity, built
# directly from the bytecode tree (each JSFunctionBytecode carries its own
# source slice, pc->line table, and nested-function constants) — no CST parser.
# This is the diagnostic bridge that de-risks increment D; the lazy NodeView
# surface (text/quote/describe/query) lands in D1. The gate: node KINDS are
# enumerated (module=1, function=2), the function CHILD tree is walked, spans
# are present, and the content hash is STABLE for identical source (R7 pin).

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
        location /pom { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/pom") {
        locs[i].handler = function(req) {
            var out = {};

            // A fragment with two nested functions -> a module node with two
            // function children.
            var frag = function(a) {
                function twice(x) { return x + x; }
                function inc(x) { return x + 1; }
                return twice(a) + inc(a);
            };

            var n = comcon.__pomInspect(frag);
            out.isObj      = (typeof n === "object") && (n !== null);
            out.rootKind   = n.kind;               // 1 = module (tree root)
            out.childCount = n.childCount;         // 2 nested functions
            out.kid0Kind   = n.children[0].kind;   // 2 = function
            out.kid0Name   = n.children[0].name;   // "twice"
            out.kid1Name   = n.children[1].name;   // "inc"
            out.hasSpan    = (n.line0 >= 0) && (n.line1 >= n.line0);
            out.kidSpan    = (n.children[0].line1 >= n.children[0].line0);
            out.hashType   = typeof n.hash;        // "string"
            out.hashNonzero= (n.hash !== "0");

            // hash is content-stable: an identical fragment hashes identically,
            // a changed one does not (R7 pin-by-hash).
            var same = comcon.__pomInspect(function(a) {
                function twice(x) { return x + x; }
                function inc(x) { return x + 1; }
                return twice(a) + inc(a);
            });
            var diff = comcon.__pomInspect(function(a) {
                function twice(x) { return x + x + 1; }   // changed body
                function inc(x) { return x + 1; }
                return twice(a) + inc(a);
            });
            out.hashStable = (same.hash === n.hash);
            out.hashDiff   = (diff.hash !== n.hash);

            // a non-fragment (native function) reflects to undefined.
            out.nativeUndef = (comcon.__pomInspect(Math.max) === undefined);

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(14);

my $body = http_get('/pom');

like($body, qr/"isObj":true/,       'inspect returns a node object');
like($body, qr/"rootKind":1/,       'tree root is a module node (kind 1)');
like($body, qr/"childCount":2/,     'two nested functions -> two children');
like($body, qr/"kid0Kind":2/,       'a child is a function node (kind 2)');
like($body, qr/"kid0Name":"twice"/, 'child 0 carries its function name');
like($body, qr/"kid1Name":"inc"/,   'child 1 carries its function name');
like($body, qr/"hasSpan":true/,     'root node has a source span (line0..line1)');
like($body, qr/"kidSpan":true/,     'child node has a source span');
like($body, qr/"hashType":"string"/,'content hash is a (decimal-string) value');
like($body, qr/"hashNonzero":true/, 'content hash is non-zero for a real fragment');
like($body, qr/"hashStable":true/,  'R7: identical source hashes identically');
like($body, qr/"hashDiff":true/,    'R7: changed source hashes differently');
like($body, qr/"nativeUndef":true/, 'a non-fragment (native fn) reflects to undefined');
like($body, qr/HTTP\/1\.1 200/,     'handler responded 200');
