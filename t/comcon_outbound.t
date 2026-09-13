#!/usr/bin/perl

# M-LIB `allowHosts` — the OUTBOUND capability.
#
# The last vocabulary word that was blocked on a missing mechanism rather than on
# enforcement: `allowHosts` had nothing to mediate, because a confined fragment
# held no way to reach the network at all.
#
# WHAT IT IS, AND WHY IT IS NOT A FETCH.  A fragment is invoked SYNCHRONOUSLY --
# JS_Call, then JSON-stringify the result.  There is no promise detection and no
# pending-job drain, so a capability that performs network I/O cannot be handed
# to a fragment without making fragment invocation asynchronous, which would
# touch the F6/F12 deadline and the F2 per-invocation memory allowance on the
# most safety-critical path in the system.  That is its own increment.
#
# So this capability RECORDS INTENT and the host performs the I/O -- the pattern
# M-CFG already established for config: the tenant proposes what it cannot apply.
# `request()` is synchronous, checks the destination against the glob IN THE
# COMPARTMENT, and appends a descriptor the host reads afterwards.  The mediation
# therefore bites where the capability is exercised, which is what makes
# `allowHosts` an attenuation of authority rather than a filter over data.
#
# Asserted here:
#
#   GLOB        an in-glob destination is recorded; an out-of-glob one is DENIED
#               with `out.host` and never reaches the queue.  Host globs wildcard
#               on the LEFT ("*.example.com"), where route globs wildcard on the
#               right -- one matcher in C knows both shapes, so the two cannot
#               drift.
#   REACH       pending()/clear() are the HOST's half.  A fragment that could
#               drain the queue would read what a SIBLING fragment sharing the
#               same cap had recorded, which is a channel and not an outbound
#               request, so the A1 reach gate denies it with `out.drain`.
#   COMPOSES    with `uses` (a rate limit on outbound) and `ttl` (a lifetime),
#               because it rides the same wrapper fields as every other
#               mediation.
#   MEET        two host globs are not ordered, so re-mediating with a different
#               one is REFUSED rather than guessed -- the `routes` rule, for the
#               identical reason.
#   NO DEFAULT  an empty glob is refused rather than read as "*": a missing
#               destination list is a mistake, not permission.
#   AUDIT       in audit mode the refusal is logged and ALLOWED, like every gate.
#
# The host's own wrapper is deliberately unmediated -- it is the authority being
# attenuated.  A tenant never sees it; include() refuses a grant whose flavour it
# cannot translate rather than defaulting to full authority.

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

        location /ob { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

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

/* One probe text, run against differently-mediated capabilities: the arm is the
 * mediation, never the fragment, so a difference in outcome is a difference in
 * the membrane and not in the code that pushes on it. */
var PROBE = "function(a){ var r = {};"
          + "  r.inGlob  = out.request('https://api.example.com/v1');"
          + "  r.offGlob = out.request('https://evil.net/steal');"
          + "  return r; }";

var DRAIN = "function(a){ var r = {};"
          + "  r.pending = out.pending();"
          + "  return r; }";

locs.forEach(function (l) {
    if (l.path !== '/ob') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* ---- the glob, and what reaches the host queue ---- */
            var cap = nginx.outbound();
            var f = comcon.include(PROBE,
                { imports: [], grants: { out: comcon.mediate(cap,
                    comcon.allowHosts('*.example.com')) } });
            var b0 = counts();
            o.glob = f({});
            o.globFired = fired(b0, counts());
            o.hostQueue = cap.pending();

            /* ---- the host's own wrapper is unmediated: the authority itself ---- */
            o.hostDirect = cap.request('https://anything.at.all/x');

            /* ---- the reach gate on the drain half ---- */
            var cap2 = nginx.outbound();
            var g = comcon.include(DRAIN,
                { imports: [], grants: { out: comcon.mediate(cap2,
                    comcon.allowHosts('*.example.com')) } });
            var b1 = counts();
            o.drain = g({});
            o.drainFired = fired(b1, counts());

            /* ---- composes with uses: a rate limit on outbound ---- */
            var cap3 = nginx.outbound();
            var med3 = comcon.mediate(cap3, comcon.allowHosts('*.example.com'));
            med3 = comcon.mediate(med3, comcon.uses('ob:probe', 2, 60));
            var h = comcon.include(
                "function(a){ var n = 0, i;"
              + "  for (i = 0; i < 4; i++) {"
              + "    if (out.request('https://api.example.com/' + i) !== undefined) { n++; }"
              + "  } return n; }",
                { imports: [], grants: { out: med3 } });
            var b2 = counts();
            o.budgeted = h({});
            o.budgetFired = fired(b2, counts());

            /* ---- composes with ttl ---- */
            var cap4 = nginx.outbound();
            var med4 = comcon.mediate(
                comcon.mediate(cap4, comcon.allowHosts('*.example.com')),
                comcon.ttl(3600));
            var k = comcon.include(
                "function(a){ return out.request('https://api.example.com/t'); }",
                { imports: [], grants: { out: med4 } });
            o.withTtl = k({});

            /* ---- the meet: two host globs are not ordered ---- */
            try {
                comcon.mediate(comcon.mediate(nginx.outbound(),
                                   comcon.allowHosts('*.example.com')),
                               comcon.allowHosts('*.other.net'));
                o.meet = 'ALLOWED';
            } catch (e) { o.meet = e.code || e.name; }

            /* the SAME glob composes, because it is the same attenuation */
            try {
                comcon.mediate(comcon.mediate(nginx.outbound(),
                                   comcon.allowHosts('*.example.com')),
                               comcon.allowHosts('*.example.com'));
                o.meetSame = 'ok';
            } catch (e) { o.meetSame = e.code || e.name; }

            /* ---- nothing is defaulted ---- */
            try { comcon.allowHosts(''); o.emptyGlob = 'ALLOWED'; }
            catch (e) { o.emptyGlob = e.code || e.name; }

            /* ---- a URL with credentials is refused, not parsed around ---- */
            var cap5 = nginx.outbound();
            var m = comcon.include(
                "function(a){ try { return out.request("
              + "'https://api.example.com@evil.net/x'); }"
              + "  catch (e) { return 'threw'; } }",
                { imports: [], grants: { out: comcon.mediate(cap5,
                    comcon.allowHosts('*.example.com')) } });
            o.creds = m({});
            o.credsQueue = cap5.pending().requests.length;

            /* ---- audit mode logs and ALLOWS, like every other gate ---- */
            comcon.mode('audit');
            var cap6 = nginx.outbound();
            var n = comcon.include(
                "function(a){ return out.request('https://evil.net/audited'); }",
                { imports: [], grants: { out: comcon.mediate(cap6,
                    comcon.allowHosts('*.example.com')) } });
            o.audited = n({});
            o.auditQueue = cap6.pending().requests.length;
            comcon.mode('enforce');

            /* F13's discipline applied to the surface this increment adds:
             * a capability a tenant holds should not be the one thing without
             * a type and a class. */
            var rows = nginx.describe(cap), names = [];
            for (var ri = 0; ri < rows.length; ri++) {
                names.push(rows[ri].name + ':' + rows[ri]['class']);
            }
            o.described = names.sort();

        } catch (e) {
            o.driverError = String(e && e.message);
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(19);

my $raw = http_get('/ob');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 400)); $o = {};
};

is($o->{driverError}, undef, 'the outbound capability is reachable and mediable');

# ---- the glob ----
is($o->{glob}{inGlob}, 1,
   'an in-glob destination is recorded (ticket 1)');
is($o->{glob}{offGlob}, undef,
   'an out-of-glob destination is DENIED -- request() returns undefined');
is_deeply($o->{globFired}, ['out.host'],
   'and it fires out.host, and nothing else');
is(scalar(@{ $o->{hostQueue}{requests} || [] }), 1,
   'the host queue holds ONLY the permitted request: a denied destination never '
   . 'reaches it, so the host cannot perform what the glob refused')
    or diag("queue: " . encode_json($o->{hostQueue}));
is($o->{hostQueue}{requests}[0]{url}, 'https://api.example.com/v1',
   'and it is the one the fragment was allowed to ask for');

# ---- the host's own wrapper is the authority, unmediated ----
cmp_ok($o->{hostDirect}, '>', 0,
   "the host's own wrapper is unmediated -- it is the authority being attenuated, "
   . 'and a tenant never holds it');

# ---- the reach gate ----
is($o->{drain}{pending}, undef,
   'a fragment CANNOT drain the queue: pending() is the host half');
is_deeply($o->{drainFired}, ['out.drain'],
   'and the refusal is out.drain, not a generic error -- a fragment reading the '
   . 'queue would read what a sibling fragment recorded');

# ---- composition ----
is($o->{budgeted}, 2,
   'composes with uses(): 4 attempts, a limit of 2, exactly 2 recorded')
    or diag("budgetFired: " . encode_json($o->{budgetFired}));
is($o->{withTtl}, 1, 'composes with ttl(): a live lifetime still records');

# ---- the meet, and the absence of defaults ----
is($o->{meet}, 'E_CAP_ESCALATE',
   'two DIFFERENT host globs are refused rather than guessed: host globs are '
   . 'not ordered, so a meet would widen one of them -- the routes rule');
is($o->{meetSame}, 'ok',
   'the SAME glob composes, because it is the same attenuation');
is($o->{emptyGlob}, 'E_CAP_FLAVOR',
   'an empty glob is REFUSED, not read as "*": a missing destination list is a '
   . 'mistake, not permission');

# A URL carrying credentials is refused rather than parsed around: in an
# allowHosts world "https://api.example.com@evil.net/x" is an invitation to
# smuggle a host past a glob, and a parser that merely looks for "//" reads the
# wrong half as the host.
is($o->{creds}, 'threw',
   'a URL with credentials is REFUSED outright, not resolved to one of its two '
   . 'plausible hosts');
is($o->{credsQueue}, 0, 'and nothing it named reached the host queue');

# Audit mode: logged and ALLOWED, like every other gate in this system, so a
# destination policy can be watched before it is enforced.
cmp_ok($o->{audited}, '>', 0,
   'in AUDIT mode an out-of-glob destination is logged and ALLOWED, so a '
   . 'destination list can be watched before it bites');
is($o->{auditQueue}, 1,
   'and it does reach the host queue in audit mode -- which is what "allowed" '
   . 'has to mean for the mode to be worth having');

is_deeply($o->{described},
   ['clear:guarded', 'pending:guarded', 'request:safe'],
   'the outbound capability is CLASSIFIED like every other surface (F13): the '
   . "tenant's half is safe, the host's half is guarded")
    or diag("described: " . join(' ', @{ $o->{described} || [] }));

$t->stop();
