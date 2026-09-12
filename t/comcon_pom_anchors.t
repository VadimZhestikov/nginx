#!/usr/bin/perl

# COMCON D5b-2 — anchors and span predicates in the POM selector grammar.
#
# An ANCHOR is FOUNDATION's "inline binding" carrier: an INERT MARKER NAMING A
# SITE (`"use comcon: checkout";`, a directive-prologue string that is a no-op
# statement in plain JS).  The policy lives in a separate unit and references
# the anchor BY NAME, so policy text is never trapped inside a string literal.
# POM.md calls anchors queryable ATTRIBUTES of a node, not nodes of their own.
#
# WHY BOTH PREDICATES ARE HERE, AND WHY THAT IS THE POINT: `line(N)` is the
# obvious way to name a site and it is BRITTLE -- insert a line above and the
# policy now points somewhere else, silently.  The pair of fragments below are
# byte-identical except for two lines added above the anchored function, and
# the test asserts the consequence directly: the anchor selector finds the site
# in both, the line selector finds it in exactly one.  That measured difference
# is the whole argument for the anchors model, so it is asserted, not narrated.
#
# NEGATIVE CONTROLS (run 2026-09-12; all six reverted to failure, and the tree
# rebuilt + re-passed after each).  Each line is a decision in ngx_js_com.c and
# the test that stops passing without it:
#
#   scan st.directive, not st.type==='ExpressionStatement'   -> test 9 fails
#     (acorn rejects parenthesized strings; a hand-rolled scan accepted them)
#   match the RAW directive, not st.expression.value         -> test 11 fails
#     (the decoded form of an escaped name IS the accepted name)
#   cstKind maps Function* to kind 2                         -> tests 3-5 fail
#   pomName strips surrounding quotes                        -> tests 3-5 fail
#   anchors() throws on a node with no anchors array         -> test 23 fails
#   line() validated by regex, not parseInt                  -> test 22 fails
#     (parseInt('5zz') is 5, so a typo becomes a line number)
#
# A sixth candidate was DROPPED rather than kept: an explicit backslash test on
# the directive could not be made to fail, because the name charset already
# excludes a backslash.  It read like the enforcement without being it.

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

            /* A is the reference site. */
            var fragA = function (r) {
                "use comcon: checkout";
                "use strict";
                var n = 0;
                function inner(x) {
                    "use comcon: pay-v2";
                    return x + 1;
                }
                return inner(n);
            };

            /* B is the SAME code after an edit ABOVE the anchored function --
             * two added lines, nothing else changed. */
            var fragB = function (r) {
                "use comcon: checkout";
                "use strict";
                var n = 0;
                var pad = 1;
                n += pad;
                function inner(x) {
                    "use comcon: pay-v2";
                    return x + 1;
                }
                return inner(n);
            };

            var A = comcon.pom(fragA).cst();
            var B = comcon.pom(fragB).cst();

            o.aAnchors = A.anchors;
            o.frozen   = Object.isFrozen(A.anchors);

            /* the function node carries kind 2 at BOTH tiers (p_symbol) */
            var innerA = A.query("function anchors('pay-v2')");
            o.innerHits = innerA.length;
            o.innerName = innerA.length ? innerA[0].name : '';
            o.innerType = innerA.length ? innerA[0].type : '';

            /* a node reports the prologue of the body IT owns -- the outer
             * function does not inherit the inner function's anchor */
            o.blockHits = A.query("block anchors('checkout')").length;
            o.globPre   = A.query("anchors('check*')").length > 0;
            o.globSuf   = A.query("anchors('*-v2')").length;

            /* PARENTHESIZED: not a directive to the language, so not an anchor
             * to us.  Control is the same fragment without the parens. */
            var paren = comcon.pom(function () {
                ("use comcon: sneaky");
                return 1;
            }).cst();
            var plain = comcon.pom(function () {
                "use comcon: sneaky";
                return 1;
            }).cst();
            o.parenAnchors = paren.anchors.length;
            o.plainAnchors = plain.anchors;

            /* ESCAPED: `directive` is the RAW slice, so a spelling a reviewer
             * would not recognize does not bind.  The second directive proves
             * the scan kept going -- rejection, not an aborted prologue. */
            var esc = comcon.pom(function () {
                "use comcon: check\u006fut";
                "use comcon: plain";
                return 1;
            }).cst();
            o.escAnchors = esc.anchors;
            o.escSameValue = ("use comcon: check\u006fut" ===
                              "use comcon: checkout");

            /* NOT IN THE PROLOGUE: inert, but not a directive. */
            var late = comcon.pom(function () {
                var z = 1;
                "use comcon: late";
                return z;
            }).cst();
            o.lateAnchors = late.anchors.length;

            /* INERT IN PLAIN JS -- the FOUNDATION claim, compiled by the plain
             * engine with no comcon involved, and run. */
            o.srcHasAnchor = A.src.indexOf('"use comcon: checkout"') > 0;
            o.plainRan = (new Function('return (' + A.src + ')'))()(0);
            o.directRan = fragA(0);

            /* SPAN PREDICATES, and the contrast that justifies anchors. */
            var aLine = innerA[0].line0;
            var bLine = B.query("function anchors('pay-v2')")[0].line0;
            o.aLine = aLine;
            o.bLine = bLine;
            o.anchorFindsBoth = (A.query("anchors('pay-v2')").length > 0 &&
                                 B.query("anchors('pay-v2')").length > 0);
            o.lineFindsA = A.query('line(' + aLine + ') function').length;
            o.lineFindsB = B.query('line(' + aLine + ') function').length;
            o.emptyLine  = A.query('line(999)').length;
            o.rangeAll   = (A.query('line(1-999)').length ===
                            A.query('*').length);
            o.rangeCut   = A.query('line(1-2)').length;

            /* every anchored site in one query -- the enumeration a policy
             * author wants, and the spelling that proves anchors(glob) is a
             * real glob rather than a name comparison */
            o.anyAnchor = A.query('anchors(*)').length;

            o.badLine = 'not-tested';
            try { A.query('line(zz)'); o.badLine = 'ACCEPTED'; }
            catch (e) { o.badLine = 'refused'; }

            /* a typo must not be READ as a line number (parseInt('5x')===5),
             * and an inverted range must not quietly match nothing */
            o.sloppyLine = 'not-tested';
            try { A.query('line(5zz)'); o.sloppyLine = 'ACCEPTED'; }
            catch (e) { o.sloppyLine = 'refused'; }
            o.invertedLine = 'not-tested';
            try { A.query('line(9-2)'); o.invertedLine = 'ACCEPTED'; }
            catch (e) { o.invertedLine = 'refused'; }

            /* FAIL LOUD, not empty: the bytecode tier does not parse, so it
             * cannot answer an anchors() query.  Returning [] there would be a
             * "no sites" answer for a question that was never asked. */
            o.bcAnchors = 'not-tested';
            try {
                comcon.pom(fragA).query("anchors('checkout')");
                o.bcAnchors = 'ACCEPTED';
            } catch (e) { o.bcAnchors = 'refused'; }

            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(23);

###############################################################################

my $r = http_get('/c');

like($r, qr/"aAnchors":\["checkout"\]/,
     'a directive-prologue anchor is exposed as a node attribute');
like($r, qr/"frozen":true/, 'the anchor list is frozen with the view');
like($r, qr/"innerHits":1/, "anchors(name) selects the anchored node");
like($r, qr/"innerName":"inner"/, '...the right one');
like($r, qr/"innerType":"FunctionDeclaration"/,
     'a FunctionDeclaration answers to `function` -- same kind as the '
     . 'bytecode tier, so a selector cannot match at one tier only');
like($r, qr/"blockHits":1/,
     'the block owning the prologue reports the anchor too');
like($r, qr/"globPre":true/, 'anchors() takes a glob (prefix)');
like($r, qr/"globSuf":2/,    'anchors() takes a glob (suffix): function+block');

# Each rejection is paired with a control, so a broken scan cannot pass as a
# correct refusal.
like($r, qr/"parenAnchors":0/,
     'a PARENTHESIZED string is not a directive, so not an anchor');
like($r, qr/"plainAnchors":\["sneaky"\]/,
     '...and the control without parens registers -- the scan works');
like($r, qr/"escAnchors":\["plain"\]/,
     'an ESCAPED spelling does not bind; the next directive still does');
like($r, qr/"escSameValue":true/,
     '...and it was rejected on RAW spelling alone: the decoded string is '
     . 'exactly the accepted one');
like($r, qr/"lateAnchors":0/,
     'a string statement outside the prologue is not an anchor');

like($r, qr/"srcHasAnchor":true/, 'the anchor is present in the source');
like($r, qr/"plainRan":1/,
     'INERT: the anchored source compiles and runs in plain JS via '
     . 'new Function -- the marker is a no-op statement');
like($r, qr/"directRan":1/, '...with the same result as the live fragment');

# The argument for anchors, as a measurement.
like($r, qr/"anchorFindsBoth":true/,
     'the anchor selector finds the site in both fragments');
like($r, qr/"aLine":5,"bLine":7/,
     '...although the edit above moved it two lines (node-local, 1-based)');
like($r, qr/"lineFindsA":1,"lineFindsB":0/,
     'THE POINT: the line predicate that finds the site before the edit '
     . 'finds nothing after it');

like($r, qr/"emptyLine":0,"rangeAll":true,"rangeCut":\d+/,
     'line() discriminates and takes a range');
like($r, qr/"anyAnchor":4/,
     'anchors(*) enumerates every anchored site (2 anchors x function+block)');
like($r, qr/"badLine":"refused","sloppyLine":"refused","invertedLine":"refused"/,
     'line() refuses a bad bound, a typo that parseInt would read as a number, '
     . 'and an inverted range -- each would otherwise match silently');
like($r, qr/"bcAnchors":"refused"/,
     'anchors() on the bytecode tier FAILS rather than answering "no sites"');
