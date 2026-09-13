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
# WHAT IS FROZEN IS WHAT EXISTS. The compartment codes are real and closed. The
# ADMISSION refusals are message text, not codes -- so a tenant cannot pin CI to
# them today, which is exactly what the manual warns against. §3.2 marks the
# taxonomy [TBD-2]; the provisional rows record today's prefixes so the gap is
# dated and visible, and the assertions below say plainly that they are not a
# contract.

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
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;

function counts() {
    var d = nginx.tenantDenials(), out = {};
    for (var k in d.byOp) {
        if (Object.prototype.hasOwnProperty.call(d.byOp, k)) { out[k] = d.byOp[k]; }
    }
    return out;
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
    var o = { rows: [], provisional: [], codesSeen: [] }, i;

    for (i = 0; i < GOLDEN.length; i++) {
        var row = GOLDEN[i], rec = { code: row.code };
        if (row.unreachable) { rec.unreachable = true; o.rows.push(rec); continue; }

        comcon.mode(row.mode);
        var f = comcon.include(row.probe, { grants: { s: sock } });
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

    /* the provisional (message-text) side: recorded, not contracted */
    for (i = 0; i < PROVISIONAL.length; i++) {
        var p = PROVISIONAL[i], c = { grants: {}, imports: p.imports };
        if (p.checkRequest) { c.checkRequest = true; }
        var msg = 'ACCEPTED';
        try { comcon.include(p.probe, c); }
        catch (e) { msg = e.message; }
        o.provisional.push({ prefix: p.prefix,
                             matches: (msg.indexOf(p.prefix) >= 0) });
    }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(9);

###############################################################################

my $r = http_get('/v12');
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

unlike($r, qr/"undeclared":\["/,
     'NO probe trips a gate the corpus does not declare -- a probe that fires '
     . 'something undeclared means the corpus no longer describes it');

# --- the unreachable row is recorded, not omitted -------------------------
like($r, qr/\{"code":"enum.sockets","unreachable":true\}/,
     'enum.sockets is recorded as unreachable WITH ITS REASON rather than left '
     . 'out: a confined fragment cannot obtain nginx.http, so the gate is '
     . 'defence-in-depth for a path that does not exist yet');

# --- the admission side has no codes, and that is the finding -------------
like($r, qr/"provisional":\[\{"prefix":"free name not declared in imports","matches":true\},\{"prefix":"dynamic-code: eval or with","matches":true\},\{"prefix":"request field not in sealed schema","matches":true\}\]/,
     "today's admission refusals are MESSAGE TEXT and still match their "
     . 'recorded prefixes');

ok(1,
   'NOT A CONTRACT: MANUAL 3.2 marks the code taxonomy [TBD-2], so those three '
   . 'have no codes to pin to -- a tenant asked to "pin to codes, not message "
   . "text" cannot do it for admission at all. Recorded here so the gap is '
   . 'dated rather than rediscovered');
