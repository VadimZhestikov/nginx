#!/usr/bin/perl

# D5b-2 — the vendored ES parser (acorn 8.14.0, MIT, src/js/vendor/).
#
# INCREMENT_D.md's "the crux — the parser" chose vendoring a proven parser over
# hand-rolling one, because the parser is security-relevant TCB: a mis-parse
# that admits something it should not is a soundness hole, so proven-ness beats
# hand-rolled. That choice only pays off if the vendored artifact is actually
# the one claimed, and if it fails CLOSED. Both are asserted here.
#
# The load-bearing output is RANGES, not node shapes: D5b-3 rewrites by splicing
# at byte offsets into the ORIGINAL source and never re-prints the AST, so no
# code generator enters the TCB and comments/formatting round-trip exactly.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /p { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/p") {
        locs[i].handler = function (req) {
            var o = {};
            var src = 'a.b(1);\n function f(){ return fetch("x"); }';

            var ast = comcon.__parse(src);
            o.type   = ast.type;
            o.nstmt  = ast.body.length;

            /* ranges must be byte offsets into the ORIGINAL source -- the
             * property D5b-3's offset splicing depends on. Check by slicing. */
            var r0 = ast.body[0].range;
            o.slice0 = src.slice(r0[0], r0[1]);
            o.line1  = ast.body[1].loc.start.line;

            /* descend below function granularity -- the point of D5b-2 */
            var fn = ast.body[1];
            o.fnKind = fn.type;
            var call = fn.body.body[0].argument;          /* fetch("x") */
            o.callee = call && call.callee && call.callee.name;
            o.callSlice = src.slice(call.range[0], call.range[1]);

            /* FAIL CLOSED */
            var closed = [];
            ['a(', 'function', '{', 'a b c', 'return 1'].forEach(function (bad) {
                try { comcon.__parse(bad); closed.push('ACCEPTED:' + bad); }
                catch (e) { closed.push('refused'); }
            });
            o.closed = closed.join(',');

            /* the parser is an internal of the analysis path, not host surface */
            o.onGlobal = (typeof globalThis.acorn);

            /* and it must not be reachable from a CONFINED fragment */
            var frag = comcon.include(
                "function(){ return { seen: (typeof comcon) }; }");
            o.inFragment = frag({}).seen;

            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(12);

###############################################################################

my $r = http_get('/p');

like($r, qr/"type":"Program"/,   'parses to an ESTree Program');
like($r, qr/"nstmt":2/,          'both top-level statements are present');
like($r, qr/"slice0":"a\.b\(1\);"/,
     'range is a byte offset into the ORIGINAL source (slices back exactly)');
like($r, qr/"line1":2/,          'locations are reported');
like($r, qr/"fnKind":"FunctionDeclaration"/, 'function nodes are typed');

# The reason D5b-2 exists: descend BELOW function granularity, which D5a's
# bytecode scan cannot do.
like($r, qr/"callee":"fetch"/,   'expression-granularity: the call node is reachable');
like($r, qr/"callSlice":"fetch\(\\"x\\"\)"/,
     'and its range slices back to the exact call site (what D5b-3 rewrites)');

# TCB invariant. A parser that accepts garbage would admit what it cannot
# analyse; five malformed inputs, none may parse.
like($r, qr/"closed":"refused,refused,refused,refused,refused"/,
     'FAILS CLOSED on every malformed input');

like($r, qr/"onGlobal":"undefined"/,
     'the parser is not left on the host global surface');
like($r, qr/"inFragment":"undefined"/,
     'and is unreachable from a confined fragment (no comcon there at all)');

# Supply chain: the vendored artifact must be the one PROVENANCE.md claims, and
# the committed header must match the .js it says it embeds. A generated file
# nobody checks is where a silent edit hides -- and this one is TCB.
chdir('..') or die;
my $sha = `sha256sum src/js/vendor/acorn.js 2>/dev/null`;
$sha = (split ' ', $sha)[0] // '';
my $prov = do { local (@ARGV, $/) = 'src/js/vendor/PROVENANCE.md'; <> } // '';
like($prov, qr/\Q$sha\E/,
     'vendored acorn.js matches the sha256 recorded in PROVENANCE.md')
    if $sha;

my $gen = system('python3 src/js/vendor/gen-acorn-h.py --check >/dev/null 2>&1');
is($gen, 0, 'committed acorn_js.h is in sync with acorn.js');
