#!/usr/bin/perl

# COMCON TM-2 / FOUNDATION §8b — identity -> environment, the last unowned finding
# in the threat model (ASSURANCE.md F7).
#
# Everything about operator security assumed a session HAS an environment; how an
# authenticated principal becomes one was host-integration work no document owned.
#
# The design this file pins, and the reason each property is here:
#
#   THE REGISTRY HOLDS DESCRIPTORS, NEVER ENVIRONMENTS. A row is cap-free data, so
#   stealing the whole table yields no authority -- and so it can live in nginx.shared,
#   which makes it FLEET-WIDE. (The mode switch shipped per-process and put four workers
#   in mixed modes; a session table with that bug authenticates on one worker and not on
#   the next.)
#
#   RESOLUTION ATTENUATES THE CALLER'S OWN ENV. resolve(principal, env) narrows the env
#   it is handed, so a session can never exceed whoever resolved it. The registry has no
#   authority to hand out -- that is what makes a lookup table safe to expose.
#
#   DENY BY DEFAULT. An unknown principal, or an expired lease, resolves to the EMPTY
#   env -- the same answer an undeclared free name gets.
#
#   REFUSE, DO NOT TRIM. A mapping naming something the base env does not grant is
#   refused: a mapping that quietly grants less than it says is one nobody can audit.
#
#   COMCON DOES NOT AUTHENTICATE. describe() says so in the surface itself, because a
#   blurred version of that boundary is how identity systems actually get broken.

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

        location /sess { }
        location /lease { }
        location /lease-check { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

/* the operator's own environment: the root of the chain is host JS at config
   time, exactly as §8b says -- nothing mints authority here. */
function operatorEnv() {
    var e = comcon.env();
    comcon.grant(e, 's', sock);
    comcon.grant(e, 'JSON', JSON);
    return e;
}

locs.find(function (l) { return l.path === "/sess"; }).handler = function (req) {
    var o = {};

    /* no capability -> no verbs. The no-backdoor property, visible. */
    var bare = comcon.std.sessions({});
    o.bareVerbs = Object.keys(bare).sort();
    o.bareDescribe = bare.describe().held;

    var S = comcon.std.sessions({ sessions: nginx.shared });
    o.verbs = Object.keys(S).sort();
    o.authenticates = S.describe().authenticates;

    /* clean slate */
    var i, had = S.list();
    for (i = 0; i < had.length; i++) { S.revoke(had[i]); }

    /* an UNKNOWN principal gets nothing at all */
    var unknown = S.resolve('nobody@nowhere', operatorEnv());
    o.unknownGranted = unknown.granted;
    o.unknownNames = Object.keys(unknown.env.grants);
    o.unknownReason = unknown.reason;

    /* a mapping is DATA */
    S.grant('ci@acme', { imports: ['JSON'], routes: '/acme/*' });
    o.stored = nginx.shared.get('comcon.session:ci@acme');
    o.list = S.list();

    var ci = S.resolve('ci@acme', operatorEnv());
    o.ciGranted = ci.granted;
    o.ciNames = Object.keys(ci.env.grants).sort();

    /* NARROWING ONLY: the resolved env holds JSON but NOT the socket the
       operator env holds, because the mapping did not name it */
    o.ciHasSocket = (Object.prototype.hasOwnProperty.call(ci.env.grants, 's'));

    /* a descriptor naming something the base env does not grant is REFUSED */
    S.grant('greedy@acme', { imports: ['JSON', 'nginx'] });
    try {
        S.resolve('greedy@acme', operatorEnv());
        o.greedy = 'RESOLVED';
    } catch (e) {
        o.greedy = (e.message.indexOf('refusing rather than granting less') >= 0)
                   ? 'REFUSED' : 'threw: ' + e.message;
    }

    /* a capability cannot be written into the registry */
    try {
        S.grant('sneaky@acme', { imports: ['JSON'], hook: function () { return 1; } });
        o.capInDescriptor = 'ACCEPTED';
    } catch (e2) {
        o.capInDescriptor = (e2.message.indexOf('never capabilities') >= 0)
                            ? 'REFUSED' : 'threw';
    }

    /* revocation is a row removal; the next resolve sees nothing */
    S.revoke('ci@acme');
    var after = S.resolve('ci@acme', operatorEnv());
    o.afterRevoke = after.granted;
    o.afterRevokeNames = Object.keys(after.env.grants);

    /* resolve() requires the env to attenuate: no ambient authority */
    try { S.resolve('ci@acme'); o.noEnv = 'ACCEPTED'; }
    catch (e3) { o.noEnv = (e3.message.indexOf('holds no authority of its own') >= 0)
                           ? 'REFUSED' : 'threw'; }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};

/* leases: the shared store's TTL, so an expired grant is reclaimed when probed */
locs.find(function (l) { return l.path === "/lease"; }).handler = function (req) {
    var S = comcon.std.sessions({ sessions: nginx.shared });
    S.revoke('lease@acme');
    S.grant('lease@acme', { imports: ['JSON'], ttl: 1 });
    var look = S.lookup('lease@acme');
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ ttl: look ? look.ttl : null,
                         granted: S.resolve('lease@acme', operatorEnv()).granted }));
};

locs.find(function (l) { return l.path === "/lease-check"; }).handler = function (req) {
    var S = comcon.std.sessions({ sessions: nginx.shared });
    var r = S.resolve('lease@acme', operatorEnv());
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ lookup: S.lookup('lease@acme'),
                         granted: r.granted, reason: r.reason,
                         names: Object.keys(r.env.grants) }));
};
JS

$t->try_run('no js module')->plan(18);

###############################################################################

my $r = http_get('/sess');
diag($1) if $r =~ /(\{.*\})/;

# --- the capability governs the verbs ------------------------------------
like($r, qr/"bareVerbs":\["describe"\]/,
     'a session given no `sessions` resource has ONLY describe() -- the verbs '
     . 'that hand out authority are absent, not present-and-throwing');
like($r, qr/"bareDescribe":false/, 'and it says it holds nothing');
like($r, qr/"verbs":\["describe","grant","list","lookup","resolve","revoke"\]/,
     'with the resource, the full verb set appears');
like($r, qr/"authenticates":false/,
     'the surface states that COMCON does NOT authenticate -- the host asserts '
     . 'the principal, and that assertion is the whole trust transfer');

# --- deny by default ------------------------------------------------------
like($r, qr/"unknownGranted":\[\]/,
     'an unknown principal resolves to NOTHING -- the same answer an undeclared '
     . 'free name gets, not an error and not a default role');
like($r, qr/"unknownNames":\[\]/, 'its environment is empty');
like($r, qr/"unknownReason":"no mapping/, 'and it says why');

# --- the registry holds data ----------------------------------------------
like($r, qr/"stored":"\{\\"imports\\":\[\\"JSON\\"\],\\"routes\\":\\"\/acme\/\*\\"\}"/,
     'what is stored is a cap-free DESCRIPTOR: JSON text in nginx.shared, so '
     . 'stealing the table yields no authority -- and so it is fleet-wide, '
     . 'because data crosses a process boundary and capabilities do not');
like($r, qr/"capInDescriptor":"REFUSED"/,
     'a function in a descriptor is refused where the caller can see it, rather '
     . 'than silently dropped by JSON.stringify');

# --- attenuation ----------------------------------------------------------
like($r, qr/"ciNames":\["JSON"\]/,
     'resolve() returns the caller env NARROWED to the mapping');
like($r, qr/"ciHasSocket":false/,
     'and NOT the socket the operator env holds: a session env is bounded by '
     . 'the mapping, inside the env of whoever resolved it');
like($r, qr/"greedy":"REFUSED"/,
     'a mapping naming something the base env does not grant is REFUSED, not '
     . 'trimmed -- a mapping that quietly grants less than it says is one '
     . 'nobody can audit');
like($r, qr/"noEnv":"REFUSED"/,
     'resolve() without an env to attenuate is refused: the registry has no '
     . 'ambient authority to fall back on');

# --- revocation -----------------------------------------------------------
like($r, qr/"afterRevokeNames":\[\]/,
     'revocation needs no chase: resolution happens per use, so the next '
     . 'resolve after revoke() returns the empty environment');

# --- leases (THREATS T5 listed them as a mitigation; they did not exist) ---
#
# In TWO requests with the sleep in PERL: ngx_time() is nginx's CACHED time, so a
# handler that waits blocks the loop that would advance it and nothing can be seen
# to expire from inside one request.

my $lease = http_get('/lease');
diag($1) if $lease =~ /(\{.*\})/;

like($lease, qr/"ttl":1/,
     'LEASE: a session grant carries a TTL, reported in whole seconds');
like($lease, qr/"granted":\["JSON"\]/,
     'LEASE: while it is live the mapping resolves normally');

select(undef, undef, undef, 1.4);

my $gone = http_get('/lease-check');
diag($1) if $gone =~ /(\{.*\})/;

like($gone, qr/"lookup":null/,
     'LEASE: an expired grant is gone from the registry -- reclaimed when it is '
     . 'probed, by the shared store\'s own expiry, so a lease nobody sweeps '
     . 'still expires');
like($gone, qr/"granted":\[\],"reason":"no mapping/,
     'LEASE: and it resolves to the EMPTY environment, which is the same answer '
     . 'an unknown principal gets -- an expired session is not a degraded '
     . 'session, it is no session');
