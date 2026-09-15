#!/usr/bin/perl

# THE RESOURCE GATES ON THE COMPILED TIER -- what a fragment can REFUSE TO
# STOP DOING, asked of native code.
#
# The S6 battery (t/comcon_mses_gate.t) and its AOT arm (F5) ask what a fragment
# can REACH.  F16 (v5.106) was a different kind of defect: a fragment lowered to
# native C could catch its own deadline interrupt, because the interpreter's
# exception path honoured the engine's uncatchable flag and maxim's generated
# catch dispatch did not.  Nothing had asked whether the RESOURCE gates -- the
# deadline, the allowance, a `uses` budget -- hold on the tier tenants actually
# run on.  M5 will erase more of the interpreter's structure through typed
# lowering; this file is the standing gate that must stay green while it does.
#
# ONE BATTERY, TWO ARMS, THE SAME SHAPE AS THE AOT GATE: every probe is included
# at config time in the master (the only place server-AOT happens -- the
# compiler thread does not survive fork()), each arm reports the tier it ran on
# through comcon.aotStatus() (read, never set), and the assertion is agreement,
# probe by probe, plus the named expectation each probe carries.  The
# interpreted arm is the control: it must report compiled == 0, or the two arms
# are not two tiers.
#
# The probes, each one request (a deadline probe runs to its deadline):
#   catch_spin       F16's polite form: try { for(;;) } catch -> must not survive
#   catch_forever    F16's hostile form: catch and spin again -> stopped promptly
#   finally_spin     a spin inside `finally`: the gosub/ret path, not the catch path
#   generator_spin   a compiled generator driven for ever by a compiled loop
#   async_spin       an async fragment spinning before its first await
#   async_after      ... and after one: the spin runs in the settle loop
#   nested_spin      a COMPILED sub-fragment (authored at config phase, so it is
#                    lowered) spinning inside a compiled parent that tries to catch
#   uses_budget      a `uses` budget of 2 spent by a compiled loop: the third read
#                    reads undefined, and the denial counter says so
#   redacted_loop    a redacted field read 1000 times in a compiled loop: 1000
#                    undefineds and no denial (redaction is not a denial)
#   memory_alloc     the allowance, hit by a compiled allocation loop
#
# Every deadline probe also reports how long it ran, and is expected to have run
# UNTIL the deadline (>= 150 ms of a 200 ms meter): an immediate throw for an
# unrelated reason would also be "stopped".

use warnings;
use strict;

use Test::More;
use File::Temp qw(tempdir);
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

my $root   = "$FindBin::Bin/..";
my $interp = "$root/objs/nginx";
my $jit    = "$root/objs_jit/nginx";

plan(skip_all => "no interpreter build objs/nginx") unless -x $interp;
plan(skip_all => "no JIT build objs_jit/nginx")     unless -x $jit;

my @PROBES = qw(catch_spin catch_forever finally_spin generator_spin async_spin
                async_after nested_spin uses_budget redacted_loop memory_alloc);

plan(tests => 6 + 3 * @PROBES);

my $dir = tempdir(CLEANUP => 1);

my $root_js = <<'JS';
var sock = nginx.createSocket("127.0.0.1:%%SOCKPORT%%");
var METER = comcon.meter({ timeoutMs: 200, memoryBytes: 1048576 });

var F = {};

F.catch_spin = comcon.include(
    "function(req){ var n = 0;" +
    "  for (;;) { try { for (;;) { n = (n + 1) % 1000000; } }" +
    "    catch (e) { return 'SURVIVED'; } } }",
    { imports: [], meter: METER });

F.catch_forever = comcon.include(
    "function(req){ var n = 0;" +
    "  for (;;) { try { for (;;) { n = (n + 1) % 1000000; } } catch (e) { n = 0; } } }",
    { imports: [], meter: METER });

F.finally_spin = comcon.include(
    "function(req){ var n = 0;" +
    "  try { return 'RETURNED'; } finally { for (;;) { n = (n + 1) % 1000000; } } }",
    { imports: [], meter: METER });

F.generator_spin = comcon.include(
    "function(req){ var g = (function*(){ var i = 0; for (;;) { i = (i + 1) % 1000; yield i; } })();" +
    "  var s = 0; for (;;) { s += g.next().value; } }",
    { imports: [], meter: METER });

F.async_spin = comcon.include(
    "async function(req){ var n = 0; for (;;) { n = (n + 1) % 1000000; } }",
    { imports: [], meter: METER });

F.async_after = comcon.include(
    "async function(req){ await null; var n = 0; for (;;) { n = (n + 1) % 1000000; } }",
    { imports: [], meter: METER });

/* the parent authors ONE sub-fragment when warmed (at config phase, below, so
   the sub-fragment is lowered too), and when run invokes it inside a try/catch
   -- F16's original symptom was this catch succeeding on the JIT build */
F.nested_spin = comcon.include(
    "(function(){ var sub = null; return function(req){" +
    "  if (req.warm) { sub = author.include('function(){ var n = 0; for (;;) { n = (n + 1) % 1000000; } }'," +
    "                                       {imports: [], timeoutMs: 200}); return 'warm'; }" +
    "  try { sub(); return 'RETURNED'; } catch (e) { return 'PARENT CAUGHT'; } }; })()",
    { imports: [], grants: { author: comcon.author({ subFragments: 1 }) } });
F.nested_spin({ warm: true });

F.uses_budget = comcon.include(
    "function(req){ var seen = [];" +
    "  for (var i = 0; i < 3; i++) { seen.push(typeof s.address); }" +
    "  return seen.join(','); }",
    { imports: [], grants: { s: comcon.mediate(sock, comcon.uses('crg-budget', 2, 60)) } });

F.redacted_loop = comcon.include(
    "function(req){ var u = 0;" +
    "  for (var i = 0; i < 1000; i++) { if (typeof s.port === 'undefined') { u++; } }" +
    "  return 'undefined x' + u; }",
    { imports: [], grants: { s: comcon.mediate(sock, comcon.redact(['port'])) } });

F.memory_alloc = comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push(new Array(1024).fill(0)); } }" +
    "  catch (e) { return 'oom'; } }",
    { imports: ['Array'], meter: METER });

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (Object.prototype.hasOwnProperty.call(d, k)) { c[k] = d[k]; } }
    return c;
}

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path !== "/probe") { continue; }
    locs[i].handler = function (req) {
        var name = String(req.args || '').replace(/^name=/, '');
        var f = F[name];
        var o = { probe: name, aot: comcon.aotStatus(f) };
        var c0 = counts();
        var t0 = Date.now();
        try {
            var v = f({ run: true });
            o.verdict = 'RETURNED ' + String(v);
        } catch (e) {
            o.verdict = /interrupted/.test(String(e.message || e)) ? 'FIRED'
                      : 'THREW ' + String(e.message || e);
        }
        o.ms = Date.now() - t0;
        var c1 = counts(), d = {}, k;
        for (k in c1) { if ((c1[k] || 0) !== (c0[k] || 0)) { d[k] = c1[k] - (c0[k] || 0); } }
        o.denials = d;
        req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
    };
}
JS

sub run_arm {
    my ($bin, $tag, $port, $sockport) = @_;

    (my $js = $root_js) =~ s/%%SOCKPORT%%/$sockport/g;
    open my $r, '>', "$dir/$tag.root.js" or die $!;
    print $r $js;
    close $r;

    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /probe { } } }\n";
    close $c;

    # nginx opens <prefix>/logs/error.log before it has read the config's own
    # error_log; without the directory that is an [alert] on stderr
    mkdir "$dir/logs";

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

    my %res;
    for my $p (@PROBES) {
        my $body = `curl -s -m 8 '127.0.0.1:$port/probe?name=$p'`;
        my ($verdict)  = $body =~ /"verdict":"([^"]*)"/;
        my ($ms)       = $body =~ /"ms":(\d+)/;
        my ($compiled) = $body =~ /"aot":\{[^}]*"compiled":(\d+)/;
        my ($denials)  = $body =~ /"denials":\{([^}]*)\}/;
        $res{$p} = { verdict => $verdict // "(no response: $body)", ms => $ms // -1,
                     compiled => $compiled // -1, denials => $denials // '' };
    }
    kill 'QUIT', $pid; waitpid($pid, 0);

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    return (\%res, $log);
}

my ($I, $ilog) = run_arm($interp, 'interp', 8953, 8955);
my ($J, $jlog) = run_arm($jit,    'jit',    8954, 8956);

for my $p (@PROBES) {
    diag(sprintf("%-15s interp: %-22s %5dms compiled=%d [%s]", $p,
                 $I->{$p}{verdict}, $I->{$p}{ms}, $I->{$p}{compiled}, $I->{$p}{denials}));
    diag(sprintf("%-15s jit:    %-22s %5dms compiled=%d [%s]", '',
                 $J->{$p}{verdict}, $J->{$p}{ms}, $J->{$p}{compiled}, $J->{$p}{denials}));
}

###############################################################################
# the two tiers, first

# Async fragments are NOT lowered at include time on either build: the codegen
# handles async bodies (P12.3), but the include-time tree compile leaves them
# at compiled == 0, so an async policy runs interpreted on the compiled tier.
# Measured here, recorded rather than hidden inside a looser assertion; M5's
# benchmark has to know it.
my %async = map { $_ => 1 } qw(async_spin async_after);

my @icomp = grep { $I->{$_}{compiled} == 0 } @PROBES;
is(scalar(@icomp), scalar(@PROBES), 'the interpreted arm ran every probe interpreted (compiled == 0)');
my @jcomp = grep { $async{$_} ? $J->{$_}{compiled} == 0 : $J->{$_}{compiled} >= 1 } @PROBES;
is(scalar(@jcomp), scalar(@PROBES),
   'THE COMPILED ARM IS ACTUALLY COMPILED: every non-async probe reports native code from '
   . 'aotStatus(), and the two async probes report 0 -- async fragments are not lowered')
    or diag("unexpected tier on the jit arm: "
            . join(' ', map { "$_=$J->{$_}{compiled}" }
                        grep { $async{$_} ? $J->{$_}{compiled} != 0 : $J->{$_}{compiled} < 1 } @PROBES));

###############################################################################
# named expectations, on both arms

my %expect = (
    catch_spin     => qr/^FIRED$/,
    catch_forever  => qr/^FIRED$/,
    finally_spin   => qr/^FIRED$/,
    generator_spin => qr/^FIRED$/,
    async_spin     => qr/^FIRED$/,
    async_after    => qr/^FIRED$/,
    nested_spin    => qr/^FIRED$/,
    uses_budget    => qr/^RETURNED string,string,undefined$/,
    redacted_loop  => qr/^RETURNED undefined x1000$/,
    memory_alloc   => qr/^RETURNED oom$/,
);
my %deadline = map { $_ => 1 } qw(catch_spin catch_forever finally_spin generator_spin
                                  async_spin async_after nested_spin);

for my $p (@PROBES) {
    like($J->{$p}{verdict}, $expect{$p}, "compiled: $p");
    if ($deadline{$p}) {
        cmp_ok($J->{$p}{ms}, '>=', 150, "compiled: $p ran until the deadline, not an unrelated throw");
    } else {
        pass("compiled: $p (no deadline)");
    }
    is($J->{$p}{verdict} . '|' . $J->{$p}{denials}, $I->{$p}{verdict} . '|' . $I->{$p}{denials},
       "AGREEMENT: $p -- the compiled tier's verdict and denials equal the interpreter's");
}

###############################################################################
# the counters the probes moved, named

like($J->{uses_budget}{denials}, qr/"budget\.uses":1/, 'compiled: the third read spent the budget -- budget.uses fired once');
is($J->{redacted_loop}{denials}, '', 'compiled: 1000 redacted reads fired no denial (redaction is not a denial)');
like($J->{nested_spin}{verdict}, qr/^FIRED$/, 'compiled: the parent could NOT catch its sub-fragment\'s abort (F16\'s symptom, at depth 2)');
my @alerts = grep { /\[alert\]|\[emerg\]|Sanitizer/ } split /\n/, $ilog . $jlog;
is(scalar(@alerts), 0, 'no alerts, no sanitizer reports in either arm')
    or diag(join("\n", @alerts[0 .. ($#alerts < 5 ? $#alerts : 5)]));
