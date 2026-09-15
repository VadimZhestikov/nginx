#!/usr/bin/perl

# F15, PHASE 2 — a fragment's own text cannot escape the wrapper it is
# compiled inside, and reach admission with no gate ever applied to it.
#
# comcon.include() builds "(function(g0,...){\"use strict\";return(" + source
# + ")})" and compiles the WHOLE buffer as one script.  If `source` closes
# that function expression early -- an unbalanced `)}` sitting inside what
# looks like a string, comment or template literal -- and supplies more
# script-level code afterward, that code used to run with NO ADMISSION GATE
# EVER APPLIED TO IT, no matter how strict the contract asked to be:
# admission only ever inspected the RESULT (the returned function), never the
# rest of the script that produced it.
#
# MEASURED, before this fix: `imports: []` (maximally strict) admitted a
# fragment whose escaped text read another fragment's declared free names and
# reassigned a shared intrinsic (`Promise = evil`) for every fragment, not
# just its own -- the same corruption F15 phase 1 closed for ADMITTED,
# non-breakout bodies, reachable again through a different door.
#
# TWO WRAPPER SHAPES, and the naive fix (count nested closures in the
# compiled unit's constant pool; must be exactly one) is sufficient for one of
# them and not the other -- found empirically, not assumed, by disabling the
# opcode check and watching exactly one class of probe below start passing.
#
# The FRAGMENT wrapper is itself function-shaped -- "(function(g0,...){...})"
# -- so breaking out of it always consumes that closure and needs a NEW one to
# keep the completion value callable: counting nested closures (must be
# exactly one) already catches every variant of it. contract.tests is wrapped
# in BARE PARENS -- "(SRC)" -- with no function shape to consume, so a comma
# expression can smuggle in a bare SIDE EFFECT with NO second closure at all:
# `(1), (globalThis.__x = 1), (function(fragment){ return true; })` has only
# ONE function anywhere in it, and the closure count alone does not see it.
# Closing that gap needed the root's own OPCODE SEQUENCE checked, not just its
# constant pool: the legitimate shape compiles to exactly "create the one
# closure; store it; return" (verified against this engine's actual compiler
# output, not a second parser), and the side effect's own opcodes appear
# before that tail regardless of which wrapper it targets -- applied
# uniformly to both shapes, since a mechanism that only covers the shape it
# was found in is a mechanism waiting to be found wanting again.
#
# THE SAME PROTECTION IS APPLIED TO contract.tests, built the same way
# ("(" + tests_source + ")"), for consistency -- but as a REFUSAL rather than
# a silent skip: that field already has one silent-skip path (a string that
# does not compile to a function at all), and turning a breakout into a
# second one would make a test that looks like it validates something quietly
# not run, which is worse than refusing loudly for a phase whose whole point
# is verifying a fragment's behaviour.
#
# WHAT MUST KEEP WORKING: an IIFE as the fragment body -- a common, legitimate
# shape for building local state -- has its own internal structure entirely
# NESTED inside the one wrapper closure, invisible to this check regardless of
# how many closures IT creates internally.

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

        location /breakout { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var l = nginx.http.servers[0].locations[0];

function attempt(o, name, src, contract) {
    try {
        var f = comcon.include(src, contract);
        o[name] = { included: true, result: f ? f({}) : null };
    } catch (e) {
        o[name] = { refused: String(e && e.message).substring(0, 160) };
    }
}

l.handler = function (req) {
    var o = {};

    /* CLASS 1: closes the wrapper, then supplies a SECOND function
     * expression so the completion value stays callable -- caught by the
     * constant-pool count alone. Under {} (no admission at all: the
     * strictest reading of "no gate" would be no free-name check either). */
    attempt(o, 'commaSecondClosure',
        "0)}), (globalThis.__f15a = 'planted'), (function(){return"
      + "(function(a){ return typeof globalThis.__f15a; }", {});

    /* A second variant on the FRAGMENT wrapper (still needs a replacement
     * closure, so the constant-pool count alone already catches this one --
     * distinct from the tests-field case below only in which wrapper it
     * targets). Under `imports: []`, the STRICTEST contract an operator can
     * write. */
    attempt(o, 'commaBreakoutStrict',
        "0)}), (globalThis.__f15b = 'planted'), (function(){return"
      + "(function(a){ return typeof globalThis.__f15b; }", { imports: [] });

    /* CLASS 3: a statement-level breakout (declaration, not an expression). */
    attempt(o, 'statementBreakout',
        "0)}); var __f15c = 'planted'; (function(){return"
      + "(function(a){ return typeof __f15c; }", { imports: [] });

    /* THE ORIGINAL SEVERITY: reassigning a shared intrinsic through the
     * breakout, under the strictest contract, and a victim reading it. */
    attempt(o, 'shadowThroughBreakout',
        "0)}), (Promise = function(){ return 'EVIL'; }), (function(){return"
      + "(function(a){ return 1; }", { imports: [] });
    attempt(o, 'shadowVictim',
        "function(a){ return String(Promise).substring(0, 8); }",
        { imports: ['Promise'] });

    /* THE SAME PROTECTION ON contract.tests: a bare side-effect breakout in
     * the test source, which must be REFUSED (not silently skipped). */
    attempt(o, 'testsBreakout',
        "function(a){ return 1; }",
        { tests: "1), (globalThis.__f15d = 'planted'), "
               + "(function(fragment){ return true; }" });

    /* ORDINARY, LEGITIMATE SHAPES MUST STILL WORK. */
    attempt(o, 'plainFunction', "function(a){ return 1; }", { imports: [] });
    attempt(o, 'iifeBody',
        "(function(){ var keep = [1,2,3];"
      + " return function(a){ return keep.length; }; })()", { imports: [] });
    attempt(o, 'iifeCalledImmediately',
        "(function(){ return function(a){ return 42; }; })()", {});
    attempt(o, 'manyGrantParams', "function(a){ return 1; }", { imports: [] });
    attempt(o, 'validTests', "function(a){ return 7; }",
        { tests: "function(fragment){ if (fragment({}) !== 7) "
               + "throw new Error('bad'); }" });

    req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(11);

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

my $o = get_json('/breakout');

like($o->{commaSecondClosure}{refused} // '', qr/single function expression/,
   'CLASS 1 (second closure) is refused, even with NO admission at all: '
   . '`{}` used to mean zero gate applied to the escaped text')
    or diag('commaSecondClosure: ' . encode_json($o->{commaSecondClosure} || {}));

like($o->{commaBreakoutStrict}{refused} // '', qr/single function expression/,
   'THE SAME BREAKOUT IS REFUSED UNDER THE STRICTEST CONTRACT TOO '
   . '(`imports: []`) -- admission never got a chance to matter either way, '
   . 'because the shape check runs first and unconditionally')
    or diag('commaBreakoutStrict: ' . encode_json($o->{commaBreakoutStrict} || {}));

like($o->{statementBreakout}{refused} // '', qr/single function expression/,
   'CLASS 3 (statement-level breakout, a declaration) is refused too')
    or diag('statementBreakout: ' . encode_json($o->{statementBreakout} || {}));

like($o->{shadowThroughBreakout}{refused} // '', qr/single function expression/,
   'THE ORIGINAL SEVERITY IS CLOSED AT THE SOURCE: reassigning Promise through '
   . 'the breakout is refused before any of the escaped text ever runs -- not '
   . 'merely caught afterward by phase 1\'s frozen binding')
    or diag('shadowThroughBreakout: ' . encode_json($o->{shadowThroughBreakout} || {}));

is($o->{shadowVictim}{result}, 'function',
   '...and a victim fragment reading Promise afterwards sees the real one')
    or diag('shadowVictim: ' . encode_json($o->{shadowVictim} || {}));

like($o->{testsBreakout}{refused} // '', qr/tests.*single function expression/,
   'THE BARE-SIDE-EFFECT CLASS IS CAUGHT ON contract.tests -- the shape the '
   . 'closure-count check alone CANNOT see, because the tests wrapper is bare '
   . 'parens with no function shape to consume, and it never needed a second '
   . 'closure of any kind. Refused, not silently skipped: `tests` already had '
   . 'one silent-skip path (a string that fails to compile at all), and a '
   . 'second one would make a test that looks like it validated something '
   . 'quietly not run -- worse than a loud refusal for a zero-blast-radius '
   . 'phase whose whole point is verifying behaviour')
    or diag('testsBreakout: ' . encode_json($o->{testsBreakout} || {}));

is($o->{plainFunction}{result}, 1, 'an ordinary fragment is unaffected');

is($o->{iifeBody}{result}, 3,
   'AN IIFE FRAGMENT BODY STILL WORKS: its own internal closures are nested '
   . 'inside the one wrapper closure, invisible to a check that only looks at '
   . 'the SCRIPT\'S top level')
    or diag('iifeBody: ' . encode_json($o->{iifeBody} || {}));

is($o->{iifeCalledImmediately}{result}, 42,
   'and a called IIFE with no imports at all still works');

is($o->{manyGrantParams}{result}, 1, 'an ordinary fragment is unaffected (2)');

is($o->{validTests}{result}, 7,
   'AND A LEGITIMATE contract.tests STILL RUNS: the same shape check that '
   . 'refuses a breakout admits a bare, well-formed test function')
    or diag('validTests: ' . encode_json($o->{validTests} || {}));
