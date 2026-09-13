#!/usr/bin/perl

# F8 / THREATS T9 — MEASURING the accepted residual: a cross-tenant timing channel.
#
# T4/T9 are accepted, not closed: "timing/cache/contention channels between co-resident
# tenants -- with IFC, the explicitly-deferred confidentiality axis." An accepted risk of
# UNKNOWN magnitude is worth less than an accepted risk of measured magnitude, and nothing
# in the tree had ever put a number on this one. This file does.
#
# THIS IS EVIDENCE, NOT A GATE. It is in t/tools/ for the same reason host-call-cost.t is:
# timing assertions in the suite are flaky, and the useful output is a number, not a
# pass/fail. Run it when the claim needs refreshing:
#
#     TEST_NGINX_BINARY=$(pwd)/objs/nginx prove -v t/tools/ifc-timing-channel.t
#
# THE CHANNEL. A fragment cannot read a clock -- `Date` is deliberately not an intrinsic,
# precisely because it is a side channel -- so the receiving tenant cannot time anything
# itself. The observer is therefore the CLIENT, and the medium is CPU contention: an nginx
# worker is single-threaded, so while tenant A burns CPU, tenant B's request simply does not
# run. A's secret bit becomes B's latency.
#
# WHAT THE NUMBER IS FOR. The per-event magnitude of this channel is bounded by the
# execution deadline (F6/F12): a tenant can only hold the worker for as long as it is
# allowed to run. So the measurement below is also a measurement of what that bound buys --
# which is the only mitigation currently in the tree for T9.

use warnings;
use strict;

use Test::More;
use Time::HiRes qw/time/;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin . '/..'); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;          # ONE worker: co-residency is the premise

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /send { }
        location /recv { }
        location /bound { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
function at(p, fn) { var l = locs.find(function (x) { return x.path === p; }); if (l) l.handler = fn; }

/* TENANT A — the sender. Its secret is the bit; it modulates CPU. */
var sender = comcon.include(
    "function(a){ var s = 0, i, n = a.bit ? a.spin : 0;"
  + " for (i = 0; i < n; i++) { s += i % 7; } return s; }",
    { imports: [] });

/* TENANT B — the receiver. Trivial work; its LATENCY is the signal. */
var receiver = comcon.include("function(a){ return 'r'; }", { imports: [] });

at('/send', function (req) {
    var bit = /bit=1/.test(req.args) ? 1 : 0;
    var spin = 40000000;
    var t0 = Date.now();
    sender({ bit: bit, spin: spin });
    req.respond(200, {'content-type':'text/plain'}, 'sent ' + (Date.now() - t0));
});

at('/recv', function (req) {
    req.respond(200, {'content-type':'text/plain'}, receiver({}));
});

/* the same sender under a TIGHT deadline: the bound caps the per-event leak */
at('/bound', function (req) {
    nginx.workerRequestTimeout = 50;
    req.respond(200, {'content-type':'text/plain'}, 'bound-armed');
});
JS

$t->try_run('no js module')->plan(3);

###############################################################################

# Fire /send and /recv on separate connections, /send first, and time /recv.
sub probe {
    my ($bit) = @_;
    my $s1 = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => port(8080),
                                   Proto => 'tcp', Timeout => 10) or die $!;
    my $s2 = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => port(8080),
                                   Proto => 'tcp', Timeout => 10) or die $!;
    $s1->print("GET /send?bit=$bit HTTP/1.0\r\nHost: localhost\r\n\r\n");
    my $t0 = time();
    $s2->print("GET /recv HTTP/1.0\r\nHost: localhost\r\n\r\n");
    my $r2 = do { local $/; <$s2> // '' };
    my $lat = (time() - $t0) * 1000;
    my $r1 = do { local $/; <$s1> // '' };
    close $s1; close $s2;
    return ($lat, $r1, $r2);
}

sub median { my @x = sort { $a <=> $b } @_; return $x[int(@x / 2)]; }

my (@zero, @one);
for (1 .. 7) {
    my ($l0) = probe(0); push @zero, $l0;
    my ($l1) = probe(1); push @one,  $l1;
}

my $m0 = median(@zero);
my $m1 = median(@one);
my $sep = $m1 - $m0;

diag(sprintf("receiver latency: bit=0 %.1f ms   bit=1 %.1f ms   separation %.1f ms",
             $m0, $m1, $sep));
diag(sprintf("=> the channel is trivially readable: ~1 bit per %.0f ms, i.e. about "
             . "%.1f bits/s, with a separation %.0fx the idle latency",
             $m1, $m1 > 0 ? 1000 / $m1 : 0, $m0 > 0 ? $sep / $m0 : 0));

ok(1, 'measured (this file reports, it does not gate)');

cmp_ok($sep, '>', 5,
       'THE CHANNEL IS REAL AND LARGE. An nginx worker is single-threaded, so '
       . 'while one tenant burns CPU the other simply does not run: the sender\'s '
       . 'secret bit shows up directly in the receiver\'s latency. T9 is accepted '
       . 'as residual -- this is what is being accepted');

# What the execution bound buys: cap the sender, re-measure the separation.
http_get('/bound');
my (@bz, @bo);
for (1 .. 7) {
    my ($l0) = probe(0); push @bz, $l0;
    my ($l1) = probe(1); push @bo,  $l1;
}
my $bm0 = median(@bz);
my $bm1 = median(@bo);
my $bsep = $bm1 - $bm0;
diag(sprintf("under a 50 ms execution deadline: bit=0 %.1f ms   bit=1 %.1f ms   "
             . "separation %.1f ms (was %.1f)", $bm0, $bm1, $bsep, $sep));

cmp_ok($bsep, '<', $sep,
       'AND THE EXECUTION DEADLINE CAPS IT: bounding how long a tenant may hold '
       . 'the worker bounds how much it can say per event. That is the only '
       . 'mitigation for T9 currently in the tree, and this is its size -- the '
       . 'channel is narrowed, not closed, which is exactly what "accepted '
       . 'residual" should mean');
