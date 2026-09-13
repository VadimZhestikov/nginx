#!/usr/bin/perl

# V8, effect-class half -- the `propagation` column, held to account.
#
# `nginx.describe()` gives every settable member a propagation class:
#
#     worker-local   a COW write; the other workers do NOT see it
#     zoned-shared   cross-worker IFF the upstream is zone-backed
#     auto-shared    shm-backed; every worker sees it
#
# This is the COW-trap axis, and it is the one registry field that nothing has
# ever checked. `t/js_com_setter_fuzz.t` checks types, reversibility and
# crosstalk WITHIN one worker; `t/js_com_schema_conformance.t` checks the
# read-only rows' types and purity. Neither can see propagation at all, because
# a single-process observation cannot distinguish "my worker" from "the fleet" --
# and 306 of the registry's rows make this claim.
#
# THE CLAIM IS CONDITIONAL, which is what makes it worth testing. The 16
# `zoned-shared` rows are all on upstream peers, and `describe()` does not report
# the table's value directly: a REFINE HOOK resolves it per object, to
# `zoned-shared` when `peers->shpool != NULL` and `worker-local` when it is NULL.
# So the same member on two upstreams in one config must be classified two
# different ways, and must then BEHAVE two different ways. That is a falsifiable
# pair, not a single claim.
#
# So the two arms are each other's control, in one fixture, one request pattern,
# one run:
#
#   upstream zoned { zone ...; }   weight is zoned-shared -> writing it in one
#                                  worker MUST be visible in every other
#   upstream plain { }             weight refines to worker-local -> writing it
#                                  in one worker MUST NOT be visible in any other
#
# and the run asserts that they DISAGREE. Without that, "the other workers still
# see the old value" would also pass if the write silently did nothing, if the
# fan-out only ever reached one worker, or if propagation never worked at all.
# The M-SES gate learned this the same way: a probe that cannot fail in the
# negative arm is not evidence about the positive one.
#
# THREE INSTRUMENT REQUIREMENTS, each of which has already broken a test in this
# tree (see t/comcon_mode_fanout.t, which is where this harness comes from):
#
#   worker_processes 4     a single-worker fixture cannot see this bug AT ALL,
#                          and would pass every assertion below vacuously.
#   concurrent connections sequential requests do not spread -- under light load
#                          one worker wins nearly every accept, so 24 sequential
#                          GETs measure ONE process and report it as four. Open
#                          every connection first, then write the requests.
#   nginx.shared.incr()    per-worker identity. Math.random() is NOT per-process:
#                          the JS runtime is built in the master pre-fork, so
#                          every worker inherits one RNG state and returns the
#                          same "identity".
#
# The precondition -- that the fan-out really reached two or more workers -- is
# asserted as a TEST, never as a skip. A run that reached one worker has measured
# nothing, and must say so by failing.
#
# This file writes into live config structures from several processes, so it is
# worth running under the sanitizers.  It is not in the S6 gate's default corpus
# (that glob is comcon_*.t), so name it:
#
#     bash t/run_sanitizers.sh 'js_com_propagation.t'
#
# Verified clean under both, 0 findings in src/js -- but rebuild objs_asan and
# objs_ubsan first.  A stale sanitizer tree reports the behaviour of the code it
# was built from, which looks exactly like a build-dependent bug.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use IO::Socket::INET;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 4;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    # THE PAIR.  Same member, same type, one config: the only difference is the
    # shm zone, which is exactly what the refine hook keys off.
    upstream zoned {
        zone zoned 64k;
        server 127.0.0.1:8091;
        server 127.0.0.1:8092;
    }

    upstream plain {
        server 127.0.0.1:8093;
        server 127.0.0.1:8094;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /probe { }

        location /z/ { proxy_pass http://zoned; }
        location /q/ { proxy_pass http://plain; }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var wid = null;
var n = 0;

/* Per-worker identity.  A shared counter is genuinely per-process: each worker
 * takes the next number the first time it serves a request. */
function workerId() {
    if (wid === null) { wid = nginx.shared.incr('__v8prop_wid__', 1); }
    return wid;
}

/* A busy worker is not accepting, so holding the handler is what actually makes
 * the accept queue spread across the pool. */
function hold(ms) {
    var t = Date.now();
    while (Date.now() - t < ms) { /* deliberate */ }
}

function upstreamByName(name) {
    var us = nginx.http.upstreams, i;
    for (i = 0; i < us.length; i++) {
        if (us[i].name === name) { return us[i]; }
    }
    return null;
}

/* The first peer of an upstream, as the LIVE runtime RR peer.  At request time
 * upstream.peers[] returns rr peers (uscf->peer.data is set once
 * postconfiguration has run); at config phase it falls back to config-phase
 * peers, which have no shpool and no refine hook -- so this test only ever runs
 * inside a request. */
function peer0(name) {
    var u = upstreamByName(name);
    if (!u) { return null; }
    var p = u.peers;
    return (p && p.length) ? p[0] : null;
}

/* What does the REGISTRY say about this peer's `weight`? */
function declaredProp(name) {
    var p = peer0(name);
    if (!p) { return 'no-peer'; }
    var rows;
    try { rows = nginx.describe(p); } catch (e) { return 'THREW: ' + e.message; }
    for (var i = 0; i < rows.length; i++) {
        if (rows[i].name === 'weight') { return rows[i].propagation; }
    }
    return 'no-row';
}

function readWeight(name) {
    var p = peer0(name);
    if (!p) { return null; }
    try { return p.weight; } catch (e) { return 'THREW: ' + e.message; }
}

function writeWeight(name, v) {
    var p = peer0(name);
    if (!p) { return 'no-peer'; }
    try { p.weight = v; return p.weight; }
    catch (e) { return 'THREW: ' + e.message; }
}

/* ------------------------------------------------------------------------- *
 * PHASE 2 -- the same claim, GENERATED PER REGISTRY ROW.
 *
 * Phase 1 proves the conditional claim on the one class where it is conditional.
 * 306 rows say `worker-local` though, and an argument that "they are plain config
 * memory so of course they are worker-local" is an argument, not a measurement.
 *
 * NO RECORD CROSSES PROCESSES.  The obvious design -- the writer stashes which
 * members it wrote in `nginx.shared` for the readers -- cannot work: a shared
 * value is capped at 512 bytes (NGX_JS_SHARED_VAL_LEN), so the record would be
 * silently truncated and the sweep would check a fraction of what it reported.
 * Instead the writer stamps a SENTINEL value, and every worker independently
 * reports any eligible member currently holding one. Nothing has to be
 * communicated, so nothing can be truncated.
 *
 * SCOPE, and why it is this narrow:
 *   - `safe` + `reversible` only, as in the setter fuzz: `guarded` rewires live
 *     dispatch and `irreversible` cannot be undone for the process's lifetime.
 *   - NUMBER-typed only. A boolean cannot carry a distinguishable sentinel --
 *     `true` is also its natural value -- so a boolean leak is INVISIBLE to this
 *     method. Strings could carry one, but writing a sentinel into every safe
 *     string setter can change routing in the very worker under test (a
 *     serverName or a root), and a worker that stops matching the request cannot
 *     report. Both exclusions are counted and reported rather than left implicit.
 *   - `worker-local` only, which excludes the zoned rows by construction, since
 *     the refine hook reports them as `zoned-shared` on a zoned upstream.
 * ------------------------------------------------------------------------- */
var SENT = 424242;
var SWEPT = null;          /* [{i, name, old}] -- this worker's own writes */

var MAX_DEPTH = 5, MAX_NODES = 400, MAX_ELEMS = 6;

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
                    queue.push({ obj: ev, path: cur.path+'['+e+']', d: cur.d+1 });
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
                queue.push({ obj: v, path: cur.path+'.'+names[i], d: cur.d+1 });
            }
        }
    }
    return nodes;
}

/* Is this row in scope for the sweep?  Returns a reason string when not, so the
 * exclusions can be counted instead of vanishing. */
function why(d) {
    if (d.access !== 'read-write') { return 'read-only'; }
    if (d.callable)                { return 'method'; }
    if (d['class'] !== 'safe')     { return 'not-safe'; }
    if (!d.reversible)             { return 'not-reversible'; }
    if (d.propagation !== 'worker-local') { return 'not-worker-local'; }
    if (d.type === 'boolean')      { return 'boolean-no-sentinel'; }
    if (d.type !== 'number')       { return 'not-number'; }
    return null;
}

/* How many eligible members in THIS worker currently hold the sentinel?
 * Run before the write it is the false-positive control; run after, it is the
 * observation. Same code both times, deliberately. */
function sentinelCount() {
    var nodes = walk(), hits = [], eligible = 0, excl = {};
    for (var n = 0; n < nodes.length; n++) {
        var nd = nodes[n];
        for (var i = 0; i < nd.desc.length; i++) {
            var d = nd.desc[i], r = why(d);
            if (r) { excl[r] = (excl[r] || 0) + 1; continue; }
            eligible++;
            var v;
            try { v = nd.obj[d.name]; } catch (e) { continue; }
            if (v === SENT) { hits.push(nd.path + '.' + d.name); }
        }
    }
    return { eligible: eligible, hits: hits.length,
             sample: hits.slice(0, 6), excluded: excl };
}

function sweepWrite() {
    var nodes = walk(), wrote = [], refused = 0;
    for (var n = 0; n < nodes.length; n++) {
        var nd = nodes[n];
        for (var i = 0; i < nd.desc.length; i++) {
            var d = nd.desc[i];
            if (why(d)) { continue; }
            var old;
            try { old = nd.obj[d.name]; } catch (e) { continue; }
            if (old === SENT) { continue; }
            try { nd.obj[d.name] = SENT; } catch (e) { refused++; continue; }
            var back;
            try { back = nd.obj[d.name]; } catch (e) { refused++; continue; }
            /* A setter that accepts the value and reads back as something else
             * has not stored our sentinel, so it cannot be observed and must not
             * be counted as swept. */
            if (back !== SENT) { refused++; continue; }
            wrote.push({ i: n, name: d.name, old: old });
        }
    }
    SWEPT = wrote;
    return { wrote: wrote.length, refused: refused };
}

function sweepRestore() {
    if (!SWEPT) { return { restored: 0, mine: false, bad: [] }; }
    var nodes = walk(), ok = 0, bad = [];
    for (var k = 0; k < SWEPT.length; k++) {
        var rec = SWEPT[k], nd = nodes[rec.i];
        if (!nd) { bad.push({ n: rec.name, why: 'node-gone' }); continue; }
        try { nd.obj[rec.name] = rec.old; }
        catch (e) { bad.push({ n: rec.name, why: 'threw:' + e.message }); continue; }
        var back;
        try { back = nd.obj[rec.name]; }
        catch (e) { bad.push({ n: rec.name, why: 'read-threw' }); continue; }
        if (back !== rec.old) {
            bad.push({ n: rec.name, old: rec.old, back: back, p: nd.path });
        } else { ok++; }
    }
    SWEPT = null;
    return { restored: ok, mine: true, bad: bad };
}

nginx.http.servers[0].locations.forEach(function (l) {
    if (l.path !== '/probe') { return; }

    l.handler = function (req) {
        var op = req.queryParams.op || 'read';
        var r = { w: workerId(), n: ++n, op: op };
        try {
            if (op === 'read') {
                hold(4);
                r.zoned = readWeight('zoned');
                r.plain = readWeight('plain');

            } else if (op === 'declare') {
                r.zonedProp = declaredProp('zoned');
                r.plainProp = declaredProp('plain');

            } else if (op === 'write') {
                var v = parseInt(req.queryParams.v, 10);
                r.zonedAfter = writeWeight('zoned', v);
                r.plainAfter = writeWeight('plain', v);

            } else if (op === 'count') {
                hold(4);
                var c = sentinelCount();
                r.eligible = c.eligible; r.hits = c.hits;
                r.sample = c.sample; r.excluded = c.excluded;

            } else if (op === 'sweep') {
                var sw = sweepWrite();
                r.wrote = sw.wrote; r.refused = sw.refused;

            } else if (op === 'restore') {
                hold(4);
                var rs = sweepRestore();
                r.restored = rs.restored; r.mine = rs.mine;
                r.bad = rs.bad;
            }
        } catch (e) {
            r.error = String(e && e.message);
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(r));
    };
});
JS

$t->try_run('no js module')->plan(17);

###############################################################################

sub concurrent_get {
    my ($path, $count) = @_;
    my (@socks, @out);
    for (1 .. $count) {
        my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1:8080',
                                      Proto => 'tcp', Timeout => 5);
        push @socks, $s if $s;
    }
    $_->print("GET $path HTTP/1.0\r\nHost: localhost\r\n\r\n") for @socks;
    for my $s (@socks) {
        local $/;
        push @out, (<$s> // '');
        $s->close;
    }
    return @out;
}

sub rows {
    my ($path, $count) = @_;
    my @r;
    for my $raw (concurrent_get($path, $count)) {
        $raw =~ s/^.*?\r\n\r\n//s;
        my $j;
        eval { $j = decode_json($raw); 1 } or next;
        push @r, $j;
    }
    return @r;
}

sub one {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $j;
    eval { $j = decode_json($raw); 1 } or return {};
    return $j;
}

# ---- warm every worker first, so each has taken its id ----
my @warm = rows('/probe?op=read', 24);
my %seen = map { $_->{w} => 1 } grep { defined $_->{w} } @warm;
my $workers = scalar keys %seen;

cmp_ok($workers, '>=', 2,
   "the fan-out reached $workers distinct worker(s) -- a one-worker run measures nothing");

my ($before_z) = map { $_->{zoned} } grep { defined $_->{zoned} } @warm;
my ($before_p) = map { $_->{plain} } grep { defined $_->{plain} } @warm;
diag("before: zoned weight=" . (defined $before_z ? $before_z : 'undef')
     . " plain weight=" . (defined $before_p ? $before_p : 'undef'));

# ---- what the registry CLAIMS, per object, via the refine hook ----
my $d = one('/probe?op=declare');
is($d->{zonedProp}, 'zoned-shared',
   'describe(): weight on a ZONE-BACKED upstream peer is zoned-shared');
is($d->{plainProp}, 'worker-local',
   'describe(): the SAME member on a non-zoned peer refines to worker-local');

# ---- one write, in one worker ----
my $NEW = 77;
my $wr = one("/probe?op=write&v=$NEW");
my $writer = $wr->{w};
diag("writer worker=" . (defined $writer ? $writer : 'undef')
     . " zonedAfter=" . (defined $wr->{zonedAfter} ? $wr->{zonedAfter} : 'undef')
     . " plainAfter=" . (defined $wr->{plainAfter} ? $wr->{plainAfter} : 'undef'));

is($wr->{zonedAfter}, $NEW, 'the write landed on the zoned peer in the writing worker');
is($wr->{plainAfter}, $NEW, 'the write landed on the plain peer in the writing worker');

# ---- observe from the whole pool ----
my @after = rows('/probe?op=read', 24);
my (%z, %p);
for my $r (@after) {
    next unless defined $r->{w};
    $z{$r->{w}} = $r->{zoned};
    $p{$r->{w}} = $r->{plain};
}
my @others = grep { $_ ne ($writer // '') } sort keys %z;
diag("observed by worker: "
     . join('  ', map { "w$_ zoned=" . (defined $z{$_} ? $z{$_} : 'undef')
                        . " plain=" . (defined $p{$_} ? $p{$_} : 'undef') }
                  sort keys %z));

cmp_ok(scalar(@others), '>=', 1,
   'at least one worker OTHER than the writer reported, so the cross-worker question is asked');

# ZONED: shared memory under the rr_peers lock -- everyone must see it.
my @z_stale = grep { ($z{$_} // -1) != $NEW } @others;
is(scalar(@z_stale), 0,
   'zoned-shared: every other worker sees the new weight')
    or diag("workers still reading the old zoned weight: " . join(',', @z_stale));

# PLAIN: a COW write -- nobody else may see it.
my @p_leaked = grep { ($p{$_} // -1) == $NEW } @others;
is(scalar(@p_leaked), 0,
   'worker-local: no other worker sees the new weight')
    or diag("workers that DID see the worker-local write: " . join(',', @p_leaked));

# THE BUILT-IN CONTROL.  If both arms behaved alike, this harness is not
# measuring propagation -- it is measuring nothing, twice.
my $z_shared = (scalar(@others) > 0 && scalar(@z_stale) == 0);
my $p_local  = (scalar(@others) > 0 && scalar(@p_leaked) == 0
                && grep { ($p{$_} // -1) != $NEW } @others);
ok($z_shared && $p_local,
   'the two arms DISAGREE: the same member propagates when zoned and not when it is not');

if (defined $before_z) { one("/probe?op=write&v=$before_z"); }

###############################################################################
# PHASE 2 -- the same claim, generated over every worker-local row the walk
# reaches.
###############################################################################

# The FALSE-POSITIVE CONTROL, run before anything is written: if some member
# naturally holds the sentinel, a later "hit" would not mean a leak.
my @pre = rows('/probe?op=count', 24);
my $pre_hits = 0;
$pre_hits += ($_->{hits} // 0) for @pre;
my ($elig) = map { $_->{eligible} } grep { defined $_->{eligible} } @pre;
my ($excl) = map { $_->{excluded} } grep { defined $_->{excluded} } @pre;
diag("sweep scope: $elig eligible row(s) per worker; excluded: "
     . join(' ', map { "$_=$excl->{$_}" } sort keys %{ $excl || {} }));

is($pre_hits, 0,
   "false-positive control: no eligible member holds the sentinel before the sweep")
    or diag("pre-existing sentinels: " . encode_json([map { $_->{sample} } @pre]));

# ---- one worker sweeps ----
my $sw = one('/probe?op=sweep');
my $swriter = $sw->{w};
diag("sweep: worker=" . ($swriter // 'undef') . " wrote=" . ($sw->{wrote} // 'undef')
     . " refused=" . ($sw->{refused} // 'undef'));

cmp_ok($sw->{wrote} // 0, '>=', 10,
   "the sweep wrote " . ($sw->{wrote} // 0) . " worker-local member(s) -- fewer would measure little");

# ---- observe from the whole pool, with the same code as the control ----
my @post = rows('/probe?op=count', 24);
my %hits;
for my $r (@post) {
    next unless defined $r->{w};
    $hits{$r->{w}} = $r->{hits};
}
diag("sentinels seen per worker: "
     . join('  ', map { "w$_=" . ($hits{$_} // 'undef') } sort keys %hits));

cmp_ok(scalar(keys %hits), '>=', 2,
   "the post-sweep fan-out reached " . scalar(keys %hits)
   . " worker(s) -- the leak question needs someone other than the writer");

cmp_ok($hits{$swriter} // 0, '>=', 10,
   'the sweeping worker sees its own writes (so the observation works at all)');

my @leaked = grep { $_ ne ($swriter // '') && ($hits{$_} // 0) > 0 } sort keys %hits;
is(scalar(@leaked), 0,
   'GENERATED: not one worker-local member leaked to another worker')
    or diag("workers seeing another worker's writes: " . join(',', @leaked)
            . "  sample: " . encode_json([map { $_->{sample} } @post]));

# ---- put it back, and prove it went back ----
# Only the worker that swept holds the old values, and a request cannot be
# addressed to a worker -- so fan out until that worker is hit. The first attempt
# at this fired 24 requests once and never reached it, and the failure then read
# as "restore ran and left 67 members changed" when nothing had run at all.
# Separating "it ran" from "it worked" is the whole point of the two assertions.
my $ran;
for my $round (1 .. 12) {
    for my $r (rows('/probe?op=restore', 24)) {
        next unless $r->{mine};
        $ran = $r;
        last;
    }
    last if $ran;
}

ok(defined $ran,
   'the restore reached the sweeping worker (the only one holding the old values)')
    or diag("12 rounds of 24 requests never landed on w" . ($swriter // '?'));

if ($ran) {
    diag("restore in w$ran->{w}: restored=$ran->{restored} failed="
         . scalar(@{ $ran->{bad} || [] })
         . (@{ $ran->{bad} || [] } ? "  " . encode_json($ran->{bad}) : ''));
}

# Reversibility, cross-checked in a live worker: the registry marks these rows
# reversible and the setter fuzz checks that within one process. This checks it
# after a full sweep, reading back through the getter.
is_deeply($ran ? $ran->{bad} : ['restore never ran'], [],
   'every swept member read back as its original value');

my @fin = rows('/probe?op=count', 24);
my $fin_hits = 0;
$fin_hits += ($_->{hits} // 0) for @fin;
# Report the COUNT, not the sample: `sample` is capped at six, and reading a
# capped sample as the total is how "67 unrestored" first looked like "6".
is($fin_hits, 0,
   'the sweep is reversible: no sentinel survives the restore')
    or diag("sentinels left, per response: "
            . join(' ', map { $_->{hits} // '?' } @fin));

$t->stop();
