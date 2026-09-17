#!/usr/bin/perl

# COMCON V11 — policy mutation testing
# (VERIFICATION.md: "widen-one-permit mutants must be killed by the deny-suite").
#
# A negative control asks "does this test fail when I break the CODE". Mutation
# testing asks the harder question: DOES THE SUITE NOTICE WHEN THE POLICY GETS
# WEAKER? For a capability system that is the question that matters, because a
# regression there does not look like a crash -- it looks like a permit nobody
# asked for. Every defect found by hand this month had exactly that shape: a
# typo'd mediation flavor that granted FULL authority, a mode switch that
# reported success and changed nothing, an admission gate that refused ordinary
# JS. A surviving mutant is the same finding, arrived at mechanically.
#
# HOW IT WORKS. One base policy; a deny-suite of probes whose outcomes under that
# policy are recorded; then each mutant from t/tools/policy-mutants.js is run
# through the same probes. A mutant is KILLED if any probe's outcome differs.
# A mutant that changes nothing SURVIVES, and a survivor is the finding: the
# policy grants one more permit and the suite cannot tell.
#
# EQUIVALENT MUTANTS ARE DECLARED, NOT DISCOVERED, and asserted in the other
# direction: `imports+eval` cannot widen anything (the deny list refuses eval
# whatever a manifest says), so it MUST survive. If it were killed, the suite
# would be reporting a permit that does not exist. Two-sided, so neither a lazy
# suite nor an over-eager one passes.

# NEGATIVE CONTROLS (run 2026-09-12). They mutate the HARNESS, not the engine,
# because V11's subject is the suite:
#
#   drop the probe that observes the address -> mask+address survives (tests 2, 5-6)
#   make every probe report one outcome      -> everything survives (tests 2-6)
#   declare a real widening as equivalent    -> equivKilled is non-empty (6, 8)
#
# WHAT THIS RUN ALREADY FOUND, on its first execution: it killed `imports+JSON`,
# which I had labelled EQUIVALENT. The label was wrong -- `intrinsics` removes
# the no-declaration free pass, it does not stop a name from being DECLARED, so
# `{intrinsics: [], imports: ['JSON']}` permits JSON after all. The technique
# corrected a belief I held about a policy I had written two commits earlier,
# which is the whole argument for having it.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $gen = do { open my $f, '<', 'tools/policy-mutants.js' or die $!; local $/; <$f> };

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

        location /v11 { }
    }
}
EOF

$t->write_file_expand('root.js', <<"JS");
/* ===== the mutation generator, verbatim from t/tools/policy-mutants.js ===== */
$gen
/* ===== end of the generator ===== */

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;
var l = locs.find(function (x) { return x.path === "/v11"; });

/* THE BASE POLICY: a mediated capability, a manifest, no language intrinsics,
 * request fields sealed, and a meter. Every mutant below relaxes exactly one
 * of those. */
var BASE = { mediated: true, allow: ['port'], imports: ['s'],
             intrinsics: [], checkRequest: true, meterMs: 100 };

function contractOf(p) {
    var cap = p.mediated ? comcon.mediate(sock, comcon.allow(p.allow)) : sock;
    var c = { grants: { s: cap }, imports: p.imports,
              checkRequest: p.checkRequest };
    if (p.intrinsics !== undefined) { c.intrinsics = p.intrinsics; }
    if (p.meterMs) { c.meter = comcon.meter({ timeoutMs: p.meterMs }); }
    return c;
}

/* THE DENY-SUITE. Each probe states what the base policy must NOT allow; the
 * runner records the OUTCOME rather than a verdict, so a mutant is killed by any
 * observable difference -- including one the author of the probe did not have
 * in mind. */
var PROBES = [
    { name: 'address', src: "function(req){ return { v: typeof s.address }; }" },
    { name: 'fd',      src: "function(req){ return { v: typeof s.fd }; }" },
    { name: 'listener',src: "function(req){ return { v: (s.listener === null) }; }" },
    { name: 'port',    src: "function(req){ return { v: (s.port|0) }; }" },
    { name: 'json',    src: "function(req){ return { v: JSON.stringify([1]) }; }" },
    { name: 'host',    src: "function(req){ return { v: typeof nginx }; }" },
    { name: 'undecl',  src: "function(req){ return { v: typeof nope }; }" },
    { name: 'evalref', src: "function(req){ var q = eval; return { v: 1 }; }" },
    { name: 'reqfield',src: "function(req){ return { v: typeof req.bogusField }; }" },
    /* F24 (v5.131): the old probe, `t += i` over 4e7, finished inside the
       100 ms meter on the native tier, so `meter=off` changed nothing and
       SURVIVED.  No iteration count mends an integer loop: gcc folds `t += i`
       into a closed form, and a data-dependent one runs eighteen times faster
       native than interpreted (measured: 30M steps, 32 ms vs 574 ms), so a
       count that outlasts the meter native takes the interpreter tens of
       seconds with the meter off.  A property read per iteration goes through
       the runtime on both tiers and keeps them within a factor of two
       (measured: 180 ms vs 296 ms at 30M); 5e7 of them is ~0.3 s native, ~0.5 s
       interpreted -- the meter cuts both, and without it both finish. */
    { name: 'spin',    src: "function(req){ var o = { x: 3 }, t = 1, i;"
                          + " for (i = 0; i < 50000000; i++) { t = (t + o.x * i) | 0; }"
                          + " return { v: 'ran' }; }" }
];

function outcome(policy, probe) {
    var f;
    try { f = comcon.include(probe.src, contractOf(policy)); }
    catch (e) { return 'refused'; }
    try { return 'ok:' + JSON.stringify(f({})); }
    catch (e) { return 'threw'; }
}

function runSuite(policy) {
    return PROBES.map(function (p) { return p.name + '=' + outcome(policy, p); });
}

l.handler = function (req) {
    var o = {}, i;

    var baseline = runSuite(BASE);
    o.baseline = baseline;

    var ms = mutants(BASE), killed = [], survived = [], equivSurvived = [],
        equivKilled = [];

    var equivDiff = {};
    for (i = 0; i < ms.length; i++) {
        var got = runSuite(ms[i].policy);
        var differs = (got.join('|') !== baseline.join('|'));
        if (ms[i].equivalent) {
            (differs ? equivKilled : equivSurvived).push(ms[i].label);
            /* say WHAT differed: an equivalent mutant killed is a finding, and
               a finding without its outcome is a guess */
            if (differs) { equivDiff[ms[i].label] = got.filter(function (g, k) { return g !== baseline[k]; }); }
        } else if (differs) {
            killed.push(ms[i].label);
        } else {
            survived.push(ms[i].label + ' (' + ms[i].why + ')');
        }
    }

    o.total = ms.length;
    o.killed = killed;
    o.survived = survived;
    o.equivSurvived = equivSurvived;
    o.equivDiff = equivDiff;
    o.equivKilled = equivKilled;

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(8);

###############################################################################

my $r = http_get('/v11');

# --- the instrument, before its verdict ----------------------------------
like($r, qr/"total":13/, 'the generator produced every widen-one-permit mutant');
# The body is JSON inside JSON, so the inner quotes are backslash-escaped.
like($r, qr/address=ok:\{\\"v\\":\\"undefined/,
     'the base policy hides the address (the suite has something to lose)');
like($r, qr/"json=refused"/,
     '...refuses JSON under the intrinsics narrowing');
like($r, qr/"host=refused"/, '...and refuses a host name');

# --- the verdict ---------------------------------------------------------
like($r, qr/"survived":\[\]/,
     'EVERY widen-one-permit mutant is KILLED: relaxing any single permit in '
     . 'the policy changes an outcome the deny-suite observes');

like($r, qr/"killed":\["mask\+address","mask\+fd","mask\+listener","mask=FULL","unmediated","imports\+nope","imports\+nginx","intrinsics\+JSON","intrinsics=off","checkRequest=off","meter=off","imports\+JSON"\]/,
     '...all twelve of them, by name -- including `imports+JSON`, which I had '
     . 'labelled EQUIVALENT until this run killed it: declaring a name the '
     . 'narrowing excluded re-admits it, because `intrinsics` removes the '
     . 'no-declaration free pass rather than the ability to declare');

# --- the classification is checked in BOTH directions --------------------
like($r, qr/"equivSurvived":\["imports\+eval"\]/,
     'the EQUIVALENT mutant survives, as it must: the deny list refuses eval '
     . 'whatever a manifest says, so it grants no permit, and a suite that '
     . 'killed it would be reporting authority that does not exist');
like($r, qr/"equivKilled":\[\]/,
     '...and none of them was killed');
