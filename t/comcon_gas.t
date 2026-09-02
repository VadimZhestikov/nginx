#!/usr/bin/perl

# COMCON gas: a confined tenant's per-request CPU time is bounded.
#
# The tenant runtime is otherwise unbounded in execution time — a
# `while(true){}` handler would hang the worker (deny-by-default caps and the
# 64MB memory limit do not stop an infinite loop). gas wires the interrupt
# handler onto the tenant runtime and sets a per-request deadline
# (NGX_JS_TENANT_TIMEOUT_MS) around the tenant JS_Call, so a runaway handler is
# interrupted (500) instead of hanging. Normal fast handlers are unaffected.

use warnings;
use strict;

use Test::More;
use FindBin;
use IO::Socket::INET;
use File::Temp qw/tempdir/;
use Time::HiRes qw/time/;

my $bin = $ENV{TEST_NGINX_BINARY} || "$FindBin::Bin/../objs/nginx";
plan(skip_all => "no nginx binary") unless -x $bin;
plan tests => 4;

my $dir = tempdir(CLEANUP => 1);

sub serve {
    my ($tag, $src, $port) = @_;
    open my $j, '>', "$dir/$tag.js" or die $!;  print $j $src; close $j;
    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { js_tenant_handler; } } }\n";
    close $c;
    my $pid = fork();
    die "fork failed" unless defined $pid;
    if ($pid == 0) { open(STDERR,'>',"$dir/$tag.stderr"); exec($bin,'-p',$dir,'-c',"$dir/$tag.conf"); exit 127; }
    for (1..100) { last if IO::Socket::INET->new(PeerAddr=>"127.0.0.1:$port",Timeout=>1); select undef,undef,undef,0.05; }
    return $pid;
}

# --- infinite loop: bounded, not hung ---
my $pid = serve('loop', 'onRequest(function(req){ while(true){} return "x"; });', 8901);
my $t0 = time;
my $code = `curl -s -m 5 -o /dev/null -w '%{http_code}' 127.0.0.1:8901/t 2>/dev/null`;
my $dt = time - $t0;
kill 'QUIT', $pid; waitpid($pid, 0);

isnt($code, '000', 'infinite-loop handler did NOT hang (request completed)');
is($code, '500',  'infinite-loop handler is interrupted -> 500');
cmp_ok($dt, '<', 4, "interrupted within the budget window (${\ sprintf('%.2f', $dt)}s < 4s)");

# --- a normal fast handler is unaffected ---
my $pid2 = serve('ok', 'onRequest(function(req){ return "ok " + req.uri + "\n"; });', 8902);
my $r = `curl -s -m 3 127.0.0.1:8902/t 2>/dev/null`;
kill 'QUIT', $pid2; waitpid($pid2, 0);
like($r, qr{ok /t}, 'a normal handler serves normally (gas does not affect fast handlers)');
