#!/usr/bin/perl

# COMCON: single-process-mode teardown must not crash.
#
# In master_process off (single-process) mode, one process plays both the
# worker and master roles, so BOTH ngx_js_exit_process and ngx_js_exit_master
# run against the same jcf — and w->rt/w->ctx alias jcf->rt/jcf->ctx. Before the
# fix, exit_process freed the runtime and exit_master then dereferenced the
# freed pointer in js_std_free_handlers → SIGSEGV. exit_process now nulls the
# shared jcf handles so the second pass skips them. This test starts a tenant
# single-process, serves, sends QUIT, and asserts a CLEAN exit (no signal).

use warnings;
use strict;

use Test::More;
use FindBin;
use IO::Socket::INET;
use File::Temp qw/tempdir/;

my $bin = $ENV{TEST_NGINX_BINARY} || "$FindBin::Bin/../objs/nginx";
plan(skip_all => "no nginx binary") unless -x $bin;
plan tests => 2;

my $dir = tempdir(CLEANUP => 1);

sub run_single_process {
    my ($tag, $tenant_src, $port) = @_;
    open my $j, '>', "$dir/$tag.js" or die $!;
    print $j $tenant_src;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; master_process off; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\n";
    print $c "http { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { js_tenant_handler; } location /h { return 200 \"ok\"; } } }\n";
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
    `curl -s 127.0.0.1:$port/h`;
    select undef, undef, undef, 0.2;
    kill 'QUIT', $pid;
    waitpid($pid, 0);
    my $status = $?;
    return ($status & 127);   # signal number, 0 = clean exit
}

# A tenant WITH a request handler (exercises the AOT path on a JIT build).
my $sig1 = run_single_process('handler',
    qq{onRequest(function(req){ return "r " + req.uri + "\\n"; });\n}, 8841);
is($sig1, 0, 'single-process shutdown is clean with a tenant handler');

# A tenant with NO handler (still creates a tenant runtime aliased into jcf).
my $sig2 = run_single_process('nohandler', qq{report("hi");\n}, 8842);
is($sig2, 0, 'single-process shutdown is clean with a handler-less tenant');
