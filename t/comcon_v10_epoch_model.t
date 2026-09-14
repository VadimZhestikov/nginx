#!/usr/bin/perl

# V10 — THE EPOCH MACHINERY, MODEL-CHECKED, and the defect the model found.
#
# VERIFICATION.md §V10 asked for this and said why: the formal semantics is
# single-threaded, while the mode fan-out is a fleet-wide protocol over shared
# memory with concurrent writers, worker respawn and master reload.  "Monotone
# rollout" was a slogan with nothing behind it.  It was the last V-item that did
# not depend on the parked compiler track.
#
# WHAT THE MODEL FOUND, before any code was touched: the epoch bump was three
# separate operations from JS -- shared.get, +1, shared.set -- so two operators
# switching concurrently both read epoch N and both wrote N+1 with THEIR OWN
# mode.  The second write won the cell; the first worker kept N+1 locally with
# the mode nobody else had.
#
# THAT WOULD HAVE BEEN A TRANSIENT LOST UPDATE IF THE RECONCILER HAD NOT EARLY-
# RETURNED ON EPOCH EQUALITY.  It did, so the worker and the cell agreed on the
# only thing the reader compares, and the divergence was PERMANENT AND SILENT:
# a fleet moved to `enforce` could leave one worker in `audit` for the rest of
# its life, unshielded, with nothing anywhere to say so.
#
# AND THE FIRST FIX WAS NOT ENOUGH -- the model said so before the code was
# written.  Changing the early return from `==` to `<=` left the identical 168
# violations, because no reading rule can repair a state where the worker and the
# cell hold the same epoch.  The publish had to become atomic; the reconciler
# change is what makes the rollout MONOTONE, which is a different property.  The
# checker keeps all three arms -- pre-fix, reconciler-only, shipped -- so the
# claim "the atomic publish is necessary" is itself checked rather than asserted.
#
# This file is both halves: the model (as a standing checker, so drift is a build
# failure) and a LIVE test of the shipped protocol, because a model of code
# nobody compared against the code is a paper exercise.

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

        location /epoch { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var MODEK = '__comconMode__';

/* any fragment will do: invoking one is what runs modeReconcile() */
var touch = comcon.include("function(a){ return 1; }", { imports: [] });

locs.forEach(function (l) {
    if (l.path !== '/epoch') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            /* --- the cell's SHAPE ties the code to the model ---
             * The model's cell is "<epoch>:<mode>", which is what the atomic
             * publish writes.  A revert to the JS get/+1/set path would write
             * JSON here, so this assertion is what stops the model from
             * describing a protocol the code no longer runs. */
            comcon.mode('enforce');
            o.cell1 = String(nginx.shared.get(MODEK));
            comcon.mode('enforce');
            o.cell2 = String(nginx.shared.get(MODEK));

            var ep1 = parseInt(o.cell1.split(':')[0], 10);
            var ep2 = parseInt(o.cell2.split(':')[0], 10);
            o.monotone = (ep2 > ep1);

            /* --- MONOTONE ROLLOUT, in both directions, on the live code ---
             * An OLDER cell must be ignored: the fleet may not be walked
             * backwards by a stale publish.  The pre-fix reconciler adopted on
             * any DIFFERENCE, so it would take this. */
            nginx.shared.set(MODEK, '1:audit');
            touch({});
            o.afterOlder = nginx.tenantDenials().mode;

            /* ...and a NEWER cell must be adopted, or the rule is not "greater",
             * it is "never" -- the same probe run the other way is what makes the
             * assertion above mean something. */
            nginx.shared.set(MODEK, (ep2 + 50) + ':audit');
            touch({});
            o.afterNewer = nginx.tenantDenials().mode;

            comcon.mode('enforce');      /* the mode is per-process: put it back */
            o.restored = nginx.tenantDenials().mode;

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(7);

# --- the model, as a standing checker -------------------------------------
my $out = `python3 tools/check-epoch-model.py 2>&1`;
my $rc = $?;
diag($out);

is($rc, 0, 'V10: the model finds no violation in the SHIPPED protocol')
    or diag("check-epoch-model.py failed");
like($out, qr/PRE-FIX protocol .*\n\s+168 violation/,
     '...and it DOES find the defect in the pre-fix protocol, so it is '
     . 'discriminating -- a model that passes everything verifies nothing');
like($out, qr/RECONCILER-ONLY fix.*\n\s+\d+ violation\(s\) -- STILL BROKEN/,
     '...and it finds the reconciler-only fix still broken, which is why the '
     . 'publish is atomic rather than the early return being relaxed. The claim '
     . '"the atomic half is the necessary one" is checked, not asserted');

# --- the live protocol ----------------------------------------------------
my $raw = http_get('/epoch');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 400)); $o = {};
};
is($o->{driverError}, undef, 'the live probe ran') or diag($o->{driverError});
diag("cells: $o->{cell1} -> $o->{cell2}");

like($o->{cell1} || '', qr/^\d+:(enforce|audit|learn)$/,
     'THE LIVE CELL HAS THE SHAPE THE MODEL DESCRIBES ("<epoch>:<mode>"), which '
     . 'is what the atomic publish writes. A revert to the JS get/+1/set path '
     . 'would write JSON here -- so this is what stops the model from describing '
     . 'a protocol the code no longer runs');

is($o->{afterOlder}, 'enforce',
   'MONOTONE ROLLOUT: an OLDER cell is IGNORED, so the fleet cannot be walked '
   . 'backwards by a stale publish. The pre-fix reconciler adopted on any '
   . 'difference and would have taken it');
is($o->{afterNewer}, 'audit',
   '...and a NEWER cell IS adopted, so the rule is "greater" and not "never" -- '
   . 'the same probe run the other way is what makes the assertion above mean '
   . 'something rather than passing because nothing is ever adopted');

$t->stop();
