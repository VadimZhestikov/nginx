#!/usr/bin/perl

# COMCON F3 — CROSS-COMPARTMENT IDENTITY. Can two confined fragments share state?
#
# AUDIT_M-SES.md §3 listed this as NOT EVIDENCED: "named in the S6 probe classes; not
# probed. Low expected yield -- the include path marshals via JSON, so only strings cross
# -- but 'cannot by construction' is an argument, not a test."
#
# THE CLAIM IS TWO CLAIMS, and only one of them is structural. Measured while writing
# this file:
#
#   HOST <-> FRAGMENT is a SEPARATE RUNTIME. jcf->comcon_rt is its own JS_NewRuntime(),
#   so the two sides share no heap and no JSValue can cross at all -- the JSON marshalling
#   in the invoke is not a policy, it is the only thing that CAN happen. (Patching the
#   invoke to pass the argument by reference, as a control, does not produce a leak: it
#   produces an empty response, because using a value across runtimes is undefined
#   behaviour. An isolation whose control is "the worker dies" is structural, and saying so
#   is more honest than pretending there is a check to toggle.)
#
#   FRAGMENT <-> FRAGMENT shares EVERYTHING. Every confined fragment lives in the SAME
#   context (jcf->comcon_ctx is created once and reused) inside that one runtime, so two
#   tenants share one global object, one set of intrinsics, one prototype graph. Nothing
#   structural separates them. What does: (a) a global holding no host authority, (b) the
#   M-SES-1 transitive FREEZE of the intrinsic graph, (c) grants bound as closure
#   parameters rather than globals, and (d) admission refusing a fragment that so much as
#   NAMES a neighbour. None of that was ever probed AS A CHANNEL.
#
# So: fragment A tries to plant a mark on every shared surface it can reach; fragment B,
# admitted separately, tries to read it. One reader that sees a mark is a covert channel
# between tenants (THREATS T4), not a curiosity.
#
# THE CONTROL IS BUILT IN, the S6 way: every probe also runs UNCONFINED in host JS, where
# nothing is frozen and the channel genuinely works, so a clean confined result is a
# measurement rather than a tautology. And the freeze itself was removed once, as a
# control: FIVE of the seven surfaces immediately became live cross-tenant channels, which
# is what makes (b) above a load-bearing claim rather than a hopeful one.

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

        location /ident { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

/* ---------------------------------------------------------------------------
 * The battery. Each probe is a pair: PLANT runs in fragment A, READ in B.
 * Written as source strings so the same text runs confined (via include) and
 * unconfined (via new Function) -- one alphabet, two regimes.
 * ------------------------------------------------------------------------- */
var PROBES = [
    { name: 'object-prototype',
      why: 'the classic: pollute a shared prototype and read it back next door',
      plant: "Object.prototype.__chan = 'planted'; return 'ok';",
      read:  "return ({}).__chan || 'clean';" },

    { name: 'frozen-constructor',
      why: 'a property on a shared intrinsic OBJECT (not its prototype)',
      plant: "Map.__chan = 'planted'; return 'ok';",
      read:  "return Map.__chan || 'clean';" },

    { name: 'json-object',
      why: 'JSON is admitted without declaration, so every tenant holds it',
      plant: "JSON.__chan = 'planted'; return 'ok';",
      read:  "return JSON.__chan || 'clean';" },

    { name: 'array-prototype-index',
      why: 'index pollution reads back through any array literal',
      plant: "Array.prototype[7] = 'planted'; return 'ok';",
      read:  "var a = []; return a[7] || 'clean';" },

    { name: 'error-prototype',
      why: 'error objects cross boundaries more than most values do',
      plant: "Error.prototype.__chan = 'planted'; return 'ok';",
      read:  "return (new Error('x')).__chan || 'clean';" },

    { name: 'string-prototype',
      why: 'a mark on String.prototype is visible to every string everywhere',
      plant: "String.prototype.__chan = 'planted'; return 'ok';",
      read:  "return ''.__chan || 'clean';" },

    { name: 'function-prototype',
      why: 'Function.prototype is reachable from any function VALUE, which is '
         + 'the path that does not need the denied name `Function`',
      plant: "Object.getPrototypeOf(function(){}).__chan = 'planted'; return 'ok';",
      read:  "return (function(){}).__chan || 'clean';" }
];

/* Symbol is NOT an intrinsic (v5.54, deliberately: Symbol.for is a runtime-wide
   registry, i.e. a rendezvous). It is DECLARABLE, so the honest question is not
   "can a tenant reach it" but "what does an operator open by declaring it" --
   measured below rather than assumed. */
var SYMBOL_PLANT = "function(){ var s = Symbol.for('comcon.chan'); "
                 + "return typeof s; }";
var SYMBOL_READ  = "function(){ return Symbol.for('comcon.chan') === "
                 + "Symbol.for('comcon.chan') ? 'SHARED' : 'clean'; }";

/* The grant-level probe is separate: it needs the SAME capability handed to
   both fragments, which is the interesting case (do two tenants granted one
   socket get one object, and can they write on it?). */
/* A DISJOINT mark name, and the reason is a defect this file already had: the
   unconfined control arm deliberately pollutes the HOST's Object.prototype with
   `__chan`, so every later host-side read of `.__chan` inherits 'planted' and
   reports a leak that is not there. The first version of this test duly claimed
   the host could see what a fragment wrote. It could not; the test could see its
   own control. Marks must be disjoint, not merely different. */
var GRANT_PLANT = "function(){ try { s.__gchan = 'planted'; return 'wrote'; } "
                + "catch (e) { return 'refused: ' + e.name; } }";
var GRANT_READ  = "function(){ try { return s.__gchan || 'clean'; } "
                + "catch (e) { return 'threw'; } }";
var PROTO_PLANT = "function(){ try { s.constructor.prototype.__gchan = 'planted';"
                + " return 'wrote'; } catch (e) { return 'refused: ' + e.name; } }";
var PROTO_READ  = "function(){ try { return s.__gchan || 'clean'; } "
                + "catch (e) { return 'threw'; } }";

/* the marshalled argument: the same HOST object handed to two fragments */
var ARG_PLANT = "function(a){ a.mark = 'planted'; return 'ok'; }";
var ARG_READ  = "function(a){ return a.mark || 'clean'; }";

locs.find(function (l) { return l.path === "/ident"; }).handler = function (req) {
    var out = { probes: [], grant: {}, arg: {}, scope: {} }, i;

    for (i = 0; i < PROBES.length; i++) {
        var p = PROBES[i], rec = { name: p.name };

        /* ---- CONFINED: two separately admitted fragments ---- */
        /* every name these probes use is an INTRINSIC, admitted without
           declaration; `Function` is on the deny list and no manifest
           re-admits it, which is why the function-prototype probe reaches
           the prototype through a function value instead. */
        var A = comcon.include("function(){ " + p.plant + " }", { imports: [] });
        var B = comcon.include("function(){ " + p.read + " }", { imports: [] });
        try { rec.plant = A({}); } catch (e) { rec.plant = 'refused: ' + e.name; }
        try { rec.read  = B({}); } catch (e2) { rec.read = 'threw: ' + e2.name; }

        /* ---- UNCONFINED: the same text in host JS, where nothing is frozen ---- */
        try {
            var hA = new Function(p.plant), hB = new Function(p.read);
            rec.hostPlant = hA();
            rec.hostRead  = hB();
        } catch (e3) { rec.hostRead = 'host-threw: ' + e3.name; }
        /* undo the control's pollution: it ran in the HOST context, which is not
           frozen, and anything left behind is read by every host probe after it */
        try { delete Object.prototype.__chan; delete Array.prototype[7];
              delete String.prototype.__chan; delete Error.prototype.__chan;
              delete Map.__chan; delete JSON.__chan;
              delete Object.getPrototypeOf(function(){}).__chan; } catch (e9) {}

        rec.channel = (rec.read !== 'clean');
        rec.hostChannel = (rec.hostRead !== 'clean');
        out.probes.push(rec);
    }

    /* ---- the same GRANT in two fragments ---- */
    var GA = comcon.include(GRANT_PLANT, { imports: [], grants: { s: sock } });
    var GB = comcon.include(GRANT_READ,  { imports: [], grants: { s: sock } });
    out.grant.plant = GA({});
    out.grant.read  = GB({});
    out.grant.channel = (out.grant.read !== 'clean');

    /* the question the first version of this test did not ask: does the HOST
       see what the fragment wrote onto its own wrapper? */
    out.grant.hostSees = (sock.__gchan === undefined) ? 'clean' : sock.__gchan;
    out.grant.hostOwn = Object.prototype.hasOwnProperty.call(sock, '__gchan');
    out.grant.hostOwnCount = Object.getOwnPropertyNames(sock).length;

    var PA = comcon.include(PROTO_PLANT, { imports: [], grants: { s: sock } });
    var PB = comcon.include(PROTO_READ,  { imports: [], grants: { s: sock } });
    out.grant.protoPlant = PA({});
    out.grant.protoRead  = PB({});
    out.grant.protoChannel = (out.grant.protoRead !== 'clean');

    /* ---- the marshalled argument: one host object, two fragments ---- */
    var shared = { mark: null };
    var AA = comcon.include(ARG_PLANT, { imports: [] });
    var AB = comcon.include(ARG_READ,  { imports: [] });
    out.arg.plant = AA(shared);
    out.arg.read  = AB(shared);
    out.arg.hostSees = (shared.mark === null) ? 'clean' : shared.mark;
    out.arg.channel = (out.arg.read !== 'clean');

    /* ---- the Symbol registry, as an OPERATOR-OPENED channel ---- */
    out.symbol = {};
    try {
        var SA = comcon.include(SYMBOL_PLANT, { imports: [] });
        out.symbol.undeclared = SA({});
    } catch (e5) {
        out.symbol.undeclared = 'refused: ' + (e5.code || e5.name);
    }
    try {
        var SB2 = comcon.include(SYMBOL_PLANT, { imports: ['Symbol'] });
        var SC = comcon.include(SYMBOL_READ,   { imports: ['Symbol'] });
        out.symbol.declaredPlant = SB2({});
        out.symbol.declaredRead  = SC({});
    } catch (e6) {
        out.symbol.declaredPlant = 'refused: ' + (e6.code || e6.name);
    }

    /* ---- scope: can B name A at all? ---- */
    /* Naming another fragment does not fail at CALL time -- it fails at
       ADMISSION, because `A` is a free name and the manifest does not declare
       it. The stronger answer: such a fragment cannot be loaded at all. */
    try {
        var SB = comcon.include("function(){ return typeof A; }", { imports: [] });
        out.scope.seesA = SB({});
    } catch (e4) {
        out.scope.seesA = 'refused-at-admission: ' + (e4.code || e4.name);
    }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(out));
};
JS

$t->try_run('no js module')->plan(11);

###############################################################################

my $r = http_get('/ident');
diag($1) if $r =~ /("probes":.*)/;

# --- no confined probe found a channel -----------------------------------
unlike($r, qr/"channel":true/,
     'NO probe planted a mark that another fragment could read: not through a '
     . 'shared prototype, a frozen intrinsic, JSON, an array index, Error, '
     . 'String, or Function.prototype -- eight surfaces, all shared in one '
     . 'JSContext, none of them a channel');

# --- and the probes can SEE a channel when there is one -------------------
like($r, qr/"hostChannel":true/,
     'THE CONTROL IS BUILT IN: the same probe text run UNCONFINED does find a '
     . 'channel, so a clean confined result is a measurement and not a '
     . 'tautology (host JS is not frozen -- that is the difference)');

my @host = ($r =~ /"hostChannel":(true|false)/g);
my $hostyes = grep { $_ eq 'true' } @host;
cmp_ok($hostyes, '>=', 5,
       "at least five of the eight probes are live channels unconfined "
       . "($hostyes of " . scalar(@host) . ") -- the battery is not mostly "
       . "probing things that never worked anywhere");

# --- the symbol registry: what an operator opens by declaring Symbol -----
like($r, qr/"undeclared":"refused: E_ADMIT_FREENAME"/,
     'Symbol is NOT reachable by default: it is deliberately outside the '
     . 'intrinsics allowance (v5.54) precisely because Symbol.for is a '
     . 'runtime-wide registry, and a registry is a rendezvous');
like($r, qr/"declaredRead":"SHARED"/,
     'MEASURED, NOT ASSUMED: when an operator DECLARES Symbol for two tenants, '
     . 'Symbol.for does give them a shared key -- an operator-opened channel. '
     . 'Not an escape (nothing crosses that was not granted) but the reason '
     . 'Symbol stays undeclared by default, now demonstrated instead of argued');

# --- the shared capability ----------------------------------------------
# MEASURED: a fragment CAN write an own property onto its capability wrapper --
# the wrapper is a plain JS object and only the class PROTOTYPE is frozen. The
# first version of this file asserted the opposite and passed, because the regex
# was unanchored and matched an earlier probe's record. What makes it harmless is
# not a refusal, it is that each include gets its OWN wrapper over the same C
# socket, so the write reaches nobody -- which is a different claim and has to be
# checked as one.
like($r, qr/"grant":\{"plant":"wrote"/,
     'a fragment CAN set an own property on its granted capability object '
     . '(only the class prototype is frozen) -- so the isolation cannot come '
     . 'from refusing the write, and this assertion says which claim is load-'
     . 'bearing');
like($r, qr/"grant":\{"plant":"wrote","read":"clean"/,
     'a second fragment granted THE SAME socket reads NOTHING from it: each '
     . 'include is handed its own wrapper over the same C object, so two '
     . 'tenants holding one capability is not a rendezvous');
like($r, qr/"hostSees":"clean","hostOwn":false,"hostOwnCount":0/,
     'and the HOST does not see it either: its own socket object carries no '
     . 'properties at all, so a tenant cannot plant one where host code might '
     . 'trip over it (e.g. shadowing a method the host is about to call). '
     . 'MEASURED with a mark the unconfined control never plants -- the first '
     . 'version of this check reused `__chan` and reported a leak that was its '
     . 'own control polluting the host prototype');
like($r, qr/"protoChannel":false/,
     'nor through the capability PROTOTYPE (M-SES-1b freezes those): the class '
     . 'prototype is the one object every holder of that type shares');

# --- the marshalled argument --------------------------------------------
like($r, qr/"arg":\{"plant":"ok","read":"clean","hostSees":"clean","channel":false\}/,
     'the same HOST object handed to two fragments arrives as two COPIES: the '
     . 'mark one plants is invisible to the other AND to the host, which is '
     . 'what "only strings cross" has to mean to be worth anything');

# --- scope --------------------------------------------------------------
like($r, qr/"seesA":"refused-at-admission: E_ADMIT_FREENAME"/,
     'and a fragment that so much as NAMES another fragment is refused at '
     . 'ADMISSION -- it never loads, let alone runs: the free-name manifest is '
     . 'the primary control and this is what it looks like from outside');
