#!/usr/bin/perl

# Regression test for the bcast-channel EOF spin:
#   When the write-end of the per-worker bcast socketpair closes (e.g. the
#   master shuts down or the file-descriptor is not inherited by a child),
#   ngx_js_bcast_recv_handler received EPOLLHUP / recvmsg() n==0 and
#   returned without calling ngx_del_event().  The fd remained registered
#   in level-triggered epoll, causing the worker event loop to spin at
#   ~100% CPU on every subsequent epoll_wait() call.
#
# Trigger: a single location block + nginx.broadcast handler.  With one
#   location the worker's first-ever request calls ngx_js_bcast_ensure_active,
#   which adds the bcast fd to epoll.  If the write-end of that fd has already
#   been closed (normal in certain OS fd-inheritance paths), ev->eof fires
#   immediately and the spin began.
#
# Fix: ngx_js_bcast_recv_handler checks ev->eof and recvmsg()==0; on either
#   condition it calls ngx_del_event + ngx_free_connection to deregister the
#   fd and stop the spin.
#
# Verification: serve 5 requests via the 1-location JS handler, wait 2 s,
#   assert that every worker's CPU% is < 20%.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/bcast_eof.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Intentionally only ONE location block — this was the trigger for the
        # bcast EOF spin (bcast fd registered by the first JS request).
        location /api/ { }
    }
}
EOF

$t->write_file('bcast_eof.js', <<'JS');
nginx.broadcast(function () {
    var locs = nginx.http.servers[0].locations;
    var loc  = locs.find(function(l) { return l.path === '/api/'; });
    if (loc) {
        loc.handler = function (req) {
            req.respond(200, {}, nginx.shared.get('hits') || '0');
            nginx.shared.set('hits',
                String((parseInt(nginx.shared.get('hits') || '0') + 1)));
        };
    }
});
JS

$t->run();

# ── 5 requests to the single JS-handler location ─────────────────────────

for my $i (1 .. 5) {
    my $r = http_get('/api/');
    like($r, qr{HTTP/1\.1 200}, "request $i: 200 OK");
}

# ── 2-second idle pause then verify no worker is spinning ────────────────

sleep 2;

my $master_pid = $t->read_file('nginx.pid');
chomp $master_pid;

my $max_cpu = 0;
if (open my $fh, '-|', 'ps', '--ppid', $master_pid,
                             '-o', 'pcpu', '--no-header')
{
    while (<$fh>) {
        chomp(my $cpu = $_);
        $cpu =~ s/^\s+//;
        $max_cpu = $cpu if $cpu > $max_cpu;
    }
    close $fh;
}

ok($max_cpu < 20,
   "no worker CPU spin after 5 requests + 2s idle (max ${max_cpu}%)");
