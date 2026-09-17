#!/usr/bin/perl

# COMCON v5.130 -- SHOWCASE-gaps G-05, the last half: THE ALLOW-SUITE.
#
# SHOWCASE 5 promised "allow-suite generated: 1,214 recorded cases; coverage
# 91% of code paths -- WARNING: error handlers never exercised": a cage
# derived from OBSERVED behaviour, and a candidate admitted against it.  The
# diff and the would-deny list closed at v5.127; this is the recorder.
#
#   std.suite.record(f)          the fragment keeps (input, output) as the JSON
#                                text the boundary carries anyway; one distinct
#                                input = one case; a second answer to the same
#                                input marks it UNSTABLE (it can pin nothing)
#   std.suite.cases(f)           the stable cases, the unstable ones, the counts
#   std.suite.tests(cases)       a contract `tests` quotation: one function
#                                expression that replays every case inside the
#                                compartment and throws on the first divergence
#   std.suite.check(cand, cases) the same replay on the host, for a rehearsal
#   std.suite.coverage(f)        which of the fragment's functions the recording
#                                entered (the engine's per-function entry
#                                counts, delta over the window) and which gates
#                                fired; `exact` says whether the tier lets the
#                                count be trusted
#   h.record / h.suite / h.coverage / h.guard   on bindAt and bindShared handles;
#                                guard(tests?) pins the recorded answers into
#                                the contract every later epoch is admitted
#                                under -- so a rebind that answers differently
#                                is REFUSED (E_ADMIT_TEST), not installed
#   ops.record / suite / coverage / guard        the same over a registered name
#
# A thrown answer is recorded as `threw` without its text (the message crosses
# the boundary prefixed; a case must compare the same on both sides).
#
# NEGATIVE CONTROL: t/tools/controls/suite-guard-inert.patch makes guard()
# report success and pin nothing -- the divergent rebinds are admitted.

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
        location /ba { }
        location /sh { }
        location /ctl { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
function L(p) { return locs.find(function (l) { return l.path === p; }); }

/* three functions: the handler, a helper the error path calls, one never called */
var SRC1 = "function(a){ function fail(x){ throw new Error('boom ' + x); }"
         + " function spare(x){ return x * 2; }"
         + " if (a.k === 'err') { return fail(a.k); }"
         + " return { echo: a.k + '!', len: String(a.k).length }; }";
var GOOD2 = SRC1.replace("a.k + '!'", "'' + a.k + '!'");          /* same answers, new text */
var BAD   = SRC1.replace("String(a.k).length", "String(a.k).length + 1");
var UNST  = "(function(){ var n = 0; return function(a){ if (a.k === 'u') { return ++n; } return 'x'; }; })()";

var f1 = comcon.include(SRC1, { imports: [] });
var f2 = comcon.include(UNST, { imports: [] });
var good = comcon.include(GOOD2, { imports: [] });
var bad  = comcon.include(BAD,   { imports: [] });

var S = comcon.std.suite;

L('/w').handler = function (req) {
    var o = {};
    S.record(f1);
    f1({ k: 'a' }); f1({ k: 'a' }); f1({ k: 'bb' });
    try { f1({ k: 'err' }); } catch (e) { o.threw = true; }
    var cs = S.cases(f1);
    o.cases = { recorded: cs.recorded, distinct: cs.distinct, dropped: cs.dropped,
                unstable: cs.unstable.length, rows: cs.cases };

    S.record(f2);
    f2({ k: 'u' }); f2({ k: 'u' }); f2({ k: 'v' });
    var c2 = S.cases(f2);
    o.unstable = c2.unstable; o.stable2 = c2.cases.length;

    var T = S.tests(f1);
    o.testsHead = T.slice(0, 12);
    o.checkGood = S.check(good, cs);
    var cb = S.check(bad, cs);
    o.checkBad = { ok: cb.ok, passed: cb.passed, failed: cb.failed };

    try { comcon.include(GOOD2, { imports: [], tests: T }); o.admitGood = 'admitted'; }
    catch (e) { o.admitGood = e.code + ':' + e.message; }
    try { comcon.include(BAD, { imports: [], tests: T }); o.admitBad = 'admitted'; }
    catch (e) { o.admitBad = e.code + ':' + (e.message.indexOf('allow-suite case') >= 0 ? 'names-the-case' : e.message); }

    var cov = S.coverage(f1), aot = comcon.aotStatus(f1);
    o.cov = { total: cov.functions.total, exact: cov.exact, tier: cov.tier, gates: cov.gates.length,
              recorded: cov.recorded, distinct: cov.distinct };
    o.covOk = cov.exact
        ? (cov.functions.total === 3 && cov.functions.called === 2 && cov.functions.percent === 67
           && cov.functions.uncalled.length === 1 && cov.functions.uncalled[0].name === 'spare')
        : (cov.functions.total === 3 && aot.compiled > 0);

    /* a limit on distinct inputs: the rest is counted, not kept */
    var f3 = comcon.include("function(a){ return a.i; }", { imports: [] });
    S.record(f3, { max: 2 });
    f3({ i: 1 }); f3({ i: 2 }); f3({ i: 3 }); f3({ i: 1 });
    var c3 = S.cases(f3);
    o.capped = { recorded: c3.recorded, distinct: c3.distinct, dropped: c3.dropped };

    try { S.cases(good); o.notRec = 'ACCEPTED'; } catch (e) { o.notRec = e.name; }
    try { S.tests({ cases: [] }); o.empty = 'ACCEPTED'; } catch (e) { o.empty = e.name; }
    o.stopped = S.stop(f1) && S.stop(f1) === false;

    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};

/* the bindings: traffic goes through the site, the operator records it */
var baT = L('/ba');
function site(callable, epoch) {
    baT.handler = callable === null
        ? function (req) { req.respond(410, {'content-type':'text/plain'}, 'gone'); }
        : function (req) {
              var r = callable({ k: String(req.args || '') });
              req.respond(200, {'content-type':'application/json', 'x-epoch': String(epoch)}, JSON.stringify(r));
          };
}
var q1 = comcon.quote(SRC1);
var h = comcon.bindAt(site, q1, { imports: [] });
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode, bindings: true });
ops.register("acme", h, q1);

function onReq(req, callable, epoch) {
    if (callable === null) { req.respond(410, {'content-type':'text/plain'}, 'gone'); return; }
    var r = callable({ k: String(req.args || '') });
    req.respond(200, {'content-type':'application/json', 'x-epoch': String(epoch)}, JSON.stringify(r));
}
var sh = comcon.bindShared("suite", q1, { imports: [] }, onReq);
L('/sh').handler = sh.handler;

L('/ctl').handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === 'record')         r = ops.record('acme');
        else if (op === 'suite')     { var s1 = ops.suite('acme'); r = { distinct: s1.distinct, recorded: s1.recorded }; }
        else if (op === 'coverage')  { var c1 = ops.coverage('acme'); r = { total: c1.functions.total, exact: c1.exact }; }
        else if (op === 'guard')     r = ops.guard('acme');
        else if (op === 'diff')      { var d = ops.diff('acme', { imports: [] }); r = { verdict: d.verdict, paths: d.changes.map(function (c) { return c.path; }) }; }
        else if (op === 'rebind-good') r = { epoch: ops.rebind('acme', comcon.quote(GOOD2)) };
        else if (op === 'rebind-bad')  { try { ops.rebind('acme', comcon.quote(BAD)); r.epoch = 'ADMITTED'; } catch (e) { r = { code: e.code, epoch: h.epoch() }; } }
        else if (op === 'sh-record')   { sh.record(); r.recording = true; }
        else if (op === 'sh-guard')    r = sh.guard();
        else if (op === 'sh-rebind-bad') { try { sh.replace(comcon.quote(BAD)); r.epoch = 'ADMITTED'; } catch (e) { r = { code: e.code, epoch: sh.epoch() }; } }
        else if (op === 'sh-rebind-good') r = { epoch: sh.replace(comcon.quote(GOOD2)) };
        else if (op === 'describe')  r.ops = h.describe().ops.filter(function (x) { return /record|suite|coverage|guard/.test(x.name); });
    } catch (e) { r.error = e.name + ':' + e.message; }
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(27);

###############################################################################

my $b = http_get('/w');

like($b, qr/"cases":\{"recorded":4,"distinct":3,"dropped":0,"unstable":0,"rows":\[\{"input":"\{\\"k\\":\\"a\\"\}","output":"\{\\"echo\\":\\"a!\\",\\"len\\":1\}","n":2\},\{"input":"\{\\"k\\":\\"bb\\"\}","output":"\{\\"echo\\":\\"bb!\\",\\"len\\":2\}","n":1\},\{"input":"\{\\"k\\":\\"err\\"\}","output":"threw","n":1\}\]\}/,
     'four calls, three distinct inputs, each with its answer and its count; a throw recorded as threw');
like($b, qr/"unstable":\[\{"input":"\{\\"k\\":\\"u\\"\}","outputs":\["1","2"\],"n":2\}\],"stable2":1/,
     'an input answered two ways is unstable, kept apart with both answers');
like($b, qr/"testsHead":"function\(f\)\{"/, 'tests() is a single function expression');
like($b, qr/"checkGood":\{"total":3,"passed":3,"failed":\[\],"ok":true\}/,
     'a candidate that answers the same passes the rehearsal');
like($b, qr/"checkBad":\{"ok":false,"passed":1,"failed":\[\{"i":0,"input":"\{\\"k\\":\\"a\\"\}","expected":"\{\\"echo\\":\\"a!\\",\\"len\\":1\}","got":"\{\\"echo\\":\\"a!\\",\\"len\\":2\}"\},\{"i":1/,
     '...and one that diverges is named case by case (the throw case still agrees)');
like($b, qr/"admitGood":"admitted"/, 'the suite as contract tests admits the faithful candidate');
like($b, qr/"admitBad":"E_ADMIT_TEST:names-the-case"/, '...and refuses the divergent one with the case in the message');
like($b, qr/"cov":\{"total":3,"exact":(true|false),"tier":"(bytecode|native)","gates":0,"recorded":4,"distinct":3\}/,
     'coverage counts the three functions and names the tier it counted on');
like($b, qr/"covOk":true/, 'on the bytecode tier: two of three functions entered (67%), spare never called; on the native tier: exact is false');
like($b, qr/"capped":\{"recorded":4,"distinct":2,"dropped":1\}/, 'past max the recorder counts what it dropped');
like($b, qr/"notRec":"TypeError"/, 'cases() on a fragment that is not recording is a TypeError');
like($b, qr/"empty":"TypeError"/, 'tests() with no stable case refuses to pin nothing');
like($b, qr/"stopped":true/, 'stop() ends a recording and reports whether there was one');

# --- the bindAt binding through the operator session ------------------------
like(http_get('/ctl?op=record'), qr/"recording":true/, 'ops.record starts recording the live epoch');
http_get('/ba?a'); http_get('/ba?a'); http_get('/ba?bb');
like(http_get('/ctl?op=suite'), qr/\{"distinct":2,"recorded":3\}/, 'ops.suite reads the traffic the site carried');
like(http_get('/ctl?op=coverage'), qr/\{"total":3,"exact":(true|false)\}/, 'ops.coverage reads the functions entered');
like(http_get('/ctl?op=guard'), qr/\{"tests":true,"length":\d+\}/, 'ops.guard pins the recorded answers into the contract');
like(http_get('/ctl?op=diff'), qr/"verdict":"widening","paths":\["tests"\]/,
     '...and the policy diff against the old contract reads the pin as what it is: removing it would widen');
like(http_get('/ctl?op=rebind-bad'), qr/\{"code":"E_ADMIT_TEST","epoch":0\}/,
     'a rebind that answers differently is refused at admission; epoch 0 stays');
like(http_get('/ctl?op=rebind-good'), qr/"epoch":1/, 'a rebind that answers the same is admitted');
like(http_get('/ba?bb'), qr/x-epoch: 1.*"len":2/s, '...and serves');
like(http_get('/ctl?op=describe'), qr/"name":"record","op":"read","cls":"R".*"name":"suite","op":"read","cls":"R".*"name":"coverage","op":"read","cls":"R".*"name":"guard","op":"rewrite","cls":"F"/,
     'the handle describes the four ops');

# --- bindShared: guard rides the shared record -------------------------------
like(http_get('/sh?a'), qr/"len":1/, 'the shared binding serves');
like(http_get('/ctl?op=sh-record'), qr/"recording":true/, 'sh.record records this worker\'s callable');
http_get('/sh?a'); http_get('/sh?bb');
like(http_get('/ctl?op=sh-guard'), qr/\{"tests":true,"length":\d+\}/, 'sh.guard pins through the shared record');
like(http_get('/ctl?op=sh-rebind-bad'), qr/\{"code":"E_ADMIT_TEST","epoch":0\}/, 'a divergent shared replace is refused');
like(http_get('/ctl?op=sh-rebind-good') . http_get('/sh?bb'), qr/"epoch":1.*"len":2/s, 'a faithful one is admitted and serves');

###############################################################################
