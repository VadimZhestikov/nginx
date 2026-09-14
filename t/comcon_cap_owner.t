#!/usr/bin/perl

# A GRANTED CAPABILITY BELONGS TO ONE FRAGMENT — the structural half of G6.16.
#
# v5.93 made every invocation drain its own queued jobs, so a continuation is
# charged to and gated at the fragment that created it.  That drain is
# BEST-EFFORT: it shares the settle loop's job budget, and a fragment that
# outruns the budget leaves work behind which runs inside a later, unrelated
# invocation.  G6.16 recorded the residual and named the fix rather than
# pretending the drain closed it:
#
#     bind each granted capability wrapper to the fragment it was granted to,
#     and have the gates refuse when the fragment being invoked is not that one.
#
# This is that.  Every granted wrapper — socket, outbound, COM facet — records
# its fragment, and every gate asks first.  A leftover job then still RUNS (it is
# ordinary JS; nothing can un-queue it) and gets NOTHING, which is the property
# worth having: the drain decides who is charged, and this decides who can spend.
#
# THE PROBE FORCES THE RESIDUAL RATHER THAN ARGUING ABOUT IT.  A fragment queues
# more jobs than the budget, so the drain provably cannot finish them; the
# remainder are run by the next invocation and must be denied.  The outbound
# queue's dropped counter makes that exact: 10,100 deferred requests, of which
# the budget lets 10,000 through, and the leftovers add NOTHING afterwards.
#
# `cap.owner` is the first denial code in the set that names a STRUCTURAL
# invariant rather than a policy the operator wrote.  Nobody configures it and
# nothing legitimate trips it: the host's own wrappers are unbound (owner 0) and a
# fragment's own wrappers match while it is running.  It fires only for code
# holding a capability that is not the running fragment's.

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

        location /owner      { }
        location /facetowner { }
        location /normal     { }
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
function delta(a, b, code) { return (b[code] || 0) - (a[code] || 0); }

locs.forEach(function (l) {

if (l.path === '/owner') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* A queues MORE jobs than the drain's budget, so the residual the
             * drain cannot cover is produced on purpose rather than argued about.
             * Each job spends A's outbound capability. */
            var cap = nginx.outbound();
            var m = comcon.mediate(cap, comcon.allowHosts('https://*.example.com'));
            var A = comcon.include(
                "function(a){ var i; for (i = 0; i < a.n; i++) {"
              + " Promise.resolve().then(function(){"
              + "   out.request('https://a.example.com/x'); }); }"
              + " return 'queued'; }",
                { imports: ['Promise'], grants: { out: m } });

            var b0 = counts();
            o.a = A({ n: 10100 });
            var q1 = cap.pending();
            o.afterA = { queued: q1.requests.length, dropped: q1.dropped || 0 };
            o.ownerDuringA = delta(b0, counts(), 'cap.owner');

            /* ...and now an unrelated fragment runs.  A's leftover jobs run
             * inside it -- they are ordinary JS and nothing can un-queue them --
             * and must get NOTHING. */
            var b1 = counts();
            var B = comcon.include("async function(b){ return await 1; }",
                                   { imports: [] });
            o.b = B({});
            var q2 = cap.pending();
            o.afterB = { queued: q2.requests.length, dropped: q2.dropped || 0 };
            o.ownerDuringB = delta(b1, counts(), 'cap.owner');

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* THE FACET, NEGATIVELY.  The positive facet case below shows a granted facet
 * works; only a leftover job can hold one that is not the running fragment's, so
 * the residual has to be forced here too -- otherwise the facet's owner check
 * would be code no control can break, in a file whose whole subject is that. */
if (l.path === '/facetowner') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var srv = nginx.http.servers[0];
            var fm = comcon.mediate(srv, comcon.routes('/facetowner*'));
            var A = comcon.include(
                "function(a){ var i; for (i = 0; i < a.n; i++) {"
              + " Promise.resolve().then(function(){"
              + "   var r = g.route; return r; }); }"
              + " return 'queued'; }",
                { imports: ['Promise'], grants: { g: fm } });

            var b0 = counts();
            o.a = A({ n: 10100 });
            o.ownerDuringA = delta(b0, counts(), 'cap.owner');

            var b1 = counts();
            var B = comcon.include("async function(b){ return await 1; }",
                                   { imports: [] });
            o.b = B({});
            o.ownerDuringB = delta(b1, counts(), 'cap.owner');

        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* THE OTHER HALF OF THE CONTROL: nothing legitimate may trip this.  The host's
 * own wrappers are unbound, a fragment's own wrappers match while it runs, and
 * two fragments granted the SAME mediated capability each get their own wrapper
 * -- so both work, and neither is the other's. */
if (l.path === '/normal') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var b0 = counts();

            /* the host's own socket, read directly */
            o.hostRead = (typeof sock.port === 'number') ? 'ok' : 'DENIED';

            /* the host's own outbound capability, used by the host */
            var cap = nginx.outbound();
            o.hostOutbound = (cap.request('https://anywhere.example/x') > 0)
                             ? 'ok' : 'DENIED';

            /* ONE mediated capability granted to TWO fragments: each include
             * builds its own wrapper, so both must work */
            var m = comcon.mediate(sock, comcon.allow(['port']));
            var P = "function(a){ var v = s.port; return v === undefined ? '-' : 'ok'; }";
            var f1 = comcon.include(P, { imports: [], grants: { s: m } });
            var f2 = comcon.include(P, { imports: [], grants: { s: m } });
            o.two = [f1({}), f2({}), f1({})];

            /* and a facet, the third wrapper kind */
            var srv = nginx.http.servers[0];
            var fm = comcon.mediate(srv, comcon.routes('/normal*'));
            var f3 = comcon.include(
                "function(a){ return (typeof g.route === 'string') ? 'ok' : '-'; }",
                { imports: [], grants: { g: fm } });
            o.facet = f3({});

            o.owner = delta(b0, counts(), 'cap.owner');

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(12);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {};
    };
    return $o;
}

my $o = get_json('/owner');
is($o->{driverError}, undef, 'the owner probe ran') or diag($o->{driverError});
diag('afterA: ' . encode_json($o->{afterA} || {}) . '  afterB: '
     . encode_json($o->{afterB} || {}));

is($o->{a}, 'queued', 'the fragment returned while its jobs were still queued');

is($o->{afterA}{dropped}, 10000 - 32,
   'THE DRAIN DID WHAT IT CAN: of 10,100 deferred requests the job budget let '
   . '10,000 through, attributed to the fragment that queued them (32 recorded, '
   . 'the rest counted as dropped). The remaining 100 are the residual G6.16 '
   . 'named and could not remove')
    or diag('afterA: ' . encode_json($o->{afterA}));
is($o->{ownerDuringA}, 0,
   '...and none of them tripped cap.owner, because during A the capability IS '
   . "A's");

is($o->{afterB}{dropped}, 10000 - 32,
   'AND THE LEFTOVERS GET NOTHING. They run inside the next, unrelated '
   . 'invocation -- they are ordinary JS and nothing can un-queue them -- and '
   . 'add not one request to the queue, because the capability they hold is not '
   . "the running fragment's")
    or diag('afterB: ' . encode_json($o->{afterB}));
cmp_ok($o->{ownerDuringB}, '>', 0,
   '...and they are COUNTED as cap.owner while doing it, so the refusal is '
   . 'observable rather than a silent nothing-happened. The drain decides who is '
   . 'charged; this decides who can spend')
    or diag("cap.owner during B: " . ($o->{ownerDuringB} // 'undef'));

# --- the facet, negatively --------------------------------------------------
my $f = get_json('/facetowner');
is($f->{driverError}, undef, 'the facet-owner probe ran') or diag($f->{driverError});
is($f->{ownerDuringA}, 0,
   'a granted FACET is usable by the fragment it was granted to, 10,100 times');
cmp_ok($f->{ownerDuringB}, '>', 0,
   '...and its leftover jobs are refused inside the next fragment, as cap.owner. '
   . 'Only a leftover job can hold a facet that is not the running fragment\'s, '
   . 'so without this the facet\'s check would be code no control can break -- in '
   . 'a file whose whole subject is that')
    or diag('facet: ' . encode_json($f));

# --- the other half of the control -----------------------------------------
my $n = get_json('/normal');
is($n->{driverError}, undef, 'the legitimate-use probe ran') or diag($n->{driverError});
is_deeply([$n->{hostRead}, $n->{hostOutbound}, $n->{two}, $n->{facet}],
   ['ok', 'ok', ['ok', 'ok', 'ok'], 'ok'],
   'NOTHING LEGITIMATE TRIPS IT: the host reads its own socket and spends its '
   . 'own outbound capability (both unbound, owner 0); one mediated capability '
   . 'granted to TWO fragments works for both, because each include builds its '
   . 'own wrapper; and a COM facet -- the third wrapper kind, so the fix has no '
   . 'named hole -- works too')
    or diag('normal: ' . encode_json($n));
is($n->{owner}, 0,
   '...with cap.owner firing exactly zero times across all of it. A gate that '
   . 'fires on correct use is not a gate, it is an outage');

$t->stop();
