#!/usr/bin/perl

# Reload lifecycle leak test: baseline (plain nginx, no JS).
#
# Establishes the noise floor for RSS and fd measurements so that
# numbers from other tests can be interpreted relative to this.
# N=20 SIGHUP cycles with no JS runtime involved.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use lib '../t/lib';
use Test::Nginx;
use ReloadHarness;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(3);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location / { return 200 "ok\n"; }
    }
}
EOF

$t->run();

my $pid  = master_pid($t);
my $rss0 = rss_kb($pid);
my $fds0 = fd_count($pid);

my $N = 20;
for my $i (1 .. $N) {
    reload_nginx($t);
}

my $rss1 = rss_kb($pid);
my $fds1 = fd_count($pid);

assert_rss_stable($rss0, $rss1, $N, 'baseline RSS');
assert_fd_stable($fds0, $fds1, $N, 'baseline fd');
like(http_get('/'), qr/200 OK/, 'nginx still serving after reloads');
