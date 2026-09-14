#!/usr/bin/perl

# ASYNC FRAGMENTS — and where the real blocker turned out to be.
#
# ROADMAP has carried "async fragment invocation" as the prerequisite for a real
# `fetch`, with the blocker recorded as the SYNCHRONOUS INVOKE: `JS_Call`, then
# JSON-stringify the result, with no promise detection and no job drain.  That
# is true, and it is not where an async fragment actually stopped.  It never
# reached the invoke:
#
#     comcon.include: admission refused: admit: arg0 not a bytecode function
#
# WHICH IS NOT TRUE OF THE THING IN FRONT OF IT.  An async function, a generator
# and an async generator are all bytecode functions — the engine says so, in
# `js_class_has_bytecode()` — they just carry a different class id.  Six COMCON
# analysis entry points tested one id instead of asking the engine, so the whole
# C3 analysis (free names, dynamic code, request-field seal) REFUSED TO LOOK at
# an entire class of functions and reported it as a property of the function.
#
# A fragment the analysis cannot read must be refused.  A fragment it will not
# read is a different and worse thing, because the refusal message sends the
# operator to rewrite code that was never the problem.
#
# With that fixed the invoke half becomes reachable, and this file pins BOTH
# halves — admission analysing async bodies, and the promise being settled:
#
#   * an async fragment is admitted AND ANALYSED — an undeclared free name
#     inside an async body is still refused, which is the assertion that says
#     the gate is reading the body rather than waving it through
#   * its promise is settled by draining THE COMPARTMENT'S OWN jobs; the
#     compartment has its own runtime, so no host job can be scheduled by it
#   * a rejection takes the throw path, with the fragment's own origin
#   * a promise nothing in reach can settle is REPORTED (E_INVOKE_PENDING), not
#     waited on and not stringified into "{}" — a plausible-looking empty object
#     is the worst of the three answers
#   * a fragment that queues microtasks forever is stopped by the DEADLINE, the
#     same clock that stops a `while (1)`

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /async { }
        location /spin  { }
        location /spinlong { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

function run(src, contract) {
    var c = { imports: [] }, k;
    for (k in (contract || {})) { c[k] = contract[k]; }
    try {
        var f = comcon.include(src, c);
        var r = f({});
        return { value: r };
    } catch (e) {
        return { code: e.code || e.name, msg: String(e.message) };
    }
}

locs.forEach(function (l) {

if (l.path === '/async') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* --- an async fragment is ADMITTED and its promise SETTLED --- */
            o.plain = run("async function(a){ return 42; }");

            /* --- an await over an already-settled value.  `await 5` rather
             * than `await Promise.resolve(5)` on purpose: `Promise` is a free
             * NAME and would have to be declared, which would make this probe
             * about the manifest instead of about the drain. */
            o.awaited = run("async function(a){ var v = await 5;"
                          + " return v * 2; }");

            /* --- a chain of awaits, so more than one job has to run --- */
            o.chained = run(
                "async function(a){ var s = 0, i;"
              + " for (i = 0; i < 5; i++) { s += await i; }"
              + " return s; }");

            /* --- THE ASSERTION THAT SAYS THE GATE IS READING THE BODY ---
             * an undeclared free name INSIDE an async body must still be
             * refused. If admission had merely stopped objecting to async
             * functions, this would be admitted and the analysis would be gone
             * exactly where it is now reachable. */
            o.freeName = run("async function(a){ return nginx.pid; }");

            /* ...and the same name DECLARED gets past the manifest, so the
             * refusal above is the manifest working and not async being refused
             * by some other route.  It reads `undefined`, because declaring a
             * name admits the fragment and does not grant the capability -- the
             * two halves an operator most often confuses. */
            o.declared = run("async function(a){ return typeof nginx; }",
                             { imports: ['nginx'] });

            /* --- a rejection takes the throw path --- */
            o.rejected = run("async function(a){ throw new Error('nope'); }");

            /* --- a promise nothing in reach can settle --- */
            o.neverSettles = run(
                "async function(a){ await new Promise(function(){}); return 1; }",
                { imports: ['Promise'] });

            /* --- a non-async fragment RETURNING a promise is the same case --- */
            o.returnsPending = run(
                "function(a){ return new Promise(function(){}); }",
                { imports: ['Promise'] });

            /* --- and a synchronous fragment is untouched by any of it --- */
            o.sync = run("function(a){ return 'plain'; }");

            /* --- a GENERATOR body is analysed too: same class widening --- */
            o.generatorFree = run("function*(a){ yield nginx.pid; }");

            o.codes = comcon.refusalCodes();

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* A fragment that queues microtasks forever.  The DEADLINE is what stops it --
 * the interrupt handler belongs to the compartment runtime, so this is the same
 * clock that stops a `while (1)`.  Metered down to 300ms so the test does not
 * take five seconds to prove it. */
if (l.path === '/spin') {
    l.handler = function (req) {
        var o = {};
        var t0 = Date.now();
        try {
            var f = comcon.include(
                "async function(a){ var n = 0;"
              + " for (;;) { n = await Promise.resolve(n + 1); } }",
                { imports: ['Promise'],
                  meter: comcon.meter({ timeoutMs: 300 }) });
            try { o.value = f({}); o.outcome = 'RETURNED'; }
            catch (e) { o.outcome = 'stopped'; o.code = e.code || e.name;
                        o.msg = String(e.message).substring(0, 120); }
        } catch (e) { o.driverError = String(e && e.message); }
        o.ms = Date.now() - t0;
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* The same runaway loop under a LONG meter, which is what separates the two
 * bounds.  The deadline is the real one and would stop this in 30 seconds; the
 * JOB CAP is what stops it in well under one -- and reports the honest outcome
 * (a promise still pending) instead of a deadline abort, which is the more
 * useful of the two messages. */
if (l.path === '/spinlong') {
    l.handler = function (req) {
        var o = {};
        var t0 = Date.now();
        try {
            var f = comcon.include(
                "async function(a){ var n = 0;"
              + " for (;;) { n = await Promise.resolve(n + 1); } }",
                { imports: ['Promise'],
                  meter: comcon.meter({ timeoutMs: 30000 }) });
            try { o.value = f({}); o.outcome = 'RETURNED'; }
            catch (e) { o.outcome = 'stopped'; o.code = e.code || e.name; }
        } catch (e) { o.driverError = String(e && e.message); }
        o.ms = Date.now() - t0;
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(16);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {};
    };
    return $o;
}

my $o = get_json('/async');
is($o->{driverError}, undef, 'the async probe ran') or diag($o->{driverError});

is($o->{plain}{value}, 42,
   'an ASYNC fragment is admitted and its promise settled: it used to be '
   . 'refused as "not a bytecode function", which is not true of an async '
   . 'function -- the engine has a four-class helper and six COMCON analysis '
   . 'entry points tested one class id instead of asking it')
    or diag('plain: ' . encode_json($o->{plain}));

is($o->{awaited}{value}, 10,
   'an await over an already-settled promise works: the compartment\'s OWN '
   . 'jobs are drained, and it has its own runtime, so no host job can be '
   . 'scheduled by that loop')
    or diag('awaited: ' . encode_json($o->{awaited}));

is($o->{chained}{value}, 10,
   'a chain of five awaits settles too, so the drain is a loop and not one job')
    or diag('chained: ' . encode_json($o->{chained}));

is($o->{freeName}{code}, 'E_ADMIT_FREENAME',
   'AN UNDECLARED FREE NAME INSIDE AN ASYNC BODY IS STILL REFUSED -- which is '
   . 'the assertion that says admission is READING the body. Had the gate '
   . 'merely stopped objecting to async functions, this would be admitted and '
   . 'the analysis would be absent exactly where it just became reachable')
    or diag('freeName: ' . encode_json($o->{freeName}));

is($o->{declared}{value}, 'undefined',
   '...and the same name DECLARED gets past the manifest and runs -- reading '
   . 'undefined, because declaring a name admits the fragment and does not '
   . 'grant the capability. So the refusal above is the manifest working, not '
   . 'async being refused by some other route')
    or diag('declared: ' . encode_json($o->{declared}));

like($o->{rejected}{msg} || '', qr/nope/,
   'a REJECTION takes the throw path, carrying the fragment\'s own message')
    or diag('rejected: ' . encode_json($o->{rejected}));
like($o->{rejected}{msg} || '', qr/comcon-fragment/,
   '...and its origin, which an operator needs more for an async fragment than '
   . 'for a synchronous one')
    or diag('rejected: ' . encode_json($o->{rejected}));

is($o->{neverSettles}{code}, 'E_INVOKE_PENDING',
   'a promise NOTHING IN REACH CAN SETTLE is reported, not waited on: a '
   . 'compartment reaches no timer and no socket, so an await on anything '
   . 'outside it is an await on something that will never arrive. The '
   . 'alternative was JSON.stringify on a pending promise -- "{}", a '
   . 'plausible-looking empty object, which is the worst of the three answers')
    or diag('neverSettles: ' . encode_json($o->{neverSettles}));

is($o->{returnsPending}{code}, 'E_INVOKE_PENDING',
   '...and a NON-async fragment that returns a pending promise is the same '
   . 'case, because the marshalling is what cannot proceed, not the syntax')
    or diag('returnsPending: ' . encode_json($o->{returnsPending}));

is($o->{sync}{value}, 'plain',
   'a synchronous fragment is untouched: the promise path is entered only when '
   . 'the result IS a promise');

is($o->{generatorFree}{code}, 'E_ADMIT_FREENAME',
   'a GENERATOR body is analysed too -- the class widening was one fix for one '
   . 'defect at six sites, not a special case for async');

# --- the runaway microtask loop ---
my $sp = get_json('/spin');
is($sp->{outcome}, 'stopped',
   'a fragment that queues microtasks FOREVER is stopped: the interrupt handler '
   . 'belongs to the compartment runtime, so the drain loop is bounded by the '
   . 'same clock that stops a `while (1)` -- draining jobs did not open a way '
   . 'around the deadline')
    or diag('spin: ' . encode_json($sp));
cmp_ok($sp->{ms}, '<', 3000,
   '...and stopped by the fragment\'s OWN 300ms meter rather than by the 5s '
   . 'default, so it is the metered deadline doing it')
    or diag('spin ms: ' . ($sp->{ms} // 'undef'));

# --- and the bound that is NOT the deadline ---
my $sl = get_json('/spinlong');
is($sl->{code}, 'E_INVOKE_PENDING',
   'THE TWO BOUNDS ARE DIFFERENT BOUNDS. The same runaway loop under a 30-second '
   . 'meter is stopped by the JOB CAP, and reports the honest outcome -- a '
   . 'promise still pending -- rather than a deadline abort, which is the less '
   . 'useful of the two messages')
    or diag('spinlong: ' . encode_json($sl));
cmp_ok($sl->{ms}, '<', 5000,
   '...in well under a second, where the deadline would have taken thirty: '
   . 'without the cap the drain loop would run until the clock, and an operator '
   . 'would be told their fragment timed out rather than that it awaited '
   . 'something nothing can settle')
    or diag('spinlong ms: ' . ($sl->{ms} // 'undef'));

$t->stop();
