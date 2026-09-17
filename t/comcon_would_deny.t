#!/usr/bin/perl

# COMCON v5.127 -- SHOWCASE-gaps G-05 (the would-deny half): the gates that
# fired are attributed to the BINDING they fired in.
#
# nginx.tenantDenials() counts per worker, per gate: "sock.listener fired 41
# times" with no way to say for which of three tenants.  Now the invoke sets a
# pointer to the running fragment's own counters for the duration of the call
# (restored after it, nested for a sub-fragment), and the compartment's one
# counting site increments both.  comcon.denials(f) reads a fragment's rows;
# ops.wouldDeny(f) reads them against its posture, so under `onViolation:
# "audit"` the nonzero rows are exactly what enforce WOULD have refused -- the
# per-binding would-deny list the rehearsal scenario (SHOWCASE 14) asked for.
#
# THE ATTRIBUTION IS WHAT THIS FILE TESTS, not the counting: a sibling that
# never reached is at zero while the fleet counter grew, and a sub-fragment's
# reaches (author tier, its own handle) do not land on its parent.
#
# NEGATIVE CONTROL: t/tools/controls/denials-not-attributed.patch removes the
# per-fragment increment -- every fragment reads zero, tests 2-6 fail while the
# fleet counter (test 1) still grows.

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
        location /w { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

var REACH = "function(a){ var i, n = 0; for (i = 0; i < a.n; i++) { if (s.listener === null) { n++; } } return n; }";

var audited = comcon.include(REACH, { imports: [], grants: { s: sock }, onViolation: "audit" });
var denied  = comcon.include(REACH, { imports: [], grants: { s: sock }, onViolation: "deny" });
var quiet   = comcon.include(REACH, { imports: [], grants: { s: sock } });

/* a parent that reaches once itself and hands a copy of its wrapper to a
   sub-fragment that reaches twice: the sub's rows must not be the parent's */
var parent = comcon.include(
    "function(a){ var sub = author.include('function(b){ var i, n = 0; for (i = 0; i < 2; i++) { if (s.listener === null) { n++; } } return n; }',"
  + "  { imports: [], grants: { s: s } });"
  + "  var mine = (s.listener === null) ? 1 : 0; return { mine: mine, sub: sub({}) }; }",
    { imports: [], grants: { s: sock, author: comcon.author({ subFragments: 1 }) } });

var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode });

locs.find(function (l) { return l.path === "/w"; }).handler = function (req) {
    var o = {};
    comcon.mode('enforce');
    var fleet0 = nginx.tenantDenials().byOp['sock.listener'];

    o.auditedRan = audited({ n: 3 });
    o.deniedRan  = denied({ n: 2 });

    o.fleetGrew = nginx.tenantDenials().byOp['sock.listener'] - fleet0;
    o.audited = comcon.denials(audited);
    o.denied  = comcon.denials(denied);
    o.quiet   = comcon.denials(quiet).total;

    var wd = ops.wouldDeny(audited);
    o.wouldDeny = { posture: wd.posture, observing: wd.observing, events: wd.events, fleet: wd.fleet };
    var dd = ops.wouldDeny(denied);
    o.did = { posture: dd.posture, observing: dd.observing, total: dd.total };

    var f1 = nginx.tenantDenials().byOp['sock.listener'];
    o.parentRan = parent({});
    o.parentGrewFleet = nginx.tenantDenials().byOp['sock.listener'] - f1;
    o.parent = comcon.denials(parent).byOp['sock.listener'];

    try { comcon.denials(function () {}); o.plain = 'ACCEPTED'; }
    catch (e) { o.plain = e.name; }

    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(10);

my $b = http_get('/w');

like($b, qr/"fleetGrew":5/, 'the fleet counter grew by every reach (3 + 2), as before');
like($b, qr/"audited":\{"total":3,"byOp":\{"sock\.listener":3,/,
     'the audited binding owns its three');
like($b, qr/"posture":"audit"\}/, '...and reads its posture back');
like($b, qr/"denied":\{"total":2,"byOp":\{"sock\.listener":2,/,
     'the deny binding owns its two');
like($b, qr/"quiet":0/, 'a sibling that never reached is at zero: attribution, not the fleet counter');
like($b, qr/"wouldDeny":\{"posture":"audit","observing":true,"events":\[\{"op":"sock\.listener","n":3\}\],"fleet":"enforce"\}/,
     'ops.wouldDeny: under audit the rows are what enforce would have refused');
like($b, qr/"did":\{"posture":"deny","observing":false,"total":2\}/,
     '...and under deny they are what it did refuse');
like($b, qr/"auditedRan":3,"deniedRan":2/, 'audit allowed nothing new: the reach edge is null in both postures');
like($b, qr/"parentRan":\{"mine":1,"sub":2\},"parentGrewFleet":3,"parent":1/,
     "a sub-fragment's two reaches count to the fleet, not to its parent's row");
like($b, qr/"plain":"TypeError"/, 'a plain function has no rows');
