#!/usr/bin/perl

# COMCON M-LIB — `uses`: a BUDGETED capability. The first mediation flavor that
# attenuates HOW MANY TIMES rather than WHAT.
#
# The mediation vocabulary shipped four words (revoke/redact/allow/routes) while the
# documents promised ten, and M-LIB's remainder was blocked on exactly that: shipping
# `uses`/`ttl`/`window` as descriptors would have been shipping policy that does nothing.
# This is the enforcement.
#
# THE PROPERTY THAT DECIDES WHETHER IT IS REAL IS FLEET-WIDENESS. A per-worker counter
# means the operator who wrote `limit: 10` gets 10 per worker -- forty on a four-worker
# box -- which is not the number they wrote, and is the same class of defect as the
# audit/enforce mode switch that shipped per-process (v5.56). So the counter lives in
# nginx.shared, and the test below spends a budget across CONCURRENT connections on four
# workers and asserts the TOTAL, having first asserted that several distinct workers
# actually served (a single-worker fixture cannot see this bug at all).
#
# The window is FIXED, not sliding, and the test says so by measuring it: the counter is
# created on first use with a TTL, and when it expires the budget is whole again.
#
# Audit mode gets the same treatment as every other gate: log-and-ALLOW, so an operator
# can watch a limit be exceeded before switching it on.

use warnings;
use strict;

use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 4;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /spend   { }
        location /basic   { }
        location /window  { }
        location /shape   { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var wid = null;

/* one fragment per budget, built at config time (pre-fork) so every worker
   inherits the SAME wrapper configuration -- the budget is in the shared
   counter, not in the fragment */
function budgeted(key, limit, window) {
    return comcon.include(
        "function(){ return (typeof s.address === 'string') ? 'spent' : 'denied'; }",
        { imports: [], grants: { s: comcon.mediate(sock,
                                     comcon.uses(key, limit, window)) } });
}

var fleet = budgeted('fleet', 10, 60);

/* one use per request, from whichever worker serves it */
locs.find(function (l) { return l.path === "/spend"; }).handler = function (req) {
    if (wid === null) { wid = nginx.shared.incr('__widseq__', 1); }
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ r: fleet({}), w: wid }));
};

locs.find(function (l) { return l.path === "/basic"; }).handler = function (req) {
    var o = {};

    var f = budgeted('basic-' + nginx.shared.incr('__runseq__', 1), 3, 60);
    o.spend = [f({}), f({}), f({}), f({})];

    /* the counter is in the SHARED store, which is what makes it fleet-wide;
       the host can read it, which is how we know where it lives */
    var keys = nginx.shared.keys().filter(function (k) {
        return k.indexOf('comcon.budget:basic-') === 0; });
    o.counterVisible = (keys.length === 1);
    o.counterValue = keys.length ? nginx.shared.get(keys[0]) : null;

    /* two capabilities, ONE name -> one budget */
    var seq = nginx.shared.incr('__runseq__', 1);
    var a = budgeted('shared-' + seq, 2, 60), b = budgeted('shared-' + seq, 2, 60);
    o.sameKey = [a({}), b({}), a({})];

    /* two capabilities, two names -> two budgets */
    var c = budgeted('c-' + seq, 1, 60), d = budgeted('d-' + seq, 1, 60);
    o.twoKeys = [c({}), d({}), c({}), d({})];

    /* audit mode logs and ALLOWS, like every other gate */
    var e = budgeted('audit-' + seq, 1, 60);
    comcon.mode('audit');
    o.audit = [e({}), e({}), e({})];
    comcon.mode('enforce');
    o.afterAudit = e({});

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};

/* the window is FIXED: spend it, wait past the TTL, spend again */
locs.find(function (l) { return l.path === "/window"; }).handler = function (req) {
    var phase = req.args.indexOf('phase=2') >= 0;
    var f = budgeted('win', 2, 1);
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ phase: phase ? 2 : 1, spend: [f({}), f({}), f({})] }));
};

/* the shape of the flavor itself: what it refuses, and how it composes */
locs.find(function (l) { return l.path === "/shape"; }).handler = function (req) {
    var o = {};
    function refuses(fn) {
        try { fn(); return 'ACCEPTED'; }
        catch (e) { return e.message.split(' -- ')[0].slice(0, 60); }
    }

    o.noKey    = refuses(function () { comcon.uses('', 5, 60); comcon.mediate(sock, comcon.uses('', 5, 60)); });
    o.noLimit  = refuses(function () { comcon.mediate(sock, comcon.uses('k')); });
    o.zeroLim  = refuses(function () { comcon.mediate(sock, comcon.uses('k', 0, 60)); });
    o.fracLim  = refuses(function () { comcon.mediate(sock, comcon.uses('k', 1.5, 60)); });
    o.noWindow = refuses(function () { comcon.mediate(sock, comcon.uses('k', 5)); });

    /* re-mediation: identical budgets compose, different ones are refused */
    var m = comcon.mediate(sock, comcon.uses('compose', 5, 60));
    o.same = refuses(function () { comcon.mediate(m, comcon.uses('compose', 5, 60)); });
    o.diff = refuses(function () { comcon.mediate(m, comcon.uses('compose', 6, 60)); });

    /* a budget composes with a field mask, and the mask still attenuates */
    var masked = comcon.mediate(comcon.mediate(sock, comcon.redact(['address'])),
                                comcon.uses('masked-' + nginx.shared.incr('__runseq__', 1), 5, 60));
    var g = comcon.include(
        "function(){ return [typeof s.address, typeof s.port].join(','); }",
        { imports: [], grants: { s: masked } });
    o.masked = g({});

    /* a REDACTED read must not be charged: it never exercised the capability */
    var key = 'redact-' + nginx.shared.incr('__runseq__', 1);
    var r = comcon.mediate(comcon.mediate(sock, comcon.redact(['address'])),
                           comcon.uses(key, 2, 60));
    var h = comcon.include(
        "function(){ var n = 0, i;"
      + " for (i = 0; i < 5; i++) { if (s.address === undefined) n++; }"
      + " return n + ':' + (typeof s.port); }",
        { imports: [], grants: { s: r } });
    o.redactedFree = h({});
    o.redactedCount = nginx.shared.get('comcon.budget:' + key);

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(16);

###############################################################################

sub concurrent_get {
    my ($path, $count) = @_;
    my (@socks, @out);
    for (1 .. $count) {
        my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1:8080',
                                      Proto => 'tcp', Timeout => 5);
        push @socks, $s if $s;
    }
    $_->print("GET $path HTTP/1.0\r\nHost: localhost\r\n\r\n") for @socks;
    for my $s (@socks) {
        local $/;
        push @out, (<$s> // '');
        $s->close;
    }
    return @out;
}

# --- the basics ----------------------------------------------------------
my $b = http_get('/basic');
diag($1) if $b =~ /(\{.*\})/;

like($b, qr/"spend":\["spent","spent","spent","denied"\]/,
     'a budget of 3 allows exactly three uses and denies the fourth');
like($b, qr/"counterVisible":true/,
     'the counter lives in nginx.shared, not on the wrapper -- which is what '
     . 'makes it fleet-wide rather than per-worker');
like($b, qr/"counterValue":"4"/,
     'and it counts the REFUSED attempt too: an audit that stops at the limit '
     . 'cannot tell "just reached it" from "hammering it"');
like($b, qr/"sameKey":\["spent","spent","denied"\]/,
     'two capabilities that NAME the same counter share one budget -- sharing '
     . 'is something the operator says, not something the wrapper decides');
like($b, qr/"twoKeys":\["spent","spent","denied","denied"\]/,
     'two capabilities with different names have separate budgets');
like($b, qr/"audit":\["spent","spent","spent"\]/,
     'AUDIT MODE logs and ALLOWS an exhausted budget, exactly as it does for '
     . 'every other gate: the audit-first rollout applies to rate limits '
     . 'without being re-invented for them');
like($b, qr/"afterAudit":"denied"/,
     'and enforce() puts the same budget back to denying');

# --- fleet-wide ----------------------------------------------------------
my @out = concurrent_get('/spend', 24);
my (%w, $spent, $denied);
for my $r (@out) {
    $w{$1}++    if $r =~ /"w":(\d+)/;
    $spent++    if $r =~ /"r":"spent"/;
    $denied++   if $r =~ /"r":"denied"/;
}
$spent  //= 0;
$denied //= 0;
diag("workers=" . scalar(keys %w) . " spent=$spent denied=$denied");

cmp_ok(scalar(keys %w), '>=', 2,
       'the requests were served by SEVERAL distinct workers -- asserted from '
       . 'nginx.shared.incr(), the one per-process identity that cannot be '
       . 'inherited across fork(); a single-worker fixture cannot see this bug');
is($spent, 10,
   'the budget of 10 was spent EXACTLY TEN TIMES across all of them: the '
   . 'counter is fleet-wide, so the operator who wrote 10 gets 10 and not 10 '
   . 'per worker');
is($spent + $denied, 24, 'every request was answered, spent or denied');

# --- the fixed window ----------------------------------------------------
my $w1 = http_get('/window');
like($w1, qr/"spend":\["spent","spent","denied"\]/,
     'the window budget is spent');

select(undef, undef, undef, 1.6);   # in PERL: ngx_time() is nginx's cached time

my $w2 = http_get('/window?phase=2');
like($w2, qr/"spend":\["spent","spent","denied"\]/,
     'after the window expires the budget is WHOLE again -- the counter is a '
     . 'FIXED window (the key carries a TTL), which also means a caller may '
     . 'spend limit at the end of one window and limit at the start of the '
     . 'next; that is stated in the code rather than discovered here');

# --- the shape -----------------------------------------------------------
my $sh = http_get('/shape');
diag($1) if $sh =~ /(\{.*\})/;

like($sh, qr/"noKey":"mediate: uses\(\) needs a key naming the counter"/,
     'uses() without a key is refused: a budget nobody named is a counter '
     . 'whose identity depends on wrapping order');
like($sh, qr/"noLimit":"mediate: uses\(\) needs an integer limit >= 1; a missing limit"/,
     'a missing limit is a MISTAKE, not "unlimited" -- the one direction a '
     . 'mediation may never take is toward more authority');
like($sh, qr/"diff":"mediate: cannot re-mediate a budgeted capability with a DIFF"/,
     're-mediating with a DIFFERENT budget is refused, on the same grounds as '
     . 'a different route glob: 10/min and 100/hour are not ordered, so a meet '
     . 'would have to guess and the guess would widen one of them');
like($sh, qr/"masked":"undefined,number"[^}]*"redactedFree":"5:number","redactedCount":"1"/,
     'a budget COMPOSES with a field mask (the mask still hides address), and '
     . 'a redacted read is NOT charged: five reads of a hidden field spend '
     . 'nothing, because a field the membrane hides was never an exercise of '
     . 'the capability');
