#!/usr/bin/perl

# M5.0 -- THE GO/NO-GO BENCHMARK FOR TYPED LOWERING.
#
# M5 (typed policy -> maxim C) was unparked 2026-09-15 on an evidence-first
# order, and this is the measurement that decides whether M5.1 (the codegen)
# starts.  The record going in (PERFORMANCE §2b, the two "M5 EVIDENCE" notes):
# compiling a host-call-dominated policy buys ~1.0x; untyped lowering is 8.3x
# off C on arithmetic and 17x on a byte scan; a hand-written TYPED-SHAPE arm
# reaches parity on arithmetic.  So the prize exists only where a fragment
# class is compute- or data-bound AND a typed shape is reachable for it.
#
# TWO CANDIDATE CLASSES, the ones t/comcon_include_faithfulness.t already holds
# under the SR-2 differential:
#   A  byte-scan validation over request bytes (the "hot path in JS" shape)
#   B  a string-heavy token check (split, charCodeAt FNV, toString)
#
# FOUR ARMS PER CLASS, and the pair that decides is the middle two:
#   interp    the JS, interpreted
#   lowered   the JS, lowered by maxim today (untyped)
#   typed     the ACHIEVABLE typed-shape bound -- for A, a C pointer walk with
#             the gas check kept (nginx.bench.scanTyped, the stand-in
#             ngx_js_bench_typed is for arithmetic); for B, the per-char
#             engine-access arm (one host call per char, lowered), because
#             string code that is typed still reads its characters through the
#             engine -- that is where a typed lowering of class B would land
#   floor     C with nothing kept (nginx.bench.scan / nginx.bench.fnv): what no
#             lowering can beat; reported for scale, never the decision
#
# THE DECISION RULE, stated before the numbers: M5.1 starts for a class iff
# lowered / typed >= 3 -- a typed lowering would have to buy at least 3x over
# what maxim already does, or the codegen is not worth its risk on the tier
# tenants run on.  A class that fails the rule is re-parked WITH ITS NUMBER.
#
# THE CONTROLS, as in lowering-ceiling.t: every arm of a class returns the SAME
# HASH; the lowered arm reads back compiled and the interpreted arm does not
# (nginx.jitStatus, which never compiles); the two JS arms of a class must not
# share a bc_hash cache key; MIN of several reps.  Against objs_jit; in-process
# (a throughput number on this box goes through WSL2's mirrored firewall and
# compresses every ratio toward 1.0).  Decision evidence, not a gate: it lives
# in t/tools/ and is re-run when the question comes up.
#
# AFTER M5.1a (v5.117) the same rule reads the REMAINING gap: class A's lowered
# arm went 11.72 -> 1.26 ns/byte and its ratio 19.2 -> 2.1, so a further typed
# cut is NOT worth it by the rule.  And a correction the re-run forced: class
# B's "typed" arm (K) is lowered JS -- `h ^ byteAt(i)` -- so M5.1a moved it
# too (20.16 -> 13.75) while the class B fragment itself did not (50.0); the
# class B ratio is therefore sensitive to how its denominator is built, and
# PERFORMANCE §2f says so.  A future re-measurement should give class B a C
# per-char kernel for that arm, as class A has.
#
#     TEST_NGINX_BINARY=$(pwd)/objs_jit/nginx prove -v t/tools/m5-go-nogo.t

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

        location /a { }
        location /b { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
nginx.workerRequestTimeout = 0;

/* ---- class A: the byte scan (lowering-ceiling.t's SRC_SCAN, unchanged) ---- */
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

/* ---- class B: the token check, as the faithfulness case has it, looped ---- */
var SRC_TOK =
  "function (tok, reps) {" +
  "  var h = 0, r, p, i, parts;" +
  "  for (r = 0; r < reps; r++) {" +
  "    parts = tok.split('.');" +
  "    h = 2166136261;" +
  "    for (p = 0; p < parts.length; p++) {" +
  "      var part = parts[p];" +
  "      for (i = 0; i < part.length; i++) {" +
  "        h = Math.imul(h ^ part.charCodeAt(i), 16777619) >>> 0;" +
  "      }" +
  "    }" +
  "    var s = (parts.length === 3 ? 'accept ' : 'reject ') + h.toString(16);" +
  "  }" +
  "  return h >>> 0;" +
  "}";

/* class B's typed bound: one engine read per character, lowered (the K arm) */
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

function mk(src, tag) {
    return (new Function("return (" + src.replace("{", "{ /* arm:" + tag + " */ ")
                         + ");"))();
}

var S = { i: mk(SRC_SCAN, 'Si'), c: mk(SRC_SCAN, 'Sc') };
var T = { i: mk(SRC_TOK,  'Ti'), c: mk(SRC_TOK,  'Tc') };
var K = { c: mk(SRC_CALLS, 'Kc') };

var REPORT = { S: nginx.jitCompile(S.c), T: nginx.jitCompile(T.c), K: nginx.jitCompile(K.c) };

var LEN = nginx.bench.buffer(16384);
var TOKEN = nginx.bench.token();

function best(fn, arg, reps) {
    var m = Infinity, k, t0, t1;
    for (k = 0; k < reps; k++) {
        t0 = Date.now(); fn(arg); t1 = Date.now();
        if (t1 - t0 < m) { m = t1 - t0; }
    }
    return m;
}

nginx.http.servers[0].locations.forEach(function (l) {

if (l.path === '/a') {
    l.handler = function (req) {
        var o = { cls: 'A', len: LEN, report: REPORT };
        try {
            var copy = new Uint8Array(nginx.bench.copy());
            o.tier = { i: nginx.jitStatus(S.i), c: nginx.jitStatus(S.c) };
            o.hash = { floor: nginx.bench.scan(1), typed: nginx.bench.scanTyped(1),
                       lowered: S.c(copy, 1), interp: S.i(copy, 1) };
            var R = 1500, RI = 300;
            function perByte(ms, reps) { return Math.round(ms * 1e6 / (reps * LEN) * 100) / 100; }
            o.ns = {
                floor:   perByte(best(function (r) { nginx.bench.scan(r); }, R, 3), R),
                typed:   perByte(best(function (r) { nginx.bench.scanTyped(r); }, R, 3), R),
                lowered: perByte(best(function (r) { S.c(copy, r); }, R, 3), R),
                interp:  perByte(best(function (r) { S.i(copy, r); }, RI, 3), RI)
            };
        } catch (e) {
            o.driverError = String(e && e.message) + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
    };
}

if (l.path === '/b') {
    l.handler = function (req) {
        var o = { cls: 'B', tokenLen: TOKEN.length, report: REPORT };
        try {
            o.tier = { i: nginx.jitStatus(T.i), c: nginx.jitStatus(T.c), k: nginx.jitStatus(K.c) };
            o.hash = { floor: nginx.bench.fnv(1), lowered: T.c(TOKEN, 1), interp: T.i(TOKEN, 1) };
            /* the typed bound reads the same number of characters, through the engine */
            var chars = TOKEN.length - 2;              /* two '.' separators skipped */
            var R = 200000, RI = 40000, RK = 200000;
            function perChar(ms, reps) { return Math.round(ms * 1e6 / (reps * chars) * 100) / 100; }
            o.ns = {
                floor:   perChar(best(function (r) { nginx.bench.fnv(r); }, R, 3), R),
                typed:   perChar(best(function (r) { K.c(chars, r); }, RK, 3), RK),
                lowered: perChar(best(function (r) { T.c(TOKEN, r); }, R, 3), R),
                interp:  perChar(best(function (r) { T.i(TOKEN, r); }, RI, 3), RI)
            };
        } catch (e) {
            o.driverError = String(e && e.message) + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(11);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do { diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {}; };
    return $o;
}

sub ratio { my ($a, $b) = @_; return ($b && $b > 0) ? sprintf('%.1f', $a / $b) : 'n/a'; }

my $a = get_json('/a');
is($a->{len}, 16384, 'class A ran and reported') or diag("driverError: " . ($a->{driverError} // 'none'));
my $ha = $a->{hash} || {};
is_deeply([$ha->{typed}, $ha->{lowered}, $ha->{interp}], [$ha->{floor}, $ha->{floor}, $ha->{floor}],
   'class A: floor, typed, lowered and interpreted arms compute the SAME HASH') or diag(encode_json($ha));
cmp_ok(($a->{tier}{c}{compiled} || 0), '>', 0, 'class A: the lowered arm is compiled');
is(($a->{tier}{i}{compiled} || 0), 0, 'class A: the interpreted arm is not (no shared cache key)');

my $b = get_json('/b');
cmp_ok(($b->{tokenLen} || 0), '>', 0, 'class B ran and reported') or diag("driverError: " . ($b->{driverError} // 'none'));
my $hb = $b->{hash} || {};
is_deeply([$hb->{lowered}, $hb->{interp}], [$hb->{floor}, $hb->{floor}],
   'class B: floor, lowered and interpreted arms compute the SAME HASH') or diag(encode_json($hb));
cmp_ok(($b->{tier}{c}{compiled} || 0), '>', 0, 'class B: the lowered arm is compiled');
is(($b->{tier}{i}{compiled} || 0), 0, 'class B: the interpreted arm is not');
cmp_ok(($b->{tier}{k}{compiled} || 0), '>', 0, 'class B: the typed-bound arm (per-char engine access) is compiled');

my ($na, $nb) = ($a->{ns} || {}, $b->{ns} || {});
diag('');
diag(sprintf('CLASS A  byte scan, ns/byte:   floor %.2f   typed %.2f   lowered %.2f   interp %.2f',
             $na->{floor} // -1, $na->{typed} // -1, $na->{lowered} // -1, $na->{interp} // -1));
diag(sprintf('         lowered/typed = %s   (rule: >= 3 -> GO)', ratio($na->{lowered}, $na->{typed})));
diag(sprintf('CLASS B  token check, ns/char: floor %.2f   typed %.2f   lowered %.2f   interp %.2f',
             $nb->{floor} // -1, $nb->{typed} // -1, $nb->{lowered} // -1, $nb->{interp} // -1));
diag(sprintf('         lowered/typed = %s   (rule: >= 3 -> GO)', ratio($nb->{lowered}, $nb->{typed})));

my $goA = ($na->{typed} && $na->{lowered} / $na->{typed} >= 3) ? 1 : 0;
my $goB = ($nb->{typed} && $nb->{lowered} / $nb->{typed} >= 3) ? 1 : 0;
diag(sprintf('DECISION: class A %s, class B %s', $goA ? 'GO' : 'NO-GO', $goB ? 'GO' : 'NO-GO'));

ok(defined $na->{lowered} && defined $nb->{lowered}, 'both classes produced the four numbers the decision needs');
ok(1, 'the decision is printed above, and recorded in PERFORMANCE §2e -- this file does not judge it');

undef $t;
