#!/usr/bin/perl

# COMCON V3 — executable reference semantics as a differential ORACLE
# (VERIFICATION.md: "the highest-value first step is not Coq: it is an executable
# reference implementation of the kernel rules... differentially tested against
# the real engine on every admission-relevant operation -- catching IMPLEMENTATION
# DRIFT FROM THE MODEL, which proofs of the model alone never see").
#
# The model lives in t/tools/kernel-oracle.js and is written from the RULES, not
# from the implementation: an oracle derived from the code it checks agrees with
# that code by construction and detects nothing. It shares no code with src/js
# and never calls comcon -- it is data manipulation over a description of a case.
# This file inlines it into the fixture, so there is exactly one copy of the model
# and no external interpreter to depend on (a test that skips when `node` is
# missing would be a pass that proves nothing).
#
# THE CORPUS IS GENERATED, not listed: every mediation chain crossed with every
# admission setting. Hand-picked cases test what the author already believed.
#
# TWO INSTRUMENT CHECKS come before the comparison, because "0 mismatches" is
# also what a broken harness prints: the corpus must be large, and the oracle's
# predictions must DISCRIMINATE (if it predicted one answer for everything, it
# would agree with an engine that did anything at all).

# NEGATIVE CONTROLS (run 2026-09-12): the oracle must DETECT, not merely agree.
#
#   break redact() so a membrane stops hiding -> mismatches appear
#   compute the meet as a JOIN instead of AND -> mismatches appear
#
# Both are engine mutations, not model mutations: an oracle is only worth having
# if a wrong engine makes it disagree. (A model mutation would also "fail", and
# would prove nothing about the engine.)

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $oracle = do {
    open my $f, '<', 'tools/kernel-oracle.js' or die "no oracle: $!";
    local $/; <$f>;
};

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

        location /v3 { }
    }
}
EOF

$t->write_file_expand('root.js', <<"JS");
/* ===== the model, verbatim from t/tools/kernel-oracle.js ===== */
$oracle
/* ===== end of the model ===== */

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;
var l = locs.find(function (x) { return x.path === "/v3"; });

/* The generated corpus: every mediation chain x every admission setting. */
var CHAINS = [
    [],
    [{flavor:'allow',  fields:['port']}],
    [{flavor:'allow',  fields:['address','port']}],
    [{flavor:'redact', fields:['address']}],
    [{flavor:'redact', fields:['address','port','fd','listener']}],
    [{flavor:'revoke'}],
    [{flavor:'allow',  fields:['port']}, {flavor:'allow', fields:['address','port']}],
    [{flavor:'allow',  fields:['port']}, {flavor:'allow', fields:['fd']}],
    [{flavor:'allow',  fields:['address','port']}, {flavor:'redact', fields:['address']}],
    [{flavor:'redact', fields:['port']}, {flavor:'allow', fields:['port']}],
    [{flavor:'revoke'}, {flavor:'allow', fields:['address']}],
    [{flavor:'allow',  fields:['address']}, {flavor:'revoke'}],
    [{flavor:'redcat', fields:['address']}],          /* a typo: unknown flavor */
    [{flavor:'allowHosts', hosts:['x']}]              /* not implemented        */
];
/* `reads` lists every FREE GLOBAL the probe references -- which includes the
 * intrinsic `undefined`, because the C3 gate has no intrinsics allowance: a
 * fragment comparing against `undefined` must declare it, or admission refuses.
 * The oracle found that by disagreeing; the corpus now covers both sides. */
var ADMIT = [
    { imports: undefined,          reads: ['s','undefined'] },
    { imports: ['s','undefined'],  reads: ['s','undefined'] },
    { imports: ['s'],              reads: ['s','undefined'] },
    { imports: ['s','undefined'],  reads: ['s','undefined','nope'] },
    { imports: ['s','undefined','eval'], reads: ['s','undefined','eval'] }
];

function build(chain) {                 /* fold mediate() over the chain */
    var cap = sock, i;
    for (i = 0; i < chain.length; i++) { cap = comcon.mediate(cap, chain[i]); }
    return cap;
}

/* What the ENGINE does with a case: the fragment reports which fields it can
 * read, and whether its name is bound at all. */
var PROBE = "function(){ if (typeof s === 'undefined') return {bound:false};"
          + " return { bound:true, address: typeof s.address !== 'undefined',"
          + " port: typeof s.port !== 'undefined', fd: typeof s.fd !== 'undefined',"
          + " listener: (s.listener !== undefined) }; }";
/* Written out rather than produced by string surgery on PROBE: a replace()
 * whose target does not occur returns the original, and a probe that silently
 * equals the one it was supposed to differ from turns a real divergence into a
 * passing test. (That happened here; hence o.probesDiffer below.) */
var PROBE2 = "function(){ nope;"
           + " if (typeof s === 'undefined') return {bound:false};"
           + " return { bound:true, address: typeof s.address !== 'undefined',"
           + " port: typeof s.port !== 'undefined', fd: typeof s.fd !== 'undefined',"
           + " listener: (s.listener !== undefined) }; }";
/* the deny list: no manifest re-admits `eval`, even when it is declared */
var PROBE3 = "function(){ var q = eval;"
           + " if (typeof s === 'undefined') return {bound:false};"
           + " return { bound:true, address: typeof s.address !== 'undefined',"
           + " port: typeof s.port !== 'undefined', fd: typeof s.fd !== 'undefined',"
           + " listener: (s.listener !== undefined) }; }";

function runEngine(chain, adm) {
    var out = { admitted: true, bound: [], visible: {} };
    var cap;
    try { cap = build(chain); }
    catch (e) { out.admitted = false; return out; }

    var contract = { grants: { s: cap } };
    if (adm.imports !== undefined) { contract.imports = adm.imports; }

    var src = PROBE;
    if (adm.reads.indexOf('nope') >= 0) { src = PROBE2; }
    if (adm.reads.indexOf('eval') >= 0) { src = PROBE3; }
    var f, r;
    try { f = comcon.include(src, contract); }
    catch (e) { out.admitted = false; out.why = e.message; return out; }
    try { r = f({}); }
    catch (e) { out.admitted = false; return out; }

    if (r.bound) {
        out.bound.push('s');
        out.visible.s = { address: !!r.address, port: !!r.port,
                          fd: !!r.fd, listener: !!r.listener };
    }
    return out;
}

function same(a, b) { return JSON.stringify(a) === JSON.stringify(b); }

l.handler = function (req) {
    var o = { cases: 0, mismatches: [], shapes: {} };
    var ci, ai;

    for (ci = 0; ci < CHAINS.length; ci++) {
        for (ai = 0; ai < ADMIT.length; ai++) {
            var chain = CHAINS[ci], adm = ADMIT[ai];
            var kase = { grants: [{ name: 's', mediations: chain }],
                         reads: adm.reads, imports: adm.imports };

            var want = predict(kase);
            var got  = runEngine(chain, adm);
            o.cases++;

            /* how many DISTINCT answers the model produces over the corpus --
             * an oracle that says one thing agrees with anything */
            var shape = JSON.stringify([want.admitted, want.bound, want.visible]);
            o.shapes[shape] = (o.shapes[shape] || 0) + 1;

            var wantCmp = { admitted: want.admitted, bound: want.bound,
                            visible: want.visible };
            /* when the model predicts refusal the engine reports nothing else,
             * so compare only the decision in that case */
            if (!want.admitted) {
                if (got.admitted) {
                    o.mismatches.push('case ' + ci + '/' + ai
                        + ' model REFUSED (' + want.refusedBy
                        + ') engine ADMITTED');
                }
                continue;
            }
            if (!same(wantCmp, got)) {
                o.mismatches.push('case ' + ci + '/' + ai + ' model='
                    + JSON.stringify(wantCmp) + ' engine=' + JSON.stringify(got));
            }
        }
    }

    /* the fixture must actually contain the thing it is about */
    o.probesDiffer = (PROBE2 !== PROBE && PROBE3 !== PROBE
                      && PROBE2.indexOf('nope') > 0
                      && PROBE3.indexOf('eval') > 0);
    o.distinct = Object.keys(o.shapes).length;
    delete o.shapes;
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(6);

###############################################################################

my $r = http_get('/v3');

# --- the instrument, before its result -----------------------------------
like($r, qr/"probesDiffer":true/,
     'the three probes really do differ -- the undeclared-name and denied-name '
     . 'cases reference what they claim to (a replace() that matched nothing '
     . 'once made both identical to the base probe, and the corpus passed)');
like($r, qr/"cases":70/,
     'the generated corpus is 14 mediation chains x 5 admission settings');
like($r, qr/"distinct":([5-9]|\d\d)/,
     'the model DISCRIMINATES: it predicts several different answers over the '
     . 'corpus, so agreement means something (an oracle that predicts one '
     . 'answer agrees with an engine that does anything)');

# --- the comparison ------------------------------------------------------
like($r, qr/"mismatches":\[\]/,
     'MODEL == ENGINE on every admission-relevant case: environment binding, '
     . 'the closed mediation vocabulary, the attenuation meet, revoke as zero, '
     . 'and free-name admission');

unlike($r, qr/"mismatches":\["case/,
     '...stated the other way round, because an empty list is also what a '
     . 'harness prints when it ran nothing');

like($r, qr/^HTTP\/1.1 200 /,
     'the corpus ran to completion (a throw mid-corpus would 500 and every '
     . 'assertion above would pass vacuously on an empty body)');
