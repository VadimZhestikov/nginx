#!/usr/bin/perl

# COMCON CONVERGE P6: single-process-mode teardown must not crash — on the
# include compartment. In master_process off mode one process plays both worker
# and master, so both ngx_js_exit_process and ngx_js_exit_master run against the
# same jcf (comcon_rt/comcon_ctx included). This starts a confined include
# fragment single-process, serves, sends QUIT, and asserts a CLEAN exit.

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
    my ($tag, $root_src, $port) = @_;
    open my $j, '>', "$dir/$tag.root.js" or die $!;
    print $j $root_src;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; master_process off; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\n";
    print $c "http { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /t { } location /h { return 200 \"ok\"; } } }\n";
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
    `curl -s 127.0.0.1:$port/t`;
    `curl -s 127.0.0.1:$port/h`;
    select undef, undef, undef, 0.2;
    kill 'QUIT', $pid;
    waitpid($pid, 0);
    return ($? & 127);   # signal number, 0 = clean exit
}

# An include fragment wired to a location (creates + tears down comcon_rt).
my $with = <<'JS';
var h = comcon.include("function(req){ return { status:200, body:'r ' + req.uri + '\n' }; }");
var locs = nginx.http.servers[0].locations;
for (var i=0;i<locs.length;i++){ if(locs[i].path==='/t'){
  locs[i].handler = function(req){ var o=h({uri:req.uri}); req.respond(o.status,{},o.body); };
}}
JS
my $sig1 = run_single_process('inc_handler', $with, 8845);
is($sig1, 0, 'single-process shutdown is clean with a confined include handler');

# A root that includes a fragment but never serves it (still builds comcon_rt).
my $none = <<'JS';
comcon.include("function(req){ return 'x'; }");
JS
my $sig2 = run_single_process('inc_nohandler', $none, 8846);
is($sig2, 0, 'single-process shutdown is clean with an unused include fragment');
