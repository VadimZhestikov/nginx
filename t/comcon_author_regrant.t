#!/usr/bin/perl

# THE AUTHORING TIER, PHASE 3 -- RE-GRANTING: a parent hands its own
# capabilities down, narrowed, and never anything else.
#
# `author.include(src, {grants: {name: cap}, attenuate: {name: {allow|redact,
# ttlSeconds}}})`.  The parent passes the wrapper objects IT was granted; each
# becomes the sub-fragment's by COPY-THEN-NARROW: the parent's opaque is
# duplicated -- generation, budget, window, cosignature and all -- the owner
# becomes the sub-fragment's, and only the mask (AND, subset asserted) and the
# expiry (min) can move, downward.  So A(sub) ⊆ A(parent) is a property of the
# copy, and a STALE parent yields a stale child rather than a laundered fresh
# one -- which is why the handle is never re-wrapped.
#
# What this file pins:
#   /regrant   the socket words, the refusals, the identity of what the sub gets
#   /conform   BOTH ARMS AGREE: the host attenuating with allow(['address']) and
#              a parent re-granting allow(['address']) produce the same view --
#              with a control arm made to DIFFER, so the comparison can fail
#   /ttl       the expiry meets by MIN: a 1-second re-grant expires while the
#              parent's own (3600 s) and a 7200-second re-grant do not
#   /stale     the host closes the socket: the parent's wrapper goes stale, the
#              sub-fragment's copy goes stale with it, and a re-grant made from
#              the stale parent is stale too (the laundering path, closed)
#   /outfacet  the other two kinds: an outbound capability keeps its host glob,
#              a route facet keeps its route glob; the words that do not apply
#              to them are refused; a session-typed wrapper is not re-grantable
#
# The ttl arm sleeps; it is one request per phase for the usual harness reason.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /regrant { }
        location /conform { }
        location /ttl { }
        location /stale { }
        location /close { }
        location /outfacet { }
        location /a { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var srv  = nginx.http.servers[0];

var sock  = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var sock2 = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
var out   = nginx.outbound();

/* what a sub-fragment sees of a socket wrapper `s`: the four fields' types */
var VIEW = "function(){ return { address: typeof s.address, port: typeof s.port,"
         + " fd: typeof s.fd, listener: String(s.listener) }; }";

/* ---- /regrant: the words and the refusals ---- */
var parent = comcon.include(
    "function(req){" +
    "  var out = {};" +
    "  function attempt(name, fn) {" +
    "    try { out[name] = { ok: fn() }; }" +
    "    catch (e) { out[name] = { code: e.code === undefined ? null : e.code," +
    "                             msg: String(e.message || e) }; } }" +
    "  out.parent = { address: typeof sock.address, port: typeof sock.port, fd: typeof sock.fd };" +
    "  attempt('verbatim',     function () { return author.include(req.view, {imports: ['String'], grants: {s: sock}})(); });" +
    "  attempt('redactPort',   function () { return author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {redact: ['port']}}})(); });" +
    "  attempt('allowAddress', function () { return author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['address']}}})(); });" +
    "  attempt('escalateFd',   function () { author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['fd']}}}); return 'admitted'; });" +
    "  attempt('badField',     function () { author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['nope']}}}); return 'admitted'; });" +
    "  attempt('badWord',      function () { author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {uses: 5}}}); return 'admitted'; });" +
    "  attempt('bothWords',    function () { author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['address'], redact: ['port']}}}); return 'admitted'; });" +
    "  attempt('zeroTtl',      function () { author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {ttlSeconds: 0}}}); return 'admitted'; });" +
    "  attempt('authorRegrant', function () { author.include('function(){ return typeof a; }', {imports: [], grants: {a: author}}); return 'admitted'; });" +
    "  attempt('plainObject',  function () { author.include('function(){ return 1; }', {imports: [], grants: {x: {not: 'a cap'}}}); return 'admitted'; });" +
    "  attempt('badName',      function () { var g = {}; g['a){'] = sock; author.include('function(){ return 1; }', {imports: [], grants: g}); return 'admitted'; });" +
    "  attempt('subSeesGrant', function () { return author.include('function(){ return typeof s; }', {imports: [], grants: {s: sock}})(); });" +
    "  attempt('subNamesAuthor', function () { author.include('function(){ return typeof author; }', {imports: [], grants: {s: sock}}); return 'admitted'; });" +
    /* the sub-fragment's wrapper is ITS OWN: the parent cannot use it back --
       there is no path for that (JSON out), so this asks the sub to hand its
       wrapper to the parent the only way it could, and shows nothing crosses */
    "  attempt('noWrapperBack', function () { var r = author.include('function(){ return { s: s }; }', {imports: [], grants: {s: sock}})(); return typeof r.s + ':' + JSON.stringify(r) + ':' + typeof r.s.address; });" +
    "  out.used = author.used;" +
    "  return { status: 200, body: JSON.stringify(out) };" +
    "}",
    {imports: ['JSON', 'String'],
     grants: {sock: comcon.mediate(sock, comcon.allow(['address', 'port'])),
              author: comcon.author({subFragments: 16})}});

/* ---- /conform: both arms agree ---- */
var armA = comcon.include(VIEW,
    {imports: ['String'], grants: {s: comcon.mediate(sock, comcon.allow(['address']))}});
var conformParent = comcon.include(
    "function(req){" +
    "  var B = author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['address']}}})();" +
    "  var C = author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {allow: ['address', 'port']}}})();" +
    "  return { status: 200, body: JSON.stringify({ B: B, C: C }) };" +
    "}",
    {imports: ['JSON', 'String'],
     grants: {sock: comcon.mediate(sock, comcon.allow(['address', 'port'])),
              author: comcon.author({subFragments: 2})}});

/* ---- /ttl: the expiry meets by MIN.  The parent caches its sub-fragments
 * across invocations (an IIFE closure), so the second request reads through
 * wrappers minted on the first. ---- */
var ttlParent = comcon.include(
    "(function(){ var subs = null; return function(req){" +
    "  if (!subs) {" +
    "    subs = { shortLived: author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {ttlSeconds: 1}}})," +
    "             longLived:  author.include(req.view, {imports: ['String'], grants: {s: sock}, attenuate: {s: {ttlSeconds: 7200}}}) };" +
    "  }" +
    "  return { status: 200, body: JSON.stringify({ parent: typeof sock.address," +
    "           shortLived: subs.shortLived().address, longLived: subs.longLived().address }) };" +
    "}; })()",
    {imports: ['JSON', 'String'],
     grants: {sock: comcon.mediate(comcon.mediate(sock, comcon.allow(['address', 'port'])), comcon.ttl(3600)),
              author: comcon.author({subFragments: 2})}});

/* ---- /stale: the host closes sock2 between the two requests.  A stale
 * wrapper's getter THROWS ("invalid handle"), so each read is guarded and
 * reports 'threw' -- the point is that all three answer the SAME. ---- */
var READ = "function(){ try { return typeof s.address; } catch (e) { return 'threw'; } }";
var staleParent = comcon.include(
    "(function(){ var sub = null; return function(req){" +
    "  if (!sub) { sub = author.include(req.read, {imports: [], grants: {s: sock}}); }" +
    "  var fresh = author.include(req.read, {imports: [], grants: {s: sock}});" +
    "  var own; try { own = typeof sock.address; } catch (e) { own = 'threw'; }" +
    "  return { status: 200, body: JSON.stringify({ parent: own, sub: sub(), regrantedNow: fresh() }) };" +
    "}; })()",
    {imports: ['JSON', 'String'],
     grants: {sock: comcon.mediate(sock2, comcon.allow(['address', 'port'])),
              author: comcon.author({subFragments: 4})}});

/* ---- /outfacet: the other kinds, and the session-typed refusal ---- */
var otherParent = comcon.include(
    "function(req){" +
    "  var out = {};" +
    "  function attempt(name, fn) {" +
    "    try { out[name] = { ok: fn() }; }" +
    "    catch (e) { out[name] = { code: e.code === undefined ? null : e.code," +
    "                             msg: String(e.message || e) }; } }" +
    "  var OF = 'function(){ return { inGlob: typeof o.request(\"https://api.example.com/v1\")," +
    "           offGlob: typeof o.request(\"https://evil.net/x\")," +
    "           routeIn: f.allowed(\"/a/x\"), routeOut: f.allowed(\"/b/x\") }; }';" +
    "  attempt('both',       function () { return author.include(OF, {imports: [], grants: {o: outb, f: facet}})(); });" +
    "  attempt('outTtl',     function () { return author.include(OF, {imports: [], grants: {o: outb, f: facet}, attenuate: {o: {ttlSeconds: 60}}})(); });" +
    "  attempt('outMask',    function () { author.include(OF, {imports: [], grants: {o: outb, f: facet}, attenuate: {o: {redact: ['port']}}}); return 'admitted'; });" +
    "  attempt('facetTtl',   function () { author.include(OF, {imports: [], grants: {o: outb, f: facet}, attenuate: {f: {ttlSeconds: 60}}}); return 'admitted'; });" +
    "  attempt('protocol',   function () { author.include(req.view, {imports: ['String'], grants: {s: psock}}); return 'admitted'; });" +
    "  out.used = author.used;" +
    "  return { status: 200, body: JSON.stringify(out) };" +
    "}",
    {imports: ['JSON', 'String'],
     grants: {outb:  comcon.mediate(out, comcon.allowHosts('*.example.com')),
              facet: comcon.mediate(srv, comcon.routes('/a/*')),
              psock: comcon.mediate(sock, comcon.protocol('address', 'fd')),
              author: comcon.author({subFragments: 8})}});

for (var i = 0; i < locs.length; i++) {
    (function (path) {
        var run = { '/regrant': parent, '/conform': conformParent, '/ttl': ttlParent,
                    '/stale': staleParent, '/outfacet': otherParent }[path];
        if (run) {
            locs[i].handler = function (req) {
                try {
                    var o = run({ method: req.method, view: VIEW, read: READ });
                    if (path === '/conform') {
                        var both = JSON.parse(o.body);
                        both.A = armA();
                        o.body = JSON.stringify(both);
                    }
                    req.respond(o.status, {'content-type': 'application/json'}, o.body);
                } catch (e) {
                    req.respond(200, {'content-type': 'text/plain'},
                                'HOST CAUGHT code=' + String(e.code) + ' ' + String(e.message || e));
                }
            };
        } else if (path === '/close') {
            locs[i].handler = function (req) {
                sock2.close();
                req.respond(200, {'content-type': 'text/plain'}, 'closed');
            };
        }
    })(locs[i].path);
}
JS

$t->try_run('no js module')->plan(34);

sub body { my ($raw) = @_; $raw =~ s/^.*?\r\n\r\n//s; return $raw; }
sub js   { my ($raw) = @_; my $o; eval { $o = decode_json(body($raw)); 1 }
           or do { diag("non-JSON: " . substr(body($raw), 0, 500)); $o = {}; }; $o }

###############################################################################
# /regrant

my $o = js(http_get('/regrant'));
diag("regrant: " . substr(encode_json($o), 0, 1800));

is_deeply($o->{parent}, { address => 'string', port => 'number', fd => 'undefined' },
   'the parent holds address+port (fd redacted by the host)');
is_deeply($o->{verbatim}{ok}, { address => 'string', port => 'number', fd => 'undefined', listener => 'undefined' },
   'a verbatim re-grant gives the sub-fragment exactly the parent\'s view: fd stays redacted, listener stays denied');
is_deeply($o->{redactPort}{ok}, { address => 'string', port => 'undefined', fd => 'undefined', listener => 'undefined' },
   'redact: [port] narrows it further');
is_deeply($o->{allowAddress}{ok}, { address => 'string', port => 'undefined', fd => 'undefined', listener => 'undefined' },
   'allow: [address] narrows it to one field');
is($o->{escalateFd}{code}, 'E_CAP_ESCALATE',
   'allow: [fd] -- a field the parent does not hold -- is E_CAP_ESCALATE, not a wider grant');
is($o->{badField}{code}, 'E_CAP_FLAVOR', 'an unknown field name is E_CAP_FLAVOR');
is($o->{badWord}{code}, 'E_CAP_FLAVOR', 'a word outside {allow, redact, ttlSeconds} (`uses`) is E_CAP_FLAVOR');
is($o->{bothWords}{code}, 'E_CAP_FLAVOR', 'allow and redact together are refused');
is($o->{zeroTtl}{code}, 'E_CAP_FLAVOR', 'ttlSeconds: 0 is refused');
is($o->{authorRegrant}{code}, 'E_CAP_GRANT', 'an author capability is not re-grantable');
is($o->{plainObject}{code}, 'E_CAP_GRANT', 'a plain object is not a capability');
is($o->{badName}{code}, 'E_ADMIT_CONTRACT', 'a grant name that is not an identifier is refused');
is($o->{subSeesGrant}{ok}, 'object', 'the sub-fragment sees its grant as a bound name');
is($o->{subNamesAuthor}{code}, 'E_ADMIT_FREENAME', 'the sub-fragment still cannot name the parent\'s author cap');
like($o->{noWrapperBack}{ok}, qr/^object:\{"s":\{\}\}:undefined$/,
   'a wrapper returned by the sub-fragment does not cross back (JSON drops it)');
is($o->{used}, 5, 'five sub-fragments admitted; every refusal was free');

###############################################################################
# /conform: both arms agree

my $c = js(http_get('/conform'));
diag("conform: " . encode_json($c));
is_deeply($c->{B}, $c->{A},
   'BOTH ARMS AGREE: the host attenuating allow([address]) and a parent re-granting allow([address]) give the identical view');
isnt(encode_json($c->{C}), encode_json($c->{A}),
   'control: a wider re-grant (allow [address, port]) DIFFERS from arm A, so the comparison can fail');
is($c->{C}{port}, 'number', '... and differs exactly in the field the control kept');

###############################################################################
# /ttl

my $t1 = js(http_get('/ttl'));
diag("ttl phase 1: " . encode_json($t1));
is_deeply($t1, { parent => 'string', shortLived => 'string', longLived => 'string' },
   'phase 1: the parent and both re-grants read the address');
sleep 2;
my $t2 = js(http_get('/ttl'));
diag("ttl phase 2: " . encode_json($t2));
is($t2->{shortLived}, 'undefined', 'phase 2: the 1-second re-grant has expired (reads undefined)');
is($t2->{longLived}, 'string', '... the 7200-second re-grant has not: min(3600, 7200) is still alive');
is($t2->{parent}, 'string', '... and the parent\'s own 3600-second wrapper is still alive');

###############################################################################
# /stale

my $s1 = js(http_get('/stale'));
diag("stale phase 1: " . encode_json($s1));
is_deeply($s1, { parent => 'string', sub => 'string', regrantedNow => 'string' },
   'phase 1: parent, cached sub-fragment and a fresh re-grant all read the address');
like(body(http_get('/close')), qr/closed/, 'the host closes the socket');
my $s2 = js(http_get('/stale'));
diag("stale phase 2: " . encode_json($s2));
isnt($s2->{parent}, 'string', 'phase 2: the parent\'s wrapper is stale');
is($s2->{sub}, $s2->{parent}, '... the cached sub-fragment\'s copy is exactly as stale');
is($s2->{regrantedNow}, $s2->{parent},
   '... and a re-grant made FROM the stale parent is stale too: nothing was laundered fresh');

###############################################################################
# /outfacet

my $f = js(http_get('/outfacet'));
diag("outfacet: " . encode_json($f));
is_deeply($f->{both}{ok}, { inGlob => 'number', offGlob => 'undefined', routeIn => JSON::PP::true, routeOut => JSON::PP::false },
   'an outbound cap keeps its host glob and a facet keeps its route glob through a re-grant');
is_deeply($f->{outTtl}{ok}, $f->{both}{ok}, 'ttlSeconds on an outbound re-grant is accepted and changes nothing else');
is($f->{outMask}{code}, 'E_CAP_FLAVOR', 'a field mask on an outbound cap is E_CAP_FLAVOR');
is($f->{facetTtl}{code}, 'E_CAP_FLAVOR', 'a ttl on a facet is E_CAP_FLAVOR');
is($f->{protocol}{code}, 'E_CAP_ESCALATE', 'a session-typed wrapper is not re-grantable (its cursor is one conversation)');
is($f->{used}, 2, 'two admitted, three refused for free');

# Test::Nginx's DESTROY runs the two standing checks (no alerts, no sanitizer
# errors); destroyed here so they run before Test::Builder's END counts the plan.
undef $t;
