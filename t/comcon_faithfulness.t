#!/usr/bin/perl

# COMCON C7 = M8 (= SR-2): the compiler-faithfulness gate.
#
# "T2 refines T1": for every representative confined fragment, the AOT-compiled
# tier (objs_jit/nginx) must produce the SAME responses AND the SAME denials as
# the interpreted tier (objs/nginx) — including that the A1 reach/mutate gates
# fire IDENTICALLY (confinement survives compilation, and the compiled code does
# not leak authority). This generalizes the single-fragment C5.0 differential
# test (t/comcon_lowering.t) into a suite covering the confinement surface:
# report, Request reads, Response shapes, granted-socket scalar reads, gated
# reach (.listener), gated mutators (close/broadcast), and compute.
#
# Scope: the COMCON tenant profile (strict-module confined fragments) — the
# maxim surface that is gate-clean in-profile. Requires both builds.

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
plan(skip_all => "no JIT build objs_jit/nginx")       unless -x $jit;

my $dir = tempdir(CLEANUP => 1);
my $portbase = 8860;

# Each case: name, optional host.js (grants), tenant.js, request paths,
# and whether we expect the A1 gates to fire (denials > 0).
my @cases = (
  { name => 'report + string response',
    tenant => q{onRequest(function(req){ report("hit " + req.uri); return "ok " + req.method + "\n"; });},
    paths  => [qw(/t/a /t/b)], expect_denials => 0 },

  { name => 'Request fields + JSON response',
    tenant => q{onRequest(function(req){ return JSON.stringify({m:req.method,u:req.uri,a:req.args})+"\n"; });},
    paths  => ['/t/x?y=1', '/t/z'], expect_denials => 0 },

  { name => 'Response object {status,body}',
    tenant => q{onRequest(function(req){ return {status:201, body:"made " + req.uri + "\n"}; });},
    paths  => [qw(/t/1 /t/2)], expect_denials => 0 },

  { name => 'compute loop (typed int)',
    tenant => q{onRequest(function(req){ var s=0; for(var i=0;i<5000;i++){s=(s+i*3)|0;} return "sum="+s+"\n"; });},
    paths  => [qw(/t/c /t/c)], expect_denials => 0 },

  { name => 'granted socket scalar reads',
    host   => q{var s=nginx.createSocket("127.0.0.1:8971"); nginx.grantToTenant("sock", s);},
    tenant => q{onRequest(function(req){ return "addr="+sock.address+" port="+sock.port+" fd="+(typeof sock.fd)+"\n"; });},
    paths  => [qw(/t/s /t/s)], expect_denials => 0 },

  { name => 'A1 gated reach (.listener -> denial)',
    host   => q{var s=nginx.createSocket("127.0.0.1:8972"); nginx.grantToTenant("sock", s);},
    tenant => q{onRequest(function(req){ return (sock.listener===null?"gated":"LEAK")+"\n"; });},
    paths  => [qw(/t/g /t/g /t/g)], expect_denials => 1 },

  { name => 'A1 gated mutator (close() -> denial)',
    host   => q{var s=nginx.createSocket("127.0.0.1:8973"); nginx.grantToTenant("sock", s);},
    tenant => q{onRequest(function(req){ try { sock.close(); return "closed\n"; } catch(e){ return "denied\n"; } });},
    paths  => [qw(/t/m /t/m)], expect_denials => 1 },
);

plan tests => scalar(@cases) * 3 + 1;

sub run_case {
    my ($bin, $tag, $case, $port) = @_;
    if ($case->{host}) {
        open my $h, '>', "$dir/$tag.host.js" or die $!;
        print $h $case->{host}, "\n"; close $h;
    }
    open my $t, '>', "$dir/$tag.js" or die $!;
    print $t $case->{tenant}, "\n"; close $t;
    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.host.js;\n" if $case->{host};
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { js_tenant_handler; } } }\n";
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
    my $sig = $? & 127;

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    my $denials = () = $log =~ /js denial:/g;
    my $lowered = ($log =~ /lowered to native C/) ? 1 : 0;
    return ($resp, $denials, $lowered, $sig);
}

my $i = 0;
my $all_compiled = 1;
for my $case (@cases) {
    my $tag = "c$i";
    my ($ri, $di, undef, $si) = run_case($interp, "${tag}i", $case, $portbase + $i*2);
    my ($rj, $dj, $lj, $sj)   = run_case($jit,    "${tag}j", $case, $portbase + $i*2 + 1);

    is($rj, $ri, "[$case->{name}] compiled responses == interpreted");
    is($dj, $di, "[$case->{name}] compiled denials ($dj) == interpreted ($di)");
    if ($case->{expect_denials}) {
        cmp_ok($di, '>', 0, "[$case->{name}] A1 gate fired in both tiers (denials>0)");
    } else {
        is($sj, 0, "[$case->{name}] compiled tier clean (no crash)");
    }
    $all_compiled &&= $lj;
    $i++;
}

ok($all_compiled, 'every fragment was actually AOT-compiled (gate is not vacuous)');
