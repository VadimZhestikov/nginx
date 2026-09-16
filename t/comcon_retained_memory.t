#!/usr/bin/perl

# F2's LEAK HALF -- what a fragment RETAINS across calls is attributed to it,
# capped, and refused past the cap.
#
# The per-invocation allowance (t/comcon_fragment_memory.t) bounds a BURST.  A
# fragment that keeps a little of every call -- an array it appends to, a cache
# -- walks the shared 64 MB runtime cap upward until every sibling fails, and
# until now nothing attributed that memory to it: AUDIT_M-SES §3's "residual: it
# bounds a burst, not a leak".
#
# THE MECHANISM (ngx_js.h at NGX_JS_COMCON_FRAGMENT_RETAINED_BYTES): every
# invocation adds its signed delta -- usage after the call, its settle and its
# marshal, minus usage before -- to its fragment's `retained`.  Refcounting
# frees most garbage at once, so the delta is what the call LEFT BEHIND plus
# cycles not yet collected; the cycles are corrected AMORTIZED, one GC per 4 MB
# of growth, scaling every positive count down to the growth that is really
# there.  O(1) per call: F14 closed a per-call heap walk and this does not
# reopen it (t/comcon_invoke_heap_independence.t still holds).  A fragment over
# its cap (`meter({retainedBytes})`, 8 MB default, narrowing only; a
# sub-fragment inherits the cap in force) is REFUSED at its next invocation with
# E_MEM_RETAINED -- not run, a refusal -- until its epoch is replaced or its
# contract raises the cap.  `comcon.memStatus(f)` reads the count.
#
# Every row below is a control for the one before it: a fragment that retains
# is charged; one that does not is not; one that makes only cycles is charged
# and then corrected; the cap refuses the first and not the second; a replaced
# epoch starts from nothing; a sub-fragment held across the parent's calls is
# charged on its own slot and refused inside the parent.

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

        location /run { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* A retains 64 KB per call in a closure it keeps; capped at 1 MB */
var KEEP = "(function(){ var keep = []; return function(req){ keep.push('x'.repeat(65536)); return 'kept ' + keep.length; }; })()";
var F = {};
F.A = comcon.include(KEEP, { imports: [], meter: comcon.meter({ retainedBytes: 1048576 }) });

/* B allocates the same per call and keeps none of it */
F.B = comcon.include("function(req){ var a = []; for (var i = 0; i < 16; i++) { a.push('y'.repeat(4096)); } return 'made ' + a.length; }",
                     { imports: [] });

/* C makes ~200 KB of CYCLES per call and keeps none: refcounting cannot free
   them, so the deltas over-count until the amortized GC corrects them */
F.C = comcon.include("function(req){ var i, a, b; for (i = 0; i < 2000; i++) { a = { p: 'z'.repeat(64) }; b = { a: a }; a.b = b; } return 'cycled'; }",
                     { imports: [] });

/* E asks for a 1 GB cap and gets the 8 MB default: narrowing only */
F.E = comcon.include(KEEP, { imports: [], meter: comcon.meter({ retainedBytes: 1073741824 }) });

/* D holds ONE sub-fragment across its calls; the sub retains 64 KB per call
   on its OWN slot, under the parent's 512 KB cap, and is refused INSIDE the
   parent, which reports it */
F.D = comcon.include(
    "(function(){ var sub = null; return function(req){" +
    "  if (!sub) { sub = author.include('(function(){ var keep = []; return function(){ keep.push(\"w\".repeat(65536)); return keep.length; }; })()', {imports: []}); }" +
    "  try { return 'sub kept ' + sub(); } catch (e) { return 'sub refused ' + (e.code || e.message); } }; })()",
    { imports: [], grants: { author: comcon.author({ subFragments: 1 }) },
      meter: comcon.meter({ retainedBytes: 524288 }) });

locs.find(function (l) { return l.path === "/run"; }).handler = function (req) {
    var q = {}, parts = String(req.args || '').split('&'), i, kv;
    for (i = 0; i < parts.length; i++) { kv = parts[i].split('='); q[kv[0]] = kv[1]; }
    var f = F[q.f], n = parseInt(q.n || '1', 10), o = { f: q.f, n: n, ok: 0, refused: 0, codes: {}, last: null };
    if (q.op === 'replace') {
        /* the way a superseded epoch is freed, then the same source again */
        comcon.__freeConfined(f.handle);
        F[q.f] = comcon.include(KEEP, { imports: [], meter: comcon.meter({ retainedBytes: 1048576 }) });
        f = F[q.f];
        o.replaced = true;
    }
    if (q.op === 'plain') {
        try { comcon.memStatus(function () {}); o.plain = 'accepted'; }
        catch (e) { o.plain = e.name; }
    }
    for (i = 0; i < n; i++) {
        try { o.last = f({}); o.ok++; }
        catch (e) { o.refused++; var c = e.code || 'none'; o.codes[c] = (o.codes[c] || 0) + 1; o.lastErr = String(e.message).slice(0, 240); }
    }
    o.mem = comcon.memStatus(f);
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(19);

###############################################################################

sub j {
    my ($path) = @_;
    my $r = http_get($path);
    my ($body) = $r =~ /\r\n\r\n(.*)\z/s;
    my %o;
    for my $k (qw(ok refused invocations retained cap)) {
        my ($v) = $body =~ /"$k":(-?\d+)/;
        $o{$k} = $v;
    }
    ($o{retained})    = $body =~ /"retained":(-?\d+)/;
    ($o{invocations}) = $body =~ /"invocations":(\d+)/;
    ($o{refusedCount}) = $body =~ /"refused":(\d+)\}/;
    ($o{last})  = $body =~ /"last":"([^"]*)"/;
    ($o{plain}) = $body =~ /"plain":"([^"]*)"/;
    ($o{code})  = $body =~ /"codes":\{"([^"]*)"/;
    $o{body} = $body;
    return \%o;
}

# (1) attribution: A keeps 64 KB per call
my $a = j('/run?f=A&n=10');
diag("A x10: retained=$a->{retained} invocations=$a->{invocations} last=$a->{last}");
is($a->{ok}, 10, 'A ran 10 times under its cap');
is($a->{invocations}, 10, 'memStatus counts the invocations');
cmp_ok($a->{retained}, '>=', 10 * 65536 * 0.8,
       'A is charged for what it kept: at least 80% of 640 KB');
cmp_ok($a->{retained}, '<=', 10 * 65536 * 1.5,
       '... and not much more than it kept (the deltas measure retention, not allocation)');

# (2) the control: B allocates the same and keeps nothing
my $b = j('/run?f=B&n=10');
diag("B x10: retained=$b->{retained}");
is($b->{ok}, 10, 'B ran 10 times');
cmp_ok($b->{retained}, '<', 65536,
       'B is charged (almost) nothing: refcounting freed what it made before the delta was taken');

# (3) cycles: C over-counts until the amortized GC corrects it (30 calls make
#     ~6 MB of cyclic garbage, past the 4 MB step)
my $c = j('/run?f=C&n=30');
diag("C x30: retained=$c->{retained}");
is($c->{ok}, 30, 'C ran 30 times');
cmp_ok($c->{retained}, '<', 512 * 1024,
       'C, which keeps only cycles, is corrected down after the GC step: under 512 KB of ~6 MB made');

# (4) the cap: A is refused past 1 MB, with the code, and only A
my $a2 = j('/run?f=A&n=10');
diag("A x10 more: ok=$a2->{ok} refused=$a2->{refused} code=" . ($a2->{code} // '') . " retained=$a2->{retained} err=" . (($a2->{body} =~ /"lastErr":"([^"]*)"/)[0] // ''));
cmp_ok($a2->{refused}, '>=', 1, 'A is REFUSED once it holds more than its 1 MB cap');
is($a2->{code}, 'E_MEM_RETAINED', '... with the code E_MEM_RETAINED on the error');
cmp_ok($a2->{ok}, '<', 10, '... and the refused calls did not run (a refusal, not a denial)');
like($a2->{body}, qr/"lastErr":"[^"]*\[E_MEM_RETAINED\]/, '... the code is in the message too, for the log line');
my $b2 = j('/run?f=B&n=5');
is($b2->{ok}, 5, 'B, a sibling, still runs: the cap is per fragment, not per runtime');

# (5) a replaced epoch starts from nothing
my $a3 = j('/run?f=A&n=3&op=replace');
diag("A replaced, x3: ok=$a3->{ok} retained=$a3->{retained}");
is($a3->{ok}, 3, 'after its epoch is replaced the same source runs again');
cmp_ok($a3->{retained}, '<', 4 * 65536, '... charged from zero: the old slot\'s count died with it');

# (6) narrowing only: a 1 GB cap is the 8 MB default
my $e = j('/run?f=E&n=140');
diag("E x140 (asked 1 GB): ok=$e->{ok} refused=$e->{refused} retained=$e->{retained}");
cmp_ok($e->{refused}, '>=', 1, 'a contract cannot widen the cap: 1 GB asked, refused past the 8 MB default');

# (7) nested: the sub-fragment charges its own slot under the parent's cap
my $d = j('/run?f=D&n=12');
diag("D x12: last=$d->{last} parent retained=$d->{retained}");
like($d->{last}, qr/^sub refused E_MEM_RETAINED$/,
     'a sub-fragment held across the parent\'s calls is refused INSIDE the parent past the cap in force');
cmp_ok($d->{retained}, '<', 65536 * 4,
       '... and the parent itself is charged little: the retention was the sub\'s, on its own slot');

# (8) the reader refuses a plain function
my $p = j('/run?f=B&n=1&op=plain');
is($p->{plain}, 'TypeError', 'memStatus(plain function) is a TypeError');

###############################################################################

undef $t;
