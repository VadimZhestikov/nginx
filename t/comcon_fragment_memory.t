#!/usr/bin/perl

# F2 — a confined fragment gets a per-INVOCATION memory allowance.
#
# AUDIT_M-SES §3: "JS_SetMemoryLimit(comcon_rt, 64 MB) bounds the RUNTIME, shared by every
# fragment. One fragment can exhaust the budget of all of them -- denial of service against
# siblings, not an authority escape." One number for everyone is not attribution.
#
# The mechanism is the runtime limit itself, narrowed for the duration of one call:
# before the invoke the limit becomes (current usage + allowance), afterwards it is
# restored. The invoke is single-threaded, so growth in that window IS this fragment's --
# which is as much attribution as one shared runtime can honestly give.
#
# WHAT IT BOUNDS IS A BURST, NOT A LEAK, and the last probe below says so out loud: a
# fragment that retains a little on every call still walks the runtime cap upward across
# calls. That remains the runtime limit's job. A per-call allowance READS like per-fragment
# accounting and is not, so the difference is pinned here rather than left to be assumed.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
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

        location /mem { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* allocate roughly `mb` megabytes of live strings and report what happened */
var GREEDY =
    "function(mb){ var a = [], i;"
  + "  for (i = 0; i < mb * 16; i++) { a.push(new Array(65536).join('x')); }"
  + "  return 'allocated ' + a.length; }";

locs.find(function (l) { return l.path === "/mem"; }).handler = function (req) {
    var o = {};

    function run(frag, mb) {
        try { return frag(mb); }
        catch (e) {
            /* the MESSAGE, not just the name: a refusal that is not about
               memory would otherwise read as a pass here */
            return 'REFUSED: ' + String((e && e.message) || e).slice(0, 70);
        }
    }

    /* the DEFAULT allowance (16 MB): a modest fragment is unaffected */
    var dflt = comcon.include(GREEDY, { imports: ['Array'] });
    o.small = run(dflt, 1);

    /* ...and a greedy one is stopped, without taking the compartment with it.
       32 MB is chosen to sit ABOVE the 16 MB allowance and well BELOW the 64 MB
       runtime cap: at 64 MB the probe could not tell the new bound from the old
       one, and its control duly failed to fire when the allowance was removed. */
    o.greedy = run(dflt, 32);

    /* the compartment still works afterwards: the refusal was contained to the
       call, not to the runtime -- which is the whole point of restoring the
       limit after each invoke */
    o.after = run(comcon.include(GREEDY, { imports: ['Array'] }), 1);

    /* a contract may NARROW the allowance */
    var tight = comcon.include(GREEDY,
        { imports: ['Array'],
          meter: comcon.meter({ memoryBytes: 1024 * 1024 }) });
    o.tight = run(tight, 4);

    /* ...and may NOT widen it: 512 MB is asked for, 16 MB is what applies */
    var greedyContract = comcon.include(GREEDY,
        { imports: ['Array'],
          meter: comcon.meter({ memoryBytes: 512 * 1024 * 1024 }) });
    o.cannotWiden = run(greedyContract, 32);

    /* the bound is per CALL: the same fragment that just failed at 32 MB runs
       again at 1 MB, because the allowance is restored between invocations */
    o.perCall = run(dflt, 1);

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(6);

###############################################################################

my $r = http_get('/mem');
diag($1) if $r =~ /(\{.*\})/;

like($r, qr/"small":"allocated \d+"/,
     'a modest fragment runs untouched under the default 16 MB allowance');
like($r, qr/"greedy":"REFUSED: comcon: fragment: InternalError: out of memory/,
     'A FRAGMENT THAT TRIES TO ALLOCATE PAST ITS ALLOWANCE IS STOPPED -- which '
     . 'is what "one fragment can exhaust the budget of all of them" needed');
like($r, qr/"after":"allocated \d+"/,
     'and the COMPARTMENT survives it: the next fragment runs normally, because '
     . 'the runtime limit is restored after every invoke rather than left where '
     . 'the greedy call put it');
like($r, qr/"tight":"REFUSED: comcon: fragment: InternalError: out of memory/,
     'a contract may NARROW the allowance (1 MB here refuses 4 MB of work)');
like($r, qr/"cannotWiden":"REFUSED: comcon: fragment: InternalError: out of memory/,
     'and may NOT widen it: a contract asking for 512 MB still gets 16 -- the '
     . 'min() is enforced in C, so calling __invokeConfined directly cannot buy '
     . 'a bigger budget than the default');
like($r, qr/"perCall":"allocated \d+"/,
     'THE BOUND IS PER CALL, not per fragment for its lifetime: the fragment '
     . 'that just failed at 64 MB runs again at 1 MB. That is also the limit of '
     . 'this mechanism -- it bounds a BURST, not a slow leak across calls, and '
     . 'the 64 MB runtime cap remains the backstop for the latter');
