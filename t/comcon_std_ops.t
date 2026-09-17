#!/usr/bin/perl

# COMCON M-LIB step 2 — std.ops: administration as library code.
#
# FOUNDATION §8a: THERE IS NO MANAGEMENT PLANE. comconctl is a shell, not a tool;
# every verb is an ordinary library program over the four kernel operators plus
# the OPS-RESOURCE CAPABILITIES. v2 §9.3's "the tooling never needs a backdoor"
# is then DERIVED rather than asserted -- and that derivation is the thing this
# file tests, not the convenience.
#
# SO A SESSION TAKES ITS RESOURCES AS ARGUMENTS and reaches for no ambient
# authority. A verb whose resource was not passed is ABSENT from the session, not
# present-and-throwing, so "what can this session do" is answerable with
# Object.keys() rather than by reading the implementation. The test asserts the
# extremes: a session given nothing has ONLY describe(), and a session given one
# resource gains exactly the verbs that decompose over it.
#
# THE RESOURCE LIST IS THE THIRD CLOSED ENUMERATION (§8a). All seven are named,
# including the two with NO host spelling (`provenance`, `signing`) -- host:null
# is what makes those gaps checkable instead of invisible, and the verbs needing
# them are reported as withheld with a reason. describe() walks the same table
# the session is built from, so the enumeration cannot drift from reality
# (ROADMAP §12 V7: generated, never maintained).
#
# The verbs that ship are the ones backed by something real: comcon.mode() is a
# genuine audit/enforce/learn switch, nginx.tenantDenials() returns exact
# counters, nginx.tenantLearning() returns the harvest. So the audit-first
# rollout -- shadow, observe, enforce -- is asserted end to end, which is the
# single workflow MANUAL.md leans on hardest.

# A DEFECT THIS FILE FOUND, and the reason the rollout assertions are about
# ENFORCEMENT rather than about a label: comcon.mode() wrote jcf->tenant_mode,
# but the mode that GATES is a static set once by ngx_js_compartment_policy_init()
# at the end of config load. So mode() worked during the host eval and was
# SILENTLY INERT at request time -- exactly when an operator runs it. An operator
# calling enforce() on a running server got "ok" and kept AUDITING: still allowing
# what they believed they had begun denying. It surfaced because std.ops reads the
# mode back through the denial report, and the two disagreed.
#
# NEGATIVE CONTROLS (run 2026-09-12; all six reverted to failure, rebuilt, and
# re-passed after restore):
#
#   comcon.mode() sets the EFFECTIVE mode    -> tests 10-11, 16 fail
#   a mode switch preserves the counters     -> countsKept fails
#   verbs absent without their resource      -> the inventory tests fail
#   remove() demands a naming confirmation   -> test 26 fails
#   rollback rewinds the snapshot record     -> tests 23-25 fail
#   rewrite hardens the live source          -> the rewrite tests fail
#
# The mode read-back inside shadow()/enforce() is not separately controlled: with
# the fix in place it returns the same answer as trusting the argument. Its value
# is that the VERB cannot report a switch that did not happen -- which is the
# shape of the bug above, so the verb refuses to be the next place it hides.

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

        location /m   { }
        location /ops { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var target = locs.find(function (l) { return l.path === "/m"; });

/* A live binding for the ops session to administer. */
var V1 = "function (n) { var g = real; return g('a') + g('b');" +
         "  function real(x) { return x.toUpperCase(); } }";
var V2 = "function (n) { return 'REBOUND'; }";

function site(callable, epoch) {
    if (callable === null) {
        target.handler = function (req) {
            req.respond(410, {'content-type':'text/plain'}, 'gone');
        };
    } else {
        target.handler = function (req) {
            req.respond(200, {'content-type':'text/plain',
                              'x-epoch': String(epoch)}, String(callable(0)));
        };
    }
}

var q1 = comcon.quote(V1);
var h  = comcon.bindAt(site, q1, { imports: [] });

/* THE OPERATOR SESSION: it holds exactly what it was handed. */
var ops = comcon.std.ops({
    log:      nginx.tenantDenials,
    learn:    nginx.tenantLearning,
    mode:     comcon.mode,
    bindings: true
});
ops.register("acme", h, q1);

/* A probe on the A1 reach gate: `s.listener` is an authority edge the gate
 * denies in ENFORCE mode and log-and-allows in AUDIT mode. The socket is granted
 * UNMEDIATED, so the field mask is not what answers -- the gate is. This is what
 * makes the rollout verbs testable as ENFORCEMENT rather than as a label. */
var sock = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);
/* A DENIED read returns null, and `typeof null` is "object" -- the same answer a
 * successful read gives. So the probe reports which one it got, not its type. */
var reach = comcon.include(
    "function(){ return { listener: (s.listener === null) ? 'null' : 'object' }; }",
    { grants: { s: sock } });

/* A session holding nothing, for the no-backdoor assertion. */
var bare = comcon.std.ops();

/* A session holding only the mode switch. */
var modeOnly = comcon.std.ops({ mode: comcon.mode });

var o = locs.find(function (l) { return l.path === "/ops"; });
o.handler = function (req) {
    var r = {}, op = req.queryParams.op;
    try {
        if (op === "inventory") {
            r.verbs = Object.keys(ops).sort();
            r.bare  = Object.keys(bare).sort();
            r.modeOnly = Object.keys(modeOnly).sort();
            var d = bare.describe();
            r.resourceCount = d.resources.length;
            r.resources = d.resources.map(function (x) { return x.name; });
            r.noHost = d.resources.filter(function (x) { return !x.host; })
                                  .map(function (x) { return x.name; });
            r.heldNone = d.resources.every(function (x) { return !x.held; });
            r.withheldCount = d.withheld.length;
            r.absent = d.absent.map(function (x) { return x.verb; });
        } else if (op === "rollout") {
            /* audit-first: shadow, read the counters, then enforce */
            r.shadow  = ops.shadow();
            r.dMode   = ops.denials().mode;
            r.enforce = ops.enforce();
            r.eMode   = ops.denials().mode;
            r.learnable = !!ops.learn();
        } else if (op === "gate") {
            /* THE POINT OF THE ROLLOUT VERBS: the same probe, two modes,
             * switched at REQUEST time. */
            r.enforceMode = ops.enforce();
            r.underEnforce = reach({}).listener;
            r.auditMode = ops.shadow();
            r.underAudit = reach({}).listener;
            r.backToEnforce = ops.enforce();
            r.again = reach({}).listener;
            /* the counters survive a mode switch: the evidence that justified
             * the switch must not be destroyed by making it */
            r.countsKept = (ops.denials().total > 0);
        } else if (op === "bindings") {
            r.list = ops.bindings();
            r.snapshotIsQuote = (ops.snapshot("acme").source === V1);
        } else if (op === "rebind") {
            r.epoch = ops.rebind("acme", comcon.quote(V2));
        } else if (op === "rollback") {
            r.epoch = ops.rollback("acme");
            r.snapBack = (ops.snapshot("acme").source === V1);
        } else if (op === "rewrite") {
            /* SHOWCASE §38 as one operator action: harden the live binding's
             * own source at a locally-bound callee, then rebind. */
            r.rep = ops.rewrite("acme", "call(g)",
                        comcon.quote("(function (t) { return 'X'; })"
                                     + "(function () { return $$; })"));
        } else if (op === "removeNoConfirm") {
            r.out = 'ACCEPTED';
            try { ops.remove("acme"); } catch (e) { r.out = 'refused'; }
            r.out2 = 'ACCEPTED';
            try { ops.remove("acme", { confirm: true }); }
            catch (e) { r.out2 = 'refused'; }
            r.out3 = 'ACCEPTED';
            try { ops.remove("acme", { confirm: "other" }); }
            catch (e) { r.out3 = 'refused'; }
        } else if (op === "removeConfirm") {
            r.epoch = ops.remove("acme", { confirm: "acme" });
            r.tomb  = ops.bindings()[0].tombstoned;
        } else if (op === "revive") {
            r.epoch = ops.revive("acme");
            r.tomb  = ops.bindings()[0].tombstoned;
        } else if (op === "trust") {
            var tr = ops.trustReport();
            r.names = tr.bindings.map(function (b) { return b.name; });
            r.classes = tr.bindings[0].ops;
            r.enforcedBy = tr.enforcedBy.map(function (x) { return x.field; });
        } else if (op === "unknown") {
            r.out = 'ACCEPTED';
            try { ops.snapshot("nope"); } catch (e) { r.out = 'refused'; }
            r.dup = 'ACCEPTED';
            try { ops.register("acme", h, q1); } catch (e) { r.dup = 'refused'; }
            r.badHandle = 'ACCEPTED';
            try { ops.register("x", {}, q1); } catch (e) { r.badHandle = 'refused'; }
        }
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(26);

###############################################################################

# --- the no-backdoor property, as an inventory ---------------------------
my $inv = http_get('/ops?op=inventory');

like($inv, qr/"bare":\["describe"\]/,
     'A SESSION GIVEN NOTHING HAS NO VERBS -- only describe(). That is the '
     . '"no backdoor" property, visible by Object.keys');
like($inv, qr/"modeOnly":\["describe","enforce","learnMode","shadow"\]/,
     'one resource yields exactly the verbs that decompose over it');
like($inv, qr/"verbs":\["bindings","coverage","denials","describe","diff","docs","enforce","guard","learn","learnMode","rebind","record","register","remove","revive","rewrite","rollback","shadow","snapshot","suite","trustReport","withdraw","withdrawn","wouldDeny"\]/,
     'a fully-provisioned session exposes all twenty-four shipped verbs');

# --- the third closed enumeration ---------------------------------------
like($inv, qr/"resourceCount":9/,
     "every ops resource FOUNDATION \x{a7}8a enumerates is enumerated here");
like($inv, qr/"resources":\["log","learn","mode","bindings","snapshot","broadcast","provenance","signing","sessions"\]/,
     '...by name. (V7\'s checker compares this table against \x{a7}8a itself: it '
     . 'caught that the mode switch shipped in the code while the document '
     . 'listed seven resources without it. The ninth, `sessions`, arrived with '
     . 'TM-2 in v5.65 -- and THIS assertion is what noticed, which is the '
     . 'enumeration doing its job in the other direction.)');
like($inv, qr/"noHost":\["provenance","signing"\]/,
     'the two with NO host spelling are marked host:null, so the gap is '
     . 'checkable instead of invisible');
like($inv, qr/"heldNone":true/, 'a bare session holds none of them');
like($inv, qr/"withheldCount":23/,
     'verbs it cannot perform are reported as withheld, with what they need');
like($inv, qr/"absent":\["revoke","cosign \/ office-hours","propose"\]/,
     'and the comconctl verbs that cannot be built yet are named with reasons');

# --- the audit-first rollout, which is real because comcon.mode() is ------
my $ro = http_get('/ops?op=rollout');
like($ro, qr/"shadow":"audit"/,     'shadow() puts the compartment in audit mode');
like($ro, qr/"dMode":"audit"/,      '...and the denial report agrees');
like($ro, qr/"enforce":"enforce"/,  'enforce() switches to enforce');
like($ro, qr/"eMode":"enforce"/,    '...and the report agrees again');
like($ro, qr/"learnable":true/,     'the learning harvest is readable');

# --- and the switch changes ENFORCEMENT, not just the label --------------
my $g = http_get('/ops?op=gate');
like($g, qr/"enforceMode":"enforce","underEnforce":"null"/,
     'in ENFORCE the A1 reach gate denies the authority edge');
like($g, qr/"auditMode":"audit","underAudit":"object"/,
     'THE ROLLOUT, FOR REAL: shadow() at REQUEST time makes the same probe '
     . 'log-and-allow -- comcon.mode() used to be silently inert here, so this '
     . 'is the assertion that the fix bites');
like($g, qr/"backToEnforce":"enforce","again":"null"/,
     '...and enforce() denies it again');
like($g, qr/"countsKept":true/,
     'a mode switch PRESERVES the denial counters -- the evidence that '
     . 'justified the switch must survive making it');

# --- the binding/epoch store, and snapshot = quote ----------------------
my $b = http_get('/ops?op=bindings');
like($b, qr/"list":\[\{"name":"acme","epoch":0,"tombstoned":false,"snapshots":1\}\]/,
     'the session lists what it administers');
like($b, qr/"snapshotIsQuote":true/,
     'SNAPSHOT = QUOTE: the snapshot is the quotation the live epoch was '
     . 'built from, inert and cap-free');

like(http_get('/ops?op=rebind'), qr/"epoch":1/, 'rebind advances the epoch');
like(http_get('/m'), qr/x-epoch: 1.*REBOUND/s, '...and the live site serves it');
my $rb = http_get('/ops?op=rollback');
like($rb, qr/"epoch":0,"snapBack":true/,
     'rollback restores the epoch AND the snapshot record with it');

# --- SHOWCASE 38 as one operator action ---------------------------------
like(http_get('/ops?op=rewrite'), qr/"count":2/,
     'rewrite() hardens the live binding at both sites of a locally-bound '
     . 'callee and rebinds -- audit, rewrite, install, in one verb');
like(http_get('/m'), qr/XX/,
     '...and the hardened epoch is what the site now serves');

# --- class X needs a confirmation that NAMES the binding ---------------
like(http_get('/ops?op=removeNoConfirm'),
     qr/"out":"refused","out2":"refused","out3":"refused"/,
     'remove() is class X: refused with no confirmation, refused with a bare '
     . 'confirm:true, and refused when the confirmation names another binding');
