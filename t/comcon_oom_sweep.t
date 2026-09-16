#!/usr/bin/perl

# THE OUT-OF-MEMORY RESIDUE SWEEP, AS A STANDING BATTERY (F18's class).
#
# F18 was a worker SIGSEGV that lived in a window of a few hundred bytes: the
# memory allowance had to bite INSIDE the engine's own backtrace annotation,
# after the error object existed and before its stack string did.  It
# reproduced 3/3 on the plain build and never under ASAN or valgrind -- a
# sanitizer's own allocations move where the allowance bites -- so the corpus
# the signatures rest on cannot see this class at all.  One test sweeps that
# one shape (t/comcon_oom_backtrace.t).  This file sweeps the OTHER places the
# allowance can bite, on BOTH tiers, so a hole of F18's kind in any of them is
# found by a run and not by a gate crash.
#
# THE SWEEP: a fragment fills memory in exact 1 KB strings and the allowance
# steps by 32 bytes across one such string, 32 alignments, so the failing
# allocation walks through every residue -- including whichever window an
# engine path has.  Seven shapes, each a place the allowance can bite:
#
#   catch        the throw inside try/catch (F18's shape)
#   finally      the throw unwinding through a finally block
#   generator    the throw inside a generator's next()
#   async        the throw inside a microtask job, after an await -- the
#                allowance must still be in force in the settle loop
#   nested       the throw inside a SUB-fragment authored by the parent, the
#                parent catching what crosses (text-only, F13/G7.14)
#   marshal      the throw inside the HOST's JSON stringify of the result,
#                under the allowance, after the fragment returned
#   catch_alloc  the throw inside the catch handler's own allocation
#   stream       the throw inside a fragment invoked from a STREAM server's
#                handler, one alignment per TCP connection: the host's
#                stream-side failure path, not the http one, and the session
#                must still be finalized
#
# WHAT IS ASSERTED, per shape and tier: the worker answers all 32 alignments;
# every outcome is one the shape allows -- the fragment's catch got the error,
# got null (even the error could not be built), the host reported an ordinary
# out-of-memory failure, or (nested) the parent caught what the sub-fragment
# could not; for the shapes with a catch, the error was caught in at least one
# alignment, so the window was reached; and the compiled arm ran compiled
# where it can (async fragments are not lowered at include time, G7.18).
# Then: no alert, no signal, in either arm's log.  The two arms are NOT
# compared alignment by alignment -- their footprints differ, so the same
# alignment lands elsewhere -- they are held to the same set of outcomes.
#
# Requires both builds: objs/nginx (interpreter) + objs_jit/nginx (JIT).
#     TEST_NGINX_BINARY=$(pwd)/objs/nginx prove t/comcon_oom_sweep.t

use warnings;
use strict;

use Test::More;
use File::Temp qw(tempdir);
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

my $root = $ENV{TEST_NGINX_BINARY} ? do { my $b = $ENV{TEST_NGINX_BINARY}; $b =~ s{/objs(_jit)?/nginx$}{}; $b } : '..';
my $interp = "$root/objs/nginx";
my $jit    = "$root/objs_jit/nginx";

plan(skip_all => "no interpreter build objs/nginx") unless -x $interp;
plan(skip_all => "no JIT build objs_jit/nginx")     unless -x $jit;

my @SHAPES = qw(catch finally generator async nested marshal catch_alloc
                catch_fine nested_include admit_tests reject_job stream);
my %CAUGHT_EXPECTED = map { $_ => 1 } qw(catch finally generator async nested catch_fine);
my %ALIGNMENTS = (catch_fine => 64);   # 16-byte step; every other shape 32 x 32 bytes
my %STREAM_PORT = (interp => 8959, jit => 8960);

# per arm: 3 per shape (answers, allowed, tier) + 1 per shape with a catch;
# then the two logs
plan(tests => 2 * (3 * @SHAPES + scalar(keys %CAUGHT_EXPECTED)) + 2);

my $dir = tempdir(CLEANUP => $ENV{KEEP_TESTDIR} ? 0 : 1);
diag("testdir: $dir") if $ENV{KEEP_TESTDIR};

my $root_js = <<'JS';
/* every shape fills in exact 1 KB strings until the allowance refuses one */
var S = {};

S.catch = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } }" +
    "  catch (e) { a.length = 0; return e === null ? 'null' : 'caught ' + e.message; } }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

S.finally = function (mem) { return comcon.include(
    "function(req){ var a = [], fin = 0;" +
    "  try { try { for (;;) { a.push('x'.repeat(1024)); } } finally { a.length = 0; fin = 1; } }" +
    "  catch (e) { return (e === null ? 'null' : 'caught ' + e.message) + ' fin=' + fin; }" +
    "  return 'unreached'; }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

S.generator = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  var g = (function*(){ try { for (;;) { a.push('x'.repeat(1024)); } }" +
    "    catch (e) { a.length = 0; yield (e === null ? 'null' : 'caught ' + e.message); } })();" +
    "  return g.next().value; }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

S.async = function (mem) { return comcon.include(
    "async function(req){ await null; var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } }" +
    "  catch (e) { a.length = 0; return e === null ? 'null' : 'caught ' + e.message; } }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

/* the parent is ONE fragment (authored at config phase, so lowered on the
   compiled arm); it authors the sub-fragment per call with the swept
   allowance, and catches what crosses back as text */
var NESTED = comcon.include(
    "function(req){" +
    "  var sub = author.include('function(){ var a = [];" +
    "      try { for (;;) { a.push(\"x\".repeat(1024)); } }" +
    "      catch (e) { a.length = 0; return e === null ? \"null\" : \"caught \" + e.message; } }'," +
    "    {imports: [], memoryBytes: req.mem});" +
    "  try { return 'sub ' + sub(); }" +
    "  catch (e) { return 'parent caught ' + String(e.message || e).slice(0, 40); } }",
    { imports: ['String'], grants: { author: comcon.author({ subFragments: 1 }) },
      meter: comcon.meter({ memoryBytes: 4194304 }) });
S.nested = function (mem) { return function () { return NESTED({ mem: mem }); }; };

S.marshal = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } } catch (e) {}" +
    "  return { n: a.length, a: a }; }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

/* F18's own shape again at a FINER step: 64 alignments 16 bytes apart, so a
   window narrower than the 32-byte step above is not stepped over */
S.catch_fine = S.catch;

/* the allowance biting INSIDE include: the parent fills memory, then authors
   a sub-fragment -- compile and admission, the request-field check included
   (checkRequest), run under the parent's allowance */
S.nested_include = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } } catch (e) {}" +
    "  try { var s = author.include('function(){ return 1; }', {imports: [], checkRequest: true}); return 'included ' + s(); }" +
    "  catch (e2) { return e2 === null ? 'null' : 'caught ' + ('' + e2.message); } }",
    { imports: [], grants: { author: comcon.author({ subFragments: 1 }) },
      meter: comcon.meter({ memoryBytes: mem }) }); };

/* the allowance biting inside an admission TEST: the sub-fragment's contract
   carries a tests function that fills memory; it runs at include time */
S.admit_tests = function (mem) { return comcon.include(
    "function(req){" +
    "  try { var s = author.include('function(){ return 2; }', {imports: [], tests: 'function(f){ var a = []; for (;;) { a.push(\"x\".repeat(1024)); } }'}); return 'included ' + s(); }" +
    "  catch (e) { return e === null ? 'null' : 'caught ' + ('' + e.message); } }",
    { imports: [], grants: { author: comcon.author({ subFragments: 1 }) },
      meter: comcon.meter({ memoryBytes: mem }) }); };

/* the allowance biting inside a REJECTED promise's reaction: the job runs in
   the host's settle loop, after the fragment returned, and throws out of it */
S.reject_job = function (mem) { return comcon.include(
    "function(req){ Promise.reject(new Error('r')).catch(function(){ var a = [];" +
    "  for (;;) { a.push('x'.repeat(1024)); } }); return 'queued'; }",
    { imports: ['Promise', 'Error'], meter: comcon.meter({ memoryBytes: mem }) }); };

/* the STREAM surface: the same uncaught out-of-memory as catch_alloc, but the
   host that receives it is a stream server's handler, once per connection */
S.stream = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } }" +
    "  catch (e) { return 'y'.repeat(65536) + (e === null ? 'null' : e.message); } }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

S.catch_alloc = function (mem) { return comcon.include(
    "function(req){ var a = [];" +
    "  try { for (;;) { a.push('x'.repeat(1024)); } }" +
    "  catch (e) { return 'y'.repeat(65536) + (e === null ? 'null' : e.message); } }",
    { imports: [], meter: comcon.meter({ memoryBytes: mem }) }); };

/* every alignment's fragment is authored HERE, at config phase, because only a
   config-phase include is lowered (the compile thread does not exist in a
   worker); a fragment authored at request time would silently run interpreted
   on the compiled arm and the tier assertion would say so */
var FRAGS = {}, name, k, n, step;
for (name in S) {
    FRAGS[name] = [];
    n = (name === 'catch_fine') ? 64 : 32;
    step = (name === 'catch_fine') ? 16 : 32;
    for (k = 0; k < n; k++) { FRAGS[name].push(S[name](1048576 + k * step)); }
}

function classify(r) {
    if (r === 'null' || r === 'sub null') { return 'nul'; }
    if (/^null fin=1$/.test(r)) { return 'nul'; }
    if (/^caught out of memory( fin=1)?$/.test(r)) { return 'caught'; }
    if (/^caught author\.include: .*out of memory$/.test(r)) { return 'caught'; }   /* the include stage's own OOM */
    if (/^sub caught out of memory$/.test(r)) { return 'caught'; }
    if (/^parent caught .*out of memory/.test(r)) { return 'parentcaught'; }
    if (/^caught .*\[E_[A-Z_]+\]/.test(r)) { return 'refused'; }   /* include refused, coded */
    if (/^(queued|included [0-9]+)$/.test(r)) { return 'ret'; }
    return null;
}

/* the stream server (created at init, no stream{} block): every connection
   runs ONE alignment of the stream shape and finalizes; the tallies are read
   over http at /probe?name=stream */
var STREAM = { attempts: 0, hostfail: 0, ret: 0, other: [], k: 0 };
nginx.createStream();
var ssrv = nginx.stream.addServer();
ssrv.handler = function (session) {
    var f = FRAGS.stream[STREAM.k % FRAGS.stream.length], r;
    STREAM.k++;
    STREAM.attempts++;
    try {
        r = f({});
        if (typeof r === 'string' && /^y+/.test(r)) { STREAM.ret++; } else { STREAM.other.push(String(r).slice(0, 60)); }
    } catch (e) {
        if (/out of memory/.test(String(e && e.message))) { STREAM.hostfail++; }
        else { STREAM.other.push('THREW ' + String(e && e.message).slice(0, 60)); }
    }
    session.finalize(200);
};
nginx.stream.attach(nginx.createSocket('127.0.0.1:%%STREAMPORT%%')).addServer(ssrv);

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path !== "/probe") { continue; }
    locs[i].handler = function (req) {
        var name = String(req.args || '').replace(/^name=/, '');
        var o = { shape: name, attempts: 0, caught: 0, nul: 0, hostfail: 0,
                  parentcaught: 0, refused: 0, ret: 0, other: [], compiled: 0, n: [] };
        var f = null, k, r, c, st;
        if (name === 'stream') {
            /* the alignments ran on the stream side, one per connection */
            o.attempts = STREAM.attempts; o.hostfail = STREAM.hostfail; o.ret = STREAM.ret;
            o.other = STREAM.other;
            for (k = 0; k < FRAGS.stream.length; k++) {
                st = comcon.aotStatus(FRAGS.stream[k]);
                if (st && st.compiled) { o.compiled += st.compiled; }
            }
            o.aot = { compiled: o.compiled };
            req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
            return;
        }
        for (k = 0; k < FRAGS[name].length; k++) {
            f = FRAGS[name][k];
            st = comcon.aotStatus(name === 'nested' ? NESTED : f);
            if (st && st.compiled) { o.compiled += st.compiled; }
            o.attempts++;
            try {
                r = f();
                if (typeof r !== 'string') { o.ret++; if (r && r.n !== undefined) { o.n.push(r.n); } continue; }   /* marshal: the array crossed */
                if (name === 'catch_alloc' && /^y+/.test(r)) { o.ret++; continue; }
                c = classify(r);
                if (c === null) { o.other.push(String(r).slice(0, 60)); } else { o[c]++; }
            } catch (e) {
                if (/out of memory/.test(String(e && e.message))) { o.hostfail++; }
                else { o.other.push('THREW ' + String(e && e.message).slice(0, 60)); }
            }
        }
        o.aot = { compiled: o.compiled };
        req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
    };
}
JS

sub run_arm {
    my ($bin, $tag, $port) = @_;

    (my $js = $root_js) =~ s/%%STREAMPORT%%/$STREAM_PORT{$tag}/g;
    open my $r, '>', "$dir/$tag.root.js" or die $!;
    print $r $js;
    close $r;

    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  location /probe { } } }\n";
    close $c;

    # nginx opens <prefix>/logs/error.log before it has read the config's own
    # error_log; without the directory that is an [alert] on stderr
    mkdir "$dir/logs";

    my $pid = fork();
    die "fork failed" unless defined $pid;
    if ($pid == 0) {
        open(STDERR, '>', "$dir/$tag.stderr") or exit 126;
        exec($bin, '-p', $dir, '-c', "$dir/$tag.conf") or exit 127;
    }
    # 192 fragments are included and, on the compiled arm, lowered at config
    # phase; give the instance up to 120 s to come up before probing
    for (1 .. 2400) {
        last if IO::Socket::INET->new(PeerAddr => "127.0.0.1:$port", Timeout => 1);
        select undef, undef, undef, 0.05;
    }

    # the stream shape: 32 connections, one alignment each, before its probe
    for (1 .. 32) {
        my $c = IO::Socket::INET->new(PeerAddr => "127.0.0.1:$STREAM_PORT{$tag}", Proto => 'tcp', Timeout => 8);
        next unless $c;
        my $buf = ''; $c->read($buf, 16); close $c;
    }

    my %res;
    for my $s (@SHAPES) {
        my $body = `curl -s -m 60 '127.0.0.1:$port/probe?name=$s'`;
        my %o = (body => $body);
        for my $k (qw(attempts caught nul hostfail parentcaught refused ret)) {
            ($o{$k}) = $body =~ /"$k":(\d+)/;
            $o{$k} //= -1;
        }
        ($o{other})    = $body =~ /"other":\[([^\]]*)\]/;
        ($o{n})        = $body =~ /"n":\[([^\]]*)\]/;
        ($o{compiled}) = $body =~ /"aot":\{[^}]*"compiled":(\d+)/;
        $o{other}    //= '(no response)';
        $o{compiled} //= -1;
        $res{$s} = \%o;
    }
    kill 'QUIT', $pid; waitpid($pid, 0);

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    return (\%res, $log);
}

my ($I, $ilog) = run_arm($interp, 'interp', 8957);
my ($J, $jlog) = run_arm($jit,    'jit',    8958);

# the outcomes each shape allows; anything else is 'other' and fails
my %ALLOWED = (
    catch       => [qw(caught nul hostfail)],
    finally     => [qw(caught nul hostfail)],
    generator   => [qw(caught nul hostfail)],
    async       => [qw(caught nul hostfail)],
    nested      => [qw(caught nul parentcaught hostfail)],
    marshal     => [qw(hostfail ret)],
    catch_alloc => [qw(hostfail ret)],
    catch_fine      => [qw(caught nul hostfail)],
    nested_include  => [qw(caught refused ret nul hostfail)],
    admit_tests     => [qw(caught refused ret nul hostfail)],
    reject_job      => [qw(ret hostfail)],
    stream          => [qw(ret hostfail)],
);

for my $arm ([interp => $I], [jit => $J]) {
    my ($tag, $R) = @$arm;
    for my $s (@SHAPES) {
        my $o = $R->{$s};
        diag(sprintf("%-6s %-14s attempts=%d caught=%d null=%d hostfail=%d parentcaught=%d refused=%d ret=%d compiled=%d other=[%s]%s",
                     $tag, $s, @$o{qw(attempts caught nul hostfail parentcaught refused ret compiled)}, $o->{other},
                     ($o->{n} // '') ne '' ? " n=[$o->{n}]" : ''));

        my $want = $ALIGNMENTS{$s} // 32;
        is($o->{attempts}, $want, "[$tag/$s] the worker answered all $want alignments");

        my %allowed = map { $_ => 1 } @{ $ALLOWED{$s} };
        my @bad = grep { $o->{$_} > 0 && !$allowed{$_} } qw(caught nul hostfail parentcaught refused ret);
        push @bad, "other=[$o->{other}]" if $o->{other} ne '';
        is(join(',', @bad), '', "[$tag/$s] every outcome is one the shape allows");

        if ($CAUGHT_EXPECTED{$s}) {
            cmp_ok($o->{caught}, '>', 0,
                   "[$tag/$s] the catch received the out-of-memory error in at least one alignment (the window was reached)");
        }

        # the tier precondition, as G7.18 states it: compiled where it can be,
        # never on the interpreter, and async fragments are not lowered
        if ($tag eq 'jit' && $s ne 'async') {
            cmp_ok($o->{compiled}, '>=', 1, "[$tag/$s] ran compiled (the tier this battery is also about)");
        } else {
            is($o->{compiled}, 0, "[$tag/$s] ran interpreted" . ($s eq 'async' ? ' (async fragments are not lowered at include time)' : ''));
        }
    }
}

unlike($ilog, qr/\[alert\]|\[emerg\]|signal 1[01]|exited on signal/, 'interpreter arm: no alert, no signal');
unlike($jlog, qr/\[alert\]|\[emerg\]|signal 1[01]|exited on signal/, 'compiled arm: no alert, no signal');
