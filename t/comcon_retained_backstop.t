#!/usr/bin/perl

# G7.22's NAMED LIMIT, QUANTIFIED -- a leaker beside a sibling that makes many
# SMALL cycles.
#
# The retained accounting is exact for what refcounting frees.  Cycles it
# cannot see are corrected in two places: a call that grows the runtime by
# NGX_JS_COMCON_GC_CALL_DELTA or more pays a collection before its delta is
# taken (exact for that call), and once usage has grown NGX_JS_COMCON_GC_STEP
# since the last mark a collection establishes the ground truth and scales any
# EXCESS out of every positive count in proportion.  That second correction
# cannot tell whose count is the excess: a leaker A whose count is exact and a
# cycle-maker S whose count is all garbage are scaled by the same factor, so A
# is UNDER-counted by S's share.  G7.22 names it; this file measures it, so the
# number in the record is a measurement and the bound below is a claim that a
# regression would fail.
#
# S makes ~24 KB of cycles per call -- BELOW the per-call collection threshold,
# which is the whole point: a cycle-maker above it pays its own collection and
# never reaches the backstop.  A keeps 64 KB per call and makes no cycles.
# A runs 48 times (3 MB, exact), then S runs 250 times, then 200 interleaved
# calls (three S, one A), with A staying under its cap throughout (~6 MB of cycles, so
# the 4 MB backstop fires at least once), then A's count is read.

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
var F = {};
/* A: 64 KB kept per call, no cycles */
F.A = comcon.include("(function(){ var keep = []; return function(req){ keep.push('x'.repeat(65536)); return keep.length; }; })()",
                     { imports: [] });
/* S: ~24 KB of cycles per call (300 pairs of small objects), nothing kept */
F.S = comcon.include("function(req){ var i, a, b; for (i = 0; i < 300; i++) { a = { p: 'z'.repeat(16) }; b = { a: a }; a.b = b; } return 'cycled'; }",
                     { imports: [] });

locs.find(function (l) { return l.path === "/run"; }).handler = function (req) {
    var q = {}, parts = String(req.args || '').split('&'), i, kv;
    for (i = 0; i < parts.length; i++) { kv = parts[i].split('='); q[kv[0]] = kv[1]; }
    var f = F[q.f], n = parseInt(q.n || '1', 10), o = { f: q.f, n: n, ok: 0, refused: 0 };
    /* mix: three S calls then one A call, so a collection -- the engine's own,
       or the accounting's -- can land INSIDE an A call and credit S's freed
       cycles to A as a negative delta: the hazard, provoked */
    for (i = 0; i < n; i++) {
        var g = (q.f === 'mix') ? ((i % 4 === 3) ? F.A : F.S) : f;
        try { g({}); o.ok++; } catch (e) { o.refused++; }
    }
    o.A = comcon.memStatus(F.A);
    o.S = comcon.memStatus(F.S);
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(4);

###############################################################################

sub j {
    my ($path) = @_;
    my $r = http_get($path);
    my ($body) = $r =~ /\r\n\r\n(.*)\z/s;
    my ($a) = $body =~ /"A":\{"retained":(-?\d+)/;
    my ($s) = $body =~ /"S":\{"retained":(-?\d+)/;
    my ($ok) = $body =~ /"ok":(\d+)/;
    return { A => $a, S => $s, ok => $ok, body => $body };
}

my $exact = 48 * 65536;
my $r1 = j("/run?f=A&n=48");
diag("after A x48:  A=$r1->{A}  S=$r1->{S}");
cmp_ok($r1->{A}, '>=', $exact * 0.9, 'A is charged its 3 MB before any sibling runs');

my $r2 = j('/run?f=S&n=250');
my $ratio = $r2->{A} / $exact;
diag(sprintf("after S x250: A=%d (%.0f%% of its exact 3 MB)  S=%d", $r2->{A}, 100 * $ratio, $r2->{S}));
is($r2->{ok}, 250, 'S ran 250 times');

# the interleaved phase: 200 calls, three S then one A, so collections land
# inside A's calls too; A keeps 50 x 64 KB more, exactly -- and stays UNDER its
# 8 MB cap throughout, or a refusal would read as an under-count
my $r3 = j('/run?f=mix&n=200');
$exact += 50 * 65536;
$ratio = $r3->{A} / $exact;
diag(sprintf("after mix x200: A=%d (%.0f%% of its exact %d)  S=%d", $r3->{A}, 100 * $ratio, $exact, $r3->{S}));
cmp_ok($r3->{S}, '<', 2 * 1024 * 1024,
       'S, which keeps nothing, is not carrying its ~6 MB of cycles after the backstop');
# THE MEASURED LIMIT, as a bound: A must keep at least this share of its exact
# count after a sibling's small cycles went through the backstop.  The number
# is the one the record cites (ASSURANCE G7.22); tightening the correction
# raises it, and a regression lowers it.
cmp_ok($ratio, '>=', 0.5,
       'the leaker keeps at least half of its exact count beside a small cycle-maker (G7.22\'s under-count, bounded)');

###############################################################################

undef $t;
