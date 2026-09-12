#!/usr/bin/perl

# comcon.include(source, contract) — the contract's own shape.
#
# The last unfuzzed entry point of the confined path, and the one I rated
# lowest-yield: arguments cross by JSON marshalling and the contract is
# validated in JS, so the C surface looked thin.  It was not the C surface that
# was wrong.
#
# Admission is opt-in: a contract that names imports/identity/checkRequest/tests
# gets the C3 gate (free names deny-by-default, dynamic code refused, optional
# request-field and identity checks); a contract that names none of them gets no
# gate, deliberately, for backward compatibility with un-admitted fragments.
#
# The trap was in how "names imports" was decided, in two places at once:
#
#   JS   `if (contract.imports || ...)` — TRUTHINESS, so `imports: ''` and
#        `imports: 0` read as "no admission asked for".
#   C    `if (JS_IsObject(imp_h))` — and that one condition governed the WHOLE
#        admission block, free names AND the dynamic-code denial AND
#        checkRequest.  So `imports: 42` built an admit descriptor and then
#        skipped the gate entirely.
#
# MEASURED on the old code, with a fragment whose body is `eval("1+1")`:
#
#     include(src, {imports: []})       ->  refused        (correct)
#     include(src, {imports: 42})       ->  ADMITTED
#     include(src, {imports: 'nginx'})  ->  ADMITTED
#     include(src, {imports: ''})       ->  ADMITTED
#     include(src, {imports: 0})        ->  ADMITTED
#
# eval is denied by the gate regardless of what is in the imports list, so its
# admission proves the gate did not run at all rather than running vacuously.
# A contract that looks stricter than it is, is worse than no contract: the
# caller believes there is a manifest.
#
# Now a malformed imports means NO NAMES GRANTED — the strictest reading, and
# the fail-closed direction, which is what the C admit check already did for a
# non-object once it was reached.
#
# PINNED BY DESIGN, not defects: `imports: undefined` and a contract with no
# admission fields at all still compile ungated.  That is the documented opt-in,
# and the test asserts it so the distinction stays deliberate.

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

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        location /c { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path !== '/c') { continue; }
    locs[li].handler = function (req) {
        var out = { gated: [], ungated: [], errors: 0 };

        /* FREE uses an ungranted free name; EVAL uses dynamic code, which the
         * gate denies whatever the imports list says -- so EVAL is the probe
         * that distinguishes "gate ran and allowed it" from "gate never ran". */
        var FREE = 'function(q){ return nginx.version; }';
        var EVAL = 'function(q){ return eval("1+1"); }';

        function admits(src, contract) {
            try { comcon.include(src, contract); return true; }
            catch (e) { return false; }
        }

        /* Every one of these contracts MENTIONS imports, so every one of them
         * has asked for admission and must get it. */
        var malformed = [
            ['emptystring', ''],
            ['zero',        0],
            ['false',       false],
            ['null',        null],
            ['nan',         NaN],
            ['string',      'nginx'],
            ['number',      42],
            ['true',        true],
            ['emptyobject', {}],
            ['emptyarray',  []]
        ];

        for (var i = 0; i < malformed.length; i++) {
            var tag = malformed[i][0], v = malformed[i][1];
            try {
                var freeOk = admits(FREE, { imports: v });
                var evalOk = admits(EVAL, { imports: v });
                if (freeOk || evalOk) {
                    out.ungated.push(tag + (freeOk ? ':free' : '') +
                                          (evalOk ? ':eval' : ''));
                } else {
                    out.gated.push(tag);
                }
            } catch (e) { out.errors++; }
        }

        /* the opt-in boundary, pinned as deliberate */
        out.absentContract   = admits(EVAL, {});
        out.importsUndefined = admits(EVAL, { imports: undefined });

        /* and the gate must still ADMIT what it should */
        out.listedName   = admits(FREE, { imports: ['nginx'] });
        out.cleanFragment = admits('function(q){ return q.a; }', { imports: [] });

        /* another field asking for admission, with a broken imports */
        out.checkRequestGates = !admits(EVAL, { checkRequest: true, imports: '' });

        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(out));
    };
}
JS

$t->try_run('no js module')->plan(9);

my $r = http_get('/c');
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

ok($j, 'the contract battery ran') or diag substr($body // 'no body', 0, 300);

SKIP: {
    skip 'no response', 8 unless $j;

    diag 'gated:   ' . join(', ', @{ $j->{gated}   || [] });
    diag 'ungated: ' . join(', ', @{ $j->{ungated} || [] });

    # work verification: ten contracts, each tried with two fragments
    is(scalar(@{ $j->{gated} || [] }) + scalar(@{ $j->{ungated} || [] }), 10,
       'all ten malformed contracts were tried');
    is($j->{errors}, 0, 'none of them threw outside the include call');

    # THE assertion: a contract that mentions imports gets a gate
    is(scalar @{ $j->{ungated} || [] }, 0,
       'every contract that mentions imports is gated, however malformed')
        or diag '  ungated: ' . join("\n  ", @{ $j->{ungated} || [] });

    # over-refusal controls: the gate must still admit what it should, or a
    # "fix" that refused everything would satisfy the assertion above
    ok($j->{listedName},
       'control: a free name listed in imports is still admitted');
    ok($j->{cleanFragment},
       'control: a fragment with no free names is still admitted');
    ok($j->{checkRequestGates},
       'checkRequest alone still turns the gate on');

    # the opt-in boundary, pinned so it stays a decision
    ok($j->{absentContract},
       'a contract with no admission fields is still ungated (opt-in, by design)');
    ok($j->{importsUndefined},
       'imports: undefined reads as absent, like no contract at all');
}
