#!/usr/bin/perl

# COMCON v5.131 -- SHOWCASE-gaps G-21: A LIVE EPOCH COMPILED IN THE MASTER.
#
# A worker cannot compile (the gcc thread is a pthread; it does not survive
# fork()), so until now an epoch admitted at request time -- every bindShared
# epoch, every bindAt replace -- stayed interpreted for good, and aotStatus()
# said so.  Now the worker keeps the wrapper text it was admitted from, sends
# it to the master, and looks for the master's index at most once a second on
# each invocation.  The master spawns ONE detached helper -- a fork of itself
# that compiles the text COMPILE-ONLY (nothing runs, not even an IIFE source),
# drains its own gcc thread, writes the index atomically and exits.  The
# worker installs by explicit hash with every check the cache-hit path makes,
# because the two processes never share a bytecode hash (atom operands are
# per runtime): the index maps each function's SOURCE KEY to the artifact.
#
# WHAT THIS FILE PINS: a request-time include starts pending and interpreted;
# it becomes native by the master, with the same answer it gave interpreted;
# every worker of a bindShared binding gets there, and again after a replace;
# a text the channel cannot carry is refused at once, not left pending; the
# master's log says what it spawned and what it compiled.
#
# Skipped on a build without the compiled tier (aotStatus().jit === false).
# Its own artifact cache (QJS_JIT_CACHE), so a previous run's index cannot
# answer this one.
#
# NEGATIVE CONTROL: t/tools/controls/aot-master-inert.patch -- the master
# ignores the request; nothing is ever native by the master.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /ctl { }
        location /sh { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
function L(p) { return locs.find(function (l) { return l.path === p; }); }

/* three functions, one answer the two tiers must agree on */
var SRC = "function(a){ function sq(x){ return x * x; } function cube(x){ return x * sq(x); }"
        + " var i, s = 0; for (i = 0; i < a.n; i++) { s += cube(i) % 7; } return { s: s, n: a.n }; }";
var SRC2 = SRC.replace("% 7", "% 11");
var BIG = "function(a){ /*" + new Array(70000).join("x") + "*/ return a.n; }";

var f = null, big = null;
function status(fr) {
    var a = comcon.aotStatus(fr);
    return { worker: nginx.workerIdx, functions: a.functions, compiled: a.compiled,
             pending: a.pending, via: a.via, tries: a.tries, unavailable: a.unavailable, jit: a.jit };
}

L('/ctl').handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === 'include') { if (f === null) { f = comcon.include(SRC, { imports: [] }); }
                                r = status(f); r.answer = f({ n: 50 }).s; }
        else if (op === 'status') { r = f ? status(f) : { worker: nginx.workerIdx, none: true };
                                    if (f) { r.answer = f({ n: 50 }).s; } }
        else if (op === 'big')    { if (big === null) { big = comcon.include(BIG, { imports: [] }); }
                                    r = status(big); r.answer = big({ n: 3 }); }
        else if (op === 'sh-replace') r = { epoch: sh.replace(comcon.quote(SRC2)) };
    } catch (e) { r = { error: e.name + ':' + e.message }; }
    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(r));
};

function onReq(req, callable, epoch) {
    if (callable === null) { req.respond(410, {'content-type':'text/plain'}, 'gone'); return; }
    var a = comcon.aotStatus(callable);
    req.respond(200, {'content-type':'application/json'}, JSON.stringify({
        worker: nginx.workerIdx, epoch: epoch, compiled: a.compiled, functions: a.functions,
        via: a.via, pending: a.pending, answer: callable({ n: 50 }).s }));
}
var sh = comcon.bindShared("aot", comcon.quote(SRC), { imports: [] }, onReq);
L('/sh').handler = sh.handler;
JS

my $cache = $t->testdir() . '/jitcache';
mkdir $cache;
$ENV{QJS_JIT_CACHE} = $cache;

$t->try_run('no js module')->plan(17);

###############################################################################

sub body { my $r = http_get(shift); $r =~ s/.*?\r\n\r\n//s; return $r; }
sub j { my ($b, $k) = @_; return $b =~ /"$k":("[^"]*"|[^,}]*)/ ? $1 : undef; }

# reach both workers: the first include on each is a request-time include
my (%seen, $first);
for (1 .. 12) {
    my $b = body('/ctl?op=include');
    my $w = j($b, 'worker');
    $seen{$w} = $b unless defined $seen{$w};
    $first = $b unless defined $first;
    last if keys %seen >= 2;
}

SKIP: {
    skip 'no compiled tier in this binary', 17 if $first =~ /"jit":false/;

    is(scalar keys %seen, 2, 'both workers included the fragment at request time');
    like($first, qr/"compiled":0,"pending":true,"via":null,"tries":1/,
         'a request-time include starts interpreted, with one request pending at the master');
    like($first, qr/"functions":3/, '...three functions to lower');
    my $bytecode_answer = j($first, 'answer');

    # the master's helper compiles; every worker adopts the artifacts
    my (%native, $last);
    for (1 .. 60) {
        my $b = body('/ctl?op=status');
        $last = $b;
        my $w = j($b, 'worker');
        $native{$w} = $b if $b =~ /"compiled":3/;
        last if keys %native >= 2;
        select(undef, undef, undef, 0.5);
    }
    is(scalar keys %native, 2, 'both workers became native (3 of 3 functions) within 30 s');
    like(join('', values %native), qr/"via":"master"/, '...by the master, and say so');
    like(join('', values %native), qr/"pending":false/, '...with nothing pending');
    my @answers = map { j($_, 'answer') } values %native;
    is($answers[0], $bytecode_answer, 'the native tier answers what the interpreter answered');
    is($answers[1], $bytecode_answer, '...on the other worker too');

    my $log = $t->read_file('error.log');
    like($log, qr/js comcon: master aot: helper \d+ spawned for key [0-9a-f]{16}/,
         'the master spawned a helper');
    like($log, qr/js comcon: master aot: 3 functions compiled for key [0-9a-f]{16} \(\d+ bytes\), index written/,
         'the helper compiled the three functions and wrote the index');
    like($log, qr/js comcon: fragment \d+ NATIVE \(master AOT: 3 of 3 functions, 3 named by the index, key [0-9a-f]{16}, after \d+ requests?\)/,
         'a worker adopted the artifacts');

    # bindShared: every epoch is a request-time include, on every worker
    my %shared;
    for (1 .. 80) {
        my $b = body('/sh');
        my $w = j($b, 'worker');
        $shared{$w} = $b if $b =~ /"epoch":0,"compiled":3/;
        last if keys %shared >= 2;
        select(undef, undef, undef, 0.5);
    }
    is(scalar keys %shared, 2, 'a shared binding\'s epoch 0 is native on both workers');
    my ($a0) = map { j($_, 'answer') } values %shared;

    like(body('/ctl?op=sh-replace'), qr/"epoch":1/, 'a live replace admits epoch 1');
    my %shared1;
    for (1 .. 80) {
        my $b = body('/sh');
        my $w = j($b, 'worker');
        $shared1{$w} = $b if $b =~ /"epoch":1,"compiled":3/;
        last if keys %shared1 >= 2;
        select(undef, undef, undef, 0.5);
    }
    is(scalar keys %shared1, 2, '...and epoch 1 is native on both workers, without a reload');
    my ($a1) = map { j($_, 'answer') } values %shared1;
    isnt($a1, $a0, 'epoch 1 answers differently, as its text does');

    # a text the channel cannot carry is refused at once
    like(body('/ctl?op=big'), qr/"compiled":0,"pending":false,"via":null,"tries":0,"unavailable":true/,
         'a wrapper larger than a channel message stays interpreted and says so at once');
    like($t->read_file('error.log'), qr/stays BYTECODE: the wrapper text exceeds the channel's message size/,
         '...with the reason in the log');
}

###############################################################################
