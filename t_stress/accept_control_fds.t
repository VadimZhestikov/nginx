#!/usr/bin/perl

# Accept-control must not leak descriptors.
#
# nginx.suspendAllWorkers() / resumeAllWorkers() each create a reply
# socketpair, wrap one end in an ngx_connection_t, register it for read, and
# tear it down in the reply handler. That teardown used to do
#
#     ngx_del_event(conn->read, NGX_READ_EVENT, NGX_CLOSE_EVENT);
#     ngx_free_connection(conn);
#     conn->fd = (ngx_socket_t) -1;
#
# with no close(). NGX_CLOSE_EVENT tells the epoll module to SKIP
# epoll_ctl(DEL) precisely because the caller is about to close the fd, so the
# descriptor leaked AND stayed registered in epoll, pointing at a connection
# that ngx_free_connection() had just put back on the free list. The manager
# closes its end, leaving the worker's end permanently EOF-ready, so
# epoll_wait spun; and once the freed connection slot was recycled for a real
# request, events fired against the wrong connection.
#
# Nothing detected that. The functional suite passed throughout -- it only
# surfaced as an intermittent wedge in t/js_sw_accept_control.t under load.
# This measures the thing directly: worker descriptor counts across many
# accept-control cycles.
#
# WORKER fds, not master: the reply socketpair is created and torn down in
# the worker that ran the handler.

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
worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /cycle/ { }
        location /noop/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* One suspend+resume round trip per request: two reply socketpairs. */
    by['/cycle/'].handler = async function (req) {
        var a = await nginx.suspendAllWorkers();
        var b = await nginx.resumeAllWorkers();
        req.respond(200, {'content-type': 'text/plain'},
                    'ok ' + (a && a.unacked) + ' ' + (b && b.unacked));
    };

    /* Control: a request that does no accept-control work at all. */
    by['/noop/'].handler = function (req) {
        req.respond(200, {'content-type': 'text/plain'}, 'noop');
    };
})();
JS

$t->run();

###############################################################################

my $master = master_pid($t);

sub worker_pids {
    my @w = split /\s+/, `pgrep -P $master 2>/dev/null` || '';
    return grep { /^\d+$/ } @w;
}

sub worker_fds {
    my $n = 0;
    for my $w (worker_pids()) {
        my $c = fd_count($w);
        $n += $c if $c > 0;
    }
    return $n;
}

my $N = 150;

# Warm up: the first calls allocate the manager channel, SW plumbing and the
# connection pool, so measure the steady state, not the first-use ramp.
http_get('/cycle/') for 1 .. 10;
my $before = worker_fds();

http_get('/cycle/') for 1 .. $N;
my $after = worker_fds();

my $delta = $after - $before;

diag("worker fds: before=$before after=$after delta=$delta over $N cycles");

# Each cycle did a suspend AND a resume, so an unfixed leak grows by ~2N.
# Allow a small allowance for connection-pool churn; the bug produced a delta
# of the same order as N, which this catches by an order of magnitude.
cmp_ok($delta, '<=', 8,
       "accept-control leaks no descriptors ($delta over $N cycles)");

# The control path must be flat too, so a failure above is attributable to
# accept-control rather than to ordinary request handling.
my $cb = worker_fds();
http_get('/noop/') for 1 .. $N;
my $cd = worker_fds() - $cb;
cmp_ok($cd, '<=', 8, "plain requests leak no descriptors ($cd over $N cycles)");

# And the workers must still be serving: a spinning or wedged worker shows up
# here as a failed request rather than as a descriptor count.
like(http_get('/cycle/'), qr/200 OK/,
     'accept-control still works after the cycles');
