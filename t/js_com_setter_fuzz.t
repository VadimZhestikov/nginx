#!/usr/bin/perl

# COM setters — property fuzz of the mutation surface.
#
# AUDIT_M-SES.md §3 lists the COM setters as an input surface with no fuzz
# corpus.  They are C functions that take an arbitrary JS value from a tenant or
# operator script and write it into live nginx configuration structures, which
# makes them the widest untrusted-value entry point in src/js.  Every existing
# t/ test sets the handful of properties it asserts on, with a value of the
# right type; nothing has ever assigned a NaN, a 16 KB string, a NUL byte, or an
# object whose toString() throws to *every* setter and looked at what happened.
#
# The corpus is DERIVED FROM THE LIVE TREE, never hand-written: the walk
# enumerates nodes, `nginx.describe(node)` names each member with its declared
# type and safety class, and only members the registry itself classifies are
# fuzzed.  A hand-kept inventory goes stale the moment someone adds a member;
# this one cannot (cf. t/js_com_describe.t's drift guard).
#
# SCOPE.  Only members classified `safe` AND `reversible` are fuzzed.  `guarded`
# rewires live dispatch and `irreversible` cannot be undone for the lifetime of
# the process, so fuzzing either would degrade the server under test rather than
# measure it.  Both are counted and reported, so the coverage claim stays honest
# about what it does not cover.
#
# Properties asserted:
#
#   LIVENESS     no assignment crashes or hangs the worker.  The batches are
#                addressed by node index so that a crash -- which produces no
#                report at all -- is still attributed to a known node window.
#   TYPE         if an assignment is ACCEPTED (does not throw), the getter must
#                then return the type the registry declares for that member, or
#                null.  A setter that accepts a value and then reads back as
#                some other type has silently reinterpreted it.
#   REVERSIBLE   the registry marks these members reversible.  After the whole
#                hostile battery, assigning the ORIGINAL value back must restore
#                the original reading.  Nothing has ever tested that claim.
#   NO CROSSTALK fuzzing member P must leave every sibling member of the same
#                object unchanged once P is restored.  This is the JS-visible
#                shadow of a setter writing outside its own field.
#
# A clean run of this file on a NON-sanitizer binary is weak evidence: the C
# bugs it is built to surface are memory errors.  Run it under the sanitizers:
#
#     bash t/run_sanitizers.sh 'js_com_setter_fuzz.t'
#
# The instrument is validated first (/ctl), including false-positive controls
# and three planted bugs -- a type-lying setter, a cross-talking setter and a
# setter that will not restore -- which the same oracle code must catch.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $BATCH = 12;

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
        server 127.0.0.1:8092 weight=2;
    }

    upstream plain {
        server 127.0.0.1:8093;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /fuzz { }
        location /ctl  { }

        location /p/ {
            proxy_pass http://zoned;
            proxy_set_header X-Test "v";
        }

        location /lim/ {
            limit_req  zone=rzone burst=5;
            limit_conn czone 3;
        }

        location /st/ {
            root /tmp;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  second;

        location / { }
        location /q/ { proxy_pass http://plain; }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var MAX_DEPTH = 5;
var MAX_NODES = 400;
var MAX_ELEMS = 6;      /* array elements visited per array */

/* ------------------------------------------------------- the battery ---
 * Rebuilt per use: one entry is an object whose conversion throws, and it must
 * be a fresh one each time so a previous throw cannot have consumed it.
 */
function battery() {
    var big = new Array(16385).join('A');
    return [
        ['undefined', undefined],
        ['null', null],
        ['true', true],
        ['false', false],
        ['zero', 0],
        ['neg1', -1],
        ['one', 1],
        ['nan', NaN],
        ['inf', Infinity],
        ['neginf', -Infinity],
        ['huge', 9007199254740993],
        ['int32min', -2147483648],
        ['frac', 0.5],
        ['emptystr', ''],
        ['shortstr', 'x'],
        ['bigstr', big],
        ['nulbyte', 'a' + String.fromCharCode(0) + 'b'],
        ['unicode', String.fromCharCode(0xe9) +
                    String.fromCharCode(0x2028) + '\ud83d\ude00'],
        ['numstr', '-1'],
        ['hugenumstr', '999999999999999999999'],
        ['emptyobj', {}],
        ['emptyarr', []],
        ['arr2', [1, 2]],
        ['throwing', { toString: function () { throw new Error('boom-ts'); },
                       valueOf:  function () { throw new Error('boom-vo'); } }],
        ['fn', function () { return 1; }]
    ];
}
var BATTERY_N = battery().length;

/* ---------------------------------------------------------- the walk ---
 * Bounded by DEPTH and COUNT, never by a visited-set: COM wrappers are rebuilt
 * on every access (proto-per-call), so object identity never repeats and a
 * visited-set would not terminate (see the tree-walk probe note).  Arrays are
 * descended into by index but are not themselves fuzz targets, or
 * Array.prototype builtins swamp the result.
 */
function walk() {
    var nodes = [];
    var queue = [{ obj: nginx, path: 'nginx', d: 0 }];

    while (queue.length > 0 && nodes.length < MAX_NODES) {
        var cur = queue.shift();
        var o = cur.obj;

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

        /* children: own keys plus every member the registry names */
        var seen = {}, names = [];
        var k;
        try { k = Object.keys(o); } catch (ke) { k = []; }
        var i;
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

/* ------------------------------------------------------- the oracles ---
 * Written against nothing but property get/set, so the very same code can be
 * run over a planted-bug object in the self-test.  An oracle that only ever
 * runs against the thing it is judging has never been shown to work.
 */
function readProp(o, n) {
    try { return { ok: true, v: o[n] }; }
    catch (e) { return { ok: false, e: String(e && e.message) }; }
}

/* The registry's type vocabulary, as the tables actually spell it: scalars,
 * `X[]` arrays, and `a|b` unions (shared verbatim with mirror/lib/schema.js).
 *
 * `absent` distinguishes the two callers.  Reading a member that was never set
 * may legitimately yield null/undefined -- the tables carry no optionality
 * marker, so the survey cannot tell absence from a type error and must allow
 * it.  Straight after an assignment that was ACCEPTED there is no such excuse,
 * so the fuzz pass passes absent=false and demands the declared type. */
function typeOne(declared, v) {
    var t = typeof v;
    if (declared.slice(-2) === '[]') { return Array.isArray(v); }
    if (declared === 'string' || declared === 'str') { return t === 'string'; }
    if (declared === 'boolean')  { return t === 'boolean'; }
    if (declared === 'number')   { return t === 'number'; }
    if (declared === 'function') { return t === 'function'; }
    if (declared === 'record' || declared === 'object') {
        return v !== null && t === 'object';
    }
    return true;                            /* handle<X> and anything else */
}

function typeOk(declared, v, absent) {
    if (v === null) { return true; }
    if (v === undefined) { return absent !== false; }
    if (typeof declared !== 'string' || declared === '') { return true; }
    var alts = declared.split('|'), i;
    for (i = 0; i < alts.length; i++) {
        if (typeOne(alts[i], v)) { return true; }
    }
    return false;
}

/* Fuzz ONE member of one object.  `sibs` are the sibling member names whose
 * values must survive untouched. */
function fuzzMember(o, name, declared, sibs, res, path) {
    var probs = [];

    var before = readProp(o, name);
    if (!before.ok) { res.getterThrew++; return probs; }
    var v0 = before.v;

    /* sibling snapshot, primitives only (an object reading is rebuilt per
     * access and would never compare equal to itself) */
    var snap = {}, si;
    for (si = 0; si < sibs.length; si++) {
        if (sibs[si] === name) { continue; }
        var sv = readProp(o, sibs[si]);
        if (sv.ok && (sv.v === null || typeof sv.v !== 'object')) {
            snap[sibs[si]] = sv.v;
        }
    }

    var b = battery(), bi;
    for (bi = 0; bi < b.length; bi++) {
        var label = b[bi][0], hv = b[bi][1];
        var accepted = true;

        res.assigns++;
        try { o[name] = hv; }
        catch (e) { accepted = false; res.assignThrew++; }

        var after = readProp(o, name);
        if (!after.ok) {
            res.readThrew++;
            probs.push('read-threw-after:' + label + ':' + after.e);
            continue;
        }
        if (accepted && !typeOk(declared, after.v, false)) {
            probs.push('type:' + label + ':declared=' + declared +
                       ':got=' + (typeof after.v));
        }
    }

    /* REVERSIBLE: the registry says so; put the original back and check */
    var restored = true;
    try { o[name] = v0; } catch (e2) { restored = false; }
    var back = readProp(o, name);
    if (!restored) {
        /* CONDITIONALLY WRITABLE, not a defect: a member whose reading is
         * absent because the feature is not configured for this object may
         * legitimately refuse the write -- `alias` on a root-based location
         * throws and tells you to set `.root` instead, exactly as its registry
         * note says.  Writing back a value that was never there is not the
         * reversibility claim.  Counted and reported, never silently allowed;
         * a member that HAD a value and will not take it back still fails. */
        if (v0 === null || v0 === undefined) {
            res.condWritable++;
        } else {
            probs.push('restore-assign-threw');
        }
    } else if (!back.ok) {
        probs.push('restore-read-threw');
    } else if (v0 === null || typeof v0 !== 'object') {
        var same = (back.v === v0) ||
                   (typeof v0 === 'number' && v0 !== v0 &&
                    typeof back.v === 'number' && back.v !== back.v);
        if (!same) {
            probs.push('not-reversible:was=' + JSON.stringify(v0) +
                       ':now=' + JSON.stringify(back.v));
        }
    }
    res.restores++;

    /* NO CROSSTALK: every sibling must read as it did before */
    var kk = Object.keys(snap), ki;
    for (ki = 0; ki < kk.length; ki++) {
        var now = readProp(o, kk[ki]);
        res.sibChecks++;
        if (!now.ok) {
            probs.push('sibling-getter-threw:' + kk[ki]);
        } else if (now.v !== snap[kk[ki]]) {
            probs.push('crosstalk:' + kk[ki] +
                       ':was=' + JSON.stringify(snap[kk[ki]]) +
                       ':now=' + JSON.stringify(now.v));
        }
    }

    return probs;
}

/* --------------------------------------------------------- the driver --- */
function runRange(from, n) {
    var res = { from: from, n: n, nodesTotal: 0, nodesFuzzed: 0,
                membersFuzzed: 0, assigns: 0, assignThrew: 0, readThrew: 0,
                restores: 0, sibChecks: 0, getterThrew: 0, condWritable: 0,
                skipGuarded: 0, skipIrreversible: 0, skipReadonly: 0,
                failCount: 0, fails: [] };

    var nodes = walk();
    res.nodesTotal = nodes.length;

    var to = from + n;
    if (to > nodes.length) { to = nodes.length; }

    for (var ni = from; ni < to; ni++) {
        var node = nodes[ni];
        res.nodesFuzzed++;

        /* the fuzzable set, and an honest count of what was left out */
        var targets = [], sibs = [], di;
        for (di = 0; di < node.desc.length; di++) {
            var d = node.desc[di];
            if (!d || d.callable === true) { continue; }
            if (d.class === 'readonly')     { res.skipReadonly++; continue; }
            if (d.class === 'guarded')      { res.skipGuarded++; continue; }
            if (d.class === 'irreversible') { res.skipIrreversible++; continue; }
            if (d.class !== 'safe' || d.reversible !== true) { continue; }
            targets.push(d);
            sibs.push(d.name);
        }

        for (di = 0; di < targets.length; di++) {
            res.membersFuzzed++;
            var probs = fuzzMember(node.obj, targets[di].name,
                                   targets[di].type, sibs, res, node.path);
            if (probs.length) {
                res.failCount++;
                if (res.fails.length < 10) {
                    res.fails.push({ path: node.path, member: targets[di].name,
                                     why: probs.slice(0, 3).join(' | ') });
                }
            }
        }
    }

    return res;
}

/* --------------------------------------------- instrument self-tests ---
 * Three PLANTED bugs, judged by the same fuzzMember() that judges the real
 * COM.  If these do not fire, a clean fuzz run means nothing.
 */
function planted() {
    var o = {}, state = { p: 'orig', q: 'sibling', r: 'ok', s: 'sticky' };

    /* p: lies about its type -- declared string, reads back a number */
    Object.defineProperty(o, 'p', {
        get: function () { return state.p; },
        set: function (x) { state.p = 12345; }, enumerable: true });

    /* q: a well-behaved string member, used as the crosstalk witness */
    Object.defineProperty(o, 'q', {
        get: function () { return state.q; },
        set: function (x) { state.q = String(x); }, enumerable: true });

    /* r: writes outside its own field -- clobbers q */
    Object.defineProperty(o, 'r', {
        get: function () { return state.r; },
        set: function (x) { state.r = String(x); state.q = 'CLOBBERED'; },
        enumerable: true });

    /* s: accepts the first write only, so the original cannot be put back */
    Object.defineProperty(o, 's', {
        get: function () { return state.s; },
        set: function (x) { if (state.s === 'sticky') { state.s = String(x); } },
        enumerable: true });

    return o;
}

function selfTest() {
    var out = {}, res;
    var sibs = ['p', 'q', 'r', 's'];

    function fresh() {
        return { getterThrew: 0, assigns: 0, assignThrew: 0, readThrew: 0,
                 restores: 0, sibChecks: 0, condWritable: 0 };
    }

    res = fresh();
    out.catchesTypeLie = (fuzzMember(planted(), 'p', 'string', sibs, res, 'x')
                          .join(' ').indexOf('type:') >= 0);
    res = fresh();
    out.catchesCrosstalk = (fuzzMember(planted(), 'r', 'string', sibs, res, 'x')
                            .join(' ').indexOf('crosstalk:q') >= 0);
    res = fresh();
    out.catchesNoRestore = (fuzzMember(planted(), 's', 'string', sibs, res, 'x')
                            .join(' ').indexOf('not-reversible') >= 0);

    /* FALSE-POSITIVE CONTROL: a member that behaves must produce nothing. */
    res = fresh();
    var clean = fuzzMember(planted(), 'q', 'string', ['q'], res, 'x');
    out.quietOnGoodMember = (clean.length === 0);
    out.batteryRan = (res.assigns === BATTERY_N);
    out.batteryN = BATTERY_N;

    /* the walk reaches real COM structure, not just the root */
    var nodes = walk();
    out.nodeCount = nodes.length;
    var paths = [], i;
    for (i = 0; i < nodes.length; i++) { paths.push(nodes[i].path); }
    var joined = paths.join(' ');
    out.reachesLocation = (joined.indexOf('locations[0]') >= 0);
    out.reachesUpstream = (joined.indexOf('upstreams[0]') >= 0);

    /* the registry actually classifies something as safe+reversible here, or
     * the whole run would fuzz nothing and still report no failures */
    var safeCount = 0;
    for (i = 0; i < nodes.length; i++) {
        var dd = nodes[i].desc;
        for (var j = 0; j < dd.length; j++) {
            if (dd[j].callable !== true && dd[j].class === 'safe' &&
                dd[j].reversible === true) { safeCount++; }
        }
    }
    out.safeMembers = safeCount;

    /* TYPE-TRUTH: no mutation at all -- for every member the registry
     * describes, INCLUDING the read-only ones the fuzz pass skips, the natural
     * getter reading must match the declared type.  This is the cheapest and
     * widest oracle here: the fuzz pass only sees a wrong type when the battery
     * happens to provoke it, whereas this compares every row against reality. */
    var mism = [], counted = 0;
    for (i = 0; i < nodes.length; i++) {
        var ds = nodes[i].desc;
        for (var m = 0; m < ds.length; m++) {
            var dsc = ds[m];
            if (!dsc || dsc.callable === true) { continue; }
            var rv = readProp(nodes[i].obj, dsc.name);
            if (!rv.ok) { continue; }
            counted++;
            if (!typeOk(dsc.type, rv.v, true)) {
                var tag = dsc.name + ':declared=' + dsc.type +
                          ':actual=' + (Array.isArray(rv.v) ? 'array'
                                                            : typeof rv.v);
                if (mism.indexOf(tag) < 0) { mism.push(tag); }
            }
        }
    }
    out.typeChecked = counted;
    out.typeMismatch = mism;

    return out;
}

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path === "/fuzz") {
        locs[li].handler = function (req) {
            var from = parseInt(req.queryParams.from) || 0;
            var n = parseInt(req.queryParams.n) || 10;
            var r;
            try { r = runRange(from, n); }
            catch (e) { r = { driverError: String(e && e.message), from: from }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(r));
        };
    }
    if (locs[li].path === "/ctl") {
        locs[li].handler = function (req) {
            var o;
            try { o = selfTest(); }
            catch (e) { o = { selfTestError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(15);

sub jget {
    my ($path) = @_;
    my $r = http_get($path);
    return { httpError => 'no response' } unless defined $r;
    my ($body) = $r =~ /\r\n\r\n(.*)/s;
    return { httpError => 'no body' } unless defined $body && length $body;
    my $j = eval { decode_json($body) };
    return { httpError => 'bad json: ' . substr($body, 0, 200) } unless $j;
    return $j;
}

# ---------------------------------------------------------------------------
# 1. The instrument, before any measurement it produces is believed.
# ---------------------------------------------------------------------------
my $c = jget('/ctl');

ok(!$c->{httpError} && !$c->{selfTestError}, 'self-test ran')
    or diag explain $c;
ok($c->{catchesTypeLie},      'oracle catches a setter that lies about its type');
ok($c->{catchesCrosstalk},    'oracle catches a setter that clobbers a sibling');
ok($c->{catchesNoRestore},    'oracle catches a member that will not restore');
ok($c->{quietOnGoodMember},   'oracle stays silent on a well-behaved member');
ok($c->{batteryRan},          'the whole battery is applied to each member');
ok($c->{reachesLocation},     'the walk reaches a location node');
ok($c->{reachesUpstream},     'the walk reaches an upstream node');
cmp_ok($c->{safeMembers} || 0, '>', 100,
       'the registry classifies enough safe+reversible members to be worth fuzzing');

diag sprintf('type-truth: %d member readings checked against declared types',
             $c->{typeChecked} || 0);
diag "  mismatch: $_" for @{ $c->{typeMismatch} || [] };
cmp_ok($c->{typeChecked} || 0, '>', 200, 'type-truth check actually read members');
is(scalar @{ $c->{typeMismatch} || [] }, 0,
   'every describe() type matches what the getter really returns');

# ---------------------------------------------------------------------------
# 2. The fuzz run, in node-indexed batches so a crash is attributable.
# ---------------------------------------------------------------------------
my $first = jget("/fuzz?from=0&n=1");
my $total = $first->{nodesTotal} || 0;
diag "COM tree: $total nodes";

my %a;
my @fails;
my $batches = 0;

for (my $from = 0; $from < $total; $from += $BATCH) {
    my $r = jget("/fuzz?from=$from&n=$BATCH");
    if ($r->{httpError} || $r->{driverError}) {
        diag "batch at node $from failed: "
             . ($r->{httpError} || $r->{driverError});
        last;
    }
    $batches++;
    for my $k (qw/nodesFuzzed membersFuzzed assigns assignThrew readThrew
                  restores sibChecks getterThrew skipGuarded skipIrreversible
                  skipReadonly failCount condWritable/) {
        $a{$k} = ($a{$k} || 0) + ($r->{$k} || 0);
    }
    push @fails, @{ $r->{fails} || [] };
}

diag sprintf("setters: %d nodes in %d batches | members %d | assigns %d " .
             "(refused %d) | restores %d | sibling reads %d",
             $a{nodesFuzzed} || 0, $batches, $a{membersFuzzed} || 0,
             $a{assigns} || 0, $a{assignThrew} || 0, $a{restores} || 0,
             $a{sibChecks} || 0);
diag sprintf("conditionally writable (absent reading refused the write): %d",
             $a{condWritable} || 0);
diag sprintf("not fuzzed by design: guarded %d, irreversible %d, read-only %d" .
             " | getters that threw: %d",
             $a{skipGuarded} || 0, $a{skipIrreversible} || 0,
             $a{skipReadonly} || 0, $a{getterThrew} || 0);

for my $f (@fails) {
    diag sprintf("  %s.%s: %s", $f->{path}, $f->{member}, $f->{why});
}

is($a{nodesFuzzed} || 0, $total, "walked and fuzzed all $total nodes");
cmp_ok($a{membersFuzzed} || 0, '>', 100, 'a real number of members was fuzzed');
is($a{assigns} || 0, ($a{membersFuzzed} || 0) * ($c->{batteryN} || 0),
   'every fuzzed member received the whole battery');
is($a{failCount} || 0, 0,
   'no setter violated type, reversibility or field isolation');
