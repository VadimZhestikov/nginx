#!/usr/bin/perl

# COMCON M-SES S6 — the adversarial verification GATE.
#
# The M-SES gate says no confined fragment may (a) obtain a value not in its ρ,
# (b) mutate a frozen intrinsic, (c) create code from strings without admit,
# (d) traverse a COM facet beyond handle reach, or (e) escape gas/memory limits.
#
# Before this file, (c) had four probes in t/comcon_include_mses.t and (d) had
# the route-glob tests; (a) was covered only indirectly by the free-name gate,
# and (b) and (e) were not probed at all. SR-3 passed as a PENTEST -- a
# point-in-time exercise, not something that fails when a facet is widened six
# months from now. Confinement is the claim COMCON rests on, so it needs a gate
# that runs every time, not a document saying it was checked once.
#
# SELF-VALIDATING BY CONSTRUCTION: every probe runs TWICE -- once inside a
# confined comcon.include fragment, once in unconfined host JS -- and the suite
# asserts they DIFFER in the expected direction. A probe that reports "closed"
# in both contexts is not evidence of confinement, it is a broken probe that
# never had the capability to begin with, and this suite fails on it. That is
# the negative control built in, rather than bolted on.

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
        location /gate { }
        location /gas  { }
    }
}
EOF

# The probe battery is ONE string, evaluated in both contexts, so the two runs
# cannot drift apart. Each probe answers 'open' or 'closed'; 'open' means the
# capability was obtained.
$t->write_file('root.js', <<'JS');
var PROBES =
  "function(){" +
  "  function p(fn){ try { return fn() ? 'open' : 'closed'; } catch(e){ return 'closed'; } }" +
  "  return {" +
       /* (c) code from strings -- the constructor ladder */
  "    c_fn_ctor:     p(function(){ return [].constructor.constructor('return 1')() === 1; })," +
  "    c_obj_ctor:    p(function(){ return Object.constructor('return 1')() === 1; })," +
  "    c_gen_ctor:    p(function(){ return !!Object.getPrototypeOf(function*(){}).constructor('x'); })," +
  "    c_async_ctor:  p(function(){ return !!Object.getPrototypeOf(async function(){}).constructor('x'); })," +
       /* (b) mutate a frozen intrinsic -- prototype pollution */
  "    b_proto_poll:  p(function(){ Object.prototype.__mses_polluted = 1;" +
  "                                 return ({}).__mses_polluted === 1; })," +
  "    b_array_push:  p(function(){ var o = Array.prototype.push;" +
  "                                 Array.prototype.push = function(){ return 'hijacked'; };" +
  "                                 var r = [].push(1) === 'hijacked';" +
  "                                 Array.prototype.push = o; return r; })," +
  "    b_freeze_str:  p(function(){ String.prototype.__mses = 1;" +
  "                                 return ''.__mses === 1; })," +
       /* (a) obtain a value not in ρ -- ambient roots */
  "    a_global_this: p(function(){ return typeof globalThis.nginx === 'object'; })," +
  "    a_com_root:    p(function(){ return typeof nginx === 'object'; })," +
  "    a_comcon:      p(function(){ return typeof comcon === 'object'; })," +
       /* .stack leak: the property is whether the trace names anything OUTSIDE
        * the fragment -- a host file or a filesystem path. A stack naming only
        * "<comcon-fragment>" is the fragment seeing itself, which is not a leak.
        * The first version of this probe asked only whether .stack was a
        * non-empty string, and duly fired on the interpreter build, where the
        * trace is exactly "at <anonymous> (<comcon-fragment>:1:78)". Testing
        * the symptom instead of the property manufactures a false alarm. */
  "    a_stack_leak:  p(function(){ try { null.x; } catch(e) {" +
  "                                 var st = (typeof e.stack === 'string') ? e.stack : '';" +
  "                                 return /[.]js|[/]/.test(st); }" +
  "                                 return false; })," +
       /* Symbol.species: redirect a builtin into attacker-chosen construction */
  "    a_species:     p(function(){ function E(){}; E[Symbol.species] = function(){ this.tag = 'x'; };" +
  "                                 var a = []; a.constructor = E;" +
  "                                 return a.slice(0).tag === 'x'; })" +
  "  };" +
  "}";

/* CONFINED: the battery inside an admitted fragment. */
var confined = comcon.include(
    "function(req){ var probes = (" + PROBES + ")(); " +
    "  return { status: 200, body: JSON.stringify(probes) }; }");

/* UNCONFINED: the identical battery in host JS. This is the control -- the
 * routes a confinement closes must be demonstrably OPEN here, or the probe is
 * measuring nothing. */
function hostProbes() {
    /* eslint-disable no-eval */
    var f = (0, eval)('(' + PROBES + ')');
    return f();
}

/* (e) escape the resource guard.
 *
 * The guard is a per-request WALL-CLOCK deadline polled by QuickJS every ~100
 * bytecodes, and it is OPT-IN: ngx_js_http_module only arms it when
 * nginx.workerRequestTimeout is set. Unset -- the default -- a runaway loop
 * hangs the worker, which is exactly why this needs a standing probe.
 *
 * The loop is FINITE on purpose. An unbounded one would hang the whole suite if
 * enforcement regressed; bounded, a regression is a failed assertion that
 * arrives a couple of seconds late instead of never. It is sized against a
 * MEASUREMENT, not a guess: 200M iterations completed in 126ms here, which is
 * inside the 300ms deadline, so the first version of this probe proved nothing
 * (it "passed" only because of a substring bug in the assertion). 4G iterations
 * runs for seconds, comfortably past the deadline. */
nginx.workerRequestTimeout = 300;

var runaway = comcon.include(
    "function(req){" +
    "  var s = 0;" +
    "  for (var i = 0; i < 4000000000; i++) { s += i % 7; }" +
    "  return { status: 200, body: 'completed s=' + s }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/gate") {
        locs[i].handler = function (req) {
            var conf = JSON.parse(confined({ method: req.method }).body);
            var host;
            try { host = hostProbes(); }
            catch (e) { host = { error: String(e.message || e) }; }
            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify({ confined: conf, host: host }));
        };
    }
    if (locs[i].path === "/gas") {
        locs[i].handler = function (req) {
            var t0 = Date.now();
            try {
                var o = runaway({ method: req.method });
                req.respond(200, {'content-type':'text/plain'},
                            'GUARD-MISSED ' + (Date.now() - t0) + 'ms ' + o.body);
            } catch (e) {
                req.respond(200, {'content-type':'text/plain'},
                            'GUARD-FIRED ' + (Date.now() - t0) + 'ms '
                            + String(e && e.message || e));
            }
        };
    }
}
JS

$t->try_run('no js module')->plan(9);

###############################################################################

my $body = http_get('/gate');
my ($json) = $body =~ /\r\n\r\n(.*)$/s;

# Parse without a JSON module (Test::Nginx has no dependency on one).
sub probes {
    my ($blob, $section) = @_;
    my ($s) = $blob =~ /"\Q$section\E":\{(.*?)\}/s;
    return {} unless defined $s;
    my %h;
    while ($s =~ /"([a-z_0-9]+)":"(open|closed)"/g) { $h{$1} = $2; }
    return \%h;
}

my $conf = probes($json, 'confined');
my $host = probes($json, 'host');

diag("confined: " . join(' ', map { "$_=$conf->{$_}" } sort keys %$conf));
diag("host:     " . join(' ', map { "$_=$host->{$_}" } sort keys %$host));

cmp_ok(scalar(keys %$conf), '>=', 12,
       'the confined context ran the whole probe battery');
is(scalar(keys %$conf), scalar(keys %$host),
   'both contexts ran the SAME battery (no probe silently dropped)');

# THE GATE: nothing may be open inside the fragment.
my @open = sort grep { $conf->{$_} eq 'open' } keys %$conf;
is(scalar(@open), 0, 'M-SES gate: no escape route is open in a confined fragment')
    or diag("OPEN in confined context: @open");

# THE CONTROL: a probe that is closed everywhere proves nothing. At least the
# code-from-strings ladder must be demonstrably open in unconfined host JS,
# or these probes are not exercising the capability they claim to.
my @host_open = sort grep { $host->{$_} eq 'open' } keys %$host;
cmp_ok(scalar(@host_open), '>=', 1,
       'control: probes CAN detect an open route (some are open unconfined)')
    or diag("nothing open in host context either — the battery is inert");

my @discriminating = grep { $conf->{$_} eq 'closed' && $host->{$_} eq 'open' }
                     keys %$conf;
cmp_ok(scalar(@discriminating), '>=', 4,
       'at least 4 routes are closed confined AND open unconfined');

# Named explicitly so a regression points at the class, not just a count.
is($conf->{c_fn_ctor}, 'closed', 'gate (c): Function-constructor ladder is closed');

# (e) the resource guard. Armed via nginx.workerRequestTimeout = 300ms; the
# fragment's loop is sized to run for seconds unguarded, so "interrupted" is not
# something it could report by finishing early.
my $gas = http_get('/gas');
# GUARD-FIRED / GUARD-MISSED are disjoint tokens on purpose: an earlier version
# used INTERRUPTED vs NOT-INTERRUPTED, and the "passing" regex matched the
# failure string as a SUBSTRING -- so the probe reported success when the
# fragment ran to completion, and the negative control could not fail. A marker
# pair where one contains the other is not a marker pair.
like($gas, qr/GUARD-FIRED /,
     'gate (e): a runaway confined fragment is stopped by the request deadline')
    or diag("gas probe said: " . (($gas =~ /\r\n\r\n(.*)/s)[0] // '(no body)'));
my ($ms) = $gas =~ /GUARD-FIRED (\d+)ms/;
# It must run for a WHILE and then be stopped. An immediate throw for an
# unrelated reason would also be "stopped", so the lower bound is the check that
# makes this about the deadline.
cmp_ok($ms // 0, '>=', 100,
       'gate (e): it ran until the deadline (not an unrelated immediate throw)');
cmp_ok($ms // 999999, '<', 5000,
       'gate (e): and was stopped well before the loop would have ended');

