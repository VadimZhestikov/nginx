#!/usr/bin/perl

# THE AUTHORING TIER, PHASE 2 -- a fragment that authors fragments.
#
# `comcon.author({subFragments: N})` is granted to a fragment like any other
# capability; inside, `author.include(source, {imports, ...})` runs the SAME
# admission pipeline the host runs (one pipeline, two entrances) and returns a
# callable that invokes the sub-fragment as a real fragment: its own handle and
# identity, its own deadline and allowance nested inside the parent's, the
# parent's posture.  What crosses the nested boundary is TEXT -- the argument in,
# the result out, an exception's message and code -- never an object.
#
# This file pins the basic contract from the PARENT's point of view (what a
# fragment authoring sub-fragments sees), plus the two fatal paths and the host
# side.  The depth-2 ESCAPE BATTERY -- every S6 probe answering identically at
# depth 2 as at depth 1 -- is t/comcon_author_depth2_gate.t.
#
# THE FATAL PATHS ARE ONE PER REQUEST, for the reason comcon_deadline_without_
# worker.t gives: Test::Nginx's http() carries an 8-second alarm, and a
# sub-fragment's deadline is bounded by the parent's 5-second default, so two in
# one request would race the harness rather than the code.

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

        location /basic { }
        location /spin { }
        location /oom { }
        location /uncaught { }
        location /late { }
        location /host { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* ---- the host side: what comcon.author() itself accepts and refuses ---- */
var host = {};
function tryHost(name, fn) {
    try { host[name] = { ok: fn() }; }
    catch (e) { host[name] = { code: e.code || null, msg: String(e.message || e) }; }
}
tryHost('noSpec',   function () { comcon.author(); return 'accepted'; });
tryHost('zero',     function () { comcon.author({subFragments: 0}); return 'accepted'; });
tryHost('frac',     function () { comcon.author({subFragments: 1.5}); return 'accepted'; });
tryHost('badTtl',   function () { comcon.author({subFragments: 1, ttlSeconds: 0}); return 'accepted'; });
tryHost('ok',       function () { return typeof comcon.author({subFragments: 1}); });
/* an author descriptor is NOT mediatable: mediate() snapshots it as a facet
   over a cap that is neither socket nor server, and include() refuses that */
tryHost('mediated', function () {
    comcon.include('function(a){ return 1; }',
                   {imports: [], grants: {a: comcon.mediate(comcon.author({subFragments: 1}),
                                                             comcon.ttl(60))}});
    return 'accepted';
});
/* describe(): the class is on the classified table like every other cap */
tryHost('describe', function () {
    var d = nginx.describeType('NginxComconAuthor');
    var names = [];
    for (var i = 0; i < d.length; i++) { names.push(d[i].name); }
    return names.sort().join(',');
});

/* ---- the parent: everything a fragment authoring sub-fragments can see ---- */
var parent = comcon.include(
    "function(req){" +
    "  var out = {};" +
    "  function attempt(name, fn) {" +
    "    try { out[name] = { ok: fn() }; }" +
    "    catch (e) { out[name] = { code: e.code === undefined ? null : e.code," +
    "                             msg: String(e.message || e) }; } }" +
    "  out.subFragments = author.subFragments;" +
    "  out.used0 = author.used;" +
    /* the plain path: author, invoke, get a value back */
    "  var add = author.include('function(a){ return a.x + a.y; }', {imports: []});" +
    "  out.used1 = author.used;" +
    "  out.callable = typeof add;" +
    "  out.sum = add({x: 2, y: 3});" +
    /* a non-object argument crosses as JSON too */
    "  out.scalar = add(5);" +
    /* a sub-fragment cannot even NAME its parent's grant: `author` is a free
       name to it, and admission refuses free names */
    "  attempt('namesParent', function () {" +
    "    author.include('function(){ return typeof author; }', {imports: []}); return 'admitted'; });" +
    "  attempt('freeName', function () {" +
    "    author.include('function(){ return nope; }', {imports: []}); return 'admitted'; });" +
    /* the contract words a sub-fragment may not use, refused rather than ignored */
    "  attempt('grantsWord', function () {" +
    "    author.include('function(){ return 1; }', {imports: [], grants: {}}); return 'admitted'; });" +
    "  attempt('onViolationWord', function () {" +
    "    author.include('function(){ return 1; }', {imports: [], onViolation: 'audit'}); return 'admitted'; });" +
    "  attempt('noContract', function () {" +
    "    author.include('function(){ return 1; }'); return 'admitted'; });" +
    "  attempt('noImports', function () {" +
    "    author.include('function(){ return 1; }', {}); return 'admitted'; });" +
    /* admission phase (iii): the contract's tests, run against the sub-fragment */
    "  attempt('testFails', function () {" +
    "    author.include('function(){ return 1; }', {imports: []," +
    "      tests: 'function(f){ if (f() !== 2) throw new Error(\"expected 2\"); }'}); return 'admitted'; });" +
    "  attempt('testPasses', function () {" +
    "    var f = author.include('function(){ return 2; }', {imports: []," +
    "      tests: function(f){ if (f() !== 2) throw new Error('expected 2'); }}); return f(); });" +
    /* the breakout shape is refused here too (F15 phase 2 through the other entrance) */
    "  attempt('breakout', function () {" +
    "    author.include('0)}); var __zz = 1; (function(){return(function(){ return 1; }', {imports: []}); return 'admitted'; });" +
    /* a sub-fragment is invoked synchronously; a promise is refused */
    "  attempt('promise', function () {" +
    "    var p = author.include('function(){ return Promise.resolve(1); }', {imports: ['Promise']});" +
    "    return p(); });" +
    /* an ordinary exception is catchable, arrives as text, and carries no object */
    "  attempt('throws', function () {" +
    "    var f = author.include('function(){ var e = new Error(\"boom\"); e.code = \"E_MINE\"; e.leak = function(){}; throw e; }'," +
    "                           {imports: ['Error']});" +
    "    try { f(); return 'no-throw'; }" +
    "    catch (e) { return { msg: String(e.message), code: e.code, leak: typeof e.leak, isError: e instanceof Error }; } });" +
    /* a returned function does not cross: JSON.stringify(function) is undefined */
    "  attempt('returnsFn', function () {" +
    "    var f = author.include('function(){ return function(){ return 1; }; }', {imports: []});" +
    "    return typeof f(); });" +
    /* toJSON runs on the sub-fragment's side, and its RESULT is what crosses */
    "  attempt('toJSON', function () {" +
    "    var f = author.include('function(){ return { toJSON: function(){ return \"marshalled\"; } }; }', {imports: []});" +
    "    return f(); });" +
    /* the budget: refused ones did not count; the last admitted spends it */
    "  out.usedMid = author.used;" +
    "  var spent = [];" +
    "  for (var i = 0; i < 20; i++) {" +
    "    try { author.include('function(){ return ' + i + '; }', {imports: []}); spent.push('ok'); }" +
    "    catch (e) { spent.push(e.code); out.spentMsg = String(e.message); break; } }" +
    "  out.spent = spent;" +
    "  out.usedEnd = author.used;" +
    "  return { status: 200, body: JSON.stringify(out) };" +
    "}",
    {imports: ['JSON', 'String', 'Promise'],
     grants: {author: comcon.author({subFragments: 8})}});

/* ---- the two fatal paths: a sub-fragment that outruns its bounds ---- */
var spinner = comcon.include(
    "function(req){" +
    "  var spin = author.include('function(){ for (;;) {} }', {imports: [], timeoutMs: 200});" +
    "  try { spin(); return { status: 200, body: 'returned' }; }" +
    "  catch (e) { return { status: 200, body: 'PARENT CAUGHT ' + String(e.message || e) }; }" +
    "}",
    {imports: ['String'], grants: {author: comcon.author({subFragments: 1})}});

/* out of memory is an ORDINARY exception at the host boundary (a fragment may
   catch its own), and the nested boundary keeps that: the first sub-fragment
   catches its own, the second does not and the PARENT catches it, as text */
var eater = comcon.include(
    "function(req){" +
    "  var catches = author.include(" +
    "    'function(){ var a = []; try { for (;;) { a.push(new Array(4096)); } } catch (e) { return \"sub caught\"; } }'," +
    "    {imports: ['Array'], memoryBytes: 1048576});" +
    "  var leaks = author.include(" +
    "    'function(){ var a = []; for (;;) { a.push(new Array(4096)); } }'," +
    "    {imports: ['Array'], memoryBytes: 1048576});" +
    "  var out = [];" +
    "  try { out.push('returned ' + catches()); } catch (e) { out.push('PARENT CAUGHT ' + String(e.message || e)); }" +
    "  try { out.push('returned ' + leaks()); } catch (e) { out.push('PARENT CAUGHT ' + String(e.message || e)); }" +
    "  return { status: 200, body: out.join(' | ') };" +
    "}",
    {imports: ['String'], grants: {author: comcon.author({subFragments: 2})}});

/* a refusal the parent does not catch reaches the host with its code intact */
var careless = comcon.include(
    "function(req){" +
    "  author.include('function(){ return 1; }', {imports: []});" +
    "  author.include('function(){ return 2; }', {imports: []});" +
    "  return { status: 200, body: 'both admitted' };" +
    "}",
    {imports: [], grants: {author: comcon.author({subFragments: 1})}});

for (var i = 0; i < locs.length; i++) {
    (function (path) {
        var run = { '/basic': parent, '/spin': spinner, '/oom': eater,
                    '/uncaught': careless }[path];
        if (run) {
            locs[i].handler = function (req) {
                var t0 = Date.now();
                try {
                    var o = run({ method: req.method });
                    req.respond(o.status, {'content-type': 'application/json'}, o.body);
                } catch (e) {
                    req.respond(200, {'content-type': 'text/plain'},
                                'HOST CAUGHT ' + (Date.now() - t0) + 'ms code=' + String(e.code)
                                + ' ' + String(e.message || e));
                }
            };
        } else if (path === '/late') {
            /* the include at REQUEST time (post-fork, in a worker), not at
               config phase: the other moment a fragment can be authored */
            locs[i].handler = function (req) {
                var out = {};
                try {
                    var late = comcon.include(
                        /* the grant is `author`; the parameter is `req` -- a grant
                           and the invocation argument sharing one name is the
                           argument shadowing the grant (found the hard way) */
                        "function(req){ var r = { incl: typeof author.include," +
                        "    tag: Object.prototype.toString.call(author) };" +
                        "  try { var f = author.include('function(){ return 41 + 1; }', {imports: []});" +
                        "    r.ret = typeof f; r.sub = f(); }" +
                        "  catch (e) { r.err = String(e.message || e); r.code = e.code; }" +
                        "  return r; }",
                        {imports: ['Object'], grants: {author: comcon.author({subFragments: 1})}});
                    out = late({});
                } catch (e) { out = { code: String(e.code), msg: String(e.message || e) }; }
                req.respond(200, {'content-type': 'application/json'}, JSON.stringify(out));
            };
        } else if (path === '/host') {
            locs[i].handler = function (req) {
                req.respond(200, {'content-type': 'application/json'}, JSON.stringify(host));
            };
        }
    })(locs[i].path);
}
JS

$t->try_run('no js module')->plan(43);

sub body { my ($raw) = @_; $raw =~ s/^.*?\r\n\r\n//s; return $raw; }
sub js   { my ($raw) = @_; my $o; eval { $o = decode_json(body($raw)); 1 }
           or do { diag("non-JSON: " . substr(body($raw), 0, 500)); $o = {}; }; $o }

###############################################################################
# the parent's view

my $o = js(http_get('/basic'));
diag("basic: " . substr(encode_json($o), 0, 1500));

is($o->{subFragments}, 8, 'author.subFragments reads the granted budget');
is($o->{used0}, 0, 'author.used starts at zero');
is($o->{callable}, 'function', 'author.include() returns a callable');
is($o->{used1}, 1, 'one sub-fragment authored: used is 1');
is($o->{sum}, 5, 'the sub-fragment ran and its result came back');
is($o->{scalar}, undef, 'a scalar argument crosses as JSON (5.x + 5.y is NaN -> null)');

is($o->{namesParent}{code}, 'E_ADMIT_FREENAME',
   'a sub-fragment cannot NAME its parent\'s grant: `author` is a free name to it');
is($o->{freeName}{code}, 'E_ADMIT_FREENAME',
   'a free name in a sub-fragment is refused at admission, exactly as at the host');
is($o->{grantsWord}{code}, 'E_ADMIT_CONTRACT',
   '`grants` is refused in a sub-fragment contract (phase 3), not ignored');
is($o->{onViolationWord}{code}, 'E_ADMIT_CONTRACT',
   '`onViolation` is refused: the posture is the parent\'s');
is($o->{noContract}{code}, 'E_ADMIT_CONTRACT',
   'a sub-fragment with no contract is refused: admission is not optional');
is($o->{noImports}{code}, 'E_ADMIT_CONTRACT',
   'a sub-fragment contract without `imports` is refused');
is($o->{testFails}{code}, 'E_ADMIT_TEST',
   'a contract test that throws refuses the sub-fragment (phase iii runs)');
is($o->{testPasses}{ok}, 2,
   'a contract test that passes admits it, and the callable works');
is($o->{breakout}{code}, 'E_ADMIT_SOURCE',
   'a wrapper breakout is refused through this entrance too (F15 phase 2)');
is($o->{promise}{code}, 'E_INVOKE_PENDING',
   'a sub-fragment returning a promise is refused: nested invocation is synchronous');

my $th = $o->{throws}{ok} || {};
like($th->{msg}, qr/boom/, 'a sub-fragment\'s exception reaches the parent as text');
is($th->{code}, 'E_MINE', 'the exception\'s string code crosses with it');
is($th->{leak}, 'undefined', 'nothing else on the exception crosses: no object, no closure');
ok($th->{isError}, 'what the parent catches is a real Error of its own');

is($o->{returnsFn}{ok}, 'undefined', 'a returned function does not cross (JSON drops it)');
is($o->{toJSON}{ok}, 'marshalled', 'toJSON runs on the sub-fragment\'s side; its result crosses');

is($o->{usedMid}, 6, 'refused sub-fragments did not spend the budget (6 admitted so far)');
is_deeply($o->{spent}, [('ok') x 2, 'E_AUTHOR_LIMIT'],
   'the budget is spent by exactly subFragments admissions, then E_AUTHOR_LIMIT');
like($o->{spentMsg}, qr/sub-fragment budget \(subFragments: 8\) is spent \[E_AUTHOR_LIMIT\]/,
   'the refusal names the budget and carries its code in the message');
is($o->{usedEnd}, 8, 'author.used equals the budget once it is spent');

###############################################################################
# the fatal paths

my $spin = body(http_get('/spin'));
diag("spin: $spin");
like($spin, qr/^HOST CAUGHT/, 'a sub-fragment past its deadline aborts the WHOLE invocation');
unlike($spin, qr/PARENT CAUGHT/, '... and the parent could not catch it (uncatchable)');
like($spin, qr/sub-fragment/, '... and the host is told it was the sub-fragment');
if ($spin =~ /HOST CAUGHT (\d+)ms/) {
    cmp_ok($1, '<', 2500, "the sub-fragment's OWN deadline fired (${1}ms), not the parent's 5s");
} else {
    fail('no timing in the abort report');
}

my $oom = body(http_get('/oom'));
diag("oom: $oom");
like($oom, qr/^returned sub caught \| /,
   'out of memory is an ordinary exception: a sub-fragment may catch its own and return');
like($oom, qr/\| PARENT CAUGHT sub-fragment \d+: .*out of memory/,
   '... and one it does not catch reaches the PARENT as text, catchable');
unlike($oom, qr/HOST CAUGHT/, '... neither aborts the invocation');

my $unc = body(http_get('/uncaught'));
diag("uncaught: $unc");
like($unc, qr/^HOST CAUGHT \d+ms code=E_AUTHOR_LIMIT /,
   'a refusal the parent does not catch reaches the host with e.code intact');
like($unc, qr/sub-fragment budget \(subFragments: 1\) is spent \[E_AUTHOR_LIMIT\]/,
   '... and with its message and bracketed code, so an error log carries it');

my $late = js(http_get('/late'));
diag("late: " . encode_json($late));
is($late->{incl}, 'function',
   'an author capability granted at REQUEST time (post-fork) has its include()');
is($late->{sub}, 42, '... and authors a working sub-fragment');

###############################################################################
# the host side

my $h = js(http_get('/host'));
diag("host: " . encode_json($h));

is($h->{noSpec}{code}, 'E_CAP_GRANT', 'comcon.author() with no spec is refused');
is($h->{zero}{code}, 'E_CAP_GRANT', 'subFragments: 0 is refused -- no budget is not a small budget');
is($h->{frac}{code}, 'E_CAP_GRANT', 'a fractional subFragments is refused');
is($h->{badTtl}{code}, 'E_CAP_GRANT', 'ttlSeconds: 0 is refused');
is($h->{mediated}{code}, 'E_CAP_GRANT', 'a mediate()d author descriptor is refused as a grant');
is($h->{describe}{ok}, 'include,subFragments,used',
   'NginxComconAuthor is on the classified table with its three members');

# Test::Nginx's DESTROY runs the two standing checks (no alerts, no sanitizer
# errors); destroyed here, explicitly, so they run before Test::Builder's own
# END block counts the plan rather than during global destruction after it.
undef $t;
