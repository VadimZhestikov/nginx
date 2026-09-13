#!/usr/bin/perl

# COMCON M-LIB `ttl` — a capability with a LIFETIME.
#
# TM-2 gave session grants a lease, and `include()` binds grants as closure parameters at
# ADMISSION. So an operator who resolves a session once and binds a fragment has handed it
# capabilities that outlive the lease indefinitely: the mapping expires, the capability does
# not. Neither feature is wrong alone; the hole is only visible when they are put together.
#
# `mediate(cap, ttl(seconds))` closes it. The clock starts when the capability CROSSES into
# the compartment, not when mediate() built the descriptor -- the descriptor carries a
# duration, so there is one clock (nginx's) rather than two that could disagree. And because
# it starts at include time, a test must include the capability BEFORE waiting: including it
# after the sleep hands out a fresh lifetime, which is how the V12 row for this code first
# reported "alive" forever.
#
# LIFETIMES COMPOSE WHERE BUDGETS REFUSE, and that contrast is the interesting part. Two
# budgets have no computable meet -- 10/min and 100/hour are not ordered, so `uses` refuses
# to re-mediate with a different one rather than guess. Two lifetimes ARE ordered: the
# shorter is strictly narrower than both, so `ttl` takes the min. Same rule (never widen),
# opposite outcome, because the lattice is different.

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

        location /now   { }
        location /later { }
        location /shape { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
function at(p, fn) { var l = locs.find(function (x) { return x.path === p; }); if (l) l.handler = fn; }

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var READ = "function(){ return (typeof s.address === 'string') ? 'alive' : 'expired'; }";

/*
 * THE SHORT-LIVED ONES ARE MINTED IN THE FIRST REQUEST, not at config time.
 * A ttl clock starts when the capability crosses into the compartment, so a
 * 1-second lifetime created at load is racing nginx's own startup: the first
 * run of this file failed because /now arrived after the capability had already
 * expired. Minting them on the first request makes the window start where the
 * measurement starts.
 */
var shortLived = null, metA = null, metB = null;

function mintShortLived() {
    shortLived = comcon.include(READ,
        { imports: [], grants: { s: comcon.mediate(sock, comcon.ttl(1)) } });
    /* the MEET: 3600 then 1 -- and 1 then 3600, so order cannot matter */
    metA = comcon.include(READ,
        { imports: [], grants: { s: comcon.mediate(
            comcon.mediate(sock, comcon.ttl(3600)), comcon.ttl(1)) } });
    metB = comcon.include(READ,
        { imports: [], grants: { s: comcon.mediate(
            comcon.mediate(sock, comcon.ttl(1)), comcon.ttl(3600)) } });
}

/* an hour: this one is unaffected by startup timing, so it stays at load */
var longLived  = comcon.include(READ,
    { imports: [], grants: { s: comcon.mediate(sock, comcon.ttl(3600)) } });
/* ttl composed with a field mask and with a budget: all three at once */
var stacked = comcon.include(
    "function(){ return [typeof s.address, typeof s.port].join(','); }",
    { imports: [], grants: { s: comcon.mediate(
        comcon.mediate(comcon.mediate(sock, comcon.redact(['address'])),
                       comcon.uses('ttl-stack', 50, 60)),
        comcon.ttl(3600)) } });

function run(f) { try { return f({}); } catch (e) { return 'threw'; } }

at('/now', function (req) {
    mintShortLived();
    req.respond(200, {'content-type':'application/json'}, JSON.stringify({
        shortLived: run(shortLived), longLived: run(longLived),
        metA: run(metA), metB: run(metB), stacked: run(stacked),
        denials: nginx.tenantDenials().byOp['cap.expired'] }));
});

at('/later', function (req) {
    var before = nginx.tenantDenials().byOp['cap.expired'];
    var o = { shortLived: run(shortLived), longLived: run(longLived),
              metA: run(metA), metB: run(metB), stacked: run(stacked) };
    o.denialsAdded = nginx.tenantDenials().byOp['cap.expired'] - before;

    /* audit mode: an expired capability is logged and ALLOWED, like every gate */
    comcon.mode('audit');
    o.audited = run(shortLived);
    comcon.mode('enforce');
    o.afterAudit = run(shortLived);

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
});

at('/shape', function (req) {
    function refuses(fn) {
        try { fn(); return 'ACCEPTED'; }
        catch (e) { return String(e.message).split(';')[0].slice(0, 58); }
    }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify({
        zero:  refuses(function () { comcon.mediate(sock, comcon.ttl(0)); }),
        neg:   refuses(function () { comcon.mediate(sock, comcon.ttl(-5)); }),
        frac:  refuses(function () { comcon.mediate(sock, comcon.ttl(1.5)); }),
        empty: refuses(function () { comcon.mediate(sock, comcon.ttl()); })
    }));
});
JS

$t->try_run('no js module')->plan(11);

###############################################################################

my $now = http_get('/now');
diag($1) if $now =~ /(\{.*\})/;

like($now, qr/"shortLived":"alive"/,
     'a capability with a 1 s lifetime works while it is alive');
like($now, qr/"longLived":"alive"/, 'so does one with an hour');
like($now, qr/"metA":"alive","metB":"alive"/,
     'and so do the composed ones, before the shorter lifetime passes');
like($now, qr/"stacked":"undefined,number"/,
     'ttl composes with a field mask AND a budget: address stays redacted, port '
     . 'reads, and the lifetime has not expired');

select(undef, undef, undef, 1.6);   # in PERL: ngx_time() is nginx's cached clock

my $later = http_get('/later');
diag($1) if $later =~ /(\{.*\})/;

like($later, qr/"shortLived":"expired"/,
     'THE CAPABILITY STOPS WORKING when its lifetime passes -- which is what '
     . 'makes a session lease bite on authority already handed out: include() '
     . 'binds grants at admission, so without this a fragment holds them for as '
     . 'long as it lives');
like($later, qr/"longLived":"alive"/,
     'while the hour-long one is untouched: the expiry is per capability, not a '
     . 'global clock');
like($later, qr/"metA":"expired","metB":"expired"/,
     'THE MEET TAKES THE SHORTER LIFETIME, in either composition order. Budgets '
     . 'refuse to compose because 10/min and 100/hour are not ordered; lifetimes '
     . 'ARE ordered, so min() is the meet rather than a refusal -- same rule '
     . '(never widen), opposite outcome, because the lattice differs');
like($later, qr/"denialsAdded":[1-9]/,
     'and each refusal is counted as cap.expired in tenantDenials()');
like($later, qr/"audited":"alive"/,
     'AUDIT MODE logs and ALLOWS an expired capability, exactly as it does for '
     . 'every other gate: a lifetime can be watched before it is enforced');
like($later, qr/"afterAudit":"expired"/, 'and enforce() puts it back to denying');

my $shape = http_get('/shape');
diag($1) if $shape =~ /(\{.*\})/;

like($shape, qr/"zero":"mediate: ttl\(\) needs an integer number of seconds >= 1[^"]*","neg":"mediate: ttl\(\)[^"]*","frac":"mediate: ttl\(\)[^"]*","empty":"mediate: ttl\(\)/,
     'zero, negative, fractional and absent lifetimes are all REFUSED: a '
     . 'missing lifetime is a mistake, not "forever" -- the one direction a '
     . 'mediation may never take is toward more authority');
