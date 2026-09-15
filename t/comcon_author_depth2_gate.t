#!/usr/bin/perl

# THE ESCAPE BATTERY AT DEPTH 2 -- does a sub-fragment's cage differ from its
# parent's?
#
# The authoring tier lets a fragment author fragments (t/comcon_author_basic.t
# is the contract).  The claim that matters is not "the sub-fragment is caged"
# but "the sub-fragment is caged EXACTLY as a fragment is": same intrinsics
# frozen, same code-from-strings ladder closed, same ambient roots absent, same
# .stack shape.  A cage that nests differently is a second confinement to
# audit, and nobody has audited it.
#
# So the S6 battery (t/tools/mses-probes.js -- ONE definition, shared with the
# S6 gate and its AOT arm) runs three times against one request:
#
#   depth 1   the battery as an admitted fragment invoked by the host
#   depth 2   the SAME battery source, handed to a parent fragment AS DATA
#             (the invocation argument), authored by it through its `author`
#             capability, invoked by it, its result forwarded
#   host      the battery unconfined -- the control, where the routes a cage
#             closes must be demonstrably OPEN or the probes measure nothing
#
# and the assertion is the comparison: depth 2 must equal depth 1, probe by
# probe.  The S6 gate already shows depth 1 differs from the host; equality
# makes depth 2 differ from the host too, transitively.
#
# THE SOURCE CROSSES AS DATA.  The parent never sees the battery in its own
# text; it receives a string and includes it.  That is the tier's whole shape
# -- a fragment authoring code it did not write -- and it also keeps this file
# free of a JS string nested inside a JS string nested inside a Perl heredoc.
#
# THE STOP CONDITION of the authoring-tier plan: any probe answering
# differently at depth 2 than at depth 1, unexplained, halts the work.

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

        location /depth { }
    }
}
EOF

my $probes = do { open my $f, '<', 'tools/mses-probes.js' or die $!; local $/; <$f> };

my $root_js = <<'JS';
%%PROBES%%

/* The battery as a fragment body, and the names it references.  A sub-fragment
 * is ALWAYS admitted (the tier does not offer an un-admitted include), so the
 * depth-1 arm is admitted under the same contract for a like-for-like
 * comparison: `imports` DECLARES names (any name -- declaring `nginx` does not
 * conjure it; the frozen compartment global has no such binding, so the probe
 * reads undefined and reports closed).  Except one: `globalThis` is a DENIED
 * name that no manifest re-admits, so the a_global_this row cannot be carried
 * by ANY admitted fragment, at either depth -- PROBES_ADMISSIBLE is the battery
 * without it (same rows, one definition each; see the header there), and the
 * FULL battery is used below to show the refusal itself is identical. */
var BATTERY = "function(req){ var probes = (" + PROBES_ADMISSIBLE + ")(); return probes; }";
var FULL    = "function(req){ var probes = (" + PROBES + ")(); return probes; }";
var IMPORTS = ['Object', 'Array', 'String', 'Symbol', 'nginx', 'comcon'];

/* depth 1: the host includes the battery. */
var depth1 = comcon.include(BATTERY, { imports: IMPORTS });

/* depth 2: the host includes a PARENT that includes whatever source it is
 * handed, through its author capability, and forwards the result. */
var parent = comcon.include(
    "function(req){" +
    "  var sub = author.include(req.src, { imports: req.imports });" +
    "  return sub({});" +
    "}",
    { imports: [], grants: { author: comcon.author({ subFragments: 4 }) } });

/* the control: unconfined host JS. */
function hostProbes() {
    /* eslint-disable no-eval */
    var f = (0, eval)('(' + PROBES + ')');
    return f();
}

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/depth") {
        locs[i].handler = function (req) {
            var out = {};
            try { out.depth1 = depth1({}); }
            catch (e) { out.depth1 = { error: String(e.message || e) }; }
            try { out.depth2 = parent({ src: BATTERY, imports: IMPORTS }); }
            catch (e) { out.depth2 = { error: String(e.message || e) }; }
            try { out.host = hostProbes(); }
            catch (e) { out.host = { error: String(e.message || e) }; }
            /* the admission gate itself, at both depths: the FULL battery
               names globalThis, and must be refused the same way by the host
               entrance and by the fragment entrance */
            try { comcon.include(FULL, { imports: IMPORTS }); out.refuse1 = { admitted: true }; }
            catch (e) { out.refuse1 = { code: e.code, msg: String(e.message || e) }; }
            try { out.refuse2 = { admitted: parent({ src: FULL, imports: IMPORTS }) }; }
            catch (e) { out.refuse2 = { code: e.code, msg: String(e.message || e) }; }
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$root_js =~ s/%%PROBES%%/$probes/;
$t->write_file('root.js', $root_js);

$t->try_run('no js module')->plan(13);

my $raw = http_get('/depth');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do { diag("non-JSON: " . substr($raw, 0, 400)); $o = {}; };

my ($d1, $d2, $host) = ($o->{depth1} || {}, $o->{depth2} || {}, $o->{host} || {});

diag("depth1: " . join(' ', map { "$_=$d1->{$_}" } sort keys %$d1));
diag("depth2: " . join(' ', map { "$_=$d2->{$_}" } sort keys %$d2));
diag("host:   " . join(' ', map { "$_=$host->{$_}" } sort keys %$host));

is($d1->{error}, undef, 'the depth-1 arm ran');
is($d2->{error}, undef, 'the depth-2 arm ran: the parent authored the battery from data and invoked it');

cmp_ok(scalar(keys %$d2), '>=', 11,
       'the sub-fragment ran the whole admissible battery (12 rows less the one '
       . 'no admitted fragment can carry)');
is(scalar(keys %$d2), scalar(keys %$d1),
   'depth 2 and depth 1 ran the SAME battery (no probe silently dropped)');

my @open2 = sort grep { $d2->{$_} eq 'open' } keys %$d2;
is(scalar(@open2), 0, 'no escape route is open in a sub-fragment')
    or diag("OPEN at depth 2: @open2");

is_deeply($d2, $d1,
   'THE GATE: depth 2 equals depth 1, probe by probe -- a sub-fragment\'s cage is '
   . 'its parent\'s cage, not a second one')
    or diag("differ: " . join(' ', map { "$_: d1=$d1->{$_} d2=$d2->{$_}" }
                              grep { ($d1->{$_} // '') ne ($d2->{$_} // '') }
                              sort keys %{{ %$d1, %$d2 }}));

# the control, the S6 way: closed everywhere proves nothing
my @host_open = sort grep { $host->{$_} eq 'open' } keys %$host;
cmp_ok(scalar(@host_open), '>=', 1,
       'control: the probes CAN detect an open route (some are open unconfined)');
my @discriminating = grep { $d2->{$_} eq 'closed' && $host->{$_} eq 'open' } keys %$d2;
cmp_ok(scalar(@discriminating), '>=', 4,
       'at least 4 routes are closed at depth 2 AND open unconfined');

is($d2->{c_fn_ctor}, 'closed', 'named: the Function-constructor ladder is closed at depth 2');

# the admission gate, compared the same way: the full battery names globalThis,
# which no manifest re-admits, and the refusal must be the same refusal
my ($r1, $r2) = ($o->{refuse1} || {}, $o->{refuse2} || {});
diag("refuse1: " . encode_json($r1));
diag("refuse2: " . encode_json($r2));
is($r1->{code}, 'E_ADMIT_FREENAME', 'depth 1: the full battery is refused (globalThis is a denied name)');
is($r2->{code}, 'E_ADMIT_FREENAME', 'depth 2: the same source through the fragment entrance is refused with the same code');
# the host appends the fragment's own location to an uncaught failure (D5b-4);
# that is the boundary's addition, not the refusal's, so it is stripped first
my ($t1) = ($r1->{msg} // '') =~ /admission refused: (.*?)(?: at <comcon-fragment>:\S+)?$/;
my ($t2) = ($r2->{msg} // '') =~ /admission refused: (.*?)(?: at <comcon-fragment>:\S+)?$/;
is($t2, $t1, 'and with the same reason, word for word past the entrance\'s name')
    or diag("d1: $r1->{msg}\nd2: $r2->{msg}");
like($r2->{msg}, qr/^comcon: fragment: TypeError: author\.include: /,
     'the depth-2 refusal reached the host uncaught, named as the fragment entrance\'s');

# Test::Nginx's DESTROY runs the two standing checks (no alerts, no sanitizer
# errors); destroyed here, explicitly, so they run before Test::Builder's own
# END block counts the plan rather than during global destruction after it.
undef $t;
