#!/usr/bin/perl

# V9 — describe ⊇ mutable, for the PROGRAM instance.
#
# The js_com discipline says every member that can be mutated appears in
# `describe()` with a safety class, and `t/js_com_describe.t` holds the COM tree
# to that.  The program instance has the same shape and never had the check:
# a POM NodeView and an epoch handle each carry a `describe()` that lists their
# ops with a rights class, and **those lists are hand-written inches from the
# members they describe.**  Add `h.freeze = ...` without a row and nothing fails.
#
# It is worse than one list: there are FOUR describe()-bearing surfaces, and two
# of them -- the bytecode-backed NodeView and the CST-backed one -- carry
# separate copies of the same eight-op list.  Two lists of one thing is how the
# mediation-flavor translation nearly shipped a fall-through to FULL AUTHORITY,
# and it is why the enumeration checker exists at all.
#
# WHY THIS IS A RUNTIME TEST AND NOT A SCRAPER.  These surfaces are built in the
# JS bootstrap and frozen; their members exist only once a fragment has been
# compiled and a site bound.  A static reader would be guessing at what
# `Object.keys` returns.  So the test asks the objects themselves, in the
# compartment, which is the only place the answer is a fact.
#
# THE ASSERTIONS, in both directions:
#
#   ⊇  every callable member of the surface appears in its describe().ops
#      -- the drift that matters: a new op that nobody classified.
#   ⊆  every row in describe().ops names a member that exists
#      -- the other drift: a classified op that was renamed or removed, which
#         leaves the list describing something imaginary.
#   CLASS  every row's `cls` is from the closed rights vocabulary, and every
#      row's `op` from the closed op vocabulary.  A typo'd class is worse than a
#      missing row: it reads as a real classification.
#   AGREE  the two NodeView variants describe the SAME op set, because they are
#      two implementations of one surface and a reader cannot be expected to know
#      which one they hold.
#
# `describe` itself is a member AND a row (it is a read op), so it must appear on
# both sides -- which is the smallest case of the ⊇/⊆ pair and worth having.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

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
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /v9   { }
        location /site { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* The closed vocabularies.  Written here, in the TEST, on purpose: if the test
 * imported them from the implementation it would agree with the implementation
 * by construction and check nothing.  A typo'd class is worse than a missing
 * row, because it reads as a real classification. */
var OPS = { read:1, invoke:1, rewrite:1, remove:1, revive:1 };
var CLS = { R:1, F:1, X:1, L:1 };

/* Which members of an object are ops a caller can invoke?  Functions only --
 * data properties like `kind`, `type` and `shared` are facts about the node, not
 * operations on it, and describe() reports them outside `ops`. */
function callables(o) {
    var out = [], k;
    for (k in o) {
        if (!Object.prototype.hasOwnProperty.call(o, k)) { continue; }
        var v;
        try { v = o[k]; } catch (e) { continue; }
        if (typeof v === 'function') { out.push(k); }
    }
    out.sort();
    return out;
}

function audit(label, o) {
    var r = { label: label, members: [], rows: [], undescribed: [],
              imaginary: [], badOp: [], badCls: [] };
    r.members = callables(o);

    var d;
    try { d = o.describe(); }
    catch (e) { r.error = 'describe() threw: ' + e.message; return r; }
    if (!d || !d.ops || typeof d.ops.length !== 'number') {
        r.error = 'describe() returned no ops[]';
        return r;
    }

    var named = {}, i;
    for (i = 0; i < d.ops.length; i++) {
        var row = d.ops[i];
        r.rows.push(row.name);
        named[row.name] = 1;
        if (!OPS[row.op])  { r.badOp.push(row.name + ':' + row.op); }
        if (!CLS[row.cls]) { r.badCls.push(row.name + ':' + row.cls); }
        /* A row naming a member that is not there describes something
         * imaginary -- the drift left behind by a rename.
         *
         * EXISTENCE, not callability.  The first version required a function
         * and reported `children` and `parent` on both NodeViews, which are
         * real ops that materialize LAZILY as data on access (that is the whole
         * point of a lazy view).  A rule that reports the correct
         * implementation of the thing it is checking gets switched off. */
        if (o[row.name] === undefined) { r.imaginary.push(row.name); }
    }
    r.rows.sort();

    for (i = 0; i < r.members.length; i++) {
        if (!named[r.members[i]]) { r.undescribed.push(r.members[i]); }
    }
    return r;
}

locs.forEach(function (l) {
    if (l.path !== '/v9') { return; }
    l.handler = function (req) {
        var out = { audits: [] };
        try {
            /* A fragment to reflect over.  A plain host function is what pom()
             * takes; include() returns a bound wrapper, not the fragment. */
            var frag = function (r2) {
                var n = 0, i;
                for (i = 0; i < 3; i++) { n = n + i; }
                return n;
            };

            var node = comcon.pom(frag);
            out.audits.push(audit('pom-node (bytecode-backed)', node));

            var cst = node.cst();
            out.audits.push(audit('pom-node (CST-backed)', cst));

            /* An epoch handle over a live site: the MUTABLE half of the program
             * instance, and the reason this check exists. */
            var installed = null;
            var site = function (callable, epoch) { installed = callable; };
            var h = comcon.bindAt(site,
                                  comcon.quote("function(){ return 'v1'; }"),
                                  { imports: [] });
            out.audits.push(audit('bindAt handle', h));

            /* The fourth surface: the class-F, multi-worker sibling of bindAt.
             * Audited here because a sibling with its own copy of a
             * hand-written op list is exactly where the same drift lands, and
             * asking three of four surfaces would have been a coverage claim
             * that reads as complete. */
            var sh = comcon.bindShared('v9probe',
                         comcon.quote("function(){ return 'v1'; }"),
                         { imports: [] },
                         function (rq, callable, ep) { return callable; });
            out.audits.push(audit('bindShared handle', sh));

            /* The two NodeView variants are NOT the same surface -- the
             * bytecode-backed one offers `cst()` to cross over, the CST-backed
             * one `origin()` to cross back -- so requiring identical op sets
             * would be requiring them to be the same object.  What must agree
             * is the CLASSIFICATION of every op they BOTH have: two lists of one
             * thing disagreeing is the drift that matters. */
            var da = node.describe(), db = cst.describe(), rowsA = {}, rowsB = {};
            var ai;
            for (ai = 0; ai < da.ops.length; ai++) { rowsA[da.ops[ai].name] = da.ops[ai]; }
            for (ai = 0; ai < db.ops.length; ai++) { rowsB[db.ops[ai].name] = db.ops[ai]; }
            var disagree = [], nm;
            for (nm in rowsA) {
                if (!Object.prototype.hasOwnProperty.call(rowsA, nm)) { continue; }
                if (!rowsB[nm]) { continue; }
                if (rowsA[nm].op !== rowsB[nm].op || rowsA[nm].cls !== rowsB[nm].cls) {
                    disagree.push(nm + ': ' + rowsA[nm].op + '/' + rowsA[nm].cls
                                  + ' vs ' + rowsB[nm].op + '/' + rowsB[nm].cls);
                }
            }
            out.variantDisagree = disagree;
            out.variantOnlyA = [];
            out.variantOnlyB = [];
            for (nm in rowsA) { if (Object.prototype.hasOwnProperty.call(rowsA, nm)
                && !rowsB[nm]) { out.variantOnlyA.push(nm); } }
            for (nm in rowsB) { if (Object.prototype.hasOwnProperty.call(rowsB, nm)
                && !rowsA[nm]) { out.variantOnlyB.push(nm); } }

            /* The self-test: the same auditor over a PLANTED object whose
             * describe() is wrong in each of the three ways, so a clean result
             * above is a measurement rather than an auditor that reports
             * nothing. */
            var planted = {
                good:   function () { return 1; },
                sneaky: function () { return 2; },   /* undescribed */
                describe: function () {
                    return { ops: [
                        { name: 'good',    op: 'read',   cls: 'R' },
                        { name: 'ghost',   op: 'read',   cls: 'R' },
                        { name: 'describe',op: 'read',   cls: 'R' },
                        { name: 'good',    op: 'reeead', cls: 'Q' } ] };
                }
            };
            var p = audit('planted', planted);
            out.control = {
                caughtUndescribed: p.undescribed.join(',') === 'sneaky',
                caughtImaginary:   p.imaginary.join(',')   === 'ghost',
                caughtBadOp:       p.badOp.length === 1,
                caughtBadCls:      p.badCls.length === 1
            };
        } catch (e) {
            out.driverError = String(e && e.message);
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(out));
    };
});
JS

$t->try_run('no js module')->plan(11);

my $raw = http_get('/v9');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 400));
    $o = {};
};

is($o->{driverError}, undef, 'the POM surfaces were all reachable');

# ---- the auditor first, over planted drift ----
my $c = $o->{control} || {};
ok($c->{caughtUndescribed}, 'control: a member missing from describe().ops is caught');
ok($c->{caughtImaginary},   'control: a row naming a member that does not exist is caught');
ok($c->{caughtBadOp},       'control: an op outside the closed vocabulary is caught');
ok($c->{caughtBadCls},      'control: a class outside the closed vocabulary is caught');

# ---- the real surfaces ----
for my $a (@{ $o->{audits} || [] }) {
    next if $a->{label} eq 'planted';
}
my @real = grep { $_->{label} ne 'planted' } @{ $o->{audits} || [] };
cmp_ok(scalar(@real), '>=', 4, 'all four program-instance surfaces were audited');

for my $a (@real) {
    diag(sprintf("%-28s %d member(s), %d row(s)", $a->{label},
                 scalar(@{ $a->{members} || [] }), scalar(@{ $a->{rows} || [] })));
}

my @undesc = map { $_->{label} . ': ' . join(' ', @{ $_->{undescribed} }) }
             grep { @{ $_->{undescribed} || [] } } @real;
is_deeply(\@undesc, [],
   'describe() ⊇ mutable: every callable member of every program-instance '
   . 'surface is named in its own describe().ops')
    or diag("UNDESCRIBED: " . join(' | ', @undesc));

my @imag = map { $_->{label} . ': ' . join(' ', @{ $_->{imaginary} }) }
           grep { @{ $_->{imaginary} || [] } } @real;
is_deeply(\@imag, [],
   'describe() ⊆ mutable: no row describes a member that does not exist')
    or diag("IMAGINARY: " . join(' | ', @imag));

my @bops = map { $_->{label} . ': ' . join(' ', @{ $_->{badOp} }) }
           grep { @{ $_->{badOp} || [] } } @real;
is_deeply(\@bops, [], 'every op is from the closed op vocabulary')
    or diag("BAD OPS: " . join(' | ', @bops));

my @bcls = map { $_->{label} . ': ' . join(' ', @{ $_->{badCls} }) }
           grep { @{ $_->{badCls} || [] } } @real;
is_deeply(\@bcls, [], 'every rights class is from the closed class vocabulary')
    or diag("BAD CLASSES: " . join(' | ', @bcls));

# The two NodeView variants are two implementations of one surface with two
# different crossings between them.  What must not drift is how they classify
# the ops they SHARE.
is_deeply($o->{variantDisagree}, [],
   'the two NodeView variants classify every op they share identically')
    or diag("DIVERGENT: " . join(' | ', @{ $o->{variantDisagree} }));

diag("ops only on the bytecode-backed view: "
     . (join(' ', @{ $o->{variantOnlyA} || [] }) || '(none)'));
diag("ops only on the CST-backed view:      "
     . (join(' ', @{ $o->{variantOnlyB} || [] }) || '(none)'));
