#!/usr/bin/perl

# M5 EVIDENCE, part 2 — WHERE does a host call's time actually go?
#
# Part 1 (t/tools/policy-compute-split.t) showed that compiling a policy's JS
# buys ~1.0x, so M5's payoff, if any, is in the typed STUB ABI. This asks the
# next question directly: if the host call became a typed stub, how much would
# come back -- and is that the same 3.44x the M1 gate measured for hand-written
# C?
#
# It matters because `shared.incr` -- the host call these policies make -- does
# three separable things per invocation: a JS->C dispatch with boxed args, a
# JS_ToCString() of the key, and a LINEAR SCAN of up to 256 slots comparing
# 128-byte keys under a spinlock. A typed stub removes the first two. The third
# is a data-structure choice, and M1's hand-written C did not have it: it used a
# slab atomic on a resolved slot. If the scan dominates, then a large part of
# M1's 3.44x is available today by fixing the counter, with no compiler at all --
# and attributing it to "typed lowering" would be wrong.
#
# nginx.__benchStub(mode, arg) is the decomposition instrument; see its comment
# in src/js/ngx_js_com.c. Run against either build; this measures the host path,
# not the tier.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib '../lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /c { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === "/c"; }).handler = function (req) {
    var N = 2000000, i, t, o = {}, k = 'rl:acme', sink = 0;

    /* Seed keys so the scan has something to walk, as it would on a server with
     * real tenants; an empty table would measure a best case nobody runs in.
     * The key under test lands EARLY, so `shared_incr` below is a near-front
     * hit -- the cheap case. `scan_miss` measures the other end: a key that is
     * not there, i.e. the full scan. A linear structure's cost lives between
     * those two, and quoting only the first would flatter it. */
    for (i = 0; i < 16; i++) { nginx.shared.set('seed:' + i, 'x'); }
    nginx.shared.incr(k, 1);

    function time(fn) {
        var j, t0;
        for (j = 0; j < 5000; j++) { fn(j); }          /* warm */
        t0 = Date.now();
        for (j = 0; j < N; j++) { sink += fn(j); }
        return Date.now() - t0;
    }

    o.n = N;
    /* the JS->C floor: dispatch and an argument, nothing else */
    o.call_floor   = time(function ()  { return nginx.__benchStub(0); });
    /* + marshalling the key string across the boundary */
    o.plus_string  = time(function ()  { return nginx.__benchStub(1, k); });
    /* a TYPED STUB: slot resolved at bind time, locked add */
    o.typed_locked = time(function ()  { return nginx.__benchStub(2, 3); });
    /* the same without the lock -- what M1's slab atomic looked like */
    o.typed_atomic = time(function ()  { return nginx.__benchStub(3, 3); });
    /* today's real path: property walk, string key, linear scan, lock */
    o.shared_incr  = time(function ()  { return nginx.shared.incr(k, 1); });
    /* THE OTHER host access these policies make. `req.headers['x-tenant']` is
     * two steps -- reach the headers surface, then look a name up in it -- and
     * a typed stub would collapse both. Timed separately so the header path is
     * not assumed to look like the counter path. */
    o.hdr_full     = time(function () { return req.headers['x-tenant'] ? 1 : 0; });
    var hh = req.headers;
    o.hdr_cached   = time(function () { return hh['x-tenant'] ? 1 : 0; });
    o.uri_read     = time(function () { return req.uri.length; });

    /* a pure-JS call of the same shape, for scale */
    var noop = function (x) { return x & 1; };
    o.js_call      = time(function (j) { return noop(j); });

    /* SECOND DENSITY: the same call with the table nearly full. A linear scan's
     * cost is a function of how many tenants share the table, so one density is
     * not a measurement of it. */
    for (i = 0; i < 200; i++) { nginx.shared.set('bulk:' + i, 'x'); }
    o.shared_incr_full = time(function () { return nginx.shared.incr(k, 1); });
    /* a MISS: the scan walks every used slot and finds nothing -- the worst
     * case, and the one a rate-limiter hits for every new tenant key. */
    o.scan_miss = time(function () { return nginx.shared.get('zz-not-here') ? 1 : 0; });
    o.keys_full = nginx.shared.keys().length;

    o.sink = sink & 1;
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(3);

my $r = http_get('/c');
my %v = $r =~ /"(\w+)":(\d+)/g;

if (defined $v{n}) {
    my $n = $v{n};
    my $us = sub { sprintf("%6.3f us", ($_[0] * 1000) / $n) };
    diag("per call, $n iterations:");
    diag(sprintf("  %-18s %s", $_, $us->($v{$_})))
        for qw(js_call call_floor plus_string typed_atomic typed_locked
               shared_incr shared_incr_full scan_miss
               hdr_full hdr_cached uri_read);
    diag(sprintf("  (table had 17 keys, then %d)", $v{keys_full} || 0));
    diag(sprintf("  shared.incr / typed_atomic      = %.2fx  (sparse table)",
                 $v{shared_incr} / ($v{typed_atomic} || 1)));
    diag(sprintf("  shared.incr_full / typed_atomic = %.2fx  (full table)",
                 $v{shared_incr_full} / ($v{typed_atomic} || 1)));
    diag(sprintf("  full-scan miss / typed_atomic   = %.2fx",
                 $v{scan_miss} / ($v{typed_atomic} || 1)));
    diag(sprintf("  scan+lock share of shared.incr  = %.0f%% sparse, %.0f%% full",
                 100 * ($v{shared_incr} - $v{plus_string}) / ($v{shared_incr} || 1),
                 100 * ($v{shared_incr_full} - $v{plus_string}) / ($v{shared_incr_full} || 1)));
}

ok(defined $v{shared_incr}, 'the decomposition ran');
cmp_ok($v{call_floor}, '<', $v{shared_incr},
       'the JS->C call floor is cheaper than the real host call');
cmp_ok($v{typed_atomic}, '<=', $v{shared_incr},
       'a typed stub on a resolved slot is not slower than the scanning path');
