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
);

plan tests => scalar(@cases) * 3 + 1;

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
    is($dj, $di, "[$case->{name}] compiled denials == interpreted ($di)");
    if ($case->{expect_denials}) {
        cmp_ok($di, '>', 0, "[$case->{name}] A1 gate fired in both tiers");
    } else {
        is($di, 0, "[$case->{name}] no spurious denials");
    }
    $all_compiled &&= $lj;
    $i++;
}

ok($all_compiled, 'every include fragment was actually AOT-compiled (gate not vacuous)');
