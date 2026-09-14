#!/usr/bin/perl

# CAN TYPED JS LOWERED TO C REACH PARITY WITH THE C IT WOULD REPLACE?
#
# The scenario this decides: pilgrim is a full proxy, so it is now imaginable to
# re-implement part of a hot path that nginx implements in C -- a header filter, a
# body step -- in JS, purely to get extra functionality into it.  Would that be
# affordable if M5 shipped with types?
#
# `t/tools/policy-compute-split.t` answers the OLD question (are real policies
# compute-bound -- no).  It does not answer this one, because a hot path is not
# policy code: it moves DATA.  This file answers the new one, in two experiments.
#
# RUN IT AGAINST objs_jit.  Without CONFIG_JIT there is no compiled tier, the
# arms collapse, and this reports FAILURE rather than skipping -- a measurement
# that cannot measure should say so.  It lives in t/tools/ and is not part of
# `prove t/`: decision evidence, re-run when the question comes up, not a gate.
#
# IN-PROCESS, deliberately.  A throughput benchmark on this box is measured
# through WSL2's mirrored-mode firewall, which adds a large fixed per-request cost
# OUTSIDE the thing under test and compresses every ratio toward 1.0 -- it would
# manufacture the answer being looked for.
#
# FAIRNESS.  nginx is compiled at -O (objs/Makefile CFLAGS); maxim compiles its
# generated C at -O2/-O3 (quickjs-jit.c).  A C arm inside nginx is therefore
# handicapped, which would FLATTER JS, so every C kernel is offered at both levels.
#
# THE CONTROLS, because a timing number that measures the wrong thing is worse
# than no number at all:
#   * every arm must return the SAME HASH -- otherwise they are not running the
#     same algorithm and every ratio is meaningless
#   * the compiled arm must read back compiled and the interpreted arm must not
#     (nginx.jitStatus, which never compiles -- F5's lesson: a JIT-capable BINARY
#     is not the same claim as COMPILED CODE)
#   * the two JS arms must not share a bc_hash cache key: identical sources are
#     ONE cache entry, so compiling either installs native code into both and the
#     A/B silently becomes compiled-vs-compiled
#   * MIN of several reps, not mean -- the question is how fast this can go
#   * each experiment is its own REQUEST: together they outlast Test::Nginx's 8 s
#     client timeout, and an empty response looks like a bug in the thing measured

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin . '/..'); }

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

        location /arith { }
        location /data  { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
/* These probes deliberately spend seconds inside one request.  0 is the
 * documented opt-out; every other handler keeps the default. */
nginx.workerRequestTimeout = 0;

/* ---- EXPERIMENT 1: the arithmetic ceiling -------------------------------- */

/* Kernel A: ^, +, <<, >>>.  Every intermediate stays inside int32, so this
 * computes the same BITS as the C uint32 version -- `h + (h << 5)` is a sum of
 * two int32s, exact in a double, and `| 0` takes the low 32 bits exactly as
 * uint32 wrapping does. */
var SRC_A =
  "function (n) {" +
  "  var h = 0x811c9dc5 | 0, i;" +
  "  for (i = 0; i < n; i++) {" +
  "    h = (h ^ i) | 0;" +
  "    h = (h + (h << 5)) | 0;" +
  "    h = (h ^ (h >>> 7)) | 0;" +
  "    h = (h + (h << 3)) | 0;" +
  "    h = (h ^ (h >>> 17)) | 0;" +
  "  }" +
  "  return h >>> 0;" +
  "}";

/* Kernel B: no logical right shift.  `h >>> 7` yields a value above 2^31, which
 * is not representable as int32 and so rides in a double -- the obvious
 * hypothesis for A's gap.  It was REFUTED: B is worse.  Kept runnable. */
var SRC_B =
  "function (n) {" +
  "  var h = 0x811c9dc5 | 0, i;" +
  "  for (i = 0; i < n; i++) {" +
  "    h = (h ^ i) | 0;" +
  "    h = (h + (h << 5)) | 0;" +
  "    h = (h + (h << 3)) | 0;" +
  "    h = (h ^ (h << 11)) | 0;" +
  "    h = (h + (h << 7)) | 0;" +
  "  }" +
  "  return h >>> 0;" +
  "}";

/* Kernel C: the shape policy-compute-split.t uses as its known-positive control.
 * Here to show what that control measures: a multiply-add accumulator has a
 * closed form and BOTH compilers take it, so its 13-23x is loop elimination. */
var SRC_C =
  "function (n) {" +
  "  var s = 0, i;" +
  "  for (i = 0; i < n; i++) { s = (s + i * 3) | 0; }" +
  "  return s >>> 0;" +
  "}";

/* ---- EXPERIMENT 2: the data plane --------------------------------------- */

/* One scan, two backings.  Both arms use the SAME function, so the only
 * difference between the view arm and the copy arm is where the bytes live. */
var SRC_SCAN =
  "function (u8, reps) {" +
  "  var h = 0x811c9dc5 | 0, r, i, n = u8.length;" +
  "  for (r = 0; r < reps; r++) {" +
  "    for (i = 0; i < n; i++) {" +
  "      h = (h ^ u8[i]) | 0;" +
  "      h = (h + (h << 5)) | 0;" +
  "    }" +
  "  }" +
  "  return h >>> 0;" +
  "}";

/* one host call per byte */
var SRC_CALLS =
  "function (n, reps) {" +
  "  var h = 0x811c9dc5 | 0, r, i;" +
  "  for (r = 0; r < reps; r++) {" +
  "    for (i = 0; i < n; i++) {" +
  "      h = (h ^ nginx.bench.byteAt(i)) | 0;" +
  "      h = (h + (h << 5)) | 0;" +
  "    }" +
  "  }" +
  "  return h >>> 0;" +
  "}";

/* THE TWO ARMS MUST NOT SHARE A CACHE KEY.  bc_hash folds the source text and
 * installation is keyed by it, so two functions built from IDENTICAL source are
 * one cache entry: compiling either installs native code into both and the A/B
 * silently becomes compiled-vs-compiled.  A distinct comment changes the hash and
 * nothing else. */
function mk(src, tag) {
    return (new Function("return (" + src.replace("{", "{ /* arm:" + tag + " */ ")
                         + ");"))();
}

var A = { i: mk(SRC_A, 'Ai'), c: mk(SRC_A, 'Ac') };
var B = { i: mk(SRC_B, 'Bi'), c: mk(SRC_B, 'Bc') };
var C = { i: mk(SRC_C, 'Ci'), c: mk(SRC_C, 'Cc') };
var S = { i: mk(SRC_SCAN, 'Si'), c: mk(SRC_SCAN, 'Sc') };
var K = { i: mk(SRC_CALLS, 'Ki'), c: mk(SRC_CALLS, 'Kc') };

/* Compiled at CONFIG LOAD, in the master, pre-fork: the gcc thread does not
 * survive fork, so this is the only place it works. */
var REPORT = { A: nginx.jitCompile(A.c), B: nginx.jitCompile(B.c),
               C: nginx.jitCompile(C.c), S: nginx.jitCompile(S.c),
               K: nginx.jitCompile(K.c) };

var LEN = nginx.bench.buffer(16384);

function best(fn, arg, reps) {
    var m = Infinity, k, t0, t1;
    for (k = 0; k < reps; k++) {
        t0 = Date.now(); fn(arg); t1 = Date.now();
        if (t1 - t0 < m) { m = t1 - t0; }
    }
    return m;
}

nginx.http.servers[0].locations.forEach(function (l) {

if (l.path === '/arith') {
    l.handler = function (req) {
        var o = { report: REPORT };
        try {
            var N = 5000000, REPS = 3, per = function (ms) {
                return Math.round(ms * 1e6 / N * 100) / 100;
            };

            o.tier = { Ai: nginx.jitStatus(A.i), Ac: nginx.jitStatus(A.c) };

            /* same algorithm in every arm, or the ratios mean nothing */
            o.hash = {
                cA:    nginx.bench.arith('A', 1000),
                cAO2:  nginx.bench.arith('A-O2', 1000),
                jsAi:  A.i(1000),
                jsAc:  A.c(1000),
                typed: nginx.bench.arith('typed', 1000)
            };
            o.hashB = { c: nginx.bench.arith('B', 1000), js: B.c(1000) };
            o.hashC = { c: nginx.bench.arith('C', 1000), js: C.c(1000) };

            o.ns = {
                cA:    per(best(function (n) { nginx.bench.arith('A', n); }, N, REPS)),
                cAO2:  per(best(function (n) { nginx.bench.arith('A-O2', n); }, N, REPS)),
                jsAc:  per(best(A.c, N, REPS)),
                jsAi:  per(best(A.i, N, REPS)),
                typed: per(best(function (n) { nginx.bench.arith('typed', n); }, N, REPS)),
                cB:    per(best(function (n) { nginx.bench.arith('B-O2', n); }, N, REPS)),
                jsBc:  per(best(B.c, N, REPS)),
                jsBi:  per(best(B.i, N, REPS)),
                cC:    per(best(function (n) { nginx.bench.arith('C-O2', n); }, N, REPS)),
                jsCc:  per(best(C.c, N, REPS)),
                jsCi:  per(best(C.i, N, REPS))
            };
            o.n = N;

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

if (l.path === '/data') {
    l.handler = function (req) {
        var o = { len: LEN };
        try {
            var view = new Uint8Array(nginx.bench.view());
            var copy = new Uint8Array(nginx.bench.copy());

            o.tier = { Si: nginx.jitStatus(S.i), Sc: nginx.jitStatus(S.c) };

            /* the zero-copy view must SEE nginx's bytes, not zeros */
            o.sane = { len: view.length, b0: view[0], b1: view[1],
                       same: (view[0] === copy[0] && view[7] === copy[7]) };

            o.hash = {
                c:     nginx.bench.scan(1),
                view:  S.c(view, 1),
                copy:  S.c(copy, 1),
                viewI: S.i(view, 1),
                calls: K.c(LEN, 1)
            };

            var R = 1500, RI = 300, RC = 30;
            function perByte(ms, reps) {
                return Math.round(ms * 1e6 / (reps * LEN) * 100) / 100;
            }

            o.nsPerByte = {
                c:     perByte(best(function (r) { nginx.bench.scan(r); }, R, 3), R),
                view:  perByte(best(function (r) { S.c(view, r); }, R, 3), R),
                copy:  perByte(best(function (r) { S.c(copy, r); }, R, 3), R),
                viewI: perByte(best(function (r) { S.i(view, r); }, RI, 3), RI),
                calls: perByte(best(function (r) { K.c(LEN, r); }, RC, 3), RC)
            };

            /* what ONE copy costs, since that is what pilgrim does today */
            var t0 = Date.now(), k;
            for (k = 0; k < 5000; k++) { nginx.bench.copy(); }
            o.copyUsPer16k = Math.round((Date.now() - t0) * 1000 / 5000 * 100) / 100;

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(17);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {};
    };
    return $o;
}

###############################################################################
# EXPERIMENT 1 — the arithmetic ceiling
###############################################################################

my $a = get_json('/arith');
# asserted on a field that MUST be present: "driverError is undef" also holds for
# an empty response, and an empty response is what a client timeout looks like.
is($a->{n}, 5000000, 'EXPERIMENT 1 ran and reported')
    or diag("driverError: " . ($a->{driverError} // 'none'));
diag("tier: " . encode_json($a->{tier} || {}));
diag("hash: " . encode_json($a->{hash} || {}));
diag("ns/iteration: " . encode_json($a->{ns} || {}));

my $h = $a->{hash} || {};
is_deeply([$h->{cAO2}, $h->{jsAi}, $h->{jsAc}, $h->{typed}],
          [$h->{cA}, $h->{cA}, $h->{cA}, $h->{cA}],
   'kernel A: C at both -O levels, both JS arms and the typed-shape arm all '
   . 'compute the SAME HASH -- so the ratios mean something')
    or diag("hash: " . encode_json($h));
is($a->{hashB}{js}, $a->{hashB}{c}, 'kernel B: C and JS agree');
is($a->{hashC}{js}, $a->{hashC}{c}, 'kernel C: C and JS agree');

cmp_ok(($a->{tier}{Ac}{compiled} || 0), '>', 0,
   'the compiled arm really is compiled (jitStatus, which never compiles)');
is(($a->{tier}{Ai}{compiled} || 0), 0,
   '...and the interpreted arm is NOT, so the arms did not collapse into one '
   . 'bc_hash cache entry');

my $n = $a->{ns};
cmp_ok($n->{jsAi}, '>', $n->{jsAc},
   'lowering beats interpreting on kernel A, so the harness sees the compiler');

cmp_ok($n->{jsAc} / $n->{cAO2}, '>', 3,
   sprintf('THE UNTYPED CEILING: lowered JS is %.2fx hand-written C at -O2 on '
           . 'genuine integer arithmetic (and %.2fx on kernel B). Pinned as ">3x" '
           . 'because the finding is the ORDER, not the digits',
           $n->{jsAc} / $n->{cAO2}, $n->{jsBc} / $n->{cB}));

cmp_ok($n->{typed} / $n->{cAO2}, '<', 1.5,
   sprintf('THE TYPED CEILING: a typed-SHAPE arm -- raw int32 locals, no boxing, '
           . 'but maxim\'s framing kept INCLUDING the back-edge gas check -- runs '
           . 'at %.2fx C. So the whole untyped gap is boxing and tag dispatch, '
           . 'not code generation: `--jit-dump-c` shows the accumulator living in '
           . 'a double and every op materialising two JSValues, tag-checking both '
           . 'operands, re-boxing and writing a type-feedback byte',
           $n->{typed} / $n->{cAO2}));

cmp_ok($n->{cC}, '<', 0.5,
   sprintf('AND policy-compute-split.t\'s KNOWN-POSITIVE CONTROL IS LOOP '
           . 'ELIMINATION, not code quality: its `s + i*3` shape runs at %.2f '
           . 'ns/iter in C and %.2f lowered -- neither compiler runs the loop. '
           . 'That control still proves the harness can see a win; it must not be '
           . 'read as "compute-bearing code gets 13x from lowering"',
           $n->{cC}, $n->{jsCc}));
cmp_ok($n->{jsCi} / $n->{jsCc}, '>', 5,
   '...which is exactly why it shows a big interpreted-to-compiled ratio');

###############################################################################
# EXPERIMENT 2 — the data plane
###############################################################################

my $d = get_json('/data');
is($d->{len}, 16384, 'EXPERIMENT 2 ran and reported')
    or diag("driverError: " . ($d->{driverError} // 'none'));
diag("sane: " . encode_json($d->{sane} || {}));
diag("ns/BYTE: " . encode_json($d->{nsPerByte} || {}));
diag("one 16 KB JS_NewArrayBufferCopy: " . ($d->{copyUsPer16k} // '?') . " us");

ok($d->{sane}{same} && $d->{sane}{len} == 16384,
   'THE ZERO-COPY VIEW WORKS: JS_NewArrayBuffer over nginx memory with a no-op '
   . 'free gives a Uint8Array that really sees nginx\'s bytes');

my $hd = $d->{hash} || {};
is_deeply([$hd->{view}, $hd->{copy}, $hd->{viewI}, $hd->{calls}],
          [$hd->{c}, $hd->{c}, $hd->{c}, $hd->{c}],
   'every data-plane arm computes the same hash')
    or diag("hash: " . encode_json($hd));

my $b = $d->{nsPerByte};
cmp_ok(abs($b->{view} - $b->{copy}) / $b->{view}, '<', 0.2,
   sprintf('AND IT BUYS NOTHING: view %.2f vs copy %.2f ns/byte -- the ACCESS '
           . 'PATH dominates, not the backing. One 16 KB copy is %.2f us '
           . '(0.012 ns/byte) against %.2f ns/byte to scan it, so the copy was '
           . 'never the problem',
           $b->{view}, $b->{copy}, $d->{copyUsPer16k}, $b->{view}));

cmp_ok($b->{view} / $b->{c}, '>', 8,
   sprintf('THE DATA-PLANE GAP IS WORSE THAN THE COMPUTE GAP: %.1fx C, against '
           . '~8x for arithmetic -- because the useful work per operation is '
           . 'smaller, so per-op boxing dominates more. The "add functionality to '
           . 'a hot path" scenario is the LEAST favourable shape for untyped '
           . 'lowering', $b->{view} / $b->{c}));

cmp_ok($b->{calls}, '<', $b->{view} * 2.5,
   sprintf('A HOST CALL PER BYTE COSTS ABOUT THE SAME AS A COMPILED TYPED-ARRAY '
           . 'READ (%.2f vs %.2f ns/byte). This assertion was first written the '
           . 'other way round -- "calls must be several times a read, so the '
           . 'representation is the decision" -- and the data refuted it. In this '
           . 'engine a per-element read is already priced like a crossing: the '
           . 'lever is not crossing less, it is not being boxed',
           $b->{calls}, $b->{view}));

$t->stop();
