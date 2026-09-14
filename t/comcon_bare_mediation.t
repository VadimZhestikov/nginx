#!/usr/bin/perl

# Every mediation word, applied ALONE to every capability kind.
#
# The vocabulary is normally reached in COMPOSITION: `allowHosts` then `uses`
# then `ttl`, because that is how a real policy reads.  Probing each word by
# itself has now found a defect twice — a bare `window()` grant fell through to
# the unknown-flavour refusal (v5.87), and this file is the same question asked
# one axis over: not "does the word work alone" but "does it work alone on each
# KIND of capability".
#
# It does not.  `uses`, `ttl` and `cosign` all normalize to an allow-everything
# MASK — they attenuate how many times, how long and by whom, never WHAT — and a
# mask lives on the socket shape.  So a bare one of them over an OUTBOUND
# capability arrived at the translation as kind 0 and was refused with
#
#     comcon.include: grant is not a NginxSocket or NginxServer
#
# for a grant that was a perfectly good outbound capability.  FAIL CLOSED, so
# nothing was ever widened — but the operator was told their capability was the
# wrong type when the real answer is that the WORD carries no type at all.  The
# kind belongs to the capability, so it is now read back from the capability.
#
# The matrix is written as a matrix on purpose.  Two words in two positions over
# two capability kinds is eight questions, and the reason this went unnoticed is
# that every existing test asked the two or three of them a real policy asks.

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

        location /bare  { }
        location /share { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

var DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
function hhmm(m) {
    m = ((m % 1440) + 1440) % 1440;
    var h = Math.floor(m / 60), mm = m % 60;
    return (h < 10 ? '0' : '') + h + ':' + (mm < 10 ? '0' : '') + mm;
}

/* One interceptor per word, each in its OPEN form — the point is the kind, not
 * the gate, so every one of these must let the operation through. */
function word(w, tag) {
    var now = new Date();
    var nm = now.getUTCHours() * 60 + now.getUTCMinutes();
    if (w === 'uses')   { return comcon.uses('bare:' + tag, 5, 60); }
    if (w === 'ttl')    { return comcon.ttl(3600); }
    if (w === 'cosign') { return comcon.cosign({ key: 'bare:' + tag, quorum: 2,
                                                 within: 60, as: 'alice' }); }
    if (w === 'window') { return comcon.window({ days: DAYS[now.getUTCDay()],
                              from: hhmm(nm - 60), to: hhmm(nm + 60) }); }
    if (w === 'redact') { return comcon.redact([]); }
    return null;
}

var WORDS = ['uses', 'ttl', 'cosign', 'window', 'redact'];

locs.forEach(function (l) {

if (l.path === '/bare') {
    l.handler = function (req) {
        var o = { out: {}, sock: {} };
        try {
            comcon.mode('enforce');

            /* OUTBOUND, each word alone */
            WORDS.forEach(function (w) {
                try {
                    var c = nginx.outbound();
                    var m = comcon.mediate(c, word(w, 'out.' + w));
                    var f = comcon.include(
                        "function(a){ return out.request('https://x.example.com/1'); }",
                        { imports: [], grants: { out: m } });
                    var r = f({});
                    /* cosign(2) with one principal is DENIED, which is the word
                     * working; anything else must produce a queued intent. */
                    o.out[w] = (w === 'cosign')
                               ? ((r === undefined) ? 'gated' : 'UNGATED')
                               : ((r === undefined) ? 'DENIED' : 'ok');
                } catch (e) { o.out[w] = 'THREW:' + (e.code || e.name); }
            });

            /* SOCKET, each word alone */
            WORDS.forEach(function (w) {
                try {
                    var m = comcon.mediate(sock, word(w, 'sock.' + w));
                    var f = comcon.include("function(a){ return s.port; }",
                                           { imports: [], grants: { s: m } });
                    var r = f({});
                    o.sock[w] = (w === 'cosign')
                                ? ((r === undefined) ? 'gated' : 'UNGATED')
                                : ((r === undefined) ? 'DENIED' : 'ok');
                } catch (e) { o.sock[w] = 'THREW:' + (e.code || e.name); }
            });

            /* A PROMOTED grant is a MEDIATED wrapper whose destination set is
             * everything -- not the host's own unmediated capability.  The
             * difference is observable: the gates must still be on the path, so a
             * ttl of 1 second on a bare-mediated outbound cap still expires. */
            var c2 = nginx.outbound();
            var m2 = comcon.mediate(c2, comcon.uses('bare:gated', 1, 60));
            var f2 = comcon.include(
                "function(a){ return [out.request('https://a.example.com/1'),"
              + " out.request('https://a.example.com/2')]; }",
                { imports: [], grants: { out: m2 } });
            o.stillGated = f2({});

            /* ...and an UNMEDIATED outbound grant is still REFUSED.  Accepting
             * one would be a widening, so the promotion is deliberately scoped to
             * grants that carry a mediation. */
            o.unmediated = 'ACCEPTED';
            try {
                comcon.include("function(a){ return 1; }",
                    { imports: [], grants: { out: nginx.outbound() } });
            } catch (e) { o.unmediated = e.code || e.name; }

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

/* ONE NAME, ONE COUNTER — across capability KINDS.
 *
 * `uses(key, ...)` documents that two capabilities share a budget exactly when
 * the operator names the same counter.  The outbound path did not namespace its
 * key the way the socket path did, so uses('k') on a socket and uses('k') on an
 * outbound capability were TWO counters: a budget an operator believed was one
 * limit of 3 was two limits of 3. */
if (l.path === '/share') {
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');
            var K = 'shared-counter';

            var so = comcon.mediate(sock, comcon.uses(K, 3, 60));
            var fs = comcon.include("function(a){ return s.port; }",
                                    { imports: [], grants: { s: so } });
            var c = nginx.outbound();
            var oo = comcon.mediate(c, comcon.allowHosts('https://*.example.com'));
            oo = comcon.mediate(oo, comcon.uses(K, 3, 60));
            var fo = comcon.include(
                "function(a){ return out.request('https://a.example.com/1'); }",
                { imports: [], grants: { out: oo } });

            /* Two spends on the socket, two on the outbound cap: with ONE
             * counter of 3 the fourth must be denied. */
            o.spend = [fs({}) === undefined ? 'denied' : 'ok',
                       fo({}) === undefined ? 'denied' : 'ok',
                       fs({}) === undefined ? 'denied' : 'ok',
                       fo({}) === undefined ? 'denied' : 'ok'];
        } catch (e) { o.driverError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
}

});
JS

$t->try_run('no js module')->plan(7);

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

my $o = get_json('/bare');
is($o->{driverError}, undef, 'the matrix ran') or diag($o->{driverError});

is_deeply($o->{out},
   { uses => 'ok', ttl => 'ok', cosign => 'gated', window => 'ok',
     redact => 'ok' },
   'every word alone over an OUTBOUND capability: `uses`, `ttl` and `cosign` '
   . 'normalize to a MASK, and a mask is the socket shape -- so all three were '
   . 'refused with a message about NginxSocket for a grant that was a perfectly '
   . 'good outbound capability. The kind belongs to the capability, not to the '
   . 'word')
    or diag('out: ' . encode_json($o->{out}));

is_deeply($o->{sock},
   { uses => 'ok', ttl => 'ok', cosign => 'gated', window => 'ok',
     redact => 'ok' },
   '...and every word alone over a SOCKET capability, which is the half that '
   . 'always worked -- here so that a future change cannot fix one kind by '
   . 'breaking the other')
    or diag('sock: ' . encode_json($o->{sock}));

is_deeply($o->{stillGated}, [1, undef],
   'a PROMOTED grant is a MEDIATED wrapper whose destination set is everything, '
   . 'not the host\'s own unmediated capability: the budget gate is still on '
   . 'the path, so uses(1) denies the second request')
    or diag('stillGated: ' . encode_json($o->{stillGated}));

is($o->{unmediated}, 'E_CAP_GRANT',
   'an UNMEDIATED outbound grant is still refused -- accepting one would be a '
   . 'widening, so the promotion is scoped to grants that carry a mediation');

my $s = get_json('/share');
is($s->{driverError}, undef, 'the shared-counter probe ran');
is_deeply($s->{spend}, ['ok', 'ok', 'ok', 'denied'],
   'ONE NAME IS ONE COUNTER, across capability kinds: the outbound path did not '
   . 'namespace its budget key the way the socket path did, so uses(\'k\') on a '
   . 'socket and uses(\'k\') on an outbound capability were two counters -- a '
   . 'budget an operator believed was one limit of 3 was two limits of 3')
    or diag('spend: ' . encode_json($s->{spend}));

$t->stop();
