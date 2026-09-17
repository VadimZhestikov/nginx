#!/usr/bin/perl

# COMCON v5.127 -- SHOWCASE-gaps G-05 (the diff half): comcon.std.policy.diff.
#
# Two contracts compared axis by axis in the lattice the kernel uses -- masks
# by inclusion, lifetimes and deadlines by size, budgets by limit under the
# same key and window, quorums up and windows down, globs and protocols by
# identity only.  The verdict is `narrowing` (auto-safe: nothing gained
# authority), `widening`, `unchanged`, or `incomparable` (a change no order
# relates), and every change is listed with its direction.
#
# THE READING IS THE KERNEL'S, NOT A SECOND ONE: the grant translation
# (polOf) is the same function include() feeds the C side, factored out for
# this -- two copies of a kind/mask mapping is how a narrower contract reads as
# wider in a report.  An include result and a bindAt handle carry their
# contract (`.contract`, a frozen copy), so a live binding can be diffed
# against a candidate without a second source of truth.
#
# NEGATIVE CONTROLS (run while writing; each restored):
#   - setRel returns NARROW for a superset -> tests 2 and 4 fail
#   - a mixed narrow+widen reported as `narrowing` -> test 6 fails
#   - `.contract` not attached to include results -> test 8 fails

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
        location /d { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var D = comcon.std.policy.diff;

function socket(flavors) { var m = sock, i;
    for (i = 0; i < flavors.length; i++) { m = comcon.mediate(m, flavors[i]); } return m; }

var base = { imports: ["JSON"], grants: { s: socket([comcon.allow(["address", "port"])]) },
             meter: comcon.meter({ timeoutMs: 200 }), checkRequest: true };

locs.find(function (l) { return l.path === "/d"; }).handler = function (req) {
    var o = {};

    /* narrowing on four axes at once */
    var narrower = { imports: ["JSON"],
        grants: { s: socket([comcon.allow(["port"]), comcon.ttl(600)]) },
        meter: comcon.meter({ timeoutMs: 100 }), checkRequest: true, tests: "function(f){}" };
    var n = D(base, narrower);
    o.narrow = { verdict: n.verdict, autoSafe: n.autoSafe,
                 paths: n.changes.map(function (c) { return c.path + ':' + c.direction; }) };

    /* widening: a name, a grant, a field, a longer deadline, the request check off */
    var wider = { imports: ["JSON", "nginx"],
        grants: { s: sock, out: comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.x.com")) },
        meter: comcon.meter({ timeoutMs: 500 }), checkRequest: false };
    var w = D(base, wider);
    o.widen = { verdict: w.verdict, autoSafe: w.autoSafe,
                paths: w.changes.map(function (c) { return c.path + ':' + c.direction; }) };

    /* unchanged */
    o.same = D(base, base).verdict;

    /* incomparable: two globs no order relates; a budget under another key */
    var g1 = { imports: [], grants: { out: comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.a.com")) } };
    var g2 = { imports: [], grants: { out: comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.b.com")) } };
    o.glob = D(g1, g2).verdict;
    var b1 = { imports: [], grants: { s: socket([comcon.uses("k1", 10, 60)]) } };
    var b2 = { imports: [], grants: { s: socket([comcon.uses("k2", 5, 60)]) } };
    var b3 = { imports: [], grants: { s: socket([comcon.uses("k1", 5, 60)]) } };
    o.budgetKey = D(b1, b2).verdict;
    o.budgetLower = D(b1, b3).verdict;

    /* mixed: one axis narrows, another widens -> not auto-safe, reported as incomparable */
    var mixed = { imports: ["JSON", "nginx"], grants: base.grants, meter: comcon.meter({ timeoutMs: 50 }), checkRequest: true };
    var m = D(base, mixed);
    o.mixed = { verdict: m.verdict, autoSafe: m.autoSafe };

    /* the posture word: deny -> audit weakens */
    o.posture = D({ imports: [], onViolation: "deny" }, { imports: [], onViolation: "audit" }).changes[0];

    /* an include result carries its contract; so does a bindAt handle */
    var f = comcon.include("function(a){ return typeof s; }", base);
    o.carried = D(f, f).verdict;
    o.carriedImports = f.contract.imports;
    o.frozen = Object.isFrozen(f.contract);
    var h = comcon.bindAt(function () {}, comcon.quote("function(){ return 1; }"), { imports: [], meter: comcon.meter({ timeoutMs: 300 }) });
    o.handle = D(h, { imports: [], meter: comcon.meter({ timeoutMs: 100 }) }).verdict;

    /* a revoked grant vs a live one */
    o.revoked = D({ imports: [], grants: { s: sock } }, { imports: [], grants: { s: socket([comcon.revoke()]) } }).verdict;

    /* cosign: a larger quorum in a shorter window narrows; another principal is incomparable */
    var c1 = { imports: [], grants: { out: comcon.mediate(comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.a.com")),
                  comcon.cosign({ key: "k", quorum: 2, within: 900, as: "alice" })) } };
    var c2 = { imports: [], grants: { out: comcon.mediate(comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.a.com")),
                  comcon.cosign({ key: "k", quorum: 3, within: 600, as: "alice" })) } };
    var c3 = { imports: [], grants: { out: comcon.mediate(comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.a.com")),
                  comcon.cosign({ key: "k", quorum: 2, within: 900, as: "bob" })) } };
    o.cosignNarrow = D(c1, c2).verdict;
    o.cosignOther = D(c1, c3).verdict;

    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(14);

my $b = http_get('/d');

like($b, qr/"narrow":\{"verdict":"narrowing","autoSafe":true,"paths":\["grants\.s\.fields:narrowing","grants\.s\.ttlSeconds:narrowing","meter\.timeoutMs:narrowing","tests:narrowing"\]\}/,
     'four narrowings, each named with its direction; auto-safe');
like($b, qr/"widen":\{"verdict":"widening","autoSafe":false,"paths":\["imports:widening","grants\.s\.fields:widening","grants\.out:widening","meter\.timeoutMs:widening","checkRequest:widening"\]\}/,
     'five widenings, each named; not auto-safe');
like($b, qr/"same":"unchanged"/, 'a contract against itself is unchanged');
like($b, qr/"glob":"incomparable"/, 'two host globs are not ordered: incomparable, never guessed');
like($b, qr/"budgetKey":"incomparable","budgetLower":"narrowing"/,
     'a budget under another key is incomparable; a lower limit under the same key narrows');
like($b, qr/"mixed":\{"verdict":"incomparable","autoSafe":false\}/,
     'one axis narrowing and another widening is not auto-safe');
like($b, qr/"posture":\{"path":"onViolation","from":"deny","to":"audit","direction":"widening"\}/,
     'deny -> audit is a widening of the posture');
like($b, qr/"carried":"unchanged","carriedImports":\["JSON"\],"frozen":true/,
     'an include result carries a frozen copy of its contract');
like($b, qr/"handle":"narrowing"/, 'a bindAt handle carries its contract too');
like($b, qr/"revoked":"narrowing"/, 'a grant revoked is a narrowing');
like($b, qr/"cosignNarrow":"narrowing"/, 'a larger quorum within a shorter window narrows');
like($b, qr/"cosignOther":"incomparable"/, 'a cosignature as another principal is incomparable');
unlike($b, qr/"error"/, 'no path threw');
like($b, qr/"verdict":"narrowing"/, 'the narrowing verdict is the auto-safe one the rollout keys on');
