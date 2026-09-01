#!/usr/bin/perl

# COMCON C5.0-b: the lowering differential test (erasure soundness on a real
# fragment). A confined handler must produce BYTE-IDENTICAL responses AND an
# identical denial-counter total whether it runs interpreted (objs/nginx) or is
# AOT-compiled to native C (objs_jit/nginx). Confinement is preserved by
# construction — the compiled handler calls the same gated host functions under
# the same host-set compartment — so the A1 reach gate on granted.listener must
# fire the same number of times in both tiers.
#
# Requires both builds present: the default interpreter objs/nginx and the JIT
# variant objs_jit/nginx (see build-and-test memory / CLAUDE.md). Skips if the
# JIT build is absent.

use warnings;
use strict;

use Test::More;
use FindBin;
use IO::Socket::INET;
use File::Temp qw/tempdir/;

my $root   = "$FindBin::Bin/..";
my $interp = "$root/objs/nginx";
my $jit    = "$root/objs_jit/nginx";

plan(skip_all => "no interpreter build objs/nginx")   unless -x $interp;
plan(skip_all => "no JIT build objs_jit/nginx (build the JIT variant)") unless -x $jit;
plan tests => 6;

my $dir = tempdir(CLEANUP => 1);

# Host: grant a socket whose reach edges are A1-gated cross-compartment.
open my $h, '>', "$dir/host.js" or die $!;
print $h qq{var s = nginx.createSocket("127.0.0.1:8899");\n};
print $h qq{nginx.grantToTenant("granted", s);\n};
close $h;

# Tenant: reads granted.listener every request (gated -> a denial each time),
# returns a data response built from the sealed Request. Strict-module,
# in-profile, JIT-compilable.
open my $tj, '>', "$dir/tenant.js" or die $!;
print $tj <<'JS';
onRequest(function(req) {
    var gated = (granted.listener === null) ? "gated" : "LEAK";
    return "resp " + req.method + " " + req.uri + " " + gated + "\n";
});
JS
close $tj;

sub run_build {
    my ($bin, $tag, $port) = @_;
    my $conf = "$dir/$tag.conf";
    # Multi-process (production-representative): master AOT-compiles at config
    # load, forks a worker that serves the compiled handler via COW inheritance.
    open my $c, '>', $conf or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/host.js;\njs_tenant_source $dir/tenant.js;\n";
    print $c "events { }\n";
    print $c "http { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { js_tenant_handler; } } }\n";
    close $c;

    my $pid = fork();
    die "fork failed" unless defined $pid;
    if ($pid == 0) {
        # config-load notices (incl. the AOT "lowered" line) go to stderr before
        # the error_log file is active — capture it.
        open(STDERR, '>', "$dir/$tag.stderr") or exit 126;
        exec($bin, '-p', $dir, '-c', $conf) or exit 127;
    }

    # wait for the listener
    my $up = 0;
    for (1 .. 100) {
        if (IO::Socket::INET->new(PeerAddr => "127.0.0.1:$port", Timeout => 1)) {
            $up = 1; last;
        }
        select undef, undef, undef, 0.05;
    }

    my $resp = '';
    if ($up) {
        for my $i (1 .. 3) {
            $resp .= `curl -s 127.0.0.1:$port/t/$i`;
        }
    }

    kill 'QUIT', $pid;              # graceful master shutdown
    waitpid($pid, 0);
    my $status = $?;               # low 7 bits = signal if it crashed
    my $crashed = ($status & 127) ? ($status & 127) : 0;

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    my $denials = () = $log =~ /js denial:/g;
    my $lowered = ($log =~ /lowered to native C/) ? 1 : 0;
    return ($resp, $denials, $lowered, $up, $crashed);
}

my ($ri, $di, $li, $ui, $ci) = run_build($interp, 'interp', 8811);
my ($rj, $dj, $lj, $uj, $cj) = run_build($jit,    'jit',    8812);

ok($ui && $uj, 'both builds started and served');
like($ri, qr/resp GET \/t\/1 gated/, 'interpreted handler serves + gate fires');
is($rj, $ri, 'compiled responses are byte-identical to interpreted (erasure)');
is($dj, $di, "compiled denial total ($dj) equals interpreted ($di) — gates preserved");
ok($lj, 'the JIT build actually AOT-compiled the handler (test is not vacuous)');
is($cj, 0, 'the JIT build shut down cleanly (no crash on exit)');
