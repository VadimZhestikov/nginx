#!/usr/bin/perl

# COMCON D5b-3 — source-rewrite hardening.  comcon.harden(node, query, wrapper)
# replaces every matched site with the wrapper, `$$` standing for the site's own
# source, and returns a QUOTATION -- so installation stays D4's (bindAt/replace,
# rebuild-on-write), and a rewrite can be reviewed and admitted before it runs.
#
# WHAT IT IS FOR, stated against what already exists.  For a FREE name the
# capability kernel is strictly better: grant(env,"fetch",mediate(cap,guard))
# needs no parser and cannot be evaded by spelling.  So this test deliberately
# targets a **locally-bound callee** (`var g = real; g('a')`) -- a call the
# kernel cannot name (there is no grant for a local) and D5a's bytecode audit
# cannot see (OP_get_loc).  That residual is the whole reason D5b-3 exists, and
# the test asserts BOTH halves of it: the bytecode scan finds nothing, and the
# rewrite reaches the site and can stop the call.
#
# "CAN STOP THE CALL" IS THE POINT.  A rewrite that only rearranges text proves
# nothing, so the guard here REFUSES: the assertion is that the wrapped function
# never ran (its log is empty), not merely that the source changed.  The
# unhardened fragment runs the same way in the same request and logs both calls,
# so the negative control is built in and its markers are disjoint (RAN: present
# vs absent, AB vs XX).
#
# A CONSTRAINT THE BOUNDARY IMPOSES ON HARDENING WRAPPERS, worth stating because
# it is not obvious from the operator surface: a HOST FUNCTION CANNOT BE GRANTED
# into a confined compartment -- only C-wrapped COM capabilities (socket /
# server facet) cross, by design, since a host closure crossing would be a
# direct authority leak.  So a wrapper installed on the confined tier must
# either carry its own logic (the self-contained wrapper below) or call a
# GRANTED COM capability.  The test asserts this holds: installing a wrapper
# that names an ungranted host guard is REFUSED, and the live site keeps serving
# the epoch it had.

# NEGATIVE CONTROLS (run 2026-09-12; all eight reverted to failure, tree rebuilt
# and re-passed after each).  Each is a decision in ngx_js_com.c:
#
#   wrapper must be a real quote(), not a look-alike   -> test 13 fails
#   wrapper must contain $$                           -> test 14 fails
#   wrapper must be ONE ExpressionStatement            -> test 15 fails
#   trailing `;` stripped, parens kept                 -> test 22 fails
#   matched sites must not overlap                     -> test 18 fails
#   splice BACK TO FRONT                               -> 25 of 31 fail
#   re-parse the rewritten source (fail closed)        -> test 20 fails
#   replace() realizes BEFORE touching history (D4a)   -> tests 30-31 fail
#
# The `rawString` check (test 12) does NOT isolate the quotation rule -- a raw
# string is refused because "str".source is undefined, so the $$ rule catches it
# first.  Test 13 (a {source:...} look-alike) is the one that discriminates; the
# control was only credible once it existed.

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

        location /r   { }
        location /m   { }
        location /ctl { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* ------------------------------------------------------------------ */
/* The fragment under test, as SOURCE.  `g` is a local alias for a local
 * function: no grant can mediate it, and the bytecode scan cannot see it. */
var FRAG =
    "function (a) {" +
    "  var log = [];" +
    "  function real(x) { log.push('RAN:' + x); return x.toUpperCase(); }" +
    "  var g = real;" +
    "  var out = g('a') + g('b');" +
    "  return out + '|' + log.join(',');" +
    "}";

/* Two wrappers, both QUOTATIONS (inert, cap-free, reviewable).  Both defer the
 * site into a THUNK, so the guard decides whether the call happens at all --
 * wrapping the result (`guard($$)`) would be too late to stop anything.
 *
 * WRAP_FREE names a host guard.  It works host-side (plain JS), and on the
 * confined tier it must FAIL: a host function cannot be granted in.
 * WRAP_SELF carries its own refusal and has no free names at all, which is what
 * a hardening pass can actually install into a compartment. */
var WRAP_FREE = comcon.quote("__guard(function () { return $$; })");
var WRAP_SELF = comcon.quote("(function (t) { return 'X'; })"
                             + "(function () { return $$; })");

/* ------------------------------------------------------------------ */
/* /m: the live D4 site.  Epoch 0 serves the fragment as-is; /ctl?op=harden
 * rewrites it and installs the result through replace(), which is the
 * "rebuild via D4" half of the deliverable. */
var target = locs.find(function (l) { return l.path === "/m"; });

function site(callable, epoch) {
    if (callable === null) {
        target.handler = function (req) {
            req.respond(410, {'content-type':'text/plain'}, 'gone');
        };
    } else {
        target.handler = function (req) {
            req.respond(200, {'content-type':'text/plain',
                              'x-epoch': String(epoch)}, String(callable(0)));
        };
    }
}

var h = comcon.bindAt(site, comcon.quote(FRAG), { imports: [] });

var ctl = locs.find(function (l) { return l.path === "/ctl"; });
ctl.handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === "harden") {
            var rep = comcon.harden(comcon.cst(FRAG), "call(g)", WRAP_SELF);
            r.count = rep.count;
            r.epoch = h.replace(rep.quotation);
        } else if (op === "hardenFree") {
            var rf = comcon.harden(comcon.cst(FRAG), "call(g)", WRAP_FREE);
            r.count = rf.count;
            r.epoch = h.replace(rf.quotation);
        } else if (op === "rollback") {
            r.epoch = h.rollback();
        } else {
            r.epoch = h.epoch();
        }
    } catch (e) { r.error = e.message; r.epoch = h.epoch(); }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};

/* ------------------------------------------------------------------ */
/* /r: the pure-rewrite half, over a LIVE function via pom().cst(). */
var rl = locs.find(function (l) { return l.path === "/r"; });
rl.handler = function (req) {
    var o = {};

    var frag = function (a) {
        var log = [];
        function real(x) { log.push('RAN:' + x); return x.toUpperCase(); }
        var g = real;
        var out = g('a') + g('b');
        return out + '|' + log.join(',');
    };

    var node = comcon.pom(frag);
    var rep  = comcon.harden(node, "call(g)", WRAP_FREE);

    o.count = rep.count;
    o.from  = (rep.from === node.cst().hash);

    /* every recorded range must slice the ORIGINAL text back out, or a rewrite
     * would be splicing at offsets that do not mean what it thinks */
    o.slices = rep.sites.map(function (s) {
        return node.cst().src.slice(s.range[0], s.range[1]) === s.before; });
    o.befores = rep.sites.map(function (s) { return s.before; });
    o.afters  = rep.sites.map(function (s) { return s.after; });

    /* THE RESIDUAL: the bytecode audit cannot see a locally-bound callee. */
    o.bytecode = node.callsites('g').length;
    o.cstSites = node.cst().query('call(g)').length;

    /* structural review of the result, not a string compare */
    o.reviewed = comcon.cst(rep.source).query('call(__guard)').length;

    /* RUN BOTH, same request, and compare.  The guard refuses, so the hardened
     * one must not have run `real` at all. */
    function run(src, g2) {
        return (new Function('__guard', 'return (' + src + ')'))(g2)(0);
    }
    var seen = 0;
    o.before = run(node.cst().src, function (thunk) { seen++; return thunk(); });
    o.after  = run(rep.source,     function (thunk) { seen++; return 'X'; });
    o.guardSaw = seen;

    /* FAIL-CLOSED CHECKS.  Each must throw; the message is the contract. */
    function refuses(fn) {
        try { fn(); return 'ACCEPTED'; } catch (e) { return 'refused'; }
    }
    o.rawString  = refuses(function () {
        comcon.harden(node, "call(g)", "__guard($$)"); });
    /* A raw string is refused, but only a LOOK-ALIKE isolates the reason: an
     * ad-hoc {source:...} object would sail past a check that merely reads
     * .source.  The wrapper must be a real quote(), because that is what makes
     * it cap-free by construction -- an ordinary object could carry a live
     * capability in a field and call itself a wrapper. */
    o.fakeQuote  = refuses(function () {
        comcon.harden(node, "call(g)", { source: "__guard($$)" }); });
    o.noSite     = refuses(function () {
        comcon.harden(node, "call(g)", comcon.quote("__guard(0)")); });
    /* the injection case: a wrapper that closes the expression and opens a
     * statement would append arbitrary code to the fragment it "hardens" */
    o.twoExprs   = refuses(function () {
        comcon.harden(node, "call(g)", comcon.quote("__guard($$); evil()")); });
    o.statement  = refuses(function () {
        comcon.harden(node, "call(g)", comcon.quote("if (1) { $$; }")); });
    o.notANode   = refuses(function () { comcon.harden("src", "expr", WRAP_FREE); });

    /* overlapping sites cannot both be spliced -- refuse, do not silently
     * leave the inner one unhardened */
    var nested = comcon.cst("function(){ return f(f(1)); }");
    o.overlap = refuses(function () {
        comcon.harden(nested, "call(f)", WRAP_FREE); });
    o.overlapMsg = '';
    try { comcon.harden(nested, "call(f)", WRAP_FREE); }
    catch (e) { o.overlapMsg = /overlap/.test(e.message) ? 'named' : e.message; }

    /* $$ SITS AT AN EXPRESSION POSITION, so a query that matches STATEMENTS
     * produces `return var log = [];;` -- caught by re-parsing the result, which
     * is the only thing standing between a bad query and a broken fragment
     * handed out as a quotation. */
    o.stmtSite = refuses(function () {
        comcon.harden(node, "stmt", WRAP_FREE); });

    /* AND WHAT THE SINGLE-EXPRESSION CHECK IS *NOT*: a wrapper is CODE, and one
     * expression can still do anything an expression can.  A comma sequence is
     * accepted -- what bounds a wrapper is the ENV it is realized under (see
     * /ctl?op=hardenFree), never its syntax.  Asserted so the check above is not
     * read as more than it is. */
    o.sequence = refuses(function () {
        comcon.harden(node, "call(g)",
                      comcon.quote("(__guard($$), evil())")); });

    /* a trailing `;` is normalized away rather than spliced verbatim (it would
     * end the statement the site sits in), and the parentheses of a wrapper are
     * NOT -- stripping those would change how the result groups */
    o.semiSame = (comcon.harden(node, "call(g)",
                    comcon.quote("__guard(function () { return $$; });")).source
                  === rep.source);

    /* a query that matches nothing is NOT an error, but it must be visible */
    var none = comcon.harden(node, "call(nosuchthing)", WRAP_FREE);
    o.noneCount = none.count;
    o.noneSame  = (none.source === node.cst().src);

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(31);

###############################################################################

my $r = http_get('/r');

# --- the rewrite ----------------------------------------------------------
like($r, qr/"count":2/, 'both call sites of the local alias are rewritten');
like($r, qr/"from":true/, 'the report pins the hash of what was hardened');
like($r, qr/"slices":\[true,true\]/,
     'every recorded range slices the original text back out');
like($r, qr/"befores":\["g\('a'\)","g\('b'\)"\]/,
     'sites are recorded in source order with their exact text');
like($r, qr/"afters":\["__guard\(function \(\) \{ return g\('a'\); \}\)"/,
     'the site is spliced into the wrapper at $$');

# --- why this exists: the residual the kernel and D5a cannot reach ---------
like($r, qr/"bytecode":0/,
     "D5a's bytecode audit cannot see a locally-bound callee");
like($r, qr/"cstSites":2/,
     '...the CST can, which is what makes the rewrite possible');

# --- the rewrite is reviewable, and it WORKS -------------------------------
like($r, qr/"reviewed":2/,
     'the result can be reviewed structurally (cst of the rewritten source)');
like($r, qr/"before":"AB\|RAN:a,RAN:b"/,
     'CONTROL: unhardened, the wrapped function runs twice');
like($r, qr/"after":"XX\|"/,
     'hardened + guard refuses: the wrapped function NEVER RAN (empty log)');
like($r, qr/"guardSaw":2/,
     '...and the guard was handed both sites, not bypassed');

# --- fail closed ----------------------------------------------------------
like($r, qr/"rawString":"refused"/,
     'a raw string wrapper is refused');
like($r, qr/"fakeQuote":"refused"/,
     'and so is an object that merely LOOKS like one -- only a real quote() is '
     . 'cap-free by construction');
like($r, qr/"noSite":"refused"/,
     'a wrapper without $$ is refused -- it would DELETE the site');
like($r, qr/"twoExprs":"refused"/,
     'INJECTION: `__guard($$); evil()` would splice to THREE valid statements '
     . '(re-parsing cannot catch it) -- the one-expression rule is what does');
like($r, qr/"statement":"refused"/,
     'a statement wrapper is refused: $$ is spliced at an expression position');
like($r, qr/"notANode":"refused"/, 'arg0 must be a node with a cst() view');
like($r, qr/"overlap":"refused"/,
     'overlapping sites are refused rather than half-rewritten');
like($r, qr/"overlapMsg":"named"/, '...and the error says so');
like($r, qr/"stmtSite":"refused"/,
     'a query matching STATEMENTS is refused: the result would not parse, and '
     . 'a rewrite that does not parse must never leave harden() as a quotation');
like($r, qr/"sequence":"ACCEPTED"/,
     'a comma-sequence wrapper IS accepted -- one expression can still do '
     . 'anything, so what bounds a wrapper is its env, not its syntax');
like($r, qr/"semiSame":true/,
     'a trailing semicolon on the wrapper is normalized, not spliced');
like($r, qr/"noneCount":0,"noneSame":true/,
     'a query matching nothing yields count 0 and unchanged source');

# --- rebuild via D4: install the rewrite at a live site --------------------
like(http_get('/m'), qr/x-epoch: 0.*AB\|RAN:a,RAN:b/s,
     'epoch 0 serves the unhardened fragment');
like(http_get('/ctl?op=harden'), qr/"count":2,"epoch":1/,
     'the hardened quotation installs through replace() -- rebuild-on-write');
like(http_get('/m'), qr/x-epoch: 1.*XX\|/s,
     'epoch 1 serves the hardened fragment, and the guard stops both calls');

# A wrapper naming a host function cannot cross into the compartment. The
# rewrite itself is fine (count 2) -- it is ADMISSION that refuses, and the
# live site must not be disturbed by the attempt.
my $f = http_get('/ctl?op=hardenFree');
like($f, qr/"error"/,   'installing a wrapper that names an ungranted host '
                      . 'guard is REFUSED at admission');
like($f, qr/"epoch":1/, '...and the live site is still on the epoch it had');
like(http_get('/m'), qr/x-epoch: 1.*XX\|/s,
     '...still serving, unchanged');

# The failed install must not have cost a rollback slot: one rollback goes
# back to the real previous epoch, not to the one that is already live.
like(http_get('/ctl?op=rollback'), qr/"epoch":0/,
     'rollback returns to epoch 0 -- a failed replace consumed no history');
like(http_get('/m'), qr/x-epoch: 0.*AB\|RAN:a,RAN:b/s,
     'the unhardened fragment is serving again, byte-identical behaviour');
