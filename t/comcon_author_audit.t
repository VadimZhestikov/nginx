#!/usr/bin/perl

# THE AUTHOR CAPABILITY'S TWO GATES, IN BOTH POSTURES.
#
# `author.include()` asks two questions before anything else: is this caller
# the fragment the capability was granted to (`cap.owner`), and has the
# capability expired (`cap.expired`)?  They are the same two questions the
# socket, outbound and facet wrappers ask, with the same posture behaviour --
# and the two behave DIFFERENTLY under audit, which is what this file pins:
#
#   cap.owner    UNCONDITIONAL (v5.96): denied in every posture.  "Is this even
#                this fragment's capability?" is not a thing an operator can
#                observe and then enable, so audit mode logs it AND denies it.
#   cap.expired  ordinary: denied in enforce, logged-and-allowed in audit.
#
# G7.14 listed the audit half of the owner gate as unprobed, because nothing can
# carry an author capability to another fragment.  It is reachable through the
# one path that runs a fragment's code under an identity that is not its own: a
# LEFTOVER.  A fragment that queues more than the 10,000-job budget leaves the
# rest behind, and the next invocation drains them first, AS NOBODY (G6.16's
# accounting half, t/comcon_leftover_accounting.t).  A leftover that calls
# `author.include()` is a call on the capability by someone who is not its
# owner, in whatever posture the worker is in.
#
# The observable is the capability's own counter, `author.used`: it moves only
# when an include was ADMITTED, so "denied" and "allowed" read differently from
# the fragment's own next invocation, without trusting a log line.

use warnings;
use strict;

use Test::More;
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /owner { }
        location /ttl { }
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
function delta(a, b, k) { return (b[k] || 0) - (a[k] || 0); }

/* A queues 13,000 jobs when asked to, and reports its author counter when asked
 * that.  Only the LAST ten jobs call author.include(): the first 10,000 run in
 * A's own trailing drain (as A), so an include there would be A's own; the ten
 * at the tail are leftovers, drained as nobody by whoever is invoked next. */
var A = comcon.include(
    "function(a){" +
    "  if (a.report) { return { used: author.used }; }" +
    "  var i;" +
    "  for (i = 0; i < 13000; i++) {" +
    "    if (i >= 12990) {" +
    "      Promise.resolve().then(function(){" +
    "        author.include('function(){ return 1; }', {imports: []}); });" +
    "    } else {" +
    "      Promise.resolve().then(function(){});" +
    "    }" +
    "  }" +
    "  return 'queued';" +
    "}",
    { imports: ['Promise'], grants: { author: comcon.author({ subFragments: 100 }) } });

/* B is anyone else: its invocation is what drains A's leftovers first, as nobody */
var B = comcon.include("function(a){ return 'b'; }", { imports: [] });

/* T holds an author capability that expires one second after it was GRANTED
 * -- so it is included lazily, on the first /ttl request, not at config phase
 * (the clock starts when the capability crosses).  It includes on request and
 * reports whether that was admitted, denied, or refused for some other reason;
 * the counter is read inside the try, because an expired capability refuses
 * its getters too. */
var T = null;
function ttlSource() {
    return "function(a){" +
    "  try { var before = author.used;" +
    "    var f = author.include('function(){ return 1; }', {imports: []});" +
    "    return { result: 'admitted', moved: author.used - before, held: typeof f }; }" +
    "  catch (e) { return { result: String(e.message || e), moved: 0 }; }" +
    "}";
}

locs.forEach(function (l) {
    if (l.path === '/owner') {
        l.handler = function (req) {
            var mode = /audit/.test(req.args) ? 'audit' : 'enforce';
            var o = { mode: mode };
            try {
                comcon.mode(mode);
                o.usedBefore = A({ report: true }).used;
                o.queued = A({});
                var c0 = counts();
                o.b = B({});                          /* drains the leftovers, as nobody */
                var c1 = counts();
                o.ownerFired = delta(c0, c1, 'cap.owner');
                o.usedAfter = A({ report: true }).used;
            } catch (e) {
                o.error = String(e.message || e);
            }
            comcon.mode('enforce');
            req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
        };
    }
    if (l.path === '/ttl') {
        l.handler = function (req) {
            var mode = /audit/.test(req.args) ? 'audit' : 'enforce';
            var o = { mode: mode };
            try {
                comcon.mode(mode);
                if (T === null) {
                    T = comcon.include(ttlSource(), { imports: ['String'],
                        grants: { author: comcon.author({ subFragments: 100, ttlSeconds: 1 }) } });
                }
                var c0 = counts();
                o.t = T({});
                var c1 = counts();
                o.expiredFired = delta(c0, c1, 'cap.expired');
            } catch (e) {
                o.error = String(e.message || e);
            }
            comcon.mode('enforce');
            req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
        };
    }
});
JS

$t->try_run('no js module')->plan(16);

sub js { my ($raw) = @_; $raw =~ s/^.*?\r\n\r\n//s; my $o;
         eval { $o = decode_json($raw); 1 } or do { diag("non-JSON: " . substr($raw, 0, 400)); $o = {}; }; $o }

###############################################################################
# cap.owner: unconditional -- denied in both postures

my $e = js(http_get('/owner?mode=enforce'));
diag("owner/enforce: " . encode_json($e));
is($e->{error}, undef, 'enforce: the round trip ran');
cmp_ok($e->{ownerFired}, '>=', 10,
       'enforce: each leftover include() on the author capability fired cap.owner');
is($e->{usedAfter}, $e->{usedBefore}, 'enforce: author.used did not move');

my $a = js(http_get('/owner?mode=audit'));
diag("owner/audit: " . encode_json($a));
is($a->{error}, undef, 'audit: the round trip ran');
cmp_ok($a->{ownerFired}, '>=', 10,
       'audit: each leftover include() still fired cap.owner');
is($a->{usedAfter}, $a->{usedBefore}, 'audit: author.used did not move either');

# `used` is a LIVE count (a leftover's callable would be dropped at once
# anyway), so the witness that NOTHING was admitted is the include's own log
# line, which a leftover under either posture never produced
my $log = $t->read_file('error.log');
unlike($log, qr/sub-fragment \d+ authored by fragment/,
       'and NONE was admitted under either posture -- cap.owner is unconditional: audit logs it and denies it');
like($log, qr/op=cap\.owner obj="author" mode=audit n=\d+ unconditional=1/,
     'the audit-mode denial record says so: op=cap.owner ... mode=audit ... unconditional=1');
like($log, qr/NginxComconAuthor: this capability was granted to another fragment/,
     'and the leftover saw the refusal as its unhandled rejection');

###############################################################################
# cap.expired: ordinary -- denied in enforce, logged-and-allowed in audit

my $t0 = js(http_get('/ttl?mode=enforce'));
diag("ttl fresh: " . encode_json($t0));
is($t0->{t}{result}, 'admitted', 'a fresh 1-second author capability admits');
is($t0->{t}{moved}, 1, '... and the counter moved');

sleep 2;

my $t1 = js(http_get('/ttl?mode=enforce'));
diag("ttl expired/enforce: " . encode_json($t1));
like($t1->{t}{result}, qr/has expired/, 'expired, enforce: the include is denied');
is($t1->{expiredFired}, 1, '... cap.expired fired once');
is($t1->{t}{moved}, 0, '... and nothing was admitted');

my $t2 = js(http_get('/ttl?mode=audit'));
diag("ttl expired/audit: " . encode_json($t2));
is($t2->{expiredFired}, 3,
   'expired, audit: cap.expired fired three times -- two counter reads and the include, each a gated operation, each logged');
is($t2->{t}{result} . ':' . $t2->{t}{moved}, 'admitted:1',
   '... and the include was ALLOWED through -- the ordinary posture behaviour, unlike cap.owner');

# Test::Nginx's DESTROY runs the two standing checks; destroyed here so they
# run before Test::Builder's END counts the plan.
undef $t;
