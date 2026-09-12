#!/usr/bin/perl

# COMCON D5b-2 — ESTree -> POM CST mapping.  node.cst() parses a POM node's
# source with the vendored acorn and maps it onto the kinds D0 reserved
# (block=3, stmt=4, expr=5), so the tree descends BELOW function granularity.
#
# WHY IT EXISTS, stated as a comparison rather than a claim: D5a's call-site
# audit is a bytecode scan, and bytecode cannot see a LOCALLY-BOUND callee
# (`var g = fetch; g('a')`) or a METHOD call (`obj.fetch('b')`).  Those are the
# residuals INCREMENT_D.md §"reuse framing" names as D5b's unique value.  The
# tests below assert the CST finds them AND that the bytecode scan does not --
# a capability claim is only meaningful against the thing it improves on.
#
# SPANS ARE RELATIVE TO THE OWNING NODE'S `source`, not to a file.  That is what
# composes with D4: D5b-3's harden() rewrites a NODE'S SOURCE and rebuilds via
# bindAt/replace, so node-local offsets are exactly what rebuild-on-write takes.
# Every range assertion below slices it back out of `cst.src` to prove the offset
# is usable as-is -- if that ever breaks, a rewrite would corrupt the source.

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

        location /c { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/c") {
        locs[i].handler = function (req) {
            var o = {};

            /* A plain host function is what comcon.pom() takes (see
             * comcon_pom_nodeview.t).  Note comcon.include() returns a BOUND
             * WRAPPER, not the fragment -- pom() on that describes the wrapper. */
            var frag = function (r2) {
                var g = fetch;          /* locally bound: invisible to bytecode */
                g('a');
                obj.fetch('b');         /* method call: also invisible */
                return 1;
            };

            var root = comcon.pom(frag);
            var cst  = root.cst();

            o.cstType = cst.type;
            o.stmts   = cst.query('stmt').length;
            o.blocks  = cst.query('block').length;
            o.exprs   = cst.query('expr').length > 0;

            /* THE COMPARISON: CST sees it, the D5a bytecode scan does not. */
            var local = cst.callsites('g');
            o.cstLocal      = local.length;
            o.cstLocalSlice = local.length
                ? cst.src.slice(local[0].range[0], local[0].range[1]) : '';
            o.bytecodeLocal = root.callsites('g').length;

            var meth = cst.callsites('fetch');
            o.cstMethodSlices = meth.map(function (x) {
                return cst.src.slice(x.range[0], x.range[1]);
            });

            /* D2's composition still applies over CST nodes */
            o.within = cst.query('call(fetch) within stmt').length;
            o.byType = cst.query('type(ReturnStatement)').length;

            /* Children come back in SOURCE ORDER. Part of the contract, not an
             * accident: a rewriter walks sites in a defined order (and splices
             * back-to-front so earlier offsets stay valid), so an unordered
             * child list would make D5b-3 wrong in a way that only shows up on
             * multi-site rewrites. */
            var blk = cst.children.filter(function (c) {
                return c.type === 'BlockStatement'; })[0];
            var starts = blk.children.map(function (c) { return c.range[0]; });
            o.ordered = starts.every(function (v, i) {
                return i === 0 || starts[i - 1] < v; });
            o.firstStmt = cst.src.slice(blk.children[0].range[0],
                                        blk.children[0].range[1]);

            /* nodes are frozen views, ids stable, parent links walk back up */
            o.frozen = Object.isFrozen(cst);
            o.idStable = (root.cst().query('stmt')[0].id === cst.query('stmt')[0].id);
            var s0 = cst.query('stmt')[0];
            o.parentUp = !!(s0.parent && s0.parent.type);

            /* FAIL CLOSED: a node whose source will not parse must throw, not
             * present an empty CST -- "no sites" for unread code is the worst
             * possible answer for a hardening query. */
            o.closed = 'not-tested';
            try {
                comcon.__parse('function(){');
                o.closed = 'ACCEPTED';
            } catch (e) { o.closed = 'refused'; }

            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(15);

###############################################################################

my $r = http_get('/c');

like($r, qr/"cstType":"FunctionExpression"/,
     'a fragment source parses in expression mode (it is not a Program)');
like($r, qr/"stmts":4/,   'descends below function granularity: 4 statements');
like($r, qr/"blocks":1/,  'block nodes use the reserved kind 3');
like($r, qr/"exprs":true/,'expression nodes are present');

# The reason D5b-2 exists, asserted against what it improves on.
like($r, qr/"cstLocal":1/,
     'CST finds a LOCALLY-BOUND callee (var g = fetch; g(..))');
like($r, qr/"bytecodeLocal":0/,
     "...and D5a's bytecode scan finds none — this is the residual D5b-2 closes");
like($r, qr/"cstLocalSlice":"g\('a'\)"/,
     'its range slices back to the exact call site (what D5b-3 rewrites)');
like($r, qr/"cstMethodSlices":\["obj\.fetch\('b'\)"\]/,
     'CST finds a METHOD call, with an exact range');

# D2 composition and selector extensions
like($r, qr/"within":1/,  "D2's `within` composes over CST nodes");
like($r, qr/"byType":1/,  'type(..) selector matches an ESTree type');

# NodeView contract carried over from D1
like($r, qr/"ordered":true/,
     'children are returned in SOURCE ORDER (D5b-3 splices depend on it)');
like($r, qr/"firstStmt":"var g = fetch;"/,
     '...and the first child really is the first statement in the text');
like($r, qr/"frozen":true/,   'CST nodes are frozen views');
like($r, qr/"parentUp":true/, 'parent links walk back up the CST');

like($r, qr/"closed":"refused"/,
     'FAILS CLOSED: an unparseable source throws rather than yielding no sites');
