#!/usr/bin/perl

# upstream.onSelectPeer(fn) — the custom load balancer.
#
# `fn(peers, connCtx)` returns the index of the peer to use, or -1 to fall back
# to round-robin.  nginx's own health, retry and accounting are preserved; JS
# only influences WHICH peer is chosen.
#
# The whole registry behind it (ngx_js_lb_choose/_get/_init/_find/_registry,
# ~100 lines) sat at 0% coverage, which is how this went unnoticed:
#
#     upstream.onSelectPeer(function (peers) {
#         for (...) { if (...) { return i; } }      // no return on this path
#     });
#
# JS_ToInt32() answers 0 without an error for undefined, NaN, {} and
# "nonsense", so a selection function that fell off its end sent EVERY request
# to peer 0.  Measured on three backends: B1,B1,B1,B1,B1,B1 where round-robin
# gives B1,B2,B3 — two thirds of the pool idle, one backend carrying
# everything, nothing logged.  The easiest mistake to make in a callback whose
# entire job is to return a value was also the one with no diagnostic.
#
# Only a real, finite, in-range number is an index now; everything else takes
# the documented -1 path.
#
# The test distinguishes the two outcomes by DISTRIBUTION: six requests across
# three equal-weight backends see all three under round-robin and exactly one
# when pinned.  Asserting a single response body could not tell "chose peer 0"
# from "round-robin happened to start at peer 0".
#
# Negative results pinned here too, because they are what makes the fix narrow:
# an out-of-range index, a negative one, and a throwing callback already fell
# back correctly, and still must.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    upstream back {
        server 127.0.0.1:8091;
        server 127.0.0.1:8092;
        server 127.0.0.1:8093;
    }

    server { listen 127.0.0.1:8091; location / { return 200 "B1"; } }
    server { listen 127.0.0.1:8092; location / { return 200 "B2"; } }
    server { listen 127.0.0.1:8093; location / { return 200 "B3"; } }

    server {
        listen       127.0.0.1:8080;
        location /p   { proxy_pass http://back; }
        location /ctl { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var MODE = 'minus1';
var CALLS = 0;
var ups = null, i;

for (i = 0; i < nginx.http.upstreams.length; i++) {
    if (String(nginx.http.upstreams[i].name) === 'back') {
        ups = nginx.http.upstreams[i];
    }
}

if (ups) {
    ups.onSelectPeer(function (peers, connCtx) {
        CALLS++;
        switch (MODE) {
        case 'fixed0':  return 0;
        case 'fixed1':  return 1;
        case 'fixed2':  return 2;
        case 'minus1':  return -1;
        case 'negative':return -5;
        case 'oob':     return 99;
        case 'huge':    return 1e300;      /* finite, but past INT32_MAX */
        case 'frac':    return 1.9;        /* truncates, like ToInt32 */
        case 'undef':   return;            /* the mistake this test exists for */
        case 'str':     return 'nonsense';
        case 'numstr':  return '2';        /* a STRING, not a number */
        case 'nan':     return NaN;
        case 'inf':     return Infinity;
        case 'obj':     return {};
        case 'arr':     return [1];
        case 'throw':   throw new Error('selection blew up');
        }
        return -1;
    });
}

for (i = 0; i < nginx.http.servers.length; i++) {
    var L = nginx.http.servers[i].locations;
    for (var j = 0; j < L.length; j++) {
        if (L[j].path !== '/ctl') { continue; }
        L[j].handler = function (req) {
            if (req.queryParams.mode) { MODE = req.queryParams.mode; }
            req.respond(200, { 'content-type': 'text/plain' },
                        MODE + ' ' + CALLS + ' ' + (ups !== null));
        };
    }
}
JS

$t->try_run('no js module')->plan(19);

sub body {
    my ($r) = @_;
    return '' unless defined $r;
    my ($b) = $r =~ /\r\n\r\n(.*)/s;
    return defined $b ? $b : '';
}

# six requests; report how many DISTINCT backends answered
sub spread {
    my ($mode) = @_;
    body(http_get("/ctl?mode=$mode"));
    my %seen;
    my @got;
    for (1 .. 6) {
        my $b = body(http_get('/p'));
        $seen{$b} = 1;
        push @got, $b;
    }
    return (scalar keys %seen, join(',', @got));
}

my $ctl0 = body(http_get('/ctl'));
like($ctl0, qr/ true$/, 'the balancer attached to the upstream')
    or diag "ctl said: $ctl0";

# --- the callback is actually being consulted -------------------------------
my ($n_rr) = spread('minus1');
my $after = body(http_get('/ctl'));
my ($calls) = $after =~ /^\S+ (\d+)/;
cmp_ok($calls || 0, '>', 0, 'the selection function was actually called')
    or diag 'nothing below means anything if the hook never ran';

# --- valid indices pin, which is the whole point of the feature -------------
# (a "fix" that always fell back to round-robin would pass every case below
#  this block, so these run first and must keep working)
for my $m (qw/fixed0 fixed1 fixed2/) {
    my ($n, $got) = spread($m);
    is($n, 1, "$m: a valid index pins every request to one peer")
        or diag "got: $got";
}

my ($n_frac, $g_frac) = spread('frac');
is($n_frac, 1, 'frac: 1.9 truncates to an index, like ToInt32')
    or diag "got: $g_frac";

# --- the documented fallback ------------------------------------------------
is($n_rr, 3, 'minus1: -1 falls back to round-robin across all peers');

# --- values that are not an index must fall back, not select peer 0 ---------
for my $m (qw/undef str nan obj arr numstr inf/) {
    my ($n, $got) = spread($m);
    is($n, 3, "$m: a non-index return falls back to round-robin")
        or diag "got: $got   (1 distinct backend means it pinned peer 0)";
}

# --- these already fell back before the fix, and still must ------------------
for my $m (qw/negative oob huge throw/) {
    my ($n, $got) = spread($m);
    is($n, 3, "$m: still falls back (unchanged behaviour)")
        or diag "got: $got";
}

# --- the upstream is still healthy after all that ---------------------------
my ($n_end, $g_end) = spread('minus1');
is($n_end, 3, 'the upstream still balances normally afterwards')
    or diag "got: $g_end";
