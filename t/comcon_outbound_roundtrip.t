#!/usr/bin/perl

# The outbound ROUND TRIP: propose -> the host performs -> the policy sees the
# result.  And the scheme-qualified glob.
#
# `t/comcon_outbound.t` tests the mediation exhaustively -- the glob admits and
# denies, the reach gate holds, composition works -- and tests the round trip NOT
# AT ALL.  A capability whose whole point is that the HOST performs the I/O, with
# nothing showing an intent becoming a response, is a half-delivery: every
# assertion about it could hold while the queued request was unperformable.
#
# So this file performs them, against a backend in the same nginx, and hands the
# results back to a SECOND fragment invocation.  Two invocations is not a
# workaround, it is the shape the synchronous invoke imposes: ask, then be told.
# A real `fetch` capability waits on asynchronous fragment invocation.
#
# ALSO HERE: the scheme-qualified glob.  `protocol` in MANUAL's vocabulary is
# enforced OPERATION ORDER ("handshake", "frames*", "close") -- a session type
# over a capability's methods -- so restricting the scheme cannot borrow that
# name.  It is an attenuation of the DESTINATION, so it lives in allowHosts: a
# glob may be written scheme-first, and a glob without one matches any scheme,
# which is exactly what shipped.  The scheme itself is matched EXACTLY rather
# than globbed, because "http*" admitting https is the opposite of what an
# operator writing a scheme wants.

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

        location /trip   { }
        location /scheme { }

        # the backend the queued intents are actually performed against
        location /backend_a { }
        location /backend_b { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var PORT = %%PORT_8080%%;

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

locs.forEach(function (l) {
    if (l.path === '/backend_a') {
        l.handler = function (r) { r.respond(200, {}, 'A-OK'); };
    }
    if (l.path === '/backend_b') {
        l.handler = function (r) { r.respond(503, {}, 'B-DOWN'); };
    }
});

/* PHASE 1: the policy asks.  PHASE 2: the same policy is handed what came back
 * and decides.  The fragment is one source text with two entry points, because
 * a policy split across two files would make this look like plumbing rather
 * than one decision made in two steps. */
var POLICY =
    "function(a){"
  + "  if (a.phase === 'ask') {"
  + "    out.request('http://127.0.0.1:' + a.port + '/backend_a');"
  + "    out.request('http://127.0.0.1:' + a.port + '/backend_b');"
  + "    out.request('http://169.254.169.254/latest/meta-data');"
  + "    return 'asked';"
  + "  }"
  /* the verdict is computed FROM THE RESPONSES -- the point of the round trip */
  + "  var ok = 0, bad = 0, i;"
  + "  for (i = 0; i < a.results.length; i++) {"
  + "    if (a.results[i].status === 200) { ok++; } else { bad++; }"
  + "  }"
  + "  return { ok: ok, bad: bad, verdict: (bad === 0) ? 'healthy' : 'degraded' };"
  + "}";

locs.forEach(function (l) {
    if (l.path !== '/trip') { return; }
    l.handler = async function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var cap = nginx.outbound();
            var f = comcon.include(POLICY,
                { imports: [], grants: { out: comcon.mediate(cap,
                    comcon.allowHosts('127.0.0.1')) } });

            var b0 = counts();
            o.asked = f({ phase: 'ask', port: PORT });
            o.askFired = fired(b0, counts());

            /* the host performs what the glob allowed -- and only that */
            o.queued = cap.pending().requests.length;
            o.results = await comcon.std.outbound.perform(cap, req);
            o.drained = cap.pending().requests.length;

            /* and the policy decides from the responses */
            o.verdict = f({ phase: 'decide', results: o.results });

        } catch (e) {
            o.driverError = String(e && e.message);
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});

locs.forEach(function (l) {
    if (l.path !== '/scheme') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var cap = nginx.outbound();
            var f = comcon.include(
                "function(a){ return {"
              + "  https: out.request('https://api.example.com/x'),"
              + "  http:  out.request('http://api.example.com/x'),"
              + "  wrongHost: out.request('https://api.other.net/x') }; }",
                { imports: [], grants: { out: comcon.mediate(cap,
                    comcon.allowHosts('https://*.example.com')) } });
            var b0 = counts();
            o.scheme = f({});
            o.schemeFired = fired(b0, counts());
            o.queued = cap.pending().requests.map(function (x) { return x.url; });

            /* The scheme is matched EXACTLY, not globbed.  A glob written
             * with a wildcard scheme matches NEITHER, because "http*" admitting
             * https is the opposite of what someone writing a scheme means.
             * Asserted because the control for it did not fire: the existing
             * cases refuse http under either rule, so exactness was unmeasured. */
            var cap3 = nginx.outbound();
            var w = comcon.include(
                "function(a){ return {"
              + "  https: out.request('https://api.example.com/x'),"
              + "  http:  out.request('http://api.example.com/x') }; }",
                { imports: [], grants: { out: comcon.mediate(cap3,
                    comcon.allowHosts('http*://*.example.com')) } });
            o.wildScheme = w({});
            o.wildQueued = cap3.pending().requests.length;

            /* clear(n) removes only the first n: an intent appended while a
             * drain was awaiting must survive it. */
            var cap4 = nginx.outbound();
            cap4.request('https://a.example.com/1');
            cap4.request('https://b.example.com/2');
            cap4.clear(1);
            o.afterPartial = cap4.pending().requests.map(function (x) { return x.url; });

            /* a glob with NO scheme still matches any scheme: what shipped */
            var cap2 = nginx.outbound();
            var g = comcon.include(
                "function(a){ return {"
              + "  https: out.request('https://api.example.com/x'),"
              + "  http:  out.request('http://api.example.com/x') }; }",
                { imports: [], grants: { out: comcon.mediate(cap2,
                    comcon.allowHosts('*.example.com')) } });
            o.noScheme = g({});
        } catch (e) {
            o.driverError = String(e && e.message);
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(15);

sub get_json {
    my ($p) = @_;
    my $r = http_get($p);
    $r =~ s/^.*?\r\n\r\n//s;
    my $j;
    eval { $j = decode_json($r); 1 } or do {
        diag("non-JSON from $p: " . substr($r, 0, 300)); return {};
    };
    return $j;
}

# ---------------------------------------------------------- the round trip ---
my $o = get_json('/trip');
is($o->{driverError}, undef, 'the round trip completed');
is($o->{asked}, 'asked', 'phase 1: the policy asked for three destinations');
is_deeply($o->{askFired}, ['out.host'],
   'the link-local metadata address was DENIED by the glob -- the destination a '
   . 'confined policy most wants and least should have');
is($o->{queued}, 2, 'only the two permitted intents reached the host queue');

cmp_ok(scalar(@{ $o->{results} || [] }), '==', 2,
   'the host performed both, and reported on both');
is($o->{results}[0]{status}, 200, 'the first intent really was performed (200 from the backend)')
    or diag("results: " . encode_json($o->{results}));
is($o->{results}[1]{status}, 503,
   'and the second reports ITS OWN status -- a failing destination is a result, '
   . 'not an exception that abandons the others');
is($o->{drained}, 0, 'the queue is cleared after performing');

# THE POINT: the verdict is computed from data that did not exist when the
# policy was admitted.
is($o->{verdict}{verdict}, 'degraded',
   'phase 2: the policy decided FROM THE RESPONSES -- propose, perform, decide, '
   . 'which is the whole loop the capability exists for');
is($o->{verdict}{ok}, 1, '...counting the one that succeeded');

# ------------------------------------------------------ the scheme-qualified --
my $s = get_json('/scheme');
is($s->{driverError}, undef, 'the scheme-qualified glob is usable');
is_deeply([sort @{ $s->{schemeFired} }], ['out.host'],
   'a scheme-qualified glob refuses the wrong scheme AND the wrong host, both '
   . 'as out.host -- one gate, because both are the destination');
is_deeply($s->{queued}, ['https://api.example.com/x'],
   'only the https destination on the right host was recorded: the http one was '
   . 'refused even though its HOST matched');

is($s->{wildQueued}, 0,
   'the scheme is matched EXACTLY, never globbed: "http*://" admits neither '
   . 'http nor https, because a wildcard scheme admitting TLS and plaintext '
   . 'alike is the opposite of what writing a scheme asks for');

is_deeply($s->{afterPartial}, ['https://b.example.com/2'],
   'clear(n) removes only the first n intents -- so an intent appended while a '
   . 'drain is awaiting I/O survives the clear that follows it');

$t->stop();
