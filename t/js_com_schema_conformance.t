#!/usr/bin/perl

# V8 -- schema conformance, generated per registry row (the READ-ONLY half).
#
# The typed tier and the config-review path both take the registry's word: a
# member's declared `type`, its `class`, and its effect claims are what they
# reason with.  `t/js_com_setter_fuzz.t` already holds the SETTABLE half of that
# surface to account -- it found four rows misdeclaring their type.  Nothing has
# ever held the READ-ONLY half to account, and the read-only half is where S4's
# reach and leak paths live: `proxy`, `ssl`, `upstream`, `location`, `sockets`
# are all read-only getters that hand back a handle reaching further into the
# tree.
#
# The registry's classification TABLES deliberately omit read-only members
# ("they carry no mutation safety class"), so a read-only row is assembled at
# runtime by a discovery pass over the prototype plus a static NAME -> TYPE map
# (`ngx_js_ro_types[]`).  A hand-kept map of 24 names against a surface of 126
# getters is exactly the kind of claim that is true when written and quietly
# false a year later.  This file is the instrument that makes it falsifiable.
#
# The corpus is DERIVED FROM THE LIVE TREE, never hand-written: the walk
# enumerates nodes and `nginx.describe(node)` names each member.  What IS
# hand-written is the expected inventory -- the set of read-only rows with no
# declared type, and the map entries this walk cannot reach.  Both are pinned,
# because a coverage number that moves silently is not coverage.
#
# Properties asserted, per row:
#
#   TYPE        if a read yields a value, it must have the declared type.
#               `handle<T>` is an object, `object[]`/`string[]` are arrays of
#               that element type.  A row declared `getter` makes NO type
#               claim -- it is UNCLASSIFIED, counted separately, and never
#               counted as a pass.  Honest, but not yet an answer.
#
#   PURE READ   `class: readonly` says reading is not a mutation.  Snapshot the
#               reachable scalars, read every read-only member, snapshot again:
#               nothing may differ.
#
#               WITH A VOLATILITY CONTROL.  Some members are live counters
#               (`conns`, `fails`, `connections`): they move on their own, and
#               without a control a test like this reports the server's own
#               traffic as a mutation caused by the read.  So it takes TWO
#               snapshots back to back with no reads in between, and every
#               member that already differs there is VOLATILE and excluded --
#               and REPORTED, so the exclusion is visible rather than silent.
#
#   INVENTORY   the set of unclassified read-only names is pinned at empty.  A
#               getter added without a type fails here, on the day it lands.
#
#   REACH       the map entries this walk never reaches are pinned by name.
#               Their declared types are NOT verified by this file and it says
#               so, rather than reporting 24-of-24 and meaning 19.
#
#   REQUEST     `nginx.describe(req)` is pinned at ZERO rows.  The request --
#               `remoteAddr`, `uri`, `method`, `headers`, `body` -- is the
#               tenant-facing surface and it is entirely outside the registry.
#               That is why the read-only descriptor's hardcoded
#               `requestScoped: false` cannot be falsified today: there are no
#               request rows to be wrong about.  Pinned so that adding them
#               becomes a deliberate act that must also fix that field.
#
# Run under the sanitizers by name -- this file is not in the S6 gate's default
# corpus (comcon_*.t), and a stale objs_asan/objs_ubsan reports the behaviour of
# whatever it was built from, which looks exactly like a build-dependent bug:
#
#     bash t/run_sanitizers.sh 'js_com_schema_conformance.t'
#
# The oracles are written against nothing but property reads and a row table, so
# the SAME code runs over planted-bug objects in the /ctl self-test: a row that
# lies about its type, a getter that mutates a sibling when read, and an extra
# unclassified row.  An oracle that only ever runs against the thing it judges
# has never been shown to work.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone limit_req limit_conn/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    limit_req_zone  $binary_remote_addr zone=rzone:1m rate=10r/s;
    limit_conn_zone $binary_remote_addr zone=czone:1m;

    upstream zoned {
        zone zoned 64k;
        server 127.0.0.1:8091;
        server 127.0.0.1:8092 weight=2 backup;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /schema { }
        location /ctl    { }

        location /p/ {
            proxy_pass http://zoned;
            proxy_set_header X-Test "v";
        }

        location /lim/ {
            limit_req  zone=rzone burst=5;
            limit_conn czone 3;
        }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var MAX_DEPTH = 5, MAX_NODES = 400, MAX_ELEMS = 6;

/* ------------------------------------------------------------ the walk ---
 * Same shape and same limits as js_com_setter_fuzz.t's walk.  Bounded by node
 * count, never by object identity: COM wrappers are rebuilt per access, so a
 * visited-set never converges.
 */
function walk() {
    var nodes = [];
    var queue = [{ obj: nginx, path: 'nginx', d: 0 }];

    while (queue.length > 0 && nodes.length < MAX_NODES) {
        var cur = queue.shift(), o = cur.obj;
        if (o === null || typeof o !== 'object') { continue; }

        if (Array.isArray(o)) {
            var lim = o.length < MAX_ELEMS ? o.length : MAX_ELEMS;
            for (var e = 0; e < lim; e++) {
                var ev;
                try { ev = o[e]; } catch (ee) { continue; }
                if (ev !== null && typeof ev === 'object' && cur.d < MAX_DEPTH) {
                    queue.push({ obj: ev, path: cur.path + '[' + e + ']',
                                 d: cur.d + 1 });
                }
            }
            continue;
        }

        var desc = [];
        try { desc = nginx.describe(o); } catch (de) { desc = []; }
        if (!Array.isArray(desc)) { desc = []; }

        nodes.push({ obj: o, path: cur.path, desc: desc });
        if (cur.d >= MAX_DEPTH) { continue; }

        var seen = {}, names = [], k, i;
        try { k = Object.keys(o); } catch (ke) { k = []; }
        for (i = 0; i < k.length; i++) {
            if (!seen[k[i]]) { seen[k[i]] = 1; names.push(k[i]); }
        }
        for (i = 0; i < desc.length; i++) {
            var dn = desc[i].name;
            if (dn && !seen[dn]) { seen[dn] = 1; names.push(dn); }
        }
        for (i = 0; i < names.length; i++) {
            var v;
            try { v = o[names[i]]; } catch (ve) { continue; }
            if (v !== null && typeof v === 'object') {
                queue.push({ obj: v, path: cur.path + '.' + names[i],
                             d: cur.d + 1 });
            }
        }
    }
    return nodes;
}

/* --------------------------------------------------------- the oracles ---
 * Each takes a plain (obj, rows) pair, so the self-test can run them over a
 * planted-bug object built out of ordinary JS.
 */

/* Does a read of declared type `decl` accept the value `v`?
 *
 * null / undefined / an empty array are NOT type evidence: an unconfigured
 * module reads back as null, and an empty list has no element type.  Those
 * abstain rather than pass, and the count of abstentions is reported.
 */
function typeVerdict(decl, v) {
    if (v === null || v === undefined) { return 'abstain'; }
    if (decl === 'getter') { return 'unclassified'; }

    if (decl.indexOf('handle<') === 0) {
        return (typeof v === 'object' && !Array.isArray(v)) ? 'ok' : 'bad';
    }
    if (decl === 'object[]' || decl === 'string[]' || decl === 'number[]'
        || decl === 'boolean[]')
    {
        if (!Array.isArray(v)) { return 'bad'; }
        if (v.length === 0) { return 'abstain'; }
        var want = decl.substr(0, decl.length - 2);
        var got = (v[0] === null) ? 'null' : typeof v[0];
        if (got === 'null') { return 'abstain'; }
        return (got === want) ? 'ok' : 'bad';
    }
    if (decl === 'function') { return (typeof v === 'function') ? 'ok' : 'bad'; }
    if (decl === 'object') {
        return (typeof v === 'object') ? 'ok' : 'bad';
    }
    if (decl === 'string' || decl === 'number' || decl === 'boolean') {
        return (typeof v === decl) ? 'ok' : 'bad';
    }
    /* An unknown vocabulary word is not a pass -- the registry and this file
     * must share one vocabulary, and a new word should be noticed here. */
    return 'unknown-decl';
}

function actualOf(v) {
    if (v === null) { return 'null'; }
    if (v === undefined) { return 'undefined'; }
    if (Array.isArray(v)) {
        if (v.length === 0) { return 'array(empty)'; }
        return ((v[0] === null) ? 'null' : typeof v[0]) + '[]';
    }
    return typeof v;
}

/* Read-only rows of one node, as {name, type} pairs. */
function roRows(rows) {
    var out = [], i;
    for (i = 0; i < rows.length; i++) {
        if (rows[i].access === 'read-only') {
            out.push(rows[i]);
        }
    }
    return out;
}

/* TYPE oracle over one (obj, rows) pair. */
function checkTypes(obj, rows, path, acc) {
    var ro = roRows(rows), i;
    for (i = 0; i < ro.length; i++) {
        var d = ro[i], v, verdict;
        try { v = obj[d.name]; }
        catch (e) {
            acc.threw.push(path + '.' + d.name + ': '
                           + String(e && e.message).substr(0, 60));
            continue;
        }
        verdict = typeVerdict(d.type, v);
        if (verdict === 'ok') { acc.ok++; }
        else if (verdict === 'abstain') { acc.abstain++; }
        else if (verdict === 'unclassified') {
            acc.unclassified[d.name] = actualOf(v);
        } else {
            acc.bad.push({ path: path, name: d.name, declared: d.type,
                           actual: actualOf(v), verdict: verdict });
        }
        acc.seen[d.name] = 1;
    }
}

/* A scalar snapshot of one node: every member that reads as a primitive.
 * Objects are followed by the walk itself, so they are not snapshotted here. */
function snapNode(obj, rows) {
    var s = {}, names = {}, i, k;
    try { k = Object.keys(obj); } catch (e) { k = []; }
    for (i = 0; i < k.length; i++) { names[k[i]] = 1; }
    for (i = 0; i < rows.length; i++) { if (rows[i].name) { names[rows[i].name] = 1; } }
    for (var n in names) {
        if (!Object.prototype.hasOwnProperty.call(names, n)) { continue; }
        var v;
        try { v = obj[n]; } catch (e) { s[n] = 'THREW'; continue; }
        if (v === null || typeof v !== 'object') {
            if (typeof v === 'function') { continue; }
            s[n] = String(v);
        }
    }
    return s;
}

function snapshot(nodes) {
    var out = {}, i;
    for (i = 0; i < nodes.length; i++) {
        out[i + '|' + nodes[i].path] = snapNode(nodes[i].obj, nodes[i].desc);
    }
    return out;
}

/* Which keys differ between two snapshots? */
function snapDiff(a, b) {
    var d = [], nk;
    for (nk in a) {
        if (!Object.prototype.hasOwnProperty.call(a, nk)) { continue; }
        var x = a[nk], y = b[nk];
        if (!y) { d.push(nk + '|<node gone>'); continue; }
        for (var m in x) {
            if (!Object.prototype.hasOwnProperty.call(x, m)) { continue; }
            if (x[m] !== y[m]) { d.push(nk + '.' + m); }
        }
    }
    return d;
}

/* ------------------------------------------------------------ the run ---- */
function run() {
    var nodes = walk();

    var acc = { ok: 0, abstain: 0, bad: [], threw: [], unclassified: {},
                seen: {} };
    /* PURE READ, with its volatility control FIRST:
     *   A -> B   no reads in between  => whatever moved here is volatile
     *   B -> C   the full read pass   => whatever moved here, minus volatile,
     *                                    was moved BY the reads
     */
    var A = snapshot(nodes);
    var B = snapshot(nodes);
    var volatileKeys = {}, vd = snapDiff(A, B), i;
    for (i = 0; i < vd.length; i++) { volatileKeys[vd[i]] = 1; }

    var n;
    for (n = 0; n < nodes.length; n++) {
        checkTypes(nodes[n].obj, nodes[n].desc, nodes[n].path, acc);
    }

    var C = snapshot(nodes);
    var moved = [], cd = snapDiff(B, C);
    for (i = 0; i < cd.length; i++) {
        if (!volatileKeys[cd[i]]) { moved.push(cd[i]); }
    }

    var un = [], name;
    for (name in acc.unclassified) {
        if (Object.prototype.hasOwnProperty.call(acc.unclassified, name)) {
            un.push(name + ':' + acc.unclassified[name]);
        }
    }
    un.sort();

    /* Which names did the walk actually reach?  Reported so the coverage claim
     * can be checked against the C map rather than asserted. */
    var reached = [];
    for (name in acc.seen) {
        if (Object.prototype.hasOwnProperty.call(acc.seen, name)) {
            reached.push(name);
        }
    }
    reached.sort();

    return {
        nodes: nodes.length,
        ok: acc.ok, abstain: acc.abstain,
        bad: acc.bad, threw: acc.threw,
        unclassified: un,
        reached: reached,
        volatile: vd.sort().slice(0, 40),
        volatileN: vd.length,
        movedByRead: moved
    };
}

/* --------------------------------------------------- the instrument test ---
 * Three planted bugs, judged by the same oracles.
 */
function selfTest() {
    var out = {};

    /* C1 -- a row that lies about its type. */
    var liar = { n: 7 };
    var liarRows = [{ name: 'n', type: 'string', access: 'read-only' }];
    var a1 = { ok: 0, abstain: 0, bad: [], threw: [], unclassified: {}, seen: {} };
    checkTypes(liar, liarRows, 'planted', a1);
    out.c1_typeLieCaught = (a1.bad.length === 1
                            && a1.bad[0].declared === 'string'
                            && a1.bad[0].actual === 'number');

    /* C1b -- the same oracle must PASS the honest version of that row. */
    var a1b = { ok: 0, abstain: 0, bad: [], threw: [], unclassified: {}, seen: {} };
    checkTypes(liar, [{ name: 'n', type: 'number', access: 'read-only' }],
               'planted', a1b);
    out.c1b_honestRowPasses = (a1b.bad.length === 0 && a1b.ok === 1);

    /* C2 -- a getter that mutates a sibling when read.  The snapshot oracle
     * must see it, and the volatility control must NOT mask it: the mutation
     * happens on read, so A->B (no reads) leaves it alone. */
    var mut = { sibling: 'before' };
    Object.defineProperty(mut, 'trap', {
        get: function () { mut.sibling = 'after'; return 1; },
        enumerable: true
    });
    var mutRows = [{ name: 'trap', type: 'number', access: 'read-only' },
                   { name: 'sibling', type: 'string', access: 'read-only' }];
    var mn = [{ obj: mut, path: 'planted', desc: mutRows }];
    /* snapNode itself reads every member, so the A/B pair would already trip
     * the trap.  Read `sibling` only for the snapshots, which is what the real
     * run does to nodes whose members are inert. */
    var sib = function () { return { 'planted': { sibling: mut.sibling } }; };
    var mA = sib(), mB = sib();
    var mVol = snapDiff(mA, mB);
    var ma = { ok: 0, abstain: 0, bad: [], threw: [], unclassified: {}, seen: {} };
    checkTypes(mut, mutRows, 'planted', ma);
    var mC = sib();
    var mMoved = snapDiff(mB, mC);
    out.c2_mutatingReadCaught = (mVol.length === 0 && mMoved.length === 1
                                 && mMoved[0] === 'planted.sibling');

    /* C3 -- an undeclared row must land in the unclassified inventory, not in
     * the pass count. */
    var a3 = { ok: 0, abstain: 0, bad: [], threw: [], unclassified: {}, seen: {} };
    checkTypes({ q: 'x' }, [{ name: 'q', type: 'getter', access: 'read-only' }],
               'planted', a3);
    out.c3_unclassifiedNotAPass = (a3.ok === 0 && a3.bad.length === 0
                                   && a3.unclassified.q === 'string');

    /* C4 -- the volatility control must actually fire on a genuinely moving
     * value, or its exclusions are meaningless. */
    var tick = 0;
    var ctr = {};
    Object.defineProperty(ctr, 'c', {
        get: function () { return tick++; }, enumerable: true
    });
    var cRows = [{ name: 'c', type: 'number', access: 'read-only' }];
    var cn = [{ obj: ctr, path: 'planted', desc: cRows }];
    out.c4_volatilityControlFires = (snapDiff(snapshot(cn), snapshot(cn)).length === 1);

    return out;
}

nginx.http.servers[0].locations.forEach(function (l) {
    if (l.path === '/schema') {
        l.handler = function (req) {
            var o;
            try { o = run(); }
            catch (e) { o = { driverError: String(e && e.message) }; }
            /* The request object's own registry rows -- pinned at zero. */
            try { o.reqRows = nginx.describe(req).length; }
            catch (e) { o.reqRows = 'THREW: ' + String(e && e.message); }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(o));
        };
    }
    if (l.path === '/ctl') {
        l.handler = function (req) {
            var o;
            try { o = selfTest(); }
            catch (e) { o = { selfTestError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(o));
        };
    }
});
JS

$t->try_run('no js module')->plan(14);

sub get_json {
    my ($path) = @_;
    my $r = http_get($path);
    $r =~ s/^.*?\r\n\r\n//s;
    my $j;
    eval { $j = decode_json($r); 1 } or do {
        diag("non-JSON from $path: " . substr($r, 0, 300));
        return {};
    };
    return $j;
}

# ---- the instrument, before anything it says is believed ----
my $ctl = get_json('/ctl');
ok($ctl->{c1_typeLieCaught},        'control: a row that lies about its type is caught');
ok($ctl->{c1b_honestRowPasses},     'control: the honest version of that row passes');
ok($ctl->{c2_mutatingReadCaught},   'control: a getter that mutates a sibling on read is caught');
ok($ctl->{c3_unclassifiedNotAPass}, 'control: an undeclared row is inventoried, not passed');
ok($ctl->{c4_volatilityControlFires},
                                    'control: the volatility control fires on a moving value');

# ---- the run ----
my $s = get_json('/schema');
is($s->{driverError}, undef, 'the walk completed');
cmp_ok($s->{nodes}, '>=', 100, "corpus is the live tree ($s->{nodes} nodes)");
cmp_ok($s->{ok},    '>=', 100, "read-only rows type-verified ($s->{ok} ok, $s->{abstain} abstained)");

is(scalar(@{ $s->{threw} || [] }), 0,
   'no read-only member threw when read')
    or diag("threw: " . join(', ', @{ $s->{threw} }[0 .. 4]));

is_deeply($s->{bad}, [],
   'every read-only row reads back as the type the registry declares')
    or diag("MISDECLARED: " . encode_json($s->{bad}));

# THE INVENTORY.  Pinned at empty: a getter added without a type fails here.
is_deeply($s->{unclassified}, [],
   'no read-only row the walk reaches is left without a declared type')
    or diag("UNCLASSIFIED (name:actual-type): " . join(' ', @{ $s->{unclassified} }));

# PURE READ.  What the volatility control excluded must be fully enumerated,
# because an exclusion nobody can see is indistinguishable from a pass.
#
# On this fixture it excludes NOTHING -- an idle tree has no moving counters --
# which makes the pure-read result below UNQUALIFIED, the strongest form it can
# take.  That the mechanism does fire on a value that really moves is proved by
# control C4, not by hoping the live tree provides one; asserting volatileN > 0
# here would be asserting that the instrument must find noise.
is(scalar(@{ $s->{volatile} || [] }), $s->{volatileN},
   "the volatility exclusions are fully enumerated ($s->{volatileN} excluded)");

is_deeply($s->{movedByRead}, [],
   'reading every read-only member mutated nothing')
    or diag("MOVED BY READ: " . join(' ', @{ $s->{movedByRead} }));

# THE REQUEST SURFACE.  Pinned at zero rows, which is what makes the read-only
# descriptor's hardcoded `requestScoped: false` unfalsifiable today.  When this
# assertion fails, request members have entered the registry and that field has
# to become per-row in the same change.
is($s->{reqRows}, 0,
   'the request object still has NO registry rows (so requestScoped:false is vacuous, not wrong)');

$t->stop();
