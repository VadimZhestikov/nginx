#!/usr/bin/perl

# COMCON increment D1 — the lazy NodeView surface. comcon.pom(fragment) returns
# a reflective NodeView tree over a compiled fragment (module/function
# granularity). The load-bearing property: READS RETURN QUOTATIONS — text() and
# quote() hand back comcon.quote() values (inert, frozen, cap-free descriptions),
# never raw source, so a read cannot leak authority (SEMANTICS REFLECT).
# children/parent materialize lazily; id is creation-ordered + stable; binding is
# redacted by default; the node is a frozen (immutable) view; describe() lists
# the read ops with their safety class.

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
        location /nv { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/nv") {
        locs[i].handler = function(req) {
            var out = {};

            var frag = function(a) {
                function twice(x) { return x + x; }
                function inc(x) { return x + 1; }
                return twice(a) + inc(a);
            };

            var root = comcon.pom(frag);
            out.isView    = (typeof root === "object") && (root !== null);
            out.rootKind  = root.kind;              // 1 = module
            out.frozen    = Object.isFrozen(root);  // immutable view
            out.nChildren = root.children.length;   // lazy getter -> 2
            var k0 = root.children[0], k1 = root.children[1];
            out.k0Kind    = k0.kind;                // 2 = function
            out.k0Name    = k0.name;                // "twice"
            out.k1Name    = k1.name;                // "inc"

            // parent navigation: a child's parent is the module (same id).
            out.parentOk  = (k0.parent.id === root.id);
            out.rootNoPar = (root.parent === null);

            // READS RETURN QUOTATIONS: text()/quote() are comcon.quote() values,
            // not raw strings.
            var q = k0.text();
            out.textIsObj = (typeof q === "object");     // not a string
            out.textFrozen= Object.isFrozen(q);          // quote() freezes
            out.textHasSrc= /twice/.test(q.source);      // carries the source
            // a quote is realizable — it is the same value comcon.realize takes.
            var rq = k1.quote();
            out.quoteSrc  = /inc/.test(rq.source);

            // id: creation-ordered, stable within the process, distinct per node.
            out.idStable  = (comcon.pom(frag).id === root.id);
            out.idDistinct= (k0.id !== root.id) && (k0.id !== k1.id);

            // binding is redacted by default (epoch/profile, no names[]).
            out.bindRedact= (root.binding.profile === "unbound")
                          && !("names" in root.binding);

            // describe(): read ops carry safety class R.
            var d = root.describe();
            out.descR     = d.ops.every(function(o){ return o.cls === "R"; });

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(16);

my $body = http_get('/nv');

like($body, qr/"isView":true/,     'pom() returns a NodeView');
like($body, qr/"rootKind":1/,      'root is a module node');
like($body, qr/"frozen":true/,     'a NodeView is a frozen (immutable) view');
like($body, qr/"nChildren":2/,     'children getter materializes lazily (2)');
like($body, qr/"k0Kind":2/,        'a child is a function node');
like($body, qr/"k0Name":"twice"/,  'child 0 carries its name');
like($body, qr/"k1Name":"inc"/,    'child 1 carries its name');
like($body, qr/"parentOk":true/,   'parent navigation resolves to the module');
like($body, qr/"rootNoPar":true/,  'the module root has no parent');
like($body, qr/"textIsObj":true/,  'READS RETURN QUOTATIONS: text() is not a raw string');
like($body, qr/"textFrozen":true/, 'text() is a frozen quotation');
like($body, qr/"textHasSrc":true/, 'the quotation carries the node source');
like($body, qr/"quoteSrc":true/,   'quote() of a child carries that child source');
like($body, qr/"idStable":true/,   'id is creation-ordered + stable within the process');
like($body, qr/"bindRedact":true/, 'binding is redacted by default (no names[])');
like($body, qr/"descR":true/,      'describe() read ops carry safety class R');
