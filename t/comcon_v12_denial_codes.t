#!/usr/bin/perl

# COMCON V12 — the golden denial-code corpus.
#
# MANUAL §3.2 tells tenants: "Codes are stable across releases -- pin your CI to
# codes, not to message text." That sentence is a promise the project had no way
# to keep or break on purpose. t/tools/golden-denials.js freezes each code with a
# probe that provokes it; this file runs them and compares the per-code counters
# before and after, so a renamed or renumbered code breaks HERE rather than in
# every tenant's CI at once.
#
# EACH PROBE MUST FIRE ITS OWN CODE AND NOTHING UNDECLARED. A probe that trips a
# second gate on the way (reaching a listener requires passing the sock.listener
# edge first) declares it in `also`; anything outside code+also is a finding, not
# a detail, because it means the corpus does not describe what the probe does.
#
# TWO AXES, FROZEN SEPARATELY. A DENIAL code names the gate that fired while a
# fragment was RUNNING (`nginx.tenantDenials().byOp`). A REFUSAL code names why
# a fragment was never admitted at all (`e.code` on the throw, `code` on an
# admit() verdict, enumerated by `comcon.refusalCodes()`). This file freezes
# both, because "my policy tripped a gate on request 41" and "my policy will not
# load" are different failures with different fixes.
#
# The refusal half was MESSAGE TEXT until [TBD-2] shipped: V12's first run dated
# that gap, and these rows are what closed it. Each refusal row asserts four
# things -- the code is the one frozen, the human sentence survives beside it,
# the code is IN the message (an error log has no properties to read), and the
# probe is still REFUSED at all.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $golden = do { open my $f, '<', 'tools/golden-denials.js' or die $!; local $/; <$f> };

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

        location /v12 { }
    }
}
EOF

$t->write_file_expand('root.js', <<"JS");
/* ===== the frozen corpus, verbatim from t/tools/golden-denials.js ===== */
$golden
/* ===== end of the corpus ===== */

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
/* The outbound capability the two `out.*` rows need.  Mediated with a glob that
 * matches nothing the probes ask for, so `out.host` fires; `out.drain` fires on
 * the reach gate regardless of the glob. */
var outbound = comcon.mediate(nginx.outbound(),
                              comcon.allowHosts("*.trusted.example"));
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;

function counts() {
    var d = nginx.tenantDenials(), out = {};
    for (var k in d.byOp) {
        if (Object.prototype.hasOwnProperty.call(d.byOp, k)) { out[k] = d.byOp[k]; }
    }
    return out;
}

/* fragments prepared on pass 1 and called on pass 2 */
var deferred = {};

/* ONE definition of "the capability this row asks for", used by both passes */
function capFor(row) {
    /* Which capability this row's probe needs.  Until `allowHosts` every row
     * was a socket, so the harness simply assumed one; a second kind had to be
     * named rather than smuggled in through probe text. */
    /* `windowClosed` is COMPUTED, not a constant: a window that is reliably
     * closed cannot be written down, because days==0 is refused and a one-minute
     * slot is a flake waiting to happen.  So the harness derives one from the
     * clock -- today, an hour that has already passed -- which is closed whenever
     * this runs. */
    if (row.cap === 'windowClosed') {
        var now = new Date();
        var nm = now.getUTCHours() * 60 + now.getUTCMinutes();
        var DN = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
        function hm(x) {
            x = ((x % 1440) + 1440) % 1440;
            var h = Math.floor(x / 60), m = x % 60;
            return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m;
        }
        return comcon.mediate(sock,
                   comcon.window({ days: DN[now.getUTCDay()],
                                   from: hm(nm - 120), to: hm(nm - 60) }));
    }

    /* `cosignSolo`: one acting principal against a quorum of two.  Reliably
     * denied however often it runs -- the record is the SET of principals, so
     * `solo` joins it once and the count never reaches two. */
    if (row.cap === 'cosignSolo') {
        return comcon.mediate(sock,
                   comcon.cosign({ key: 'v12solo', quorum: 2, within: 300,
                                   as: 'solo' }));
    }

    /* `protoWrong`: a one-step protocol over `port`, probed by reading
     * `address`.  Out of order on the very first operation, so it is denied
     * however often the row runs -- a violation does not advance the cursor. */
    if (row.cap === 'protoWrong') {
        return comcon.mediate(sock, comcon.protocol('port'));
    }

    var cap = (row.cap === 'outbound') ? outbound : sock;
    if (row.budget) {
        cap = comcon.mediate(cap, comcon.uses(row.budget.key, row.budget.limit,
                                              row.budget.window));
    }
    if (row.ttl) { cap = comcon.mediate(cap, comcon.ttl(row.ttl)); }
    return cap;
}

/* The grant object for a row: one capability, under the name its probe uses. */
function grantsFor(row) {
    var g = {};
    g[row.grant || 's'] = capFor(row);
    return g;
}

function fired(before, after) {
    var out = [];
    for (var k in after) {
        if (Object.prototype.hasOwnProperty.call(after, k)
            && (after[k] - (before[k] || 0)) > 0) { out.push(k); }
    }
    return out.sort();
}

locs.find(function (l) { return l.path === "/v12"; }).handler = function (req) {
    var o = { rows: [], refusals: [], codesSeen: [] }, i;

    for (i = 0; i < GOLDEN.length; i++) {
        var row = GOLDEN[i], rec = { code: row.code };
        if (row.unreachable) { rec.unreachable = true; o.rows.push(rec); continue; }
        /*
         * A row needing wall-clock time is INCLUDED on the first request and
         * CALLED on the second. Both halves matter: ngx_time() is nginx's cached
         * clock so nothing expires while one handler runs, AND a `ttl` clock
         * starts when the capability crosses into the compartment -- so
         * including it after the sleep would hand out a fresh lifetime and the
         * row would report 'alive' forever. It did, until this was fixed.
         */
        if (row.sleepBefore) {
            if (!/phase=2/.test(req.args)) {
                deferred[row.code] = comcon.include(row.probe,
                    { grants: grantsFor(row) });
                rec.deferred = true; o.rows.push(rec); continue;
            }
            var df = deferred[row.code];
            if (!df) { rec.missing = true; o.rows.push(rec); continue; }
            var before0 = counts();
            try { rec.result = df({}); } catch (e0) { rec.result = 'threw'; }
            var after0 = counts();
            rec.fired = fired(before0, after0);
            rec.firedOwn = (rec.fired.indexOf(row.code) >= 0);
            rec.undeclared = rec.fired.filter(function (c) {
                return [row.code].concat(row.also || []).indexOf(c) < 0; });
            if (row.expect !== null && row.expect !== undefined) {
                rec.expectOk = (rec.result === row.expect);
            }
            o.rows.push(rec); continue;
        }

        /*
         * A LEFTOVER row is measured during a DIFFERENT fragment's invocation,
         * because that is the only place its code can fire: the probe queues more
         * jobs than the drain's budget, and what is left runs inside the next
         * fragment -- holding capabilities that are not that fragment's. Same
         * reason the `ttl` row has a two-phase shape: a code whose only reachable
         * path needs two invocations cannot be pinned by one.
         */
        if (row.leftover) {
            comcon.mode(row.mode);
            var lcap = capFor(row);
            var lf = comcon.include(row.probe, { grants: grantsFor(row) });
            try { rec.result = lf({}); } catch (el) { rec.result = 'threw'; }

            var lb = counts();
            var bystander = comcon.include(
                "async function(x){ return await 1; }", {});
            try { bystander({}); } catch (eb) { /* not the subject */ }
            rec.fired = fired(lb, counts());
            rec.firedOwn = (rec.fired.indexOf(row.code) >= 0);
            rec.undeclared = rec.fired.filter(function (c) {
                return [row.code].concat(row.also || []).indexOf(c) < 0; });
            if (row.expect !== null && row.expect !== undefined) {
                rec.expectOk = (rec.result === row.expect);
            }
            if (lcap) { /* held so the wrapper is not collected mid-row */ }
            o.rows.push(rec); continue;
        }

        comcon.mode(row.mode);
        /* a row may ask for its capability to be BUDGETED (the `uses`
           mediation); everything else is granted straight. */
        var cap = capFor(row);
        var f = comcon.include(row.probe, { grants: grantsFor(row) });
        var before = counts();
        try { rec.result = f({}); } catch (e) { rec.result = 'threw'; }
        var after = counts();

        var got = fired(before, after);
        var allowed = [row.code].concat(row.also || []).sort();
        rec.fired = got;
        rec.firedOwn = (got.indexOf(row.code) >= 0);
        rec.undeclared = got.filter(function (c) { return allowed.indexOf(c) < 0; });
        if (row.expect !== null && row.expect !== undefined) {
            rec.expectOk = (rec.result === row.expect);
        }
        o.rows.push(rec);
    }
    comcon.mode('enforce');

    /* every code the runtime knows about, from the report itself */
    var d = nginx.tenantDenials();
    for (var k in d.byOp) {
        if (Object.prototype.hasOwnProperty.call(d.byOp, k)) { o.codesSeen.push(k); }
    }
    o.codesSeen.sort();
    o.golden = GOLDEN.map(function (r) { return r.code; }).sort();
    o.complete = (JSON.stringify(o.codesSeen) === JSON.stringify(o.golden));

    /* ---- the REFUSAL half ([TBD-2]): admission-time, code-carrying ---- */
    for (i = 0; i < REFUSALS.length; i++) {
        var r = REFUSALS[i], rec2 = { code: r.code }, got = null, msg = '';

        try {
            if (r.via === 'admit') {
                var verdict = eval(r.probe);
                rec2.certified = verdict.certified;
                got = verdict.code;
                msg = verdict.reject || '';

            } else if (r.via === 'stale') {
                /* the only row that needs a sequence: admit a fragment, free
                   its epoch the way a superseded bindAt epoch is freed, then
                   call the handle that is now stale. */
                var frag = comcon.include('function(){ return 1; }',
                                          { imports: [] });
                comcon.__freeConfined(frag.handle);
                frag({});
                rec2.accepted = true;

            } else {
                /* 'include' and 'call' are both just expressions that must
                   throw; the difference is only which layer refuses. */
                eval(r.probe);
                rec2.accepted = true;
            }
        } catch (e) {
            got = e.code;
            msg = e.message || '';
        }

        rec2.got = (got === undefined) ? null : got;
        rec2.ok = (got === r.code);
        /* the prose must survive too: the code is the contract, but an
           operator reading the error log gets the sentence. */
        rec2.prose = (msg.indexOf(r.msg) >= 0);
        /* and the code must be IN the message, so a log line carries it */
        rec2.inMsg = (msg.indexOf('[' + r.code + ']') >= 0)
                     || (r.via === 'admit');
        o.refusals.push(rec2);
    }

    /* completeness, the same two-way check the denial codes get: the runtime
       enumerates its own closed set, and the corpus must equal it. */
    o.refusalRuntime = comcon.refusalCodes().slice().sort();
    o.refusalGolden = REFUSALS.map(function (r) { return r.code; }).sort();
    o.refusalComplete = (JSON.stringify(o.refusalRuntime)
                         === JSON.stringify(o.refusalGolden));

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(13);

###############################################################################

# the first pass prepares the time-dependent rows; the second probes them
http_get('/v12');
select(undef, undef, undef, 1.5);
my $r = http_get('/v12?phase=2');
diag($1) if $r =~ /("rows":.*?"complete":\w+)/;

# --- the corpus describes the runtime, both ways --------------------------
like($r, qr/"complete":true/,
     'the corpus and the runtime agree on the code set -- every code the '
     . 'runtime reports is frozen here, and every frozen code is one the '
     . 'runtime reports');

# --- each probe fires its own code ---------------------------------------
like($r, qr/\{"code":"sock.listener","result":"denied","fired":\["sock.listener"\],"firedOwn":true,"undeclared":\[\]/,
     'sock.listener: the reach edge fires, and the read is denied');
like($r, qr/"code":"listener.read"[^}]*"firedOwn":true/,
     'listener.read: the listener getter fires its own code');
like($r, qr/"code":"listener.serverByName"[^}]*"firedOwn":true/,
     'listener.serverByName: the server escalation fires its own code');
like($r, qr/\{"code":"sock.mutate","result":"denied"[^}]*"firedOwn":true/,
     'sock.mutate: close() on a socket the compartment does not own is denied');

like($r, qr/\{"code":"cap.expired","result":"expired"[^}]*"firedOwn":true/,
     'cap.expired: a capability granted with a ttl STOPS WORKING when its '
     . 'lifetime passes -- pinned explicitly, because the first version of this '
     . 'row reported "alive" with firedOwn:false and the suite passed anyway: '
     . 'nothing asserted it, and a corpus row that never fires is decoration');

unlike($r, qr/"undeclared":\["/,
     'NO probe trips a gate the corpus does not declare -- a probe that fires '
     . 'something undeclared means the corpus no longer describes it');

# --- the unreachable row is recorded, not omitted -------------------------
like($r, qr/\{"code":"enum.sockets","unreachable":true\}/,
     'enum.sockets is recorded as unreachable WITH ITS REASON rather than left '
     . 'out: a confined fragment cannot obtain nginx.http, so the gate is '
     . 'defence-in-depth for a path that does not exist yet');

# --- the REFUSAL half ([TBD-2]) -------------------------------------------
diag($1) if $r =~ /("refusals":.*?"refusalComplete":\w+)/;

like($r, qr/"refusalComplete":true/,
     'the corpus and comcon.refusalCodes() agree on the REFUSAL set -- the '
     . 'admission half is now enumerable from the runtime, the same two-way '
     . 'check the denial codes get');

unlike($r, qr/"ok":false/,
     'EVERY refusal row reports its own code -- a probe whose refusal carries '
     . 'a different code than the corpus froze means the taxonomy has drifted '
     . 'or the probe provokes a different gate than it claims');

unlike($r, qr/"prose":false/,
     'the human sentence survives beside the code: an operator reading the '
     . 'error log still gets a message that says what happened, not a bare '
     . 'identifier to look up');

unlike($r, qr/"inMsg":false/,
     'the code is IN the thrown message (bracketed) as well as on .code, so a '
     . 'refusal is greppable in an error log where no one can read a property');

unlike($r, qr/"accepted":true/,
     'NO refusal probe was ADMITTED -- a probe that stops being refused is the '
     . 'failure mode a frozen corpus exists to catch (the gate went away, and '
     . 'the code it used to raise became unreachable in silence)');
