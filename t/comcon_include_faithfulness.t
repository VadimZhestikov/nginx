#!/usr/bin/perl

# COMCON CONVERGE P5 (= SR-2 for include): compiler-faithfulness gate for the
# include primitive. For every representative confined fragment, the AOT-compiled
# tier (objs_jit/nginx) must produce the SAME response AND the SAME denials as
# the interpreted tier (objs/nginx) — confinement survives lowering, and the
# compiled code leaks no authority. This is the include analogue of
# comcon_faithfulness.t (which covers the tenant onRequest path); it is what lets
# the compiled tier stop depending on the tenant path (CONVERGE P5, gating P6).
#
# Requires both builds: objs/nginx (interpreter) + objs_jit/nginx (JIT).

use warnings;
use strict;

use Test::More;
use File::Temp qw/tempdir/;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

my $root = $ENV{TEST_NGINX_BINARY} ? do { my $b = $ENV{TEST_NGINX_BINARY}; $b =~ s{/objs/nginx$}{}; $b } : '..';
my $interp = "$root/objs/nginx";
my $jit    = "$root/objs_jit/nginx";

plan(skip_all => "no interpreter build objs/nginx") unless -x $interp;
plan(skip_all => "no JIT build objs_jit/nginx")     unless -x $jit;

my $dir = tempdir(CLEANUP => 1);
my $portbase = 8940;

# Each case: name, optional grant (socket bound to $addr, granted as `sock`),
# the fragment body (a function(req){...} returning a string or {status,body}),
# request paths, and whether the A1 gates should fire (denials > 0).
my @cases = (
  { name => 'string response',
    frag => q{function(req){ return "ok " + req.method + "\n"; }},
    paths => [qw(/t/a /t/b)], expect_denials => 0 },

  { name => 'Request fields + JSON response',
    frag => q{function(req){ return JSON.stringify({m:req.method,u:req.uri,a:req.args})+"\n"; }},
    paths => ['/t/x?y=1', '/t/z'], expect_denials => 0 },

  { name => 'Response object {status,body}',
    frag => q{function(req){ return {status:201, body:"made " + req.uri + "\n"}; }},
    paths => [qw(/t/1 /t/2)], expect_denials => 0 },

  { name => 'compute loop (typed int)',
    frag => q{function(req){ var s=0; for(var i=0;i<5000;i++){s=(s+i*3)|0;} return "sum="+s+"\n"; }},
    paths => [qw(/t/c /t/c)], expect_denials => 0 },

  { name => 'granted socket scalar reads',
    grant => 1,
    frag => q{function(req){ return "addr="+sock.address+" port="+sock.port+" fd="+(typeof sock.fd)+"\n"; }},
    paths => [qw(/t/s /t/s)], expect_denials => 0 },

  { name => 'A1 gated reach (.listener -> denial)',
    grant => 1,
    frag => q{function(req){ return (sock.listener===null?"gated":"LEAK")+"\n"; }},
    paths => [qw(/t/g /t/g /t/g)], expect_denials => 1 },

  { name => 'A1 gated mutator (close() -> denial)',
    grant => 1,
    frag => q{function(req){ try { sock.close(); return "closed\n"; } catch(e){ return "denied\n"; } }},
    paths => [qw(/t/m /t/m)], expect_denials => 1 },

  # M5.0's two candidate fragment classes (M5 unparked, v5.112): the go/no-go
  # benchmark measures these against the typed-shape arm, so they are under the
  # differential FIRST -- whatever the compiler does to them later must keep
  # producing the interpreter's bytes.  Both are deterministic in the request.
  { name => 'byte-scan validation (M5.0 class A)',
    frag => q{function(req){ var s = req.uri + "?" + req.args; var bad = 0; for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (c < 32 || c > 126) { bad++; } } var acc = 0; for (var k = 0; k < 20000; k++) { acc = ((acc * 31) + s.charCodeAt(k % s.length)) | 0; } return "bad=" + bad + " h=" + acc + "\n"; }},
    paths => ['/t/scan?x=1&y=two', '/t/scan?z=3'], expect_denials => 0,
    expect_re => qr/^bad=0 h=-?\d+$/m },

  { name => 'token check, string-heavy (M5.0 class B)',
    frag => q{function(req){ var tok = "eyJhbGciOiJIUzI1NiJ9." + (req.args || "") + ".sig"; var parts = tok.split("."); var h = 2166136261; for (var p = 0; p < parts.length; p++) { var part = parts[p]; for (var i = 0; i < part.length; i++) { h = Math.imul(h ^ part.charCodeAt(i), 16777619) >>> 0; } } var ok = parts.length === 3 && parts[0].length > 0 && (h % 7) < 7; return (ok ? "accept " : "reject ") + h.toString(16) + " " + parts[1].length + "\n"; }},
    paths => ['/t/tok?sub=alice', '/t/tok?sub=bob&exp=1'], expect_denials => 0,
    expect_re => qr/^accept [0-9a-f]+ \d+$/m },

  # M5.1a (v5.117): two codegen changes, each pinned by the value the
  # interpreter produces AND by the value the spec says, written out.
  #
  # (1) A bit op with ONE provably-numeric operand now yields a typed int32,
  #     so an int accumulator survives `h ^ arr[i]`.  The untyped operand goes
  #     through ToInt32 exactly as before -- undefined/NaN/-0/null/""/"abc" are
  #     0, "3" and [5] are 3 and 5, 1.9 is 1, 2^32+5 is 5, "0x10" is 16, true
  #     is 1, valueOf is called -- so every row below is a spec value.
  { name => 'half-typed bit ops, mixed operands (M5.1a)',
    frag => q{function(req){ var xs = [undefined, "3", 1.9, 4294967301, -0, NaN, {valueOf:function(){return 3;}}, "0x10", null, true, [], [5], "abc"]; var o = []; var i, h; for (i = 0; i < xs.length; i++) { h = 5; h = (h ^ xs[i]) | 0; o.push(h); } for (i = 0; i < xs.length; i++) { h = 5; h = h | xs[i]; o.push(h); } for (i = 0; i < xs.length; i++) { h = 5; h = h & xs[i]; o.push(h); } for (i = 0; i < xs.length; i++) { h = 1; h = h << xs[i]; o.push(h); } for (i = 0; i < xs.length; i++) { h = -8; h = h >> xs[i]; o.push(h); } return o.join(",") + "\n"; }},
    paths => [qw(/t/bits /t/bits)], expect_denials => 0,
    expect_re => qr/^5,6,4,0,5,5,6,21,5,4,5,0,5,5,7,5,5,5,5,7,21,5,5,5,5,5,0,1,1,5,0,0,1,0,0,1,0,5,0,1,8,2,32,1,1,8,65536,1,2,1,32,1,-8,-1,-4,-1,-8,-8,-1,-1,-8,-4,-8,-1,-8$/m },

  #     ...and the one non-int32 outcome, a BigInt, is an exception on both
  #     tiers with the same name and message (the runtime path is shared).
  #     F22 (v5.131): a frozen global binding is read-only on BOTH tiers.  The
  #     compiled store wrote the variable cell directly whenever it was
  #     initialised, never asking whether the reference was CONST -- which is
  #     how the compartment's global freeze (F15 phase 1) is represented -- so
  #     compiled fragment code could reassign an intrinsic the interpreter
  #     refused.  Found by the warm-cache pass G-21 made possible.
  { name => 'assigning a frozen global binding throws on both tiers (F22)',
    frag => q{function(req){ try { Promise = function(){ return 1; }; return "assigned\n"; } catch (e) { return e.name + ": " + e.message + "\n"; } }},
    paths => [qw(/t/frz /t/frz)], expect_denials => 0,
    expect_re => qr/^TypeError: /m },
  { name => 'half-typed bit op against a BigInt throws (M5.1a)',
    frag => q{function(req){ var xs = [3n]; var h = 5; try { h = (h ^ xs[0]) | 0; return "nothrow " + h + "\n"; } catch (e) { return e.name + ": " + e.message + "\n"; } }},
    paths => [qw(/t/big /t/big)], expect_denials => 0,
    expect_re => qr/^TypeError: /m },

  # (2) An in-bounds element of an INTEGER typed array is read in place as an
  #     int32; every other read -- float arrays, out of bounds, a negative or
  #     fractional index -- takes the runtime path.  Values per the spec: the
  #     clamped array clamps, Uint32 4294967295 is -1 after ToInt32, 1e10 is
  #     1410065408, NaN is 0, out of bounds is undefined and so 0.
  { name => 'integer typed-array element reads (M5.1a)',
    frag => q{function(req){ var i8 = new Int8Array([-1, 127, -128]); var u8 = new Uint8Array([0, 255, 7]); var u8c = new Uint8ClampedArray([300, -5]); var i16 = new Int16Array([-2, 32767]); var u16 = new Uint16Array([65535, 1]); var i32 = new Int32Array([-2147483648, 2147483647]); var u32 = new Uint32Array([4294967295, 7]); var f32 = new Float32Array([1.5, -2.5]); var f64 = new Float64Array([1e10, NaN]); var o = []; var k; for (k = 0; k < 3; k++) { o.push(0 ^ i8[k]); } for (k = 0; k < 3; k++) { o.push(0 | u8[k]); } for (k = 0; k < 2; k++) { o.push(0 ^ u8c[k]); } for (k = 0; k < 2; k++) { o.push(0 | i16[k]); } for (k = 0; k < 2; k++) { o.push(0 ^ u16[k]); } for (k = 0; k < 2; k++) { o.push(0 | i32[k]); } for (k = 0; k < 2; k++) { o.push(0 ^ u32[k]); } for (k = 0; k < 2; k++) { o.push(0 | f32[k]); } for (k = 0; k < 2; k++) { o.push(0 ^ f64[k]); } for (k = 3; k < 5; k++) { o.push(7 ^ i8[k]); } o.push(7 ^ i8[-1]); o.push(7 | u8[2.5]); o.push(typeof u8[5]); return o.join(",") + "\n"; }},
    paths => [qw(/t/ta /t/ta)], expect_denials => 0,
    expect_re => qr/^-1,127,-128,0,255,7,255,0,-2,32767,65535,1,-2147483648,2147483647,-1,7,1,-2,1410065408,0,7,7,7,7,undefined$/m },

  # M5.1c (v5.125): two builtins inlined by IDENTITY, and doubles in half-typed
  # bit ops.  Every value below is the spec's, written out.
  #
  # (1) String.prototype.charCodeAt on a string receiver with an int index is
  #     that code unit, or NaN out of range -- and ONLY that shape: a String
  #     object, a fractional index, an object with its own charCodeAt, and a
  #     replaced builtin all take the ordinary call (the inlining checks the C
  #     function's identity, not the name).
  { name => 'charCodeAt inlined by identity (M5.1c)',
    frag => q{function(req){ var s = "A\u00e9\ud83d\ude00z", o = [], i; for (i = 0; i < 6; i++) { o.push(s.charCodeAt(i)); } o.push(s.charCodeAt(-1)); o.push(s.charCodeAt(1.5)); o.push(s.charCodeAt(99)); var so = new String("xy"); o.push(so.charCodeAt(1)); var fake = { charCodeAt: function (k) { return "fake" + k; } }; o.push(fake.charCodeAt(0)); var t = "hello"; var h = 0; for (i = 0; i < t.length; i++) { h = (h * 31 + t.charCodeAt(i)) | 0; } o.push(h); return o.join(",") + "\n"; }},
    paths => [qw(/t/cca /t/cca)], expect_denials => 0,
    expect_re => qr/^65,233,55357,56832,122,NaN,NaN,233,NaN,121,fake0,99162322$/m },

  # (2) Math.imul on two ints is the int32 product; anything else is the call:
  #     doubles (ToInt32 first, by the builtin), strings, huge values, NaN, and
  #     a shadowing `Math` whose imul is the fragment's own function.
  { name => 'Math.imul inlined by identity (M5.1c)',
    frag => q{function(req){ var o = []; o.push(Math.imul(3, 4)); o.push(Math.imul(-5, 12)); o.push(Math.imul(0xffffffff, 5)); o.push(Math.imul(65536, 65536)); o.push(Math.imul(2147483647, 2)); o.push(Math.imul(1.9, 3.9)); o.push(Math.imul("7", "6")); o.push(Math.imul(4294967301, 3)); o.push(Math.imul(NaN, 9)); o.push(Math.imul(1e10, 1)); var Math2 = { imul: function (a, b) { return "mine" + (a + b); } }; o.push(Math2.imul(2, 3)); var h = 2166136261; var t = "abc"; for (var i = 0; i < t.length; i++) { h = Math.imul(h ^ t.charCodeAt(i), 16777619) >>> 0; } o.push(h); return o.join(",") + "\n"; }},
    paths => [qw(/t/imul /t/imul)], expect_denials => 0,
    expect_re => qr/^12,-60,-5,0,-2,3,42,15,0,1410065408,mine5,440920331$/m },

  # (3) a half-typed bit op whose untyped operand is a DOUBLE -- a uint32 from
  #     `>>> 0`, a fraction, 2^31, 2^32+5, 1e10, negatives, NaN, the infinities,
  #     2^63 and -2^70 -- is ToInt32'd inline, exactly (fmod, not a cast):
  #     ToInt32(-1.5) is -1, ToInt32(-4294967301) is -5, 1 << 1e10 shifts by
  #     1410065408 & 31 = 0.
  { name => 'half-typed bit ops with double operands (M5.1c)',
    frag => q{function(req){ var xs = [4294967295, 2147483648, 4294967301, 1e10, -1.5, -4294967301, NaN, Infinity, -Infinity, 9223372036854775808, -1180591620717411303424, 0.5, -0.5]; var o = [], i, h; for (i = 0; i < xs.length; i++) { h = 5; h = (h ^ xs[i]) | 0; o.push(h); } for (i = 0; i < xs.length; i++) { h = 1; h = h << xs[i]; o.push(h); } var u = 0; for (i = 0; i < 40; i++) { u = ((u * 3 + i) >>> 0); u = (u ^ 0x5bd1e995) | 0; } o.push(u); return o.join(",") + "\n"; }},
    paths => [qw(/t/dbl /t/dbl)], expect_denials => 0,
    expect_re => qr/^-6,-2147483643,0,1410065413,-6,-2,5,5,5,5,5,5,5,-2147483648,1,32,1,-2147483648,134217728,1,1,1,1,1,1,1,790674668$/m },

  # F20 (v5.125), found by the double-operand row above and then by the
  # differential fuzz (t/tools/jit-diff-fuzz.py): the typed lowering read a
  # value from the wrong slot at five kinds of codegen site and typed one local
  # wrongly.  (a) a NUMBER local stored from an INT slot, a NUMBER local
  # `+=` an INT, and a branch on an INT condition read _tsd where the value
  # was in _ti; (b) a branch on an untyped condition, and a fused
  # compare-and-branch on untyped operands, left the typed slots below the
  # condition unboxed, so the join label read a stale value; (c) the type
  # inference walked the bytecode linearly, so a value arriving at a join
  # over a jump edge (`b = c ? 1.5 : 0`) never reached the store's type and
  # the local stayed INT: the compiled tier stored 1.  Every value below is
  # the spec's, and every shape here was reachable before M5.1a.
  { name => 'typed local stores, branches and joins from int slots (F20)',
    frag => q{function(req){ var o = []; var u = 1.5; u = 3; o.push(u); var v = 0.5; v += 2; o.push(v); var w = 2.5; for (var i = 0; i < 3; i++) { w = (i & 1) ? 7 : w + 1; } o.push(w); var x = 6, y = 0; if (x & 1) { y = 1; } else { y = 2; } o.push(y); if (x & 2) { y = 3; } o.push(y); var z = 0.25, k; for (k = 0; k < 4; k++) { z = (k * 2) | 0; } o.push(z); var j = 0; j = (x & 2) ? 1.5 : 0; o.push(j); var j2 = 1; j2 = (j2 ? -0.5 : j2); o.push(j2); var p = 7, pb = -0.5; p <<= (((0.25 !== pb) < pb) ? pb : (+ p)); o.push(p); var q2 = 0, qa = 0, qc = 1e10; qc = (qa >>> 0); q2 |= (1 !== (qc ? 0 : qa)); o.push(q2); var q = 0; for (k = 0; k < 40; k++) { q = ((q * 3 + k) >>> 0); q = (q ^ 0x5bd1e995) | 0; } o.push(q); return o.join(",") + "\n"; }},
    paths => [qw(/t/f20 /t/f20)], expect_denials => 0,
    expect_re => qr/^3,2.5,8,2,3,6,1.5,-0.5,896,1,790674668$/m },

  # F21 (v5.125), found by the same fuzz: the compiled tier's helper for
  # bitwise NOT called the unary-ARITHMETIC slow path with OP_not, whose
  # switch has no such case and calls abort().  Reached by `~x` on any
  # operand the type stack does not prove INT: a double, a boolean, a
  # string, null -- so a fragment holding `~1.5` took the worker process
  # down.  The helper now takes the interpreter's own not-slow path
  # (ToNumeric, then ~ToInt32).  Values are ToInt32's: ~1.5 is -2, ~"3" is
  # -4, ~1e10 is -1410065409, ~2147483648 is 2147483647.
  { name => 'bitwise not on untyped operands does not abort the worker (F21)',
    frag => q{function(req){ var o = []; var d = 1.5; o.push(~d); var b = true; o.push(~b); var s = "3"; o.push(~s); o.push(~null, ~undefined, ~{}, ~[], ~"", ~NaN, ~-0.5, ~4294967296.5, ~1e10); var big = 2147483648; o.push(~big); var arr = [2.5, false, "7", 1]; var acc = 0; for (var i = 0; i < arr.length; i++) { acc += ~arr[i]; } o.push(acc); return o.join(",") + "\n"; }},
    paths => [qw(/t/f21 /t/f21)], expect_denials => 0,
    expect_re => qr/^-2,-2,-4,-1,-1,-1,-1,-1,-1,-1,-1,-1410065409,2147483647,-14$/m },

  # The class A shape itself, on a Uint8Array with an int accumulator: the
  # interpreter is the oracle for the hash, the regex proves it ran.
  { name => 'Uint8Array byte scan, int accumulator (M5.1a, class A shape)',
    frag => q{function(req){ var u8 = new Uint8Array(1024); var i, r; for (i = 0; i < 1024; i++) { u8[i] = (i * 7 + 3) & 255; } var n = u8.length; var h = 0x811c9dc5 | 0; for (r = 0; r < 200; r++) { for (i = 0; i < n; i++) { h = (h ^ u8[i]) | 0; h = (h + (h << 5)) | 0; } } return "h=" + (h >>> 0) + "\n"; }},
    paths => [qw(/t/u8 /t/u8)], expect_denials => 0,
    expect_re => qr/^h=\d+$/m },
);

plan tests => scalar(@cases) * 4 + 1;

sub run_case {
    my ($bin, $tag, $case, $port, $sockport) = @_;

    my $grant = '';
    my $opts  = '';
    if ($case->{grant}) {
        # SAME socket address for both builds of a case, so addr/port scalar
        # reads are comparable (interp and jit run sequentially, not concurrent).
        $grant = "var s = nginx.createSocket('127.0.0.1:$sockport');\n"
               . "nginx.http.attach(s).addServer(nginx.http.servers[0]);\n";
        $opts  = ", { grants: { sock: s } }";
    }

    open my $r, '>', "$dir/$tag.root.js" or die $!;
    print $r $grant;
    print $r "var h = comcon.include(" . perl_qq($case->{frag}) . "$opts);\n";
    print $r "var locs = nginx.http.servers[0].locations;\n";
    print $r "for (var i=0;i<locs.length;i++){ if(locs[i].path==='/t'){\n";
    print $r "  locs[i].handler = function(req){\n";
    print $r "    var o = h({method:req.method, uri:req.uri, args:req.args});\n";
    print $r "    if (typeof o === 'string') req.respond(200, {}, o);\n";
    print $r "    else req.respond(o.status||200, o.headers||{}, String(o.body||''));\n";
    print $r "  }; } }\n";
    close $r;

    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { } } }\n";
    close $c;

    my $pid = fork();
    die "fork failed" unless defined $pid;
    if ($pid == 0) {
        open(STDERR, '>', "$dir/$tag.stderr") or exit 126;
        exec($bin, '-p', $dir, '-c', "$dir/$tag.conf") or exit 127;
    }
    for (1 .. 100) {
        last if IO::Socket::INET->new(PeerAddr => "127.0.0.1:$port", Timeout => 1);
        select undef, undef, undef, 0.05;
    }
    my $resp = '';
    $resp .= `curl -s 127.0.0.1:$port$_` for @{ $case->{paths} };
    kill 'QUIT', $pid; waitpid($pid, 0);

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    my $denials = () = $log =~ /js denial:/g;
    # D4c: match the NATIVE notice, which is printed only when functions really
    # have compiled code.  The line this used to grep ("lowered to native C")
    # was printed for EVERY include whether or not anything was lowered --
    # js_comcon_aot_compile() returns 0 for any bytecode function -- so this
    # non-vacuity check could not actually tell.  Now it can, and the answer is
    # that these fragments are genuinely native (1 of 1 functions each): the
    # gate really is comparing a compiled tier against an interpreted one.  A
    # BYTECODE line here would mean it was comparing two interpreted tiers
    # while claiming otherwise.
    my $lowered = ($log =~ /include fragment NATIVE \(COMCON C5 server-AOT: /)
                  ? 1 : 0;
    return ($resp, $denials, $lowered);
}

# quote a JS source string as a JS single-quoted literal for embedding
sub perl_qq {
    my ($s) = @_;
    $s =~ s/\\/\\\\/g;
    $s =~ s/'/\\'/g;
    return "'$s'";
}

my $i = 0;
my $all_compiled = 1;
for my $case (@cases) {
    my $tag = "c$i";
    my $sockport = $portbase + 100 + $i;   # same for both builds of this case
    my ($ri, $di, undef) = run_case($interp, "${tag}i", $case, $portbase + $i*2, $sockport);
    my ($rj, $dj, $lj)   = run_case($jit,    "${tag}j", $case, $portbase + $i*2 + 1, $sockport);

    is($rj, $ri, "[$case->{name}] compiled response == interpreted");
    # EQUAL IS NOT ENOUGH: two arms that fail the same way are equal too.  Each
    # case says what a correct response looks like (an explicit expect_re, or
    # at least a non-empty body without the include error's signature), so an
    # include that refused on both tiers cannot pass as agreement.
    if ($case->{expect_re}) {
        like($ri, $case->{expect_re}, "[$case->{name}] the response is the fragment's own output");
    } else {
        ok(length($ri) > 0 && $ri !~ /comcon\.include|comcon: fragment/,
           "[$case->{name}] the response is a fragment response, not an include error");
    }
    is($dj, $di, "[$case->{name}] compiled denials == interpreted ($di)");
    if ($case->{expect_denials}) {
        cmp_ok($di, '>', 0, "[$case->{name}] A1 gate fired in both tiers");
    } else {
        is($di, 0, "[$case->{name}] no spurious denials");
    }
    diag("[$case->{name}] compiled arm lowered=$lj") unless $lj;
    $all_compiled &&= $lj;
    $i++;
}

ok($all_compiled, 'every include fragment was actually AOT-compiled (gate not vacuous)');
