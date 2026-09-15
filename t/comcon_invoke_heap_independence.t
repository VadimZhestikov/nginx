#!/usr/bin/perl

# PERFORMANCE NONINTERFERENCE: what one fragment retains must not set what every
# other fragment's invocation costs.
#
# Every confined invocation narrows the compartment's memory limit to "what is
# allocated now + this call's allowance", and it used to learn "what is allocated
# now" from JS_ComputeMemoryUsage().  That reports the right number -- by walking
# every context, module and live GC object in the runtime.  The compartment is
# SHARED by every fragment, so each invocation was O(everyone's heap).  Measured on
# `function(a){ return 1; }`, before the fix:
#
#     another fragment retains      per invocation      the call itself
#     ~0 objects                    13.5 us             0.5 us
#     ~50,000                       205 us
#     ~250,000                      2,440 us
#
# So the walk was 96% of an invocation with nothing retained, and above that it
# was a CROSS-TENANT CHANNEL: a tenant holding memory -- legitimately, inside its
# allowance -- set every other tenant's latency, and could modulate it at will.
# F8 had been accepted on a CPU-contention measurement that never included this.
# Nothing measured invocation cost against heap size, so nothing saw it.
#
# The fix reads the same counter in O(1) (JS_GetMallocSize, added to the vendored
# engine).  This file asserts a RATIO, not a time: the same trivial invocation
# with and without 200,000 objects retained by a DIFFERENT fragment must cost
# about the same.  Before the fix that ratio was two orders of magnitude.
#
# Each timed loop stops at 250 ms rather than at a count, so the control run
# (fix reverted, ~2.4 ms per call) finishes instead of taking minutes -- and a
# fixed count small enough for that would sit under Date.now()'s 1 ms
# resolution on the fixed build.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /cost { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var l = nginx.http.servers[0].locations[0];

/* Microseconds per invocation, timed until 250 ms have passed (see the header). */
function usPerCall(F) {
    var t0 = Date.now(), n = 0, dt;
    do {
        var i;
        for (i = 0; i < 500; i++) { F({}); }
        n += 500;
        dt = Date.now() - t0;
    } while (dt < 250 && n < 2000000);
    return { us: (dt * 1000) / n, calls: n };
}

l.handler = function (req) {
    var o = {};
    try {
        comcon.mode('enforce');

        var F = comcon.include("function(a){ return 1; }", { imports: [] });
        for (var w = 0; w < 2000; w++) { F({}); }          /* warm */

        o.idle = usPerCall(F);

        /* A DIFFERENT fragment retains 200,000 objects in the shared
         * compartment, inside its own allowance. */
        var B = comcon.include(
            "(function(){ var keep = [], i;"
          + " for (i = 0; i < 200000; i++) { keep.push({ i: i }); }"
          + " return function(a){ return keep.length; }; })()",
            { imports: [] });
        o.retained = B({});

        o.loaded = usPerCall(F);
        o.ratio = o.loaded.us / o.idle.us;
    } catch (e) {
        o.driverError = String(e && e.message);
    }
    req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(3);

my $raw = http_get('/cost');
$raw =~ s/^.*?\r\n\r\n//s;
my $o = eval { decode_json($raw) } || { driverError => "non-JSON: " . substr($raw, 0, 200) };

is($o->{driverError}, undef, 'the heap-independence probe ran')
    or diag("driverError: $o->{driverError}");

diag(sprintf('idle %.2f us/call (%d calls), with 200,000 retained elsewhere '
             . '%.2f us/call (%d calls), ratio %.2fx',
             $o->{idle}{us} // -1, $o->{idle}{calls} // 0,
             $o->{loaded}{us} // -1, $o->{loaded}{calls} // 0,
             $o->{ratio} // -1));

is($o->{retained}, 200000,
   'a different fragment really holds 200,000 objects in the shared compartment '
   . '-- without this the comparison below would pass vacuously');

cmp_ok($o->{ratio} // 1e9, '<', 4,
   'AN INVOCATION DOES NOT COST MORE BECAUSE SOMEONE ELSE RETAINS MEMORY: the '
   . 'same trivial call with and without 200,000 objects held by another fragment '
   . 'costs within 4x. Before the fix this ratio was two orders of magnitude, '
   . 'because every invocation walked the whole shared heap to read one counter '
   . '-- a cross-tenant latency channel any tenant could modulate');
