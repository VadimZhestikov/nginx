#!/usr/bin/perl

# The L4 FILTER WINDOW is a wait state, and it must behave like one.
#
# Between accept() and ngx_http_init_connection() a connection with an L4 filter
# armed belongs to src/js: c->read->handler is ngx_js_l4_read_handler and the
# http module has never seen the connection.  It used to belong to us with no
# deadline of any kind, which is two defects wearing one coat:
#
#   1. A connection could be held FOREVER by sending zero bytes.  Slow-loris
#      without the loris — no header to dribble, just an open socket pinning a
#      connection slot and its pool until the worker is reloaded.
#
#   2. The worker ABORTED at graceful shutdown: "open socket #N left in
#      connection M", then "aborting".  Stock nginx never trips that for a
#      connection awaiting its first request, because that connection holds a
#      non-cancelable client_header_timeout and ngx_event_no_timers_left() makes
#      the worker decline to exit while such a timer exists.  A timerless window
#      held nothing, so a FIN still in flight lost the race to SIGQUIT.  That was
#      a ~2.8% flake in t/js_pilgrim_p17_server_accept.t (7 failures in 250 runs)
#      whose connection #10 is waitforsocket()'s probe — connect and close at
#      once.  Park one connection deliberately and it was 100%.
#
# THE CONTROL IS BUILT IN, and it is the reason this file has three servers.
# :8080 has no L4 filter, so it is stock nginx's own wait state; :8081 has one.
# The assertion is that the two AGREE — a shape that keeps working as a check
# because the control arm cannot be broken by a change to src/js.  :8082 carries
# a long header timeout so a connection is still genuinely parked in the window
# when nginx is asked to shut down.
#
# What this file does NOT self-control is the shutdown half: nothing here can
# make the alert appear on a fixed tree.  Revert ngx_js_l4_wait_arm()'s call in
# ngx_js_run_accept_handler and test 7 fails — that is the manual control.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use IO::Socket::INET;
use Time::HiRes qw/time/;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    # The control arm: no L4 filter, so this is nginx's own wait state.
    server {
        listen       127.0.0.1:8080;
        server_name  plain;
        client_header_timeout 1s;

        location /hello/ { }
    }

    # The treatment arm: same timeout, one pass-through L4 filter.
    server {
        listen       127.0.0.1:8081;
        server_name  filtered;
        client_header_timeout 1s;

        location /hello/ { }
    }

    # The shutdown arm: long enough that a connection is still parked in the
    # window when nginx is stopped, short enough that $t->stop() does not sit
    # through it.  It DOES sit through it -- that is the fix working: the worker
    # declines to exit while a non-cancelable timer is pending, exactly as stock
    # nginx sits out a client_header_timeout, and worker_shutdown_timeout is the
    # knob for operators who will not wait.  At 30s this file took 32 wallclock
    # seconds; at 3s it takes 8.
    server {
        listen       127.0.0.1:8082;
        server_name  parked;
        client_header_timeout 3s;

        location /hello/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    /* By INDEX, in config order: [0] plain :8080, [1] filtered :8081,
     * [2] parked :8082.  (An earlier version of this comment called
     * `server.serverNames` reading undefined "a gap": it is not -- the server's
     * list is `server.names` (t/js_com_phase1.t), and `serverNames` is the
     * LISTENER's property.  Indexing is kept because it is simpler here.) */
    var servers = nginx.http.servers;

    for (var i = 0; i < servers.length; i++) {
        servers[i].locations[0].handler = function (r) {
            r.respond(200, {}, 'hi\n');
        };
    }

    /* A pass-through filter: it changes no bytes, so any behaviour difference
     * between :8080 and :8081 is the WINDOW's, not the filter's. */
    function passthrough() {
        return async function* (source) {
            for await (const chunk of source) {
                yield chunk;
                return;
            }
        };
    }

    servers[1].addL4Filter(passthrough());
    servers[2].addL4Filter(passthrough());
})();
JS

$t->try_run('no js module')->plan(8);

# Connect, send nothing, and report how long the server takes to give up.
# Returns the elapsed seconds, or undef if it never did within $limit.
sub silent_hold {
    my ($port, $limit) = @_;

    my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => $port,
                                  Proto => 'tcp', Timeout => 2);
    return undef unless $s;

    my $t0 = time();
    my $rin = '';
    vec($rin, fileno($s), 1) = 1;

    my $n = select($rin, undef, undef, $limit);
    my $elapsed = time() - $t0;

    if ($n) {
        my $buf;
        my $r = sysread($s, $buf, 1);
        close($s);
        # 0 bytes = the server closed.  Anything else is not a give-up.
        return (defined($r) && $r == 0) ? $elapsed : undef;
    }

    close($s);
    return undef;
}

like(http_get('/hello/', socket => IO::Socket::INET->new('127.0.0.1:8080')),
     qr/200 OK/, 'the unfiltered server serves');

like(http_get('/hello/', socket => IO::Socket::INET->new('127.0.0.1:8081')),
     qr/200 OK/, 'the filtered server serves — the L4 filter is pass-through');

my $plain = silent_hold(8080, 5);
ok(defined($plain),
   'CONTROL ARM: with no L4 filter, a client that sends nothing is dropped at '
   . 'client_header_timeout — this is stock nginx and cannot be broken by src/js');

my $filtered = silent_hold(8081, 5);
ok(defined($filtered),
   'AND THE FILTERED ARM AGREES: a connection parked inside the L4 window is '
   . 'dropped too. Before the fix the window armed no timer at all, so this hold '
   . 'was unbounded — an unauthenticated peer pinned a connection slot and its '
   . 'pool until reload by sending zero bytes');

# The window must be closed BY THE DEADLINE, not by some unrelated instant
# teardown that would pass test 4 for the wrong reason.
cmp_ok(defined($filtered) ? $filtered : 0, '>', 0.5,
   sprintf('...and it is dropped by the DEADLINE, not instantly (%.2fs, with a '
           . '1s client_header_timeout): a window that closed at once would '
           . 'satisfy the assertion above while breaking every real filter',
           defined($filtered) ? $filtered : 0));

like(http_get('/hello/', socket => IO::Socket::INET->new('127.0.0.1:8081')),
     qr/200 OK/,
     'the filtered server still serves after a window timed out — tearing the '
     . 'window down did not take the listener or the worker with it');

# The OTHER window teardown: the client vanishes while the window is still
# waiting for bytes, i.e. ngx_js_l4_read_handler's `n <= 0` path.  Like the reject
# path, that path used to close with ngx_close_connection() and leak the 512-byte
# connection pool ngx_event_accept() had just created -- 13 sites in the window
# had it wrong.  NOTHING IN THIS FILE CAN SEE THAT LEAK: the assertion for it is
# the leak stage in t/run_sanitizers.sh, which runs this file under ASAN with
# detect_leaks=1 and greps for allocations stacked in ngx_event_accept.  These 50
# connections exist so that stage's corpus actually reaches the path -- without
# them its control does not fire, which is how this step came to be written.
my $vanished = 0;
for (1 .. 50) {
    my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => 8081,
                                  Proto => 'tcp', Timeout => 2);
    if ($s) { $vanished++; close($s); }
}

like(http_get('/hello/', socket => IO::Socket::INET->new('127.0.0.1:8081')),
     qr/200 OK/,
     "$vanished clients that connect and vanish inside the window are reaped and "
     . 'the server still serves (the leak they used to cause is asserted by the '
     . 'ASAN leak stage in t/run_sanitizers.sh, not here)');

# -----------------------------------------------------------------------
# The shutdown half: park a connection in the window, then stop nginx.
# -----------------------------------------------------------------------

my $parked = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => 8082,
                                   Proto => 'tcp', Timeout => 2);
select(undef, undef, undef, 0.3);   # let the accept hook run

$t->stop();

unlike($t->read_file('error.log'), qr/open socket #\d+ left in connection/,
   'AND GRACEFUL SHUTDOWN IS CLEAN with a connection still parked in the window: '
   . 'the window now holds a non-cancelable timer, so the worker declines to exit '
   . 'while it exists instead of aborting over the socket it forgot');

close($parked) if $parked;
