#!/usr/bin/perl

# F15, PHASE 3 — the compartment meters itself, whether or not a worker
# exists.
#
# comcon_rt is a SEPARATE runtime from the host's.  Its own interrupt handler
# used to require a worker (`w != NULL`) before it was ever installed at all
# -- so a fragment's own top-level evaluation, an admission test that invokes
# the fragment, a confined invocation, and the leftover drain were ALL
# genuinely unbounded whenever they were reached before a worker existed:
# CONFIG PHASE, meaning `js_source` evaluation, including `nginx -t`.
#
# MEASURED, before this fix, each hanging until killed:
#
#   comcon.include("(function(){ for(;;){} })()", {imports:[]})
#       -- the wrapper's own body IS "return(source)", so a looping source
#          runs during include() itself, before the fragment is even admitted.
#   var f = comcon.include("function(a){ for(;;){} }", {imports:[]}); f({});
#       -- invoking an already-admitted fragment.
#   comcon.include(src, {tests: "function(fragment){ fragment({}); }"})
#       -- an admission test that calls the (looping) fragment it is testing.
#
# THE CONFIG-PHASE CASE IS DRIVEN OUTSIDE Test::Nginx's OWN run()/try_run(),
# DELIBERATELY.  Test::Nginx's run() waits up to 5 SECONDS for nginx's pid
# file to appear before giving up -- and the pid file is written only AFTER
# js_source finishes, so a single 5-SECOND config-phase timeout already sits
# right at that budget's edge.  Driving `nginx -t` directly (which opens no
# listen socket and has no pid-file wait to race) is what makes this file's
# own timing independent of the fix it is testing: a REGRESSION that widened
# the default past a few seconds would make prove's own harness time out
# ambiguously instead of failing this test cleanly.
#
# THE REQUEST-TIME CASES ARE SPLIT ONE PER REQUEST for the same reason in the
# other direction: Test::Nginx's http() has its own 8-second alarm, and two
# ~5-second scenarios stacked in one handler would exceed it regardless of
# whether the fix works.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use File::Temp qw/tempdir/;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /invocation    { }
        location /admissionTest { }
        location /alive         { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var locs = nginx.http.servers[0].locations, by = {};
for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

function timed(fn) {
    var t0 = Date.now();
    try { fn(); return { outcome: 'returned', ms: Date.now() - t0 }; }
    catch (e) { return { outcome: 'stopped', ms: Date.now() - t0,
                          message: String(e.message) }; }
}

by['/invocation'].handler = function (req) {
    var r = timed(function () {
        var f = comcon.include("function(a){ for(;;){} }", { imports: [] });
        f({});
    });
    req.respond(200, { 'content-type': 'application/json' },
                JSON.stringify(r));
};

by['/admissionTest'].handler = function (req) {
    var r = timed(function () {
        comcon.include("function(a){ for(;;){} }",
            { imports: [], tests: "function(fragment){ fragment({}); }" });
    });
    req.respond(200, { 'content-type': 'application/json' },
                JSON.stringify(r));
};

/* Not wedged by anything that ran before it -- the push/pop discipline
 * paying off rather than leaving jcf->comcon_deadline_ms stuck. */
by['/alive'].handler = function (req) {
    var r = timed(function () {
        var g = comcon.include("function(a){ return 'alive'; }",
                               { imports: [] });
        if (g({}) !== 'alive') { throw new Error('did not recover'); }
    });
    req.respond(200, { 'content-type': 'application/json' },
                JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(7);

# --- config phase: driven directly, outside Test::Nginx's own run() ---

my $cpdir = tempdir(CLEANUP => 1);
mkdir("$cpdir/logs") or die $!;
open(my $fh, '>', "$cpdir/nginx.conf") or die $!;
print $fh <<"EOF";
daemon off;
master_process off;
pid $cpdir/nginx.pid;
error_log $cpdir/logs/error.log;
js_source $cpdir/init.js;
events { }
EOF
close $fh;

open($fh, '>', "$cpdir/init.js") or die $!;
print $fh <<'EOF';
var t0 = Date.now();
try {
    comcon.include("(function(){ for(;;){} })()", { imports: [] });
    nginx.log('info', 'CONFIG_PHASE=returned');
} catch (e) {
    nginx.log('info', 'CONFIG_PHASE=stopped ms=' + (Date.now() - t0)
                       + ' message=' + e.message);
}
EOF
close $fh;

my $binary = $Test::Nginx::NGINX;
my $out = `timeout 12 "$binary" -t -c "$cpdir/nginx.conf" -p "$cpdir/" 2>&1`;
open(my $elog, '<', "$cpdir/logs/error.log") or die "no error.log: $!";
my $cplog = do { local $/; <$elog> };
close $elog;

like($cplog, qr/CONFIG_PHASE=stopped ms=\d+ message=comcon\.include: InternalError: interrupted/,
   'THE WRAPPER\'S OWN BODY IS BOUNDED AT CONFIG PHASE: a looping `source` '
   . 'runs during include() itself (the wrapper is literally "return(source)"), '
   . 'and it is stopped -- before this fix it hung `nginx -t` until killed, '
   . 'because no worker existed yet and the interrupt handler was never '
   . 'installed')
    or diag("nginx -t output: $out\nerror.log: $cplog");

if ($cplog =~ /ms=(\d+)/) {
    cmp_ok($1, '>=', 4500,
       '...stopped BY THE DEADLINE (>= 4.5s), not instantly, which would '
       . 'satisfy the assertion above for the wrong reason');
} else {
    fail('config phase: could not find ms= to check the deadline actually fired');
}

# --- request time: a worker exists; one scenario per request (8s alarm) ---

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval {
        require JSON::PP;
        $o = JSON::PP::decode_json($raw);
        1;
    } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 300)); $o = {};
    };
    return $o;
}

my $inv = get_json('/invocation');
is($inv->{outcome}, 'stopped',
   'AN INVOCATION OF AN ALREADY-ADMITTED FRAGMENT IS BOUNDED AT REQUEST TIME '
   . 'BY THE COMPARTMENT\'S OWN DEADLINE, not merely by whichever ambient '
   . 'request deadline happens to be running -- a widening here would not be '
   . 'caught by the config-phase assertions above at all')
    or diag('invocation: ' . ($inv->{message} // 'no message'));
cmp_ok($inv->{ms} // -1, '>=', 4500,
   '...and stopped by the deadline, not instantly');

my $adm = get_json('/admissionTest');
is($adm->{outcome}, 'stopped',
   'AND AN ADMISSION TEST THAT CALLS THE FRAGMENT IT IS TESTING IS BOUNDED '
   . 'TOO: `tests` exists precisely to invoke the fragment, so it runs the '
   . 'same risk the other two paths do')
    or diag('admissionTest: ' . ($adm->{message} // 'no message'));
cmp_ok($adm->{ms} // -1, '>=', 4500,
   '...and stopped by the deadline, not instantly');

my $alive = get_json('/alive');
is($alive->{outcome}, 'returned',
   'THE COMPARTMENT IS NOT WEDGED BY EITHER TIMEOUT ABOVE: a trivial '
   . 'fragment included and invoked right afterward still works, which is '
   . 'the push/pop discipline paying off rather than leaving a stuck '
   . 'deadline behind')
    or diag('alive: ' . encode_json_safe($alive));

sub encode_json_safe {
    my ($h) = @_;
    return join(',', map { "$_=" . ($h->{$_} // 'undef') } sort keys %$h);
}
