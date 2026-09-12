#!/usr/bin/perl

# COMCON M-LIB step 1 — the standard policy library, and the fail-open it found.
#
# ROADMAP M-LIB: "the user-facing surface is not the kernel but the combinators."
# The kernel is 20 operators; before this there was no `std.*` at all, while
# MANUAL.md was written as-if-shipped against std.profiles.tenant(acme).
#
# TWO THINGS ARE ASSERTED, and the second is why this went first.
#
# 1. A PROFILE IS A BUNDLE OF FIELDS THE KERNEL ENFORCES, and nothing else.
#    `imports` is DERIVED from the env, so the manifest and the grants cannot
#    drift -- writing both is the footgun the library exists to remove (omit a
#    granted name and admission refuses the fragment; list an ungranted one and
#    the fragment sees undefined). MANUAL's `{profile:"restrictive",
#    onViolation:"audit"}` and std.postures.* are NOT shipped: nothing reads
#    them, and a posture of ignored keys would be believed. std.describe()
#    states, per field, what enforces it -- and names what is absent.
#
# 2. THE MEDIATION VOCABULARY IS CLOSED, BECAUSE AN UNKNOWN WORD USED TO MEAN
#    FULL AUTHORITY. include()'s flavor translation fell through to its default
#    `{kind:0, mask:FULL}`, so a descriptor the enforcement layer does not
#    implement (`allowHosts`) or a one-letter typo (`redcat` for `redact`)
#    granted the capability IN FULL. Measured before the fix: the fragment read
#    s.address as a string through both, where redact() hid it -- a misspelling
#    that WIDENED authority. A library generates these descriptors mechanically,
#    so this had to be closed before shipping any vocabulary.

# NEGATIVE CONTROLS (run 2026-09-12; all seven reverted to failure, rebuilt and
# re-passed after each):
#
#   mediate() validates the flavor          -> tests 15-16 fail
#   mediate() SNAPSHOTS the descriptor      -> the whole response fails (the
#                                              mutated descriptor throws at
#                                              include time, which is the
#                                              fail-open reopened)
#   mediate()'s message names the vocabulary-> test 17 fails
#   imports derived from the env            -> test 4 fails
#   tenant bounded by default               -> test 6 fails
#   pure_library declares imports []        -> test 10 fails
#   the namespace is frozen                 -> test 3 fails
#
# NOT controlled, deliberately: include()'s `else throw` on an unknown flavor.
# No test can reach it now that mediate() refuses and snapshots, and it is kept
# anyway -- what it guards is DRIFT between two lists of the same closed
# vocabulary, where the fall-through decides whether the mistake means REFUSE or
# FULL AUTHORITY. That is the one place this project keeps an uncontrollable
# guard on purpose, and the reason is written next to it in the source.

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

        location /s { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var locs = nginx.http.servers[0].locations;
var l = locs.find(function (x) { return x.path === "/s"; });

/* A tenant env: one mediated capability, granted under a route/field membrane. */
var acme = comcon.grant(comcon.env(), "s",
                        comcon.mediate(sock, comcon.redact(['address'])));

var tenantContract = comcon.std.profiles.tenant(acme);
var libContract    = comcon.std.profiles.pure_library();

/* The tenant fragment uses exactly the granted name. */
var tenant = comcon.include(
    "function(){ return { addrType: typeof s.address, port: (s.port|0) }; }",
    tenantContract);

/* A pure library: no grants at all, so any HOST name must refuse admission --
 * but language intrinsics need no declaration (decided 2026-09-12 after the V3
 * oracle found the gate refusing `x !== undefined`), so a cap-free fragment can
 * actually compute with JSON and Object rather than arithmetic alone. */
var pure = comcon.include(
    "function(n){ return JSON.parse('[6,7]').reduce(function(a,b){return a*b;}); }",
    libContract);

l.handler = function (req) {
    var o = {};

    o.version  = comcon.std.version;
    o.profiles = comcon.std.profiles ? Object.keys(comcon.std.profiles) : [];
    o.frozen   = Object.isFrozen(comcon.std) && Object.isFrozen(tenantContract);

    /* imports are DERIVED from the env, not written a second time */
    o.imports  = tenantContract.imports;
    o.sameAsEnv = (tenantContract.grants === acme.grants);
    o.bounded  = !!tenantContract.meter;
    o.checkReq = tenantContract.checkRequest;

    /* the profiles actually work end to end */
    o.tenant = tenant({});
    o.pure   = pure(0);

    /* pure_library admits NOTHING free: a fragment touching a free name is
     * refused, which is the profile's whole point */
    o.freeName = 'ACCEPTED';
    try {
        comcon.include("function(){ return nginx.gc(); }",
                       comcon.std.profiles.pure_library());
        o.freeName = 'ACCEPTED';
    } catch (e) { o.freeName = 'refused'; }

    /* describe() names what enforces each field, and what is absent */
    var d = comcon.std.describe();
    o.enforced = d.enforced.map(function (r) { return r.field; });
    o.absentCount = d.absent.length;
    o.namesPostures = d.absent.some(function (r) {
        return /postures/.test(r.name); });

    /* a profile needs a real env */
    o.badEnv = 'ACCEPTED';
    try { comcon.std.profiles.tenant({}); }
    catch (e) { o.badEnv = 'refused'; }

    /* THE CLOSED VOCABULARY: both spellings that used to mean FULL authority */
    o.unknownFlavor = 'ACCEPTED';
    try {
        comcon.mediate(sock, { flavor: 'allowHosts', hosts: ['a.example'] });
    } catch (e) { o.unknownFlavor = 'refused'; }

    o.typoFlavor = 'ACCEPTED';
    try { comcon.mediate(sock, { flavor: 'redcat', fields: ['address'] }); }
    catch (e) { o.typoFlavor = 'refused'; }

    o.noInterceptor = 'ACCEPTED';
    try { comcon.mediate(sock); }
    catch (e) { o.noInterceptor = /revoke.redact.allow.routes/.test(e.message)
                                  ? 'named' : 'refused'; }

    /* TIME OF CHECK vs TIME OF USE: the descriptor is an ordinary object the
     * caller still holds, so mutating it after mediate() would have reached the
     * flavor translation as an unknown word -- the same fail-open from the other
     * end.  mediate() snapshots, so the mediation that was checked is the one
     * that applies: address stays redacted. */
    var it = comcon.redact(['address']);
    var med = comcon.mediate(sock, it);
    it.flavor = 'redcat';
    it.fields = [];
    var mutated = comcon.include(
        "function(){ return { addrType: typeof s.address, port: (s.port|0) }; }",
        comcon.std.profiles.tenant(comcon.grant(comcon.env(), "s", med)));
    o.toctou = mutated({});

    /* ...and the four real words still work */
    o.known = ['revoke', 'redact', 'allow', 'routes'].map(function (f) {
        try {
            comcon.mediate(sock, comcon[f](f === 'routes' ? '/x/*' : ['port']));
            return 'ok';
        } catch (e) { return 'BROKE:' + f; }
    });

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(19);

###############################################################################

my $r = http_get('/s');

# --- the library exists and is inert ------------------------------------
like($r, qr/"version":"comcon-std-1"/, 'the library is versioned');
like($r, qr/"profiles":\["tenant","pure_library"\]/,
     'two profiles ship: tenant and pure_library');
like($r, qr/"frozen":true/, 'the namespace and the contracts it makes are frozen');

# --- a profile bundles fields that BITE ---------------------------------
like($r, qr/"imports":\["s"\]/,
     'imports are DERIVED from the env, so the manifest cannot drift from '
     . 'the grants');
like($r, qr/"sameAsEnv":true/, '...from that exact env, not a copy');
like($r, qr/"bounded":true/,
     'a tenant is BOUNDED BY DEFAULT -- unbounded-by-default is a named '
     . "residual risk in the audit's gap list, not a thing to repeat");
like($r, qr/"checkReq":true/, 'request-field admission is on for a tenant');

# --- and they work end to end -------------------------------------------
like($r, qr/"tenant":\{"addrType":"undefined","port":8091\}/,
     'the tenant profile grants the mediated cap: port visible, address '
     . 'redacted by the membrane');
like($r, qr/"pure":42/,
     'the pure_library profile runs a cap-free computation -- and may use the '
     . 'language intrinsics (JSON, Object) without declaring them, which it '
     . 'could not before the C3 intrinsics allowance');
like($r, qr/"freeName":"refused"/,
     'pure_library refuses a fragment that touches ANY free name -- imports '
     . 'is present and empty, which is what switches admission on');

# --- describe() is the honesty surface ----------------------------------
like($r, qr/"enforced":\["imports","grants","checkRequest","meter","identity","tests","deps"\]/,
     'describe() names every contract field a profile may set, with what '
     . 'enforces it');
like($r, qr/"absentCount":3/, '...and names what is deliberately absent');
like($r, qr/"namesPostures":true/,
     "...including MANUAL's postures/onViolation, which nothing reads");

like($r, qr/"badEnv":"refused"/, 'a profile refuses anything but a real env()');

# --- the closed vocabulary (the fail-open this increment found) ----------
like($r, qr/"unknownFlavor":"refused"/,
     'an unimplemented flavor (allowHosts) is REFUSED -- it used to grant the '
     . 'capability in full');
like($r, qr/"typoFlavor":"refused"/,
     'a one-letter typo of a real flavor is refused -- it used to grant MORE '
     . 'authority than the correct spelling');
like($r, qr/"noInterceptor":"named"/,
     'mediate() needs an interceptor, and says which four words exist');
like($r, qr/"toctou":\{"addrType":"undefined","port":8091\}/,
     'TIME OF CHECK = TIME OF USE: mutating the descriptor after mediate() '
     . 'cannot reopen the fail-open -- the membrane still redacts');
like($r, qr/"known":\["ok","ok","ok","ok"\]/,
     '...and all four real words still work');
