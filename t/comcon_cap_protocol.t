#!/usr/bin/perl

# M-LIB `protocol` — ENFORCED OPERATION ORDER, and the tenth vocabulary word.
#
# A session type over a capability's own operations.  `revoke`/`redact`/`allow`
# say WHICH operations exist, `uses` says how often, `ttl`/`window` say when,
# `cosign` says by whom — this one says IN WHAT ORDER.
#
#   comcon.protocol('address', 'port*', 'fd')   // look, poll, then take
#   comcon.protocol('request')                  // one outbound intent, ever
#
# A starred step may happen any number of times including zero; a bare step must
# happen exactly once, in place; once the last step is consumed THE CONVERSATION
# IS OVER and every further operation is denied.  That last property is what
# makes `protocol('fd')` a ONE-SHOT capability — a different attenuation from
# `uses(1)`, because a budget is fleet-wide and resets with its window while a
# protocol is per-wrapper and never resets.  Both halves are asserted.
#
# IT ENFORCES ORDER, NOT COMPLETION, and that is stated rather than discovered:
# "you cannot take the fd before looking at the address" is checkable at the
# moment of the call, but "you must eventually close" is not — a fragment can
# simply return, and there is no event at which the host could notice.
#
# THE ONE INTERESTING THING ABOUT THE GATE IS WHERE IT SITS.  Every other gate's
# decision is also its effect: a budget charge happens when it is decided, and a
# cosign consent IS the decision.  A protocol's effect — advancing the cursor —
# can be deferred, and it must be, because an operation a later gate still
# refuses did not happen and must not move the conversation on.  So the
# transition is CHECKED before cosign and COMMITTED after the budget, and two
# assertions here measure exactly that: a cosign-denied operation must not
# advance, and a budget-denied one must not either.
#
# THE STATE IS PER WRAPPER, not fleet-wide like a cosign record: a session type
# describes ONE conversation, and two holders sharing a cursor would interleave
# into nonsense.  Asserted by giving two fragments the same protocol and watching
# them each get their own.

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

        location /proto { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (Object.prototype.hasOwnProperty.call(d, k)) { c[k] = d[k]; } }
    return c;
}
function fired(a, b) {
    var out = [], k;
    for (k in b) {
        if (!Object.prototype.hasOwnProperty.call(b, k)) { continue; }
        if ((b[k] || 0) > (a[k] || 0)) { out.push(k); }
    }
    return out.sort();
}

/* A sequence of field reads, reported as the literal `undefined` for a denied
 * one so the shape of the conversation is readable in the result. */
function armSock(steps, extra) {
    var m = comcon.mediate(sock, comcon.allow(['address','port','fd']));
    m = comcon.mediate(m, comcon.protocol.apply(null, steps));
    if (extra) { m = comcon.mediate(m, extra); }
    return comcon.include(
        "function(a){ var o=[],i; for(i=0;i<a.ops.length;i++){"
      + " var v=s[a.ops[i]]; o.push(v===undefined?'-':'ok'); } return o; }",
        { imports: [], grants: { s: m } });
}

locs.forEach(function (l) {
    if (l.path !== '/proto') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* --- the declared order is allowed --- */
            o.inOrder = armSock(['address','port*','fd'])(
                            { ops: ['address','port','port','fd'] });

            /* --- and a step out of order is denied --- */
            var b = counts();
            o.outOfOrder = armSock(['address','port*','fd'])(
                               { ops: ['fd'] });
            o.outOfOrderFired = fired(b, counts());

            /* --- a STARRED step may happen zero times --- */
            o.skipStar = armSock(['address','port*','fd'])(
                             { ops: ['address','fd'] });

            /* --- past the end, the conversation is OVER: ONE-SHOT --- */
            o.oneShot = armSock(['fd'])({ ops: ['fd','fd','fd'] });

            /* --- a required step cannot be skipped even if a later one matches
             * something the caller wants: 'port' before 'address' is a violation,
             * and the conversation does NOT advance, so 'address' still works. */
            o.recover = armSock(['address','fd'])(
                            { ops: ['port','address','fd'] });

            /* --- PER WRAPPER: two fragments, the same protocol, two cursors --- */
            var f1 = armSock(['fd']), f2 = armSock(['fd']);
            o.perWrapper = [f1({ ops: ['fd'] }), f2({ ops: ['fd'] }),
                            f1({ ops: ['fd'] })];

            /* --- THE GATE'S POSITION, and it took two attempts to measure ---
             *
             * The claim is that the transition is CHECKED before the gates that
             * can still refuse the operation and COMMITTED only after all of
             * them pass.  The obvious probe -- one principal denied, a second
             * principal allowed -- CANNOT SEE IT, because the cursor is per
             * WRAPPER and those are two wrappers with two cursors.  A control
             * that moved the commit up to the check passed that probe.
             *
             * So the same wrapper has to attempt TWICE, with the cosignature
             * arriving in between.  W is cosigned and sequenced; V is cosigned
             * on the same key and NOT sequenced, and exists only to be the
             * second signature.
             *
             *   1. W reads fd   -> the order is fine, the cosignature is not:
             *                      DENIED, and the cursor must NOT move
             *   2. V reads fd   -> bob's consent completes the quorum
             *   3. W reads fd   -> now cosigned, and legal only if step 1 left
             *                      the cursor at 0
             *   4. W reads address -> the second step of the protocol
             *
             * Had the commit happened at the check, step 1 would have advanced
             * the cursor to `address`, and step 3's fd would be out of order. */
            var K = 'proto-cosign';
            var mw = comcon.mediate(sock, comcon.allow(['address','fd']));
            mw = comcon.mediate(mw, comcon.protocol('fd','address'));
            mw = comcon.mediate(mw, comcon.cosign({ key: K, quorum: 2,
                                      within: 60, as: 'alice' }));
            var W = comcon.include(
                "function(a){ var v=s[a.op]; return v===undefined?'-':'ok'; }",
                { imports: [], grants: { s: mw } });

            var mv = comcon.mediate(sock, comcon.allow(['address','fd']));
            mv = comcon.mediate(mv, comcon.cosign({ key: K, quorum: 2,
                                      within: 60, as: 'bob' }));
            var V = comcon.include(
                "function(a){ var v=s[a.op]; return v===undefined?'-':'ok'; }",
                { imports: [], grants: { s: mv } });

            var cb = counts();
            o.splitSeq = [W({ op: 'fd' }), V({ op: 'fd' }),
                          W({ op: 'fd' }), W({ op: 'address' })];
            o.splitFired = fired(cb, counts());

            /* ...and a BUDGET-denied operation must not advance it either: a
             * budget of 1 over a two-step protocol spends its one use on the
             * first step, and the second is denied by the budget -- so the
             * cursor must still be at the second step, not past it. */
            var mb = comcon.mediate(sock, comcon.allow(['address','port','fd']));
            mb = comcon.mediate(mb, comcon.protocol('address','fd'));
            mb = comcon.mediate(mb, comcon.uses('proto:budget', 1, 60));
            var fb = comcon.include(
                "function(a){ return [s.address===undefined?'-':'ok',"
              + " s.fd===undefined?'-':'ok']; }",
                { imports: [], grants: { s: mb } });
            var bb = counts();
            o.budgetSteps = fb({});
            o.budgetFired = fired(bb, counts());

            /* --- an outbound capability: protocol('request') is a one-shot --- */
            var oc = nginx.outbound();
            var om = comcon.mediate(oc, comcon.allowHosts('https://*.example.com'));
            om = comcon.mediate(om, comcon.protocol('request'));
            var fo = comcon.include(
                "function(a){ return [out.request('https://a.example.com/1'),"
              + " out.request('https://a.example.com/2')]; }",
                { imports: [], grants: { out: om } });
            o.outbound = fo({});
            o.outboundQueued = oc.pending().requests.length;

            /* --- the meet --- */
            function meet(a, b2) {
                try {
                    comcon.mediate(comcon.mediate(sock,
                        comcon.protocol.apply(null, a)),
                        comcon.protocol.apply(null, b2));
                    return 'ALLOWED';
                } catch (e) { return e.code || e.name; }
            }
            o.meet = { same:  meet(['address','fd'], ['address','fd']),
                       order: meet(['address','fd'], ['fd','address']),
                       star:  meet(['address*','fd'], ['address','fd']) };

            /* --- nothing is defaulted --- */
            o.bad = {};
            var cases = {
                empty:     [],
                unknown:   ['frobnicate'],
                mixed:     ['address', 'request'],
                duplicate: ['address', 'port', 'address'],
                tooMany:   ['address','port','fd','listener','address2','b','c','d','e'],
                notAName:  ['ad dress']
            };
            Object.keys(cases).forEach(function (k) {
                try { comcon.protocol.apply(null, cases[k]); o.bad[k] = 'ACCEPTED'; }
                catch (e) { o.bad[k] = e.code || e.name; }
            });

            /* --- applied to the WRONG KIND of capability, it is a POLICY error
             * at mediate() and not a run-time denial: every step would be a
             * violation, so the capability would be dead. */
            o.wrongKind = 'ACCEPTED';
            try {
                comcon.mediate(nginx.outbound(), comcon.protocol('address'));
            } catch (e) { o.wrongKind = e.code || e.name; }
            o.wrongKindRev = 'ACCEPTED';
            try {
                comcon.mediate(sock, comcon.protocol('request'));
            } catch (e) { o.wrongKindRev = e.code || e.name; }

            /* --- a HAND-BUILT descriptor cannot slip past the producer --- */
            o.handBuilt = 'ACCEPTED';
            try {
                comcon.mediate(sock, { flavor: 'protocol' });
            } catch (e) { o.handBuilt = e.code || e.name; }

            /* --- audit mode logs and ALLOWS, like every gate.  And the cursor
             * must NOT advance on a violation that was merely logged: a
             * transition that was not legal is not a transition. */
            comcon.mode('audit');
            o.audited = armSock(['address','fd'])({ ops: ['fd','address','fd'] });
            comcon.mode('enforce');

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(19);

my $raw = http_get('/proto');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 500)); $o = {};
};

is($o->{driverError}, undef, 'protocol() is usable on a capability')
    or diag("driverError: $o->{driverError}");

# --- order ---
is_deeply($o->{inOrder}, ['ok','ok','ok','ok'],
   "the declared order is allowed: address, port, port again (starred), fd")
    or diag('inOrder: ' . encode_json($o->{inOrder}));

is_deeply($o->{outOfOrder}, ['-'],
   'a step taken out of order is denied');
is_deeply($o->{outOfOrderFired}, ['cap.protocol'],
   '...with cap.protocol, its own code: "out of order" and "too often" send an '
   . 'operator to different places');

is_deeply($o->{skipStar}, ['ok','ok'],
   'a STARRED step may happen zero times, so address then fd is legal');

# --- one-shot ---
is_deeply($o->{oneShot}, ['ok','-','-'],
   'once the last step is consumed THE CONVERSATION IS OVER: protocol(\'fd\') '
   . 'is a one-shot capability, which uses(1) cannot express -- a budget is '
   . 'fleet-wide and resets with its window, a protocol is per-wrapper and never '
   . 'resets')
    or diag('oneShot: ' . encode_json($o->{oneShot}));

is_deeply($o->{recover}, ['-','ok','ok'],
   'a violation does not advance the cursor: `port` before `address` is denied '
   . 'and the conversation is still at step 0, so address then fd still work')
    or diag('recover: ' . encode_json($o->{recover}));

is_deeply($o->{perWrapper}, [['ok'],['ok'],['-']],
   'the cursor is PER WRAPPER, not fleet-wide: two fragments given the same '
   . 'one-shot protocol each get their own shot, and neither gets a second -- a '
   . 'session type describes one conversation')
    or diag('perWrapper: ' . encode_json($o->{perWrapper}));

# --- the gate's position, measured on ONE wrapper ---
is_deeply($o->{splitSeq}, ['-','ok','ok','ok'],
   'a cosign-denied operation does NOT advance the conversation: the same '
   . 'wrapper is refused for want of a second signature, the signature arrives '
   . 'elsewhere, and its RETRY of the same first step is legal -- which it could '
   . 'only be if the cursor had stayed put. The transition is CHECKED before the '
   . 'gates that can still refuse and COMMITTED after them, because a gate whose '
   . 'decision is also its effect can only ever be last')
    or diag('splitSeq: ' . encode_json($o->{splitSeq}));
is_deeply($o->{splitFired}, ['cap.cosign'],
   '...and the only denial in that sequence is the COSIGNATURE: no cap.protocol, '
   . 'so nothing in it was ever out of order')
    or diag('splitFired: ' . encode_json($o->{splitFired}));

is_deeply($o->{budgetSteps}, ['ok','-'],
   'a budget of 1 over a two-step protocol: the first step spends the use, the '
   . 'second is denied')
    or diag('budgetSteps: ' . encode_json($o->{budgetSteps}));
is_deeply($o->{budgetFired}, ['budget.uses'],
   '...and the denial is the BUDGET, not the protocol: the cursor was not '
   . 'advanced past a step whose operation never happened')
    or diag('budgetFired: ' . encode_json($o->{budgetFired}));

# --- outbound ---
is_deeply($o->{outbound}, [1, undef],
   'protocol(\'request\') on an outbound capability is one intent, ever')
    or diag('outbound: ' . encode_json($o->{outbound}));
is($o->{outboundQueued}, 1, '...and only the first reached the queue');

# --- the meet ---
is_deeply($o->{meet},
   { same => 'ALLOWED', order => 'E_CAP_ESCALATE', star => 'E_CAP_ESCALATE' },
   'an identical protocol composes; a different ORDER or a different starring '
   . 'is refused -- two session types do not intersect in one session type, so '
   . 'a meet would have to guess and guessing widens')
    or diag('meet: ' . encode_json($o->{meet}));

# --- nothing defaulted ---
is_deeply($o->{bad},
   { empty => 'E_CAP_FLAVOR', unknown => 'E_CAP_FLAVOR', mixed => 'E_CAP_FLAVOR',
     duplicate => 'E_CAP_FLAVOR', tooMany => 'E_CAP_FLAVOR',
     notAName => 'E_CAP_FLAVOR' },
   'every malformed protocol is REFUSED: no steps (a dead capability is spelled '
   . 'revoke()), an operation no capability has, a MIXTURE of two capability '
   . 'kinds, a REPEATED step (whose order depends on which reading the matcher '
   . 'takes), too many steps, and a name that is not one')
    or diag('bad: ' . encode_json($o->{bad}));

is_deeply([$o->{wrongKind}, $o->{wrongKindRev}],
   ['E_CAP_FLAVOR', 'E_CAP_FLAVOR'],
   'a protocol naming the WRONG capability kind is refused at mediate(), in '
   . 'both directions: every step would be a violation, so the capability would '
   . 'be dead -- that is a policy error, not a run-time denial')
    or diag('wrongKind: ' . encode_json([$o->{wrongKind}, $o->{wrongKindRev}]));

is($o->{handBuilt}, 'E_CAP_FLAVOR',
   'a HAND-BUILT protocol descriptor with no steps is refused at the producer');

# --- audit ---
is_deeply($o->{audited}, ['ok','ok','ok'],
   'in AUDIT mode an out-of-order call logs and ALLOWS, so an order can be '
   . 'watched before it bites -- and the cursor does not advance on a violation '
   . 'that was merely logged, so the legal sequence that follows still runs')
    or diag('audited: ' . encode_json($o->{audited}));

$t->stop();
