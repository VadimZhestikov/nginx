#!/usr/bin/perl

# F6 — HOST JS IS BOUNDED BY DEFAULT.
#
# AUDIT_M-SES §3 carried this as OPEN and as a deliberate scope choice: "bind the guard to
# the confined path only, so host JS behaviour does not change." The cost was a worker that
# hangs FOREVER on one accidental while(true) in a location.handler, taking every other
# client on that worker with it -- and nginx.workerRequestTimeout, the knob that would have
# prevented it, defaulted to 0. A guard that is off by default protects only the operators
# who already knew they needed it.
#
# Ten seconds, not the tenant's one: host JS is trusted and may legitimately spend real
# SYNCHRONOUS time in a request (a COM tree walk, a large parse). Nothing legitimate
# approaches ten seconds; a worker wedged for ten is still enormously better than one wedged
# until SIGKILL.
#
# THE KNOB TAKES EFFECT ON THE NEXT REQUEST, by design (it is read once, before the handler
# runs). The probes below exploit that rather than fighting it: one request sets the value,
# the next one is measured under it. The first probe runs against the SHIPPED DEFAULT with
# nothing set, which costs ~10 s of wall clock and is worth it exactly once -- it is the only
# assertion that tests what an operator who changes nothing actually gets.
#
# An aborted handler produces no response body (the interrupt unwinds the whole JS_Call and
# nginx answers 500 or closes), so these assert on the ELAPSED TIME and the error log rather
# than on anything the handler says about itself. A handler cannot report its own execution.

use warnings;
use strict;

use Test::More;
use Time::HiRes qw/time/;

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

        location /knob   { }
        location /set    { }
        location /burn   { }
        location /work   { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

function at(path, fn) {
    var l = locs.find(function (x) { return x.path === path; });
    if (l) { l.handler = fn; }
}

/* what an operator who changes nothing gets */
at('/knob', function (req) {
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ dflt: nginx.workerRequestTimeout }));
});

/* set the value for the NEXT request (the documented semantics) */
at('/set', function (req) {
    var ms = parseInt(req.args.replace(/^ms=/, ''), 10);
    nginx.workerRequestTimeout = ms;
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ now: nginx.workerRequestTimeout }));
});

/* a runaway: sized to run for minutes, so a regression is a failed assertion
   rather than a hung suite -- the assertion is that it is STOPPED. */
at('/burn', function (req) {
    var s = 0, i;
    for (i = 0; i < 4000000000; i++) { s += i % 7; }
    req.respond(200, {'content-type':'text/plain'}, 'MISSED s=' + s);
});

/* bounded work that outlasts a 300 ms deadline but finishes quickly: the probe
   for the opt-out, which must not be able to hang the suite either. */
at('/work', function (req) {
    var t0 = Date.now(), s = 0, i;
    for (i = 0; i < 200000000; i++) { s += i % 7; }
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ ms: Date.now() - t0, completed: true, s: s }));
});
JS

$t->try_run('no js module')->plan(7);

###############################################################################

sub timed {
    my ($path) = @_;
    my $t0 = time();
    my $r = http_get($path);
    return (sprintf('%.2f', time() - $t0) + 0, $r);
}

like(http_get('/knob'), qr/"dflt":10000/,
     'the knob READS as its own default (10000 ms): a guard whose value is '
     . 'visible is one an operator can reason about without a document');

# --- what nothing-set gets you -------------------------------------------
#
# The CLIENT gives up before the server's 10 s bound does (Test::Nginx's own
# request timeout is 8 s), so the client cannot witness the abort -- which is
# also what an operator sees in production: a runaway handler looks like a
# client timeout. The evidence is therefore the error log, which is where the
# interrupt actually announces itself.
my ($dsec, $dresp) = timed('/burn');
diag("default: the client returned after ${dsec}s");
unlike($dresp, qr/MISSED/,
       'the handler did not complete: an aborted JS_Call unwinds without '
       . 'reaching the respond() at the end');

# give the 10 s deadline time to fire after the client has already given up
select(undef, undef, undef, 3.5);
my $log = $t->read_file('error.log');
like($log, qr/InternalError: interrupted/,
     'A RUNAWAY HANDLER WITH NOTHING SET IS STOPPED. This is F6: the loop would '
     . 'run for minutes, and before this change it ran until SIGKILL, taking '
     . 'every other client on the worker with it. The interrupt says so in the '
     . 'log -- the client had already timed out, which is exactly the symptom '
     . 'an operator would be debugging');

# --- a short explicit deadline -------------------------------------------
like(http_get('/set?ms=300'), qr/"now":300/, 'the knob is settable');

my ($ssec, $sresp) = timed('/burn');
diag("300 ms: the runaway ended after ${ssec}s");
cmp_ok($ssec, '<', 3,
       'under a 300 ms deadline the same runaway is stopped in well under a '
       . 'second, so what stops it is the deadline and not a coincidence');

# --- zero is a real opt-out ----------------------------------------------
http_get('/set?ms=0');
my $w = http_get('/work');
diag($w =~ /(\{.*\})/ ? $1 : $w);
like($w, qr/"completed":true/,
     'ZERO IS A REAL OPT-OUT: work that the 300 ms deadline would have killed '
     . 'completes. An operator who needs an unbounded handler can still have '
     . 'one, deliberately rather than by default');
like($w, qr/"ms":(?:[3-9]\d\d|[1-9]\d{3})/,
     'and it really did outlast that deadline, so the opt-out is what let it '
     . 'finish rather than a loop too fast to matter');
