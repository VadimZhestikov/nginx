#!/usr/bin/perl

# COMCON v5.129 -- SHOWCASE-gaps G-01: LIVE REVOCATION of a granted capability.
#
# Until now `revoke()` was a grant-time flavour (a mask of zero, decided at
# admission) and the only run-time verb was ops.remove, which tombstones the
# whole binding.  The CVE-day story (SHOWCASE 2) and the offboarding story
# (SHOWCASE 20) both need the other thing: a switch the host holds on a grant
# it has ALREADY made, that follows every copy the grant was delegated onward
# as, and that no posture can lift.
#
# THE MECHANISM IS ONE POINTER PER WRAPPER.  Every granted wrapper (socket,
# outbound, COM facet, author) holds a refcounted GRANT RECORD; a re-grant's
# copy holds a record of its own whose parent is the original's.  The gate
# asks "is any record on the chain revoked?" right after cap.owner, and the
# fragment's stats slot keeps the records under the contract's names so the
# host can flip one by (fragment, name) after the wrappers have vanished into
# the closure.  comcon.withdraw(f, name?) flips (`revoke()` is already the
# grant-time flavour, so the live verb has its own name); comcon.withdrawn(f) reads back
# from the kernel's table.  cap.revoked is the second UNCONDITIONAL code: the
# operator who revoked is not observing a policy, they are exercising one.
#
# A revocation STICKS TO A BINDING: bindAt re-applies it to every epoch a
# replace() realizes or a rollback() restores (the same contract, the same
# grants come back, and an edit must not lift a revocation); bindShared fans
# it out on the shared record beside the epoch.  Irreversible by design --
# restoring authority is a widening, and every widening here is a new
# admission under a new contract.
#
# NEGATIVE CONTROL: t/tools/controls/revoke-not-checked.patch makes the chain
# walk answer "not revoked" -- the flips still happen and read back, and every
# capability keeps working.

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
        location /ctl { }
        location /sh { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var srv = nginx.http.servers[0];
var locs = srv.locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var outbound = comcon.mediate(nginx.outbound(), comcon.allowHosts("*.example.com"));

function L(p) { return locs.find(function (l) { return l.path === p; }); }

var PORT = "function(a){ return typeof s.port; }";
var f1  = comcon.include(PORT, { imports: [], grants: { s: sock } });
var f2  = comcon.include(PORT, { imports: [], grants: { s: sock }, onViolation: "audit" });
var sib = comcon.include(PORT, { imports: [], grants: { s: sock } });

/* a parent that delegates a copy of its socket to a sub-fragment it KEEPS
   across calls -- the copy must die with the parent's grant */
var P = comcon.include(
    "(function(){ var sub = null; return function(a){"
  + " if (sub === null) { sub = author.include('function(b){ return typeof s.port; }',"
  + "   { imports: [], grants: { s: s } }); }"
  + " return { mine: typeof s.port, sub: sub({}) }; }; })()",
    { imports: [], grants: { s: sock, author: comcon.author({ subFragments: 1 }) } });

var f4 = comcon.include(
    "function(a){ return { port: typeof s.port, req: typeof out.request('https://a.example.com/x') }; }",
    { imports: [], grants: { s: sock, out: outbound } });
var f5 = comcon.include(
    "function(a){ try { return typeof api.paths(); } catch (e) { return 'threw:' + e.message; } }",
    { imports: [], grants: { api: comcon.mediate(srv, comcon.routes('/w*')) } });
var f6 = comcon.include(
    "function(a){ try { author.include('function(b){ return 1; }', { imports: [] }); return 'authored'; }"
  + " catch (e) { return 'threw:' + e.message; } }",
    { imports: [], grants: { author: comcon.author({ subFragments: 4 }) } });

/* a bindAt binding under an operator session, and a bindShared one */
var baTarget = L('/ba');
function site(callable, epoch) {
    if (callable === null) {
        baTarget.handler = function (req) { req.respond(410, {'content-type':'text/plain'}, 'gone'); };
    } else {
        baTarget.handler = function (req) {
            req.respond(200, {'content-type':'text/plain', 'x-epoch': String(epoch)}, String(callable(0)));
        };
    }
}
var q1 = comcon.quote("function(n){ return typeof s.port; }");
var q2 = comcon.quote("function(n){ return 'v2:' + typeof s.port; }");
var renv = comcon.grant(comcon.env(), 's', sock);
var h  = comcon.bindAt(site, q1, { imports: ['s'], env: renv });
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode, bindings: true });
ops.register("acme", h, q1);

function onReq(req, callable, epoch) {
    if (callable === null) { req.respond(410, {'content-type':'text/plain'}, 'gone'); return; }
    req.respond(200, {'content-type':'text/plain', 'x-epoch': String(epoch)}, String(callable(0)));
}
var sh = comcon.bindShared("rv", comcon.quote("function(){ return typeof s.port; }"),
                           { imports: ['s'], env: renv }, onReq);
L('/sh').handler = sh.handler;

L('/w').handler = function (req) {
    var o = {};
    comcon.mode('enforce');
    var fleet0 = nginx.tenantDenials().byOp['cap.revoked'] || 0;

    o.before = f1({});
    o.r1 = comcon.withdraw(f1, 's');
    o.after = f1({});
    o.own = comcon.denials(f1).byOp['cap.revoked'];
    o.r1again = comcon.withdraw(f1, 's');
    o.revokedList = comcon.withdrawn(f1);
    o.sibling = sib({});

    /* unconditional: the audit posture does not lift it */
    o.auditBefore = f2({});
    o.rAudit = comcon.withdraw(f2);
    o.auditAfter = f2({});

    /* the cascade: the parent's grant, and the copy it delegated onward */
    o.p0 = P({});
    o.rp = comcon.withdraw(P, 's');
    o.p1 = P({});

    /* whole fragment, two kinds at once */
    o.f4a = f4({});
    o.r4 = comcon.withdraw(f4);
    o.f4b = f4({});
    o.revoked4 = comcon.withdrawn(f4);

    /* the facet and the author capability */
    o.f5a = f5({});
    comcon.withdraw(f5, 'api');
    o.f5b = f5({});
    o.f6a = f6({});
    comcon.withdraw(f6, 'author');
    o.f6b = f6({});

    try { comcon.withdraw(f1, 'nope'); o.unknown = 'ACCEPTED'; }
    catch (e) { o.unknown = e.name + ':' + e.message; }
    try { comcon.withdraw(function () {}); o.plain = 'ACCEPTED'; }
    catch (e) { o.plain = e.name; }
    o.unrevokedFresh = comcon.withdrawn(sib);

    o.fleetGrew = (nginx.tenantDenials().byOp['cap.revoked'] || 0) - fleet0;
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};

L('/ctl').handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === 'ops-revoke-noconfirm') { ops.withdraw('acme', 's'); r.accepted = true; }
        else if (op === 'ops-revoke')      r = ops.withdraw('acme', 's', { confirm: 'acme' });
        else if (op === 'ops-revoked')     r.revoked = ops.withdrawn('acme');
        else if (op === 'ops-docs')        r.docs = ops.docs('acme');
        else if (op === 'ops-trust')       r.revoked = ops.trustReport().bindings[0].revoked;
        else if (op === 'replace')         r.epoch = h.replace(q2);
        else if (op === 'rollback')        r.epoch = h.rollback();
        else if (op === 'describe')        r.ops = h.describe().ops.filter(function (x) {
                                               return x.name === 'withdraw' || x.name === 'withdrawn'; });
        else if (op === 'sh-revoke')       r = sh.withdraw('s');
        else if (op === 'sh-revoked')      r.revoked = sh.withdrawn();
        else if (op === 'sh-describe')     r.ops = sh.describe().ops.filter(function (x) {
                                               return x.name === 'withdraw' || x.name === 'withdrawn'; });
    } catch (e) { r.error = e.name + ':' + e.message; }
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(31);

###############################################################################

my $b = http_get('/w');

like($b, qr/"before":"number","r1":\{"revoked":\["s"\],"delegated":0\},"after":"undefined"/,
     'a granted socket reads before revoke and is gone after it');
like($b, qr/"own":1/, "...and the denial is attributed to the fragment as cap.revoked");
like($b, qr/"r1again":\{"revoked":\["s"\],"delegated":0\}/, 'withdraw is idempotent');
like($b, qr/"revokedList":\["s"\]/, 'comcon.withdrawn reads the name back from the kernel table');
like($b, qr/"sibling":"number"/, 'a sibling holding its own wrapper on the same socket is untouched');

like($b, qr/"auditBefore":"number","rAudit":\{"revoked":\["s"\],"delegated":0\},"auditAfter":"undefined"/,
     'cap.revoked is unconditional: an audit binding is denied too');

like($b, qr/"p0":\{"mine":"number","sub":"number"\},"rp":\{"revoked":\["s"\],"delegated":1\}/,
     'the parent and the copy it delegated both read; the revoke reports one delegation reached');
like($b, qr/"p1":\{"mine":"undefined","sub":"undefined"\}/,
     'the cascade: the kept sub-fragment\'s copy died with the parent\'s grant');

like($b, qr/"f4a":\{"port":"number","req":"number"\},"r4":\{"revoked":\["s","out"\],"delegated":0\}/,
     'withdraw with no name takes every grant, socket and outbound alike');
like($b, qr/"f4b":\{"port":"undefined","req":"undefined"\},"revoked4":\["out","s"\]/,
     '...both dead afterwards, both read back');

like($b, qr/"f5a":"object"/, 'a facet answers paths() before');
like($b, qr/"f5b":"threw:NginxComFacet: this facet was revoked"/, '...and throws revoked after');
like($b, qr/"f6a":"authored"/, 'an author capability authors before');
like($b, qr/"f6b":"threw:NginxComconAuthor: this capability was revoked"/, '...and refuses after');

like($b, qr/"unknown":"TypeError:comcon.withdraw: `nope` is not a grant of this fragment"/,
     'a name the fragment was not granted is a TypeError, not a silent no-op');
like($b, qr/"plain":"TypeError"/, 'a plain function has no grants to revoke');
like($b, qr/"unrevokedFresh":\[\]/, 'an untouched fragment reads back nothing revoked');
like($b, qr/"fleetGrew":8/, 'the fleet counter grew by exactly the eight denied uses');

# the log says what happened in audit mode, not just which mode
my $log = $t->read_file('error.log');
like($log, qr/js denial: comp=\d+ op=cap\.revoked obj="[^"]*" mode=audit n=\d+ unconditional=1/,
     'the audit-mode record carries unconditional=1');

# --- the bindAt handle through the operator session ------------------------
like(http_get('/ba'), qr/x-epoch: 0.*\r\n\r\nnumber$/s, 'the bound epoch 0 reads its socket');
like(http_get('/ctl?op=ops-revoke-noconfirm'), qr/"error":"TypeError:ops.withdraw is class X/,
     'ops.withdraw refuses without a confirmation naming the binding');
like(http_get('/ctl?op=ops-revoke'), qr/\{"revoked":\["s"\],"delegated":0\}/,
     'ops.withdraw(name, grant, {confirm}) revokes through the handle');
like(http_get('/ba'), qr/\r\n\r\nundefined$/s, '...and the live site denies');
like(http_get('/ctl?op=replace'), qr/"epoch":1/, 'replace admits new text under the same contract');
like(http_get('/ba'), qr/x-epoch: 1.*\r\n\r\nv2:undefined$/s,
     '...and the revocation sticks to the binding: the new epoch\'s grant is dead too');
like(http_get('/ctl?op=rollback'), qr/"epoch":0/, 'rollback restores epoch 0');
like(http_get('/ba'), qr/x-epoch: 0.*\r\n\r\nundefined$/s, '...still revoked: a rollback cannot lift it');
like(http_get('/ctl?op=ops-revoked'), qr/"revoked":\["s"\]/, 'ops.withdrawn reads it back');
my $d = http_get('/ctl?op=ops-docs');
like($d, qr/REVOKED: every use denies as cap.revoked/, 'the rendered manual says the grant is revoked');

# --- bindShared: the shared record fans it out ------------------------------
like(http_get('/sh'), qr/\r\n\r\nnumber$/s, 'the shared binding reads its socket');
like(http_get('/ctl?op=sh-revoke') . http_get('/sh') . http_get('/ctl?op=sh-revoked')
     . http_get('/ctl?op=describe') . http_get('/ctl?op=ops-trust'),
     qr/\{"revoked":\["s"\],"delegated":0\}.*\r\n\r\nundefined.*"revoked":\["s"\].*"name":"withdrawn","op":"read","cls":"R".*"name":"withdraw","op":"withdraw","cls":"X".*"revoked":\["s"\]/s,
     'sh.withdraw revokes through the shared record, the next request denies, withdrawn() reads it back; the handle ops and the trust report name it');

###############################################################################
