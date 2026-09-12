#!/usr/bin/perl

# COMCON V4 — monotonicity as an ASSERTION, not only a theorem
# (VERIFICATION.md: "the theorem holds *given* an unforgeable TCB; a TCB bug
# currently fails silently... converts TCB bugs into loud failures").
#
# The claim is A*(child) SUBSET-OF A*(parent): authority never grows on the way
# down. Two places can break it, and both are now checked rather than argued.
#
# 1. RE-MEDIATION. An outer membrane over an already-mediated capability may only
#    NARROW what the inner one allows. Before this, re-mediation failed with
#    "grant is not a NginxSocket" -- fail-closed by accident, with a message
#    about the wrong thing, because the translation unwraps one facet level and
#    found another. Now mediate() computes the ATTENUATION MEET (field masks are
#    a lattice, so the meet is an AND) and asserts the inclusion. A glob has no
#    computable meet, so a routes facet is re-mediated only by an identical glob
#    and otherwise REFUSED: guessing would be the widening this exists to stop.
#
# 2. REALIZATION. The restricted env a fragment runs under must be a sub-map of
#    the REALIZER's, name by name and value by value. That is true by
#    construction, which is precisely why it is asserted -- a substituted cap or
#    an extra name is the shape a TCB bug takes, and it would otherwise confer
#    authority nobody granted, silently.
#
# The test asserts the WIDENING ATTEMPT IS DEFEATED, not that a call throws: the
# fragment still cannot read the field the inner membrane hid.

# NEGATIVE CONTROLS (run 2026-09-12, rebuilt and re-passed after each):
#
#   the meet is an AND, not a JOIN            -> this file fails
#   a routes glob is refused, not guessed     -> the glob assertions fail
#   the realize sub-map assertion FIRES       -> injecting a name the realizer
#                                                never granted is caught, loudly
#
# The first control had to be re-aimed twice, and the reason is worth keeping:
# REMOVING the meet made nginx fail to start, and a config-time failure reaches
# prove(1) as "skipped: no js module" -- a pass that proves nothing. So the
# membranes are composed at REQUEST time and the mutation is arithmetic (AND ->
# OR) rather than deletion, which makes the regression a 500 and a real failure.

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

        location /v4 { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;
var l = locs.find(function (x) { return x.path === "/v4"; });

var READ = "function(){ return { addr: typeof s.address, port: (s.port|0),"
         + " fd: typeof s.fd }; }";

/* EVERYTHING IS BUILT AT REQUEST TIME, deliberately. The V4 assertion fires
 * where the membranes are composed, so composing them at config time would turn
 * any regression into "nginx did not start" -- which prove(1) reports as a
 * SKIP, i.e. a pass that proves nothing. At request time a regression is a 500
 * and every assertion below fails, which is what a control needs. */
l.handler = function (req) {
    var o = {};

    /* The inner membrane: only `port` survives it. */
    var inner = comcon.mediate(sock, comcon.allow(['port']));
    /* An outer membrane that ASKS FOR MORE than the inner one allows. */
    var widened  = comcon.mediate(inner, comcon.allow(['address', 'port', 'fd']));
    /* An outer membrane that narrows further. */
    var narrowed = comcon.mediate(inner, comcon.allow(['fd']));
    /* redact composes the same way. */
    var redacted = comcon.mediate(inner, comcon.redact(['port']));

    o.inner    = comcon.include(READ, { grants: { s: inner } })({});
    o.widened  = comcon.include(READ, { grants: { s: widened } })({});
    o.narrowed = comcon.include(READ, { grants: { s: narrowed } })({});
    o.redacted = comcon.include(READ, { grants: { s: redacted } })({});

    /* revoke absorbs: meeting anything with zero authority is zero, and a
     * revoked grant is WITHHELD -- the name is not bound at all, so the probe
     * has to ask whether it exists rather than dereference it. */
    var BOUND = "function(){ return (typeof s === 'undefined') ? 'unbound'"
              + " : ('bound:' + (typeof s.address)); }";
    o.revokeOuter = comcon.include(BOUND,
        { grants: { s: comcon.mediate(inner, comcon.revoke()) } })({});
    o.revokeInner = comcon.include(BOUND,
        { grants: { s: comcon.mediate(comcon.mediate(sock, comcon.revoke()),
                                      comcon.allow(['address'])) } })({});
    o.boundCtl = comcon.include(BOUND, { grants: { s: inner } })({});

    /* a routes facet has no computable glob meet: identical is fine, different
     * is refused rather than guessed */
    var srv = nginx.http.servers[0];
    o.sameGlob = 'ACCEPTED';
    try {
        comcon.mediate(comcon.mediate(srv, comcon.routes('/a/*')),
                       comcon.routes('/a/*'));
        o.sameGlob = 'ok';
    } catch (e) { o.sameGlob = 'THREW:' + e.message; }
    o.diffGlob = 'ACCEPTED';
    try {
        comcon.mediate(comcon.mediate(srv, comcon.routes('/a/*')),
                       comcon.routes('/b/*'));
    } catch (e) { o.diffGlob = /glob meet/.test(e.message) ? 'refused' : 'other'; }

    /* V4 at realization: the restricted env must be a sub-map of the realizer's */
    var env = comcon.grant(comcon.env(), "s", inner);
    o.realized = comcon.realize(comcon.quote(READ),
                                { imports: ["s"] }, env)({});
    o.notInEnv = comcon.realize(comcon.quote("function(){ return typeof s; }"),
                                { imports: ["s"] }, comcon.env())(0);

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(12);

###############################################################################

my $r = http_get('/v4');

# --- the inner membrane, for reference -----------------------------------
like($r, qr/"inner":\{"addr":"undefined","port":8091,"fd":"undefined"\}/,
     'the inner membrane allows only `port`');

# --- THE CLAIM: an outer membrane cannot restore what the inner hid -------
like($r, qr/"widened":\{"addr":"undefined","port":8091,"fd":"undefined"\}/,
     'ASKING FOR MORE GETS NO MORE: re-mediating with allow(address,port,fd) '
     . 'yields the MEET -- address and fd stay hidden');
like($r, qr/"narrowed":\{"addr":"undefined","port":0,"fd":"undefined"\}/,
     'and re-mediating with a disjoint field narrows to nothing, as the meet '
     . 'of {port} and {fd} demands');
like($r, qr/"redacted":\{"addr":"undefined","port":0,"fd":"undefined"\}/,
     'redact composes the same way: the meet of {port} and NOT{port} is empty');

# --- revoke is the zero of the lattice ----------------------------------
like($r, qr/"revokeOuter":"unbound"/,
     'an outer revoke() withholds the grant entirely -- the name is not bound');
like($r, qr/"revokeInner":"unbound"/,
     'and an outer allow() over an INNER revoke() restores nothing: zero '
     . 'authority absorbs');
like($r, qr/"boundCtl":"bound:undefined"/,
     'CONTROL: the same probe under the inner membrane IS bound, with address '
     . 'hidden -- so "unbound" above is the revoke, not a broken probe');

# --- globs: refuse rather than guess ------------------------------------
like($r, qr/"sameGlob":"ok"/,
     'a routes facet re-mediated by an IDENTICAL glob is fine');
like($r, qr/"diffGlob":"refused"/,
     'a DIFFERENT glob is refused -- a glob meet is not computable, and '
     . 'guessing it would be the widening this check exists to prevent');

# --- V4 at realization ---------------------------------------------------
like($r, qr/"realized":\{"addr":"undefined","port":8091/,
     'realize() binds the restricted env and the membrane still holds');
like($r, qr/"notInEnv":"undefined"/,
     'a name the realizer does not hold is simply not bound -- the restricted '
     . 'env can only ever be a SUB-map');

like($r, qr/"inner":\{"addr":"undefined"/,
     'sanity: the reference membrane is unchanged by everything above');
