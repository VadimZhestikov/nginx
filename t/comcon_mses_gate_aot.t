#!/usr/bin/perl

# COMCON F5 — the M-SES escape battery against a fragment that is ACTUALLY
# LOWERED TO NATIVE C.
#
# AUDIT_M-SES.md §3 carried this as PARTIAL: "t/comcon_mses_gate.t has been run on both
# objs and objs_jit, but AOT-compiled FRAGMENTS are not separately asserted against the
# probe battery."  Running the gate on a JIT-capable BINARY is not the same claim as
# running it against COMPILED CODE, and the difference is not academic: server-AOT happens
# in the master pre-fork, js_comcon_aot_compile() returns 0 for any bytecode function
# ("eligible", never "compiled"), and for weeks the include site logged success on that 0.
# A gate that never checks whether lowering happened will report a green compiled tier
# while measuring an interpreted one.
#
# So this file asserts the PRECONDITION FIRST and fails loudly without it:
#
#   the compiled arm must report compiled >= 1 from comcon.aotStatus(), which is a
#   read-only tier probe, and the interpreted arm must report compiled == 0 -- the same
#   fragment, the same battery, demonstrably on two different tiers.
#
# Then the battery itself, from t/tools/mses-probes.js -- the SAME text the standing S6
# gate runs, because two copies of an escape battery is how one of them quietly stops
# testing what the other still does.

use warnings;
use strict;

use Test::More;
use File::Temp qw/tempdir/;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

my $root = $ENV{TEST_NGINX_BINARY} ? do { my $b = $ENV{TEST_NGINX_BINARY};
                                          $b =~ s{/objs(_jit)?/nginx$}{}; $b } : '..';
my $interp = "$root/objs/nginx";
my $jit    = "$root/objs_jit/nginx";

plan(skip_all => "no interpreter build objs/nginx") unless -x $interp;
plan(skip_all => "no JIT build objs_jit/nginx")     unless -x $jit;

plan(tests => 9);

my $probes = do { open my $f, '<', 'tools/mses-probes.js' or die $!; local $/; <$f> };
my $dir = tempdir(CLEANUP => 1);

sub run_arm {
    my ($bin, $tag, $port) = @_;

    open my $r, '>', "$dir/$tag.root.js" or die $!;
    print $r $probes;
    # The fragment is included HERE, at config time in the master -- which is the
    # only place server-AOT can happen, because the compiler thread does not
    # survive fork(). A fragment included inside a handler is never native.
    print $r <<'JS';
var confined = comcon.include(
    "function(req){ var probes = (" + PROBES + ")(); " +
    "  return { status: 200, body: JSON.stringify(probes) }; }");

function hostProbes() {
    var f = (0, eval)('(' + PROBES + ')');
    return f();
}

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/gate") {
        locs[i].handler = function (req) {
            var conf = JSON.parse(confined({ method: req.method }).body);
            var host;
            try { host = hostProbes(); }
            catch (e) { host = { error: String(e.message || e) }; }
            /* the tier the battery ACTUALLY ran on, read (never set) */
            var aot = comcon.aotStatus(confined);
            req.respond(200, {'content-type':'application/json'},
                JSON.stringify({ confined: conf, host: host, aot: aot }));
        };
    }
}
JS
    close $r;

    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /gate { } } }\n";
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
    my $resp = `curl -s 127.0.0.1:$port/gate`;
    kill 'QUIT', $pid; waitpid($pid, 0);

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    return ($resp, $log);
}

# a tiny JSON reader: Test::Nginx has no JSON dependency
sub probes_of {
    my ($body, $section) = @_;
    my %p;
    return %p unless $body =~ /"$section":\{(.*?)\}/s;
    my $inner = $1;
    $p{$1} = $2 while $inner =~ /"(\w+)":"(\w+)"/g;
    return %p;
}

my ($ibody, $ilog) = run_arm($interp, 'interp', 8951);
my ($jbody, $jlog) = run_arm($jit,    'jit',    8952);

my ($icomp) = $ibody =~ /"aot":\{[^}]*"compiled":(\d+)/;
my ($jcomp) = $jbody =~ /"aot":\{[^}]*"compiled":(\d+)/;
$icomp //= -1; $jcomp //= -1;
diag("interpreted arm compiled=$icomp   compiled arm compiled=$jcomp");

# --- THE PRECONDITION ----------------------------------------------------
cmp_ok($jcomp, '>=', 1,
       'THE COMPILED ARM IS ACTUALLY COMPILED: aotStatus() reports native code '
       . 'for the probe fragment. Without this assertion the rest of the file '
       . 'would be a second interpreted run wearing a JIT binary -- which is '
       . 'exactly what AUDIT §3 called PARTIAL');
is($icomp, 0,
   'and the interpreted arm reports NO native code for the same fragment: the '
   . 'two arms are demonstrably on different tiers, read with aotStatus(), '
   . 'which never compiles anything itself');
like($jlog, qr/include fragment NATIVE \(COMCON C5 server-AOT: /,
     'the log agrees with the probe -- the NATIVE notice fires only when '
     . 'functions really have compiled code (the line it replaced was printed '
     . 'for every include, compiled or not)');

# --- THE BATTERY, ON BOTH TIERS -----------------------------------------
my %ic = probes_of($ibody, 'confined');
my %jc = probes_of($jbody, 'confined');
my %ih = probes_of($ibody, 'host');
my %jh = probes_of($jbody, 'host');

cmp_ok(scalar(keys %jc), '>=', 11,
       'the battery ran inside the compiled fragment (' . scalar(keys %jc)
       . ' probes) -- an empty parse would otherwise pass every assertion below');

my @jopen = sort grep { $jc{$_} eq 'open' } keys %jc;
is(scalar(@jopen), 0,
   'NOTHING IS OPEN INSIDE THE COMPILED FRAGMENT: no ambient root, no frozen '
   . 'intrinsic mutated, no code built from a string, no .stack leak, no '
   . 'Symbol.species redirect' . (@jopen ? ' -- OPEN: ' . join(',', @jopen) : ''));

my @iopen = sort grep { $ic{$_} eq 'open' } keys %ic;
is(scalar(@iopen), 0, 'nor inside the interpreted one');

is_deeply(\%jc, \%ic,
          'THE TWO TIERS AGREE PROBE BY PROBE. Lowering a fragment to native C '
          . 'changes neither what it can reach nor what it is refused -- the '
          . 'claim SR-2 makes for the confinement surface, now made for the '
          . 'escape battery specifically');

# --- NON-VACUITY: the battery must be live in both regimes ---------------
my @jhopen = sort grep { $jh{$_} eq 'open' } keys %jh;
my @ihopen = sort grep { $ih{$_} eq 'open' } keys %ih;
cmp_ok(scalar(@jhopen), '>=', 3,
       'THE CONTROL: the identical battery run UNCONFINED on the same binary '
       . 'finds capabilities OPEN (' . scalar(@jhopen) . ') -- so "closed" '
       . 'inside the compiled fragment is a measurement, not a battery that '
       . 'never worked');
is_deeply(\@jhopen, \@ihopen,
          'and the unconfined control opens the SAME set on both builds, so '
          . 'the difference between the arms is confinement rather than the '
          . 'binary they ran on');
