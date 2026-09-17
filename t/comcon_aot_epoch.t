#!/usr/bin/perl

# COMCON D4c — the compiled tier under a live epoch switch.
#
# POM.md §4 drew class-F propagation as "rewrite -> dirty -> (bytecode fallback)
# -> re-AOT -> live(e+1)".  Nothing could check any of it, because nothing could
# say which tier a fragment was running on: js_comcon_aot_compile() returns 0 for
# any bytecode function ("eligible", never "compiled"), and the include site
# logged "lowered to native C" on that 0 -- on every include, including the
# request-time epoch switches of a live rewrite.
#
# comcon.aotStatus(fragment) now answers it, and the answer is architectural: a
# live rewrite's new epoch is built in a WORKER, post-fork, where THERE IS NO GCC
# THREAD (it does not survive fork()).  So the bytecode fallback is what runs AT
# THE INSTANT THE EPOCH IS ADMITTED.  Since v5.131 (G-21) the worker asks the
# master, whose helper compiles the text and whose index the worker adopts on a
# later invocation -- t/comcon_aot_master.t pins that path; this file pins the
# instant, in both directions:
#
#   * the SAFETY property, which is the one that matters: a rewrite of a fragment
#     that WAS compiled natively takes effect -- the new epoch's behaviour is
#     served, never the old native code.  A stale .so still answering requests
#     after a rewrite would be the worst failure this class has.
#   * the HONESTY property: aotStatus reports compiled:0 for that epoch at
#     admission instead of implying a re-AOT (and, on the compiled build, that
#     the master has been asked: pending), and the include's log line says
#     BYTECODE, not NATIVE.
#
# Runs on both builds and says which it measured: on a build without CONFIG_JIT
# there is no tier to report, and the test asserts that answer rather than
# skipping (a skip here would hide a broken status call).

# NEGATIVE CONTROLS (run 2026-09-12, engine rebuilt for each, re-passed after
# restore):
#
#   js_comcon_aot_status counts jit_func         -> tests 10-11 fail
#   the NATIVE / BYTECODE messages are distinct  -> tests 10-11 fail
#   aotStatus refuses a non-fragment             -> test 9 fails
#
# The safety assertions (2-7) are not controlled here on purpose: breaking them
# means breaking D4a's epoch switch, which t/comcon_pom_mutate.t already covers
# with its own controls.  This file's new claims are the two above.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /m   { }
        location /ctl { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* Enough arithmetic that the tier is a real choice, not a formality. */
var V1 = "function (n) {"
       + "  var s = 0;"
       + "  for (var i = 0; i < 200; i++) { s += (i * 3) % 7; }"
       + "  return 'EPOCH-ONE:' + s;"
       + "}";
var V2 = "function (n) {"
       + "  var s = 0;"
       + "  for (var i = 0; i < 200; i++) { s += (i * 3) % 7; }"
       + "  return 'EPOCH-TWO:' + s;"
       + "}";

var target = locs.find(function (l) { return l.path === "/m"; });
var live = null;

function site(callable, epoch) {
    live = callable;
    if (callable === null) {
        target.handler = function (req) {
            req.respond(410, {'content-type':'text/plain'}, 'gone');
        };
    } else {
        target.handler = function (req) {
            req.respond(200, {'content-type':'text/plain',
                              'x-epoch': String(epoch)}, String(callable(0)));
        };
    }
}

var h = comcon.bindAt(site, comcon.quote(V1), { imports: [] });

/* The status of epoch 0, taken at CONFIG time (master, pre-fork), where a gcc
 * thread does exist.  Captured now because the workers cannot reproduce it. */
var atLoad = comcon.aotStatus(live);

var ctl = locs.find(function (l) { return l.path === "/ctl"; });
ctl.handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === "replace") {
            r.epoch = h.replace(comcon.quote(V2));
            r.after = comcon.aotStatus(live);
        } else if (op === "status") {
            r.atLoad = atLoad;
            r.now    = comcon.aotStatus(live);
            r.epoch  = h.epoch();
        } else if (op === "rollback") {
            r.epoch = h.rollback();
            r.after = comcon.aotStatus(live);
        } else if (op === "hostFn") {
            /* nginx.jitStatus(fn): the same question for HOST JS, read-only --
             * jitCompile() cannot answer it, because calling it to find out
             * changes the answer. */
            var f = function (n) { return n + 1; };
            r.before = nginx.jitStatus(f);
            r.rep = nginx.jitCompile(f);
            r.after = nginx.jitStatus(f);
            r.bad = 'ACCEPTED';
            try { nginx.jitStatus(42); } catch (e) { r.bad = 'refused'; }
        } else if (op === "badArg") {
            r.bad = 'ACCEPTED';
            try { comcon.aotStatus(function () {}); }
            catch (e) { r.bad = 'refused'; }
        }
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};
JS

# a private artifact cache: a previous run's master-compiled index would make
# the rewritten epoch native at once (G-21, "compiled before it was asked"),
# and this file pins the instant of admission
my $cache = $t->testdir() . '/jitcache';
mkdir $cache;
$ENV{QJS_JIT_CACHE} = $cache;

$t->try_run('no js module')->plan(13);

###############################################################################

# Which build is this?  The test measures it rather than assuming, and says so.
my $s0 = http_get('/ctl?op=status');
my $jit = ($s0 =~ /"atLoad":\{"jit":true/) ? 1 : 0;
diag($jit ? "compiled tier PRESENT (CONFIG_JIT build)"
          : "no compiled tier in this build (interpreter)");

like($s0, qr/"atLoad":\{"jit":(true|false),"functions":\d+,"compiled":\d+[,}]/,
     'aotStatus reports {jit, functions, compiled, ...}');

# --- the safety property -------------------------------------------------
like(http_get('/m'), qr/x-epoch: 0.*EPOCH-ONE:/s, 'epoch 0 serves V1');

my $rep = http_get('/ctl?op=replace');
like($rep, qr/"epoch":1/, 'the live rewrite installs epoch 1');
like(http_get('/m'), qr/x-epoch: 1.*EPOCH-TWO:/s,
     'THE SAFETY PROPERTY: epoch 1 serves the NEW code -- a rewrite is never '
     . 'answered by the previous epoch, compiled or not');
unlike(http_get('/m'), qr/EPOCH-ONE/,
     '...and no trace of the old epoch remains in the response');

like(http_get('/ctl?op=rollback'), qr/"epoch":0/, 'rollback returns to epoch 0');
like(http_get('/m'), qr/x-epoch: 0.*EPOCH-ONE:/s,
     '...serving V1 again, so the retained epoch is intact');

# --- the honesty property ------------------------------------------------
like($rep, $jit ? qr/"after":\{"jit":true,"functions":\d+,"compiled":0,"pending":true,/
                : qr/"after":\{"jit":false,"functions":\d+,"compiled":0[,}]/,
     'a live-rewritten epoch reports compiled:0 at admission -- the BYTECODE '
     . 'FALLBACK runs first, because a worker has no gcc thread after fork(); '
     . 'on the compiled build the master has been asked (pending)');

my $hf = http_get('/ctl?op=hostFn');
# Two honest shapes, keyed on the flag: with a compiled tier it walks the tree
# (1 function, 0 native yet); without one there is no tier to report, and it says
# so rather than inventing counts -- the same convention as aotStatus.
like($hf, qr/"before":\{"jit":true,"functions":1,"compiled":0\}|"before":\{"jit":false,"functions":0,"compiled":0\}/,
     'nginx.jitStatus reports a host function as not-yet-native, without '
     . 'compiling it to find out');
like($hf, qr/"bad":"refused"/, 'jitStatus refuses a non-function');

like(http_get('/ctl?op=badArg'), qr/"bad":"refused"/,
     'aotStatus refuses anything that is not a confined fragment');

# The two log messages share no substring, so grepping for one cannot match
# the other.  A request-time epoch switch must not claim a lowering.
my $log = $t->read_file('error.log');
if ($jit) {
    my @native = ($log =~ /include fragment NATIVE/g);
    my @bc     = ($log =~ /include fragment BYTECODE/g);
    # The ASYMMETRY is the finding: the same include path compiled at load
    # (master, pre-fork, gcc thread alive) and did not at request time.
    # If this fails with 0 native, check that gcc is installed and on PATH --
    # a box with no compiler legitimately reports 0, and then this file is
    # measuring an interpreter and should be read as such.
    ($s0 =~ /"atLoad":\{"jit":true,"functions":\d+,"compiled":(\d+)[,}]/);
    cmp_ok($1, '>', 0,
       "the LOAD-time include really was lowered ($1 functions native) -- "
       . 'needs gcc on PATH; 0 here means no compiler, not a broken tier');
    ok(@native > 0 && @bc > 0,
       'the request-time epoch switch logs BYTECODE where the load-time '
       . 'include logged NATIVE -- the same code path, two tiers ('
       . scalar(@native) . ' native, ' . scalar(@bc) . ' bytecode)');
} else {
    ok($log !~ /include fragment NATIVE/,
       'no build without CONFIG_JIT ever claims a native lowering');
    ok($log !~ /include fragment BYTECODE/,
       '...and it does not log the compiled tier at all');
}
